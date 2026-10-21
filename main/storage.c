#include "storage.h"
#include <string.h>
#include <esp_crc.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <nvs.h>
#if CONFIG_FL_STORAGE_EEPROM
#include "eeprom.h"
#endif

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t seq;  // incremented on every write, the newest EEPROM slot wins
    uint16_t size;
    uint16_t crc;
} record_header_t;

typedef struct
{
    const char *name;
    size_t max_size;
} record_descr_t;

static const record_descr_t records[STORAGE_RECORD_MAX] = {
    [STORAGE_SYSTEM]   = { .name = "system",   .max_size = STORAGE_SYSTEM_MAX_SIZE },
    [STORAGE_VOLATILE] = { .name = "volatile", .max_size = STORAGE_VOLATILE_MAX_SIZE },
    [STORAGE_EFFECTS]  = { .name = "effects",  .max_size = STORAGE_EFFECTS_MAX_SIZE },
    [STORAGE_SCHEDULE] = { .name = "schedule", .max_size = STORAGE_SCHEDULE_MAX_SIZE },
};

static const char *NVS_NAMESPACE = "storage";

static SemaphoreHandle_t lock = NULL;
static TaskHandle_t saver_task = NULL;
static portMUX_TYPE pending_mux = portMUX_INITIALIZER_UNLOCKED;
static storage_saver_t pending[STORAGE_RECORD_MAX] = { 0 };
static bool use_eeprom = false;

static inline uint16_t crc16(const void *data, size_t size)
{
    return esp_crc16_le(0, data, size);
}

static bool header_valid(const record_header_t *h, storage_record_t rec)
{
    return h->magic == SETTINGS_MAGIC && h->size <= records[rec].max_size;
}

////////////////////////////////////////////////////////////////////////////////
/// NVS backend

static esp_err_t nvs_load(storage_record_t rec, void *data, size_t *size)
{
    nvs_handle_t nvs;
    esp_err_t res = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        return ESP_ERR_NOT_FOUND;
    CHECK(res);

    size_t len = 0;
    res = nvs_get_blob(nvs, records[rec].name, NULL, &len);
    if (res != ESP_OK || len < sizeof(record_header_t) || len > sizeof(record_header_t) + records[rec].max_size)
    {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *buf = malloc(len);
    if (!buf)
    {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }
    res = nvs_get_blob(nvs, records[rec].name, buf, &len);
    nvs_close(nvs);

    record_header_t *h = (record_header_t *)buf;
    if (res == ESP_OK)
    {
        if (!header_valid(h, rec) || h->size != len - sizeof(record_header_t)
                || crc16(buf + sizeof(record_header_t), h->size) != h->crc)
            res = ESP_ERR_NOT_FOUND;
        else if (h->size > *size)
            res = ESP_ERR_INVALID_SIZE;
        else
        {
            memcpy(data, buf + sizeof(record_header_t), h->size);
            *size = h->size;
        }
    }
    free(buf);

    return res;
}

static esp_err_t nvs_save(storage_record_t rec, const void *data, size_t size)
{
    size_t len = sizeof(record_header_t) + size;
    uint8_t *buf = malloc(len);
    if (!buf)
        return ESP_ERR_NO_MEM;

    record_header_t *h = (record_header_t *)buf;
    h->magic = SETTINGS_MAGIC;
    h->seq = 0;
    h->size = size;
    h->crc = crc16(data, size);
    memcpy(buf + sizeof(record_header_t), data, size);

    nvs_handle_t nvs;
    esp_err_t res = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res == ESP_OK)
    {
        res = nvs_set_blob(nvs, records[rec].name, buf, len);
        if (res == ESP_OK)
            res = nvs_commit(nvs);
        nvs_close(nvs);
    }
    free(buf);

    return res;
}

////////////////////////////////////////////////////////////////////////////////
/// EEPROM backend

#if CONFIG_FL_STORAGE_EEPROM

typedef struct
{
    size_t offset;    // offset of slot 0, slot 1 follows
    size_t slot_size;
    int slot;         // slot holding the newest valid copy, -1 if none
    uint32_t seq;
    bool scanned;
} record_layout_t;

static record_layout_t layout[STORAGE_RECORD_MAX] = { 0 };

static esp_err_t eeprom_layout_init()
{
    size_t page = eeprom_page_size();
    size_t offset = 0;
    for (size_t r = 0; r < STORAGE_RECORD_MAX; r++)
    {
        size_t slot = sizeof(record_header_t) + records[r].max_size;
        slot = (slot + page - 1) / page * page;
        layout[r].offset = offset;
        layout[r].slot_size = slot;
        layout[r].slot = -1;
        layout[r].scanned = false;
        offset += slot * 2;
    }
    if (offset > eeprom_size())
    {
        ESP_LOGE(TAG, "Settings need %d bytes, EEPROM has only %d", offset, eeprom_size());
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "EEPROM layout: %d of %d bytes used", offset, eeprom_size());

    return ESP_OK;
}

static inline size_t slot_addr(storage_record_t rec, int slot)
{
    return layout[rec].offset + slot * layout[rec].slot_size;
}

