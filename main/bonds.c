#include "bonds.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>

#define TAG "bonds"

// A separate namespace, not the settings storage: the names belong to the NimBLE bond store
// (which always lives in NVS), even on a lamp whose settings are kept in an EEPROM.
#define BONDS_NAMESPACE "bonds"
#define BONDS_KEY "names"

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint8_t addr[6];
    char name[BLE_BOND_NAME_MAX + 1]; // zero terminated UTF-8
} bond_name_t;

typedef struct __attribute__((packed))
{
    uint8_t count;
    bond_name_t items[BLE_MAX_BONDS];
} bond_names_t;

static bool id_equal(const bond_id_t *a, const bond_id_t *b)
{
    return a->type == b->type && !memcmp(a->addr, b->addr, sizeof(a->addr));
}

static int find(const bond_names_t *db, const bond_id_t *id)
{
    for (int i = 0; i < db->count; i++)
        if (id_equal((const bond_id_t *)&db->items[i], id))
            return i;
    return -1;
}

static esp_err_t load(bond_names_t *db)
{
    memset(db, 0, sizeof(*db));

    nvs_handle_t nvs;
    esp_err_t res = nvs_open(BONDS_NAMESPACE, NVS_READONLY, &nvs);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        return ESP_ERR_NOT_FOUND;
    if (res != ESP_OK)
        return res;

    size_t len = 0;
    res = nvs_get_blob(nvs, BONDS_KEY, NULL, &len);
    if (res == ESP_OK && len == sizeof(*db))
        res = nvs_get_blob(nvs, BONDS_KEY, db, &len);
    else if (res == ESP_OK)
        res = ESP_ERR_INVALID_SIZE;
    nvs_close(nvs);

    if (res == ESP_ERR_NVS_NOT_FOUND || res == ESP_ERR_INVALID_SIZE)
        return ESP_ERR_NOT_FOUND;
    if (res != ESP_OK)
        return res;

    if (db->count > BLE_MAX_BONDS)
        memset(db, 0, sizeof(*db));
    return ESP_OK;
}

static esp_err_t save(const bond_names_t *db)
{
    nvs_handle_t nvs;
    esp_err_t res = nvs_open(BONDS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res != ESP_OK)
        return res;

    res = db->count ? nvs_set_blob(nvs, BONDS_KEY, db, sizeof(*db)) : nvs_erase_key(nvs, BONDS_KEY);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        res = ESP_OK;
    if (res == ESP_OK)
        res = nvs_commit(nvs);
    nvs_close(nvs);
    return res;
}

esp_err_t bonds_set_name(const bond_id_t *id, const char *name)
{
    bond_names_t db;
    bool loaded = load(&db) == ESP_OK;
    int idx = loaded ? find(&db, id) : -1;

    if (!name || !name[0])
    {
        if (idx < 0)
            return ESP_OK; // nothing to forget
        memmove(&db.items[idx], &db.items[idx + 1], (db.count - idx - 1) * sizeof(db.items[0]));
        db.count--;
        return save(&db);
    }

    if (idx < 0)
    {
        if (db.count >= BLE_MAX_BONDS)
            return ESP_ERR_NO_MEM;
        idx = db.count++;
    }

    bond_name_t *item = &db.items[idx];
    item->type = id->type;
    memcpy(item->addr, id->addr, sizeof(item->addr));
    snprintf(item->name, sizeof(item->name), "%s", name);
    return save(&db);
}

size_t bonds_get_name(const bond_id_t *id, char *out, size_t size)
{
    if (!out || !size)
        return 0;
    out[0] = 0;

    bond_names_t db;
    if (load(&db) != ESP_OK)
        return 0;
    int idx = find(&db, id);
    if (idx < 0)
        return 0;

    snprintf(out, size, "%s", db.items[idx].name);
    return strlen(out);
}

void bonds_forget(const bond_id_t *id)
{
    bond_names_t db;
    if (load(&db) != ESP_OK)
        return;
    int idx = find(&db, id);
    if (idx < 0)
        return;

    memmove(&db.items[idx], &db.items[idx + 1], (db.count - idx - 1) * sizeof(db.items[0]));
    db.count--;
    if (save(&db) != ESP_OK)
        ESP_LOGW(TAG, "Could not save the bond names");
}

void bonds_prune(const bond_id_t *ids, size_t count)
{
    bond_names_t db;
    if (load(&db) != ESP_OK)
        return;

    size_t kept = 0;
    bool changed = false;
    for (size_t i = 0; i < db.count; i++)
    {
        bool found = false;
        for (size_t j = 0; j < count; j++)
            if (id_equal((const bond_id_t *)&db.items[i], &ids[j]))
            {
                found = true;
                break;
            }
        if (found)
            db.items[kept++] = db.items[i];
        else
            changed = true;
    }

    if (!changed)
        return;
    db.count = kept;
    if (save(&db) != ESP_OK)
        ESP_LOGW(TAG, "Could not save the bond names");
}

void bonds_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t res = nvs_open(BONDS_NAMESPACE, NVS_READWRITE, &nvs);
    if (res != ESP_OK)
        return;
    res = nvs_erase_key(nvs, BONDS_KEY);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        res = ESP_OK;
    if (res == ESP_OK)
        res = nvs_commit(nvs);
    nvs_close(nvs);
    if (res != ESP_OK)
        ESP_LOGW(TAG, "Could not clear the bond names");
}
