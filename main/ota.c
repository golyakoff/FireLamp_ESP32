#include "ota.h"
#include <string.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

typedef enum {
    ITEM_BEGIN = 0,
    ITEM_DATA,
    ITEM_END,
    ITEM_ABORT,
} item_type_t;

typedef struct
{
    uint8_t type;
    uint16_t len;
    uint8_t data[OTA_CHUNK_MAX];
} item_t;

static QueueHandle_t queue = NULL;
static EventGroupHandle_t flags = NULL;
static SemaphoreHandle_t send_lock = NULL;
#define FLAG_READY BIT(0)
static ota_status_cb_t status_cb = NULL;
static volatile bool started = false; // between ota_begin() and the end of the update

// writer task state
static esp_ota_handle_t handle = 0;
static const esp_partition_t *partition = NULL;
static mbedtls_sha256_context sha;
static uint8_t expected_sha[32];
static bool check_sha = false;
static bool writing = false;
static uint32_t total = 0, received = 0, reported = 0;

static void status(ota_state_t state, ota_error_t err)
{
    if (status_cb)
        status_cb(state, err, received, total);
}

static void fail(ota_error_t err)
{
    ESP_LOGE(TAG, "OTA failed: error %d, received %lu of %lu bytes", err, received, total);
    if (writing)
        esp_ota_abort(handle);
    writing = false;
    started = false;
    mbedtls_sha256_free(&sha);
    status(OTA_STATE_ERROR, err);
}

static void reboot_cb(void *arg)
{
    esp_restart();
}

static void process(const item_t *item)
{
    switch (item->type)
    {
        case ITEM_BEGIN:
            partition = esp_ota_get_next_update_partition(NULL);
            if (!partition || total > partition->size)
            {
                fail(OTA_ERR_SIZE);
                return;
            }
            ESP_LOGI(TAG, "OTA: erasing partition '%s' for %lu bytes...", partition->label, total);
            // erases the space needed for the image now, so data writes are short
            if (esp_ota_begin(partition, total, &handle) != ESP_OK)
            {
                fail(OTA_ERR_FLASH);
                return;
            }
            writing = true;
            xEventGroupSetBits(flags, FLAG_READY);
            mbedtls_sha256_init(&sha);
            mbedtls_sha256_starts(&sha, 0);
            ESP_LOGI(TAG, "OTA: receiving");
            status(OTA_STATE_RECEIVING, OTA_ERR_NONE);
            break;

        case ITEM_DATA:
            if (!writing)
                return;
            if (received + item->len > total)
            {
                fail(OTA_ERR_OVERFLOW);
                return;
            }
            if (esp_ota_write(handle, item->data, item->len) != ESP_OK)
            {
                fail(OTA_ERR_FLASH);
                return;
            }
            mbedtls_sha256_update(&sha, item->data, item->len);
            received += item->len;
            if (received - reported >= OTA_PROGRESS_STEP || received == total)
            {
                reported = received;
                status(OTA_STATE_RECEIVING, OTA_ERR_NONE);
            }
            // let the radio breathe between flash writes (lesson from Matrix Clock)
            vTaskDelay(pdMS_TO_TICKS(OTA_WRITE_YIELD_MS));
            break;

        case ITEM_END:
        {
            if (!writing)
                return;
            if (received != total)
            {
                fail(OTA_ERR_SIZE);
                return;
            }
            uint8_t hash[32];
            mbedtls_sha256_finish(&sha, hash);
            mbedtls_sha256_free(&sha);
            if (check_sha && memcmp(hash, expected_sha, sizeof(hash)))
            {
                esp_ota_abort(handle);
                writing = false;
                fail(OTA_ERR_SHA);
                return;
            }
            writing = false;
            // validates the image (header, checksum, app SHA)
            esp_err_t res = esp_ota_end(handle);
            if (res != ESP_OK)
            {
                ESP_LOGE(TAG, "OTA: image is invalid: %d (%s)", res, esp_err_to_name(res));
                started = false;
                status(OTA_STATE_ERROR, OTA_ERR_IMAGE);
                return;
            }
            if (esp_ota_set_boot_partition(partition) != ESP_OK)
            {
                started = false;
                status(OTA_STATE_ERROR, OTA_ERR_FLASH);
                return;
            }
            ESP_LOGI(TAG, "OTA: success, rebooting");
            status(OTA_STATE_SUCCESS, OTA_ERR_NONE);
            esp_timer_create_args_t args = { .callback = reboot_cb, .name = "ota_reboot" };
            esp_timer_handle_t t;
            if (esp_timer_create(&args, &t) == ESP_OK)
                esp_timer_start_once(t, OTA_REBOOT_DELAY_MS * 1000);
            else
                esp_restart();
            break;
        }

        case ITEM_ABORT:
        {
            // data[0]: error code to report, OTA_ERR_NONE for a plain abort
            ota_error_t err = item->len ? item->data[0] : OTA_ERR_NONE;
            if (writing)
                esp_ota_abort(handle);
            if (writing || started)
                mbedtls_sha256_free(&sha);
            writing = false;
            started = false;
            ESP_LOGW(TAG, "OTA aborted, error %d", err);
            status(err ? OTA_STATE_ERROR : OTA_STATE_IDLE, err);
            break;
        }
    }
}

