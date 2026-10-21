#include "systime.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "settings.h"
#include "ds3231.h"

_Static_assert(sizeof(sys_settings.time.tz) == TZ_MAX_LEN, "Time zone buffer size mismatch");

static time_source_t source = TIME_SOURCE_NONE;
static bool rtc_present = false;

static void apply_tz()
{
    setenv("TZ", sys_settings.time.tz, 1);
    tzset();
}

static void set_system_time(time_t utc)
{
    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void log_time(const char *what)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    ESP_LOGI(TAG, "%s: %s (%s), source: %s", what, buf, sys_settings.time.tz, systime_source_name(source));
}

esp_err_t systime_init()
{
    apply_tz();

    rtc_present = ds3231_init() == ESP_OK;
    if (rtc_present)
    {
        time_t utc;
        esp_err_t res = ds3231_get_time(&utc);
        if (res == ESP_OK)
        {
            set_system_time(utc);
            source = TIME_SOURCE_RTC;
            log_time("Time from RTC");
        }
        else if (res == ESP_ERR_INVALID_STATE)
            ESP_LOGW(TAG, "RTC time is not valid (oscillator was stopped), waiting for time sync");
        else
            ESP_LOGW(TAG, "Could not read RTC: %d (%s)", res, esp_err_to_name(res));
    }
    else
        ESP_LOGI(TAG, "No RTC, waiting for time sync");

    return ESP_OK;
}

bool systime_is_valid()
{
    return source != TIME_SOURCE_NONE;
}

time_source_t systime_source()
{
    return source;
}

const char *systime_source_name(time_source_t s)
{
    switch (s)
    {
        case TIME_SOURCE_RTC:  return "rtc";
        case TIME_SOURCE_BLE:  return "ble";
        case TIME_SOURCE_SNTP: return "sntp";
        case TIME_SOURCE_API:  return "api";
        default:               return "none";
    }
}

bool systime_has_rtc()
{
    return rtc_present;
}

esp_err_t systime_set(time_t utc, time_source_t src)
{
    CHECK_ARG(src != TIME_SOURCE_NONE && utc >= MIN_VALID_TIME);

    set_system_time(utc);
    source = src;

    if (rtc_present && src != TIME_SOURCE_RTC)
    {
        esp_err_t res = ds3231_set_time(utc);
        if (res != ESP_OK)
            ESP_LOGW(TAG, "Could not set RTC time: %d (%s)", res, esp_err_to_name(res));
    }

    log_time("Time set");

    return ESP_OK;
}

esp_err_t systime_set_tz(const char *tz)
{
    CHECK_ARG(tz && strlen(tz) > 0 && strlen(tz) < TZ_MAX_LEN);

    strncpy(sys_settings.time.tz, tz, TZ_MAX_LEN - 1);
    sys_settings.time.tz[TZ_MAX_LEN - 1] = 0;
    apply_tz();
    log_time("Time zone set");

    return sys_settings_save();
}

const char *systime_get_tz()
{
    return sys_settings.time.tz;
}

esp_err_t systime_sync_from_rtc()
{
    if (!rtc_present)
        return ESP_ERR_NOT_SUPPORTED;

    time_t utc;
    CHECK(ds3231_get_time(&utc));

    time_t now = time(NULL);
    if (llabs((long long)(utc - now)) > 1)
    {
        ESP_LOGI(TAG, "System time drift %lld s, corrected by RTC", (long long)(now - utc));
        set_system_time(utc);
    }
    if (source == TIME_SOURCE_NONE)
        source = TIME_SOURCE_RTC;

    return ESP_OK;
}
