#include "updater.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_app_desc.h>
#include <cJSON.h>
#include "ota.h"
#include "settings.h"
#include "surface.h"
#include "demo.h"
#include "scheduler.h"
#include "wifi.h"

#define MANIFEST_MAX_SIZE 4096

// builds are selected by chip and hardware configuration, e.g. "esp32c3-default"
#define BUILD_KEY CONFIG_IDF_TARGET "-" CONFIG_FL_BUILD_VARIANT

static update_info_t info = { 0 };
static updater_cb_t info_cb = NULL;
static volatile bool check_requested = false, install_requested = false;

// latest manifest data for this chip
static char url[512];
static uint32_t size = 0;
static uint8_t sha[32];
static bool sha_valid = false;

static void set_state(update_state_t s)
{
    info.state = s;
    if (info_cb)
        info_cb();
}

// "1.2.3" -> comparable number, 0 if not parsed
static uint32_t version_num(const char *v)
{
    unsigned major = 0, minor = 0, patch = 0;
    if (v[0] == 'v')
        v++;
    if (sscanf(v, "%u.%u.%u", &major, &minor, &patch) < 2)
        return 0;
    return (major << 20) | ((minor & 0x3ff) << 10) | (patch & 0x3ff);
}

static bool parse_hex(const char *s, uint8_t *out, size_t len)
{
    if (!s || strlen(s) != len * 2)
        return false;
    for (size_t i = 0; i < len; i++)
        if (sscanf(s + i * 2, "%2hhx", &out[i]) != 1)
            return false;
    return true;
}

// Open a GET request following redirects (GitHub release assets are redirected twice)
static esp_err_t http_open(esp_http_client_handle_t client, int64_t *content_length)
{
    for (int i = 0; i < UPDATER_MAX_REDIRECTS; i++)
    {
        CHECK(esp_http_client_open(client, 0));
        *content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 200)
            return ESP_OK;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            esp_http_client_set_redirection(client);
            esp_http_client_flush_response(client, NULL);
            esp_http_client_close(client);
            continue;
        }
        ESP_LOGW(TAG, "Updater: HTTP status %d", status);
        esp_http_client_close(client);
        return ESP_FAIL;
    }
    esp_http_client_close(client);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_http_client_handle_t http_client(const char *u)
{
    esp_http_client_config_t cfg = {
        .url = u,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = UPDATER_HTTP_TIMEOUT_MS,
        .buffer_size = UPDATER_HTTP_BUFFER_SIZE,     // GitHub redirect headers are long
        .buffer_size_tx = UPDATER_HTTP_BUFFER_SIZE,
    };
    return esp_http_client_init(&cfg);
}

// `report`: also send OTA status notifications (the app asked to install)
static esp_err_t check(bool report)
{
    set_state(UPDATE_CHECKING);
    if (report)
        ota_report(OTA_STATE_CHECKING, OTA_ERR_NONE);

    esp_http_client_handle_t client = http_client(CONFIG_FL_OTA_MANIFEST_URL);
    if (!client)
        return ESP_ERR_NO_MEM;

    char *buf = calloc(1, MANIFEST_MAX_SIZE);
    cJSON *json = NULL;
    esp_err_t res = ESP_FAIL;
    int64_t len = 0;
    if (!buf)
    {
        res = ESP_ERR_NO_MEM;
        goto exit;
    }
    if ((res = http_open(client, &len)) != ESP_OK)
        goto exit;

    int total = 0, n;
    while (total < MANIFEST_MAX_SIZE - 1 && (n = esp_http_client_read(client, buf + total, MANIFEST_MAX_SIZE - 1 - total)) > 0)
        total += n;
    esp_http_client_close(client);

    res = ESP_ERR_INVALID_RESPONSE;
    json = cJSON_Parse(buf);
    const char *version = cJSON_GetStringValue(cJSON_GetObjectItem(json, "version"));
    if (!version || !version_num(version))
    {
        ESP_LOGW(TAG, "Updater: invalid manifest");
        goto exit;
    }
    strncpy(info.version, version, sizeof(info.version) - 1);
    info.checked_at = time(NULL);

    cJSON *build = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "builds"), BUILD_KEY);
    if (!build)
    {
        // a custom hardware configuration: official releases are not for this lamp
        ESP_LOGI(TAG, "Updater: release %s has no build " BUILD_KEY, version);
        set_state(UPDATE_UP_TO_DATE);
        res = ESP_OK;
        goto exit;
    }
    const char *build_url = cJSON_GetStringValue(cJSON_GetObjectItem(build, "url"));
    cJSON *build_size = cJSON_GetObjectItem(build, "size");
    if (!build_url || strlen(build_url) >= sizeof(url) || strncmp(build_url, "https://", 8)
            || !cJSON_IsNumber(build_size) || cJSON_GetNumberValue(build_size) < 1)
    {
        ESP_LOGW(TAG, "Updater: invalid build " BUILD_KEY " in manifest");
        goto exit;
    }

    strcpy(url, build_url);
    size = (uint32_t)cJSON_GetNumberValue(build_size);
    sha_valid = parse_hex(cJSON_GetStringValue(cJSON_GetObjectItem(build, "sha256")), sha, sizeof(sha));

    const char *current = esp_app_get_description()->version;
    bool newer = version_num(version) > version_num(current);
    ESP_LOGI(TAG, "Updater: current %s, released %s%s", current, version, newer ? ": update available" : "");
    set_state(newer ? UPDATE_AVAILABLE : UPDATE_UP_TO_DATE);
    res = ESP_OK;

