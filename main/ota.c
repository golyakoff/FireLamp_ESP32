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
            if (writing)
                esp_ota_abort(handle);
            if (writing || started)
                mbedtls_sha256_free(&sha);
            writing = false;
            started = false;
            ESP_LOGW(TAG, "OTA aborted");
            status(OTA_STATE_IDLE, OTA_ERR_NONE);
            break;
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
    static item_t item; // callers are serialized by the BLE host task
    item.type = type;
    item.len = len;
    if (len)
        memcpy(item.data, data, len);
    return xQueueSend(queue, &item, pdMS_TO_TICKS(OTA_QUEUE_TIMEOUT_MS)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ota_init(ota_status_cb_t cb)
{
    status_cb = cb;
    queue = xQueueCreate(OTA_QUEUE_LEN, sizeof(item_t));
    if (!queue)
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

bool ota_in_progress()
{
    return started;
}