static void writer_task(void *arg)
{
    static item_t item;
    while (1)
    {
        if (xQueueReceive(queue, &item, started ? pdMS_TO_TICKS(OTA_DATA_TIMEOUT_MS) : portMAX_DELAY) != pdTRUE)
        {
            if (started)
                fail(OTA_ERR_TIMEOUT);
            continue;
        }
        process(&item);
    }
}

static esp_err_t send(item_type_t type, const void *data, size_t len)
{
    // BLE host task and the WiFi task (internet update) may call this
    static item_t item;
    xSemaphoreTake(send_lock, portMAX_DELAY);
    item.type = type;
    item.len = len;
    if (len)
        memcpy(item.data, data, len);
    esp_err_t res = xQueueSend(queue, &item, pdMS_TO_TICKS(OTA_QUEUE_TIMEOUT_MS)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    xSemaphoreGive(send_lock);
    return res;
}

esp_err_t ota_init(ota_status_cb_t cb)
{
    status_cb = cb;
    queue = xQueueCreate(OTA_QUEUE_LEN, sizeof(item_t));
    flags = xEventGroupCreate();
    send_lock = xSemaphoreCreateMutex();
    if (!queue || !flags || !send_lock)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(writer_task, "ota", OTA_TASK_STACK_SIZE, NULL, OTA_TASK_PRIORITY, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t ota_begin(uint32_t size, const uint8_t *sha256)
{
    if (started)
    {
        if (status_cb)
            status_cb(OTA_STATE_ERROR, OTA_ERR_BUSY, received, total);
        return ESP_ERR_INVALID_STATE;
    }
    CHECK_ARG(size);

    started = true;
    xEventGroupClearBits(flags, FLAG_READY);
    total = size;
    received = reported = 0;
    check_sha = sha256 != NULL;
    if (check_sha)
        memcpy(expected_sha, sha256, sizeof(expected_sha));

    ESP_LOGI(TAG, "OTA started, %lu bytes", size);
    return send(ITEM_BEGIN, NULL, 0);
}

esp_err_t ota_write(const void *data, size_t len)
{
    CHECK_ARG(data && len && len <= OTA_CHUNK_MAX);
    if (!started)
        return ESP_ERR_INVALID_STATE;
    return send(ITEM_DATA, data, len);
}

esp_err_t ota_end()
{
    if (!started)
        return ESP_ERR_INVALID_STATE;
    return send(ITEM_END, NULL, 0);
}

void ota_abort()
{
    if (started)
        send(ITEM_ABORT, NULL, 0);
}

void ota_fail(ota_error_t err)
{
    if (!started)
    {
        ota_report(OTA_STATE_ERROR, err);
        return;
    }
    uint8_t e = err;
    send(ITEM_ABORT, &e, 1);
}

esp_err_t ota_wait_ready(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(flags, FLAG_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return b & FLAG_READY ? ESP_OK : ESP_ERR_TIMEOUT;
}

void ota_report(ota_state_t state, ota_error_t err)
{
    if (status_cb)
        status_cb(state, err, received, total);
}

bool ota_in_progress()
{
    return started;
}

static const esp_partition_t *previous_partition(esp_app_desc_t *desc)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!p || p == esp_ota_get_running_partition())
        return NULL;

    // a firmware that did not start properly is not offered again
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(p, &state) == ESP_OK
            && (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED))
        return NULL;

    esp_app_desc_t d;
    if (esp_ota_get_partition_description(p, desc ? desc : &d) != ESP_OK)
        return NULL;
    return p;
}

bool ota_previous_version(char *version, size_t size)
{
    esp_app_desc_t desc;
    // the slot is being written
    if (started || !previous_partition(&desc))
        return false;
    strlcpy(version, desc.version, size);
    return true;
}

esp_err_t ota_rollback()
{
    if (started)
        return ESP_ERR_INVALID_STATE;
    const esp_partition_t *p = previous_partition(NULL);
    if (!p)
        return ESP_ERR_NOT_FOUND;
    // checks the whole image
    CHECK_LOGE(esp_ota_set_boot_partition(p), "Could not switch to the previous firmware");
    ESP_LOGW(TAG, "Going back to the previous firmware in '%s'", p->label);
    return ESP_OK;
}