exit:
    if (res != ESP_OK)
    {
        ESP_LOGW(TAG, "Updater: check failed: %d (%s)", res, esp_err_to_name(res));
        set_state(UPDATE_CHECK_FAILED);
    }
    cJSON_Delete(json);
    free(buf);
    esp_http_client_cleanup(client);
    return res;
}

static esp_err_t install()
{
    ESP_LOGI(TAG, "Updater: installing %s from %s", info.version, url);

    // like the BLE upload: blank the matrix, stop automatic changes
    demo_stop();
    scheduler_cancel_dawn();
    surface_stop();

    esp_http_client_handle_t client = http_client(url);
    if (!client)
        return ESP_ERR_NO_MEM;

    static uint8_t chunk[OTA_CHUNK_MAX];
    int64_t len = 0;
    esp_err_t res = http_open(client, &len);
    if (res != ESP_OK)
    {
        ota_fail(OTA_ERR_DOWNLOAD);
        esp_http_client_cleanup(client);
        return res;
    }

    res = ota_begin(size, sha_valid ? sha : NULL);
    // the partition is erased first: do not let the download overrun the write queue
    if (res == ESP_OK)
        res = ota_wait_ready(UPDATER_ERASE_TIMEOUT_MS);
    uint32_t received = 0;
    while (res == ESP_OK && received < size)
    {
        int n = esp_http_client_read(client, (char *)chunk, MIN((uint32_t)sizeof(chunk), size - received));
        if (n <= 0)
        {
            ESP_LOGE(TAG, "Updater: download interrupted at %lu of %lu bytes", received, size);
            res = ESP_FAIL;
            break;
        }
        res = ota_write(chunk, n);
        received += n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (res != ESP_OK)
    {
        ota_fail(OTA_ERR_DOWNLOAD);
        return res;
    }
    // verifies the image, switches the boot partition and reboots
    return ota_end();
}

////////////////////////////////////////////////////////////////////////////////

void updater_get_info(update_info_t *i)
{
    *i = info;
}

void updater_set_cb(updater_cb_t cb)
{
    info_cb = cb;
}

static bool wifi_usable()
{
    return sys_settings.wifi.mode != WIFI_CLIENT_OFF && sys_settings.wifi.ssid[0];
}

esp_err_t updater_request_check()
{
    if (!wifi_usable())
        return ESP_ERR_INVALID_STATE;
    check_requested = true;
    wifi_request_session();
    return ESP_OK;
}

esp_err_t updater_request_install()
{
    if (!wifi_usable())
        return ESP_ERR_INVALID_STATE;
    if (ota_in_progress())
        return ESP_ERR_INVALID_STATE;
    install_requested = true;
    wifi_request_session();
    return ESP_OK;
}

bool updater_pending()
{
    return check_requested || install_requested;
}

void updater_run(bool regular_check)
{
    bool do_install = install_requested;
    bool do_check = regular_check || check_requested || do_install;
    check_requested = install_requested = false;

    if (!do_check)
        return;
    if (check(do_install) != ESP_OK)
    {
        if (do_install)
            ota_report(OTA_STATE_ERROR, OTA_ERR_DOWNLOAD);
        return;
    }
    if (!do_install)
        return;
    if (info.state != UPDATE_AVAILABLE)
    {
        ota_report(OTA_STATE_NO_UPDATE, OTA_ERR_NONE);
        return;
    }
    if (install() != ESP_OK)
        ESP_LOGE(TAG, "Updater: installation failed");
}
