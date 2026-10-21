// Host test of storage.c EEPROM backend: A/B slots, power loss, corruption
#include <assert.h>
#include "storage.h"
#include "eeprom.h"

#define SIZE 4096
#define PAGE 32
static uint8_t mem[SIZE];
static long fail_after = -1; // bytes until simulated power loss, -1 = never
static long writes[SIZE];

esp_err_t eeprom_init() { return ESP_OK; }
size_t eeprom_size() { return SIZE; }
size_t eeprom_page_size() { return PAGE; }
esp_err_t eeprom_read(size_t addr, void *data, size_t len) { assert(addr + len <= SIZE); memcpy(data, mem + addr, len); return ESP_OK; }
esp_err_t eeprom_write(size_t addr, const void *data, size_t len)
{
    assert(addr + len <= SIZE);
    const uint8_t *d = data;
    for (size_t i = 0; i < len; i++)
    {
        if (fail_after == 0) return ESP_FAIL;
        if (fail_after > 0) fail_after--;
        mem[addr + i] = d[i];
        writes[addr + i]++;
    }
    return ESP_OK;
}

typedef struct { uint8_t a; uint32_t b; char s[40]; } rec_t;

static void reload_check(storage_record_t r, const rec_t *expect)
{
    rec_t got; size_t sz = sizeof(got);
    memset(&got, 0, sizeof(got));
    esp_err_t res = storage_load(r, &got, &sz);
    if (!expect) { assert(res == ESP_ERR_NOT_FOUND); return; }
    assert(res == ESP_OK && sz == sizeof(rec_t) && !memcmp(&got, expect, sizeof(rec_t)));
}

int fake_level; int64_t fake_now_us;

static int calls = 0;
static esp_err_t saver(void) { calls++; return ESP_OK; }

int main(void)
{
    memset(mem, 0xff, sizeof(mem)); // erased chip
    assert(storage_init() == ESP_OK);
    assert(!strcmp(storage_backend_name(), "EEPROM"));

    // empty chip
    reload_check(STORAGE_SYSTEM, NULL);

    rec_t v1 = { 1, 111, "first" }, v2 = { 2, 222, "second" }, v3 = { 3, 333, "third" };
    assert(storage_save(STORAGE_SYSTEM, &v1, sizeof(v1)) == ESP_OK);
    reload_check(STORAGE_SYSTEM, &v1);
    assert(storage_save(STORAGE_SYSTEM, &v2, sizeof(v2)) == ESP_OK);
    reload_check(STORAGE_SYSTEM, &v2);

    // other records are independent
    reload_check(STORAGE_VOLATILE, NULL);
    assert(storage_save(STORAGE_VOLATILE, &v3, sizeof(v3)) == ESP_OK);
    reload_check(STORAGE_VOLATILE, &v3);
    reload_check(STORAGE_SYSTEM, &v2);

    // power loss at every byte of a write: must load either the old or the new value, never garbage
    for (long cut = 0; cut < (long)(sizeof(rec_t) + 12); cut++)
    {
        assert(storage_save(STORAGE_SYSTEM, &v1, sizeof(v1)) == ESP_OK); // known state
        // "reboot": forget cached layout by re-initializing
        assert(storage_init() == ESP_OK);
        fail_after = cut;
        storage_save(STORAGE_SYSTEM, &v2, sizeof(v2));
        fail_after = -1;
        assert(storage_init() == ESP_OK);
        rec_t got; size_t sz = sizeof(got);
        assert(storage_load(STORAGE_SYSTEM, &got, &sz) == ESP_OK);
        assert(!memcmp(&got, &v1, sizeof(rec_t)) || !memcmp(&got, &v2, sizeof(rec_t)));
        // a further save after the interrupted one must win
        assert(storage_save(STORAGE_SYSTEM, &v3, sizeof(v3)) == ESP_OK);
        assert(storage_init() == ESP_OK);
        reload_check(STORAGE_SYSTEM, &v3);
    }

    // corrupted newest copy (bit rot): previous copy is used
    assert(storage_save(STORAGE_SYSTEM, &v1, sizeof(v1)) == ESP_OK);
    assert(storage_save(STORAGE_SYSTEM, &v2, sizeof(v2)) == ESP_OK);
    // find v2 in memory and flip a byte
    uint8_t *p = memmem(mem, SIZE, "second", 6);
    assert(p); *p ^= 0xff;
    assert(storage_init() == ESP_OK);
    reload_check(STORAGE_SYSTEM, &v1);

    // buffer too small
    rec_t got; size_t small = 4;
    assert(storage_load(STORAGE_SYSTEM, &got, &small) == ESP_ERR_INVALID_SIZE);

    // too big record is rejected
    static uint8_t big[STORAGE_SYSTEM_MAX_SIZE + 1];
    assert(storage_save(STORAGE_SYSTEM, big, sizeof(big)) == ESP_ERR_INVALID_ARG);

    // wear is spread over two slots: 1000 saves -> ~500 writes per byte
    memset(writes, 0, sizeof(writes));
    for (int i = 0; i < 1000; i++) { v3.b = i; assert(storage_save(STORAGE_VOLATILE, &v3, sizeof(v3)) == ESP_OK); }
    long maxw = 0; for (int i = 0; i < SIZE; i++) if (writes[i] > maxw) maxw = writes[i];
    assert(maxw == 500);
    assert(storage_init() == ESP_OK);
    reload_check(STORAGE_VOLATILE, &v3);

    // deferred saving: only the last saver call matters, flush runs it once
    storage_defer(STORAGE_EFFECTS, saver);
    storage_defer(STORAGE_EFFECTS, saver);
    assert(storage_flush() == ESP_OK && calls == 1);
    assert(storage_flush() == ESP_OK && calls == 1);

    printf("storage: all tests passed (max writes per byte for 1000 saves: %ld)\n", maxw);
    return 0;
}