// Read the newest valid copy (if data != NULL) or just find it
static esp_err_t eeprom_load(storage_record_t rec, void *data, size_t *size)
{
    record_header_t h[2];
    for (int s = 0; s < 2; s++)
        CHECK(eeprom_read(slot_addr(rec, s), &h[s], sizeof(record_header_t)));

    // try the newest slot first
    int order[2] = { 0, 1 };
    if (header_valid(&h[1], rec) && (!header_valid(&h[0], rec) || (int32_t)(h[1].seq - h[0].seq) > 0))
    {
        order[0] = 1;
        order[1] = 0;
    }

    uint8_t *buf = malloc(records[rec].max_size);
    if (!buf)
        return ESP_ERR_NO_MEM;

    esp_err_t res = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < 2; i++)
    {
        int s = order[i];
        if (!header_valid(&h[s], rec))
            continue;
        res = eeprom_read(slot_addr(rec, s) + sizeof(record_header_t), buf, h[s].size);
        if (res != ESP_OK)
            break;
        if (crc16(buf, h[s].size) != h[s].crc)
        {
            ESP_LOGW(TAG, "EEPROM record '%s' slot %d is corrupted", records[rec].name, s);
            res = ESP_ERR_NOT_FOUND;
            continue;
        }

        layout[rec].slot = s;
        layout[rec].seq = h[s].seq;
        if (data)
        {
            if (h[s].size > *size)
                res = ESP_ERR_INVALID_SIZE;
            else
            {
                memcpy(data, buf, h[s].size);
                *size = h[s].size;
            }
        }
        break;
    }
    free(buf);
    layout[rec].scanned = true;

    return res;
}

static esp_err_t eeprom_save(storage_record_t rec, const void *data, size_t size)
{
    if (!layout[rec].scanned)
        eeprom_load(rec, NULL, NULL);

    // overwrite the older copy, the newest one stays intact until this write completes
    int s = layout[rec].slot == 0 ? 1 : 0;
    record_header_t h = {
        .magic = SETTINGS_MAGIC,
        .seq = layout[rec].slot >= 0 ? layout[rec].seq + 1 : 0,
        .size = size,
        .crc = crc16(data, size),
    };

    // data first, header last: an interrupted write leaves the slot invalid
    CHECK(eeprom_write(slot_addr(rec, s) + sizeof(record_header_t), data, size));
    CHECK(eeprom_write(slot_addr(rec, s), &h, sizeof(h)));

    layout[rec].slot = s;
    layout[rec].seq = h.seq;

    return ESP_OK;
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// Deferred saving

static void saver_task_fn(void *arg)
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // wait until there are no changes for STORAGE_SAVE_DELAY_MS
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STORAGE_SAVE_DELAY_MS)))
            ;
        storage_flush();
    }
}

static void shutdown_handler()
{
    storage_flush();
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t storage_init()
{
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing NVS partition...");
        CHECK_LOGE(nvs_flash_erase(), "Could not erase NVS flash");
        res = nvs_flash_init();
    }
    CHECK_LOGE(res, "Could not init NVS flash");

    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;

#if CONFIG_FL_STORAGE_EEPROM
    use_eeprom = eeprom_init() == ESP_OK && eeprom_layout_init() == ESP_OK;
    if (!use_eeprom)
        ESP_LOGW(TAG, "EEPROM is not available, using NVS for settings");
#endif
    ESP_LOGI(TAG, "Settings storage: %s", storage_backend_name());

    if (xTaskCreate(saver_task_fn, "storage", STORAGE_TASK_STACK_SIZE, NULL, STORAGE_TASK_PRIORITY, &saver_task) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create storage task");
        return ESP_ERR_NO_MEM;
    }
    CHECK(esp_register_shutdown_handler(shutdown_handler));

    return ESP_OK;
}

const char *storage_backend_name()
{
    return use_eeprom ? "EEPROM" : "NVS";
}

esp_err_t storage_load(storage_record_t rec, void *data, size_t *size)
{
    CHECK_ARG(rec < STORAGE_RECORD_MAX && data && size);

    xSemaphoreTake(lock, portMAX_DELAY);
#if CONFIG_FL_STORAGE_EEPROM
    esp_err_t res = use_eeprom ? eeprom_load(rec, data, size) : nvs_load(rec, data, size);
#else
    esp_err_t res = nvs_load(rec, data, size);
#endif
    xSemaphoreGive(lock);

    if (res != ESP_OK)
        ESP_LOGW(TAG, "Could not load '%s': %d (%s)", records[rec].name, res, esp_err_to_name(res));

    return res;
}

esp_err_t storage_save(storage_record_t rec, const void *data, size_t size)
{
    CHECK_ARG(rec < STORAGE_RECORD_MAX && data && size <= records[rec].max_size);

    xSemaphoreTake(lock, portMAX_DELAY);
#if CONFIG_FL_STORAGE_EEPROM
    esp_err_t res = use_eeprom ? eeprom_save(rec, data, size) : nvs_save(rec, data, size);
#else
    esp_err_t res = nvs_save(rec, data, size);
#endif
    xSemaphoreGive(lock);

    if (res != ESP_OK)
        ESP_LOGE(TAG, "Could not save '%s': %d (%s)", records[rec].name, res, esp_err_to_name(res));
    else
        ESP_LOGD(TAG, "Saved '%s', %d bytes", records[rec].name, size);

    return res;
}

void storage_defer(storage_record_t rec, storage_saver_t saver)
{
    if (rec >= STORAGE_RECORD_MAX || !saver)
        return;

    portENTER_CRITICAL(&pending_mux);
    pending[rec] = saver;
    portEXIT_CRITICAL(&pending_mux);

    if (saver_task)
        xTaskNotifyGive(saver_task);
}

esp_err_t storage_flush()
{
    esp_err_t res = ESP_OK;
    for (size_t r = 0; r < STORAGE_RECORD_MAX; r++)
    {
        portENTER_CRITICAL(&pending_mux);
        storage_saver_t saver = pending[r];
        pending[r] = NULL;
        portEXIT_CRITICAL(&pending_mux);

        if (saver)
        {
            esp_err_t r_res = saver();
            if (r_res != ESP_OK)
                res = r_res;
        }
    }
    return res;
}
