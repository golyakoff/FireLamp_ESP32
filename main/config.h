#ifndef EVLAMP_CONFIG_H_
#define EVLAMP_CONFIG_H_

#include <soc/soc_caps.h>

////////////////////////////////////////////////////////////////////////////////
/// System

#if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    #error Unsupported target
#endif

#define APP_NAME "FireLamp"

////////////////////////////////////////////////////////////////////////////////
/// Settings

#define SETTINGS_MAGIC 0xf1a30001

#if CONFIG_FL_WIFI_MODE_PERIODIC
    #define DEFAULT_WIFI_MODE WIFI_CLIENT_PERIODIC
#elif CONFIG_FL_WIFI_MODE_ALWAYS_ON
    #define DEFAULT_WIFI_MODE WIFI_CLIENT_ALWAYS_ON
#else
    #define DEFAULT_WIFI_MODE WIFI_CLIENT_OFF
#endif

#if CONFIG_FL_WIFI_DHCP
    #define DEFAULT_WIFI_DHCP true
#else
    #define DEFAULT_WIFI_DHCP false
#endif


#if CONFIG_FL_LED_TYPE_SK6812
    #define DEFAULT_LED_TYPE LED_TYPE_SK6812
#elif CONFIG_FL_LED_TYPE_WS2811
    #define DEFAULT_LED_TYPE LED_TYPE_WS2811
#else
    #define DEFAULT_LED_TYPE LED_TYPE_WS2812
#endif

#if CONFIG_FL_LED_COLOR_ORDER_RGB
    #define DEFAULT_LED_COLOR_ORDER LED_COLOR_ORDER_RGB
#else
    #define DEFAULT_LED_COLOR_ORDER LED_COLOR_ORDER_GRB
#endif

#if CONFIG_FL_MATRIX_MIRROR
    #define DEFAULT_MATRIX_MIRROR true
#else
    #define DEFAULT_MATRIX_MIRROR false
#endif

#if CONFIG_FL_MATRIX_ROTATION_90
    #define DEFAULT_MATRIX_ROTATION MATRIX_ROTATION_90
#elif CONFIG_FL_MATRIX_ROTATION_180
    #define DEFAULT_MATRIX_ROTATION MATRIX_ROTATION_180
#elif CONFIG_FL_MATRIX_ROTATION_270
    #define DEFAULT_MATRIX_ROTATION MATRIX_ROTATION_270
#else
    #define DEFAULT_MATRIX_ROTATION MATRIX_ROTATION_0
#endif

////////////////////////////////////////////////////////////////////////////////
/// Storage

#define STORAGE_SAVE_DELAY_MS 3000
#define STORAGE_TASK_STACK_SIZE 4096
#define STORAGE_TASK_PRIORITY 2

////////////////////////////////////////////////////////////////////////////////
/// Time and scheduler

#define MIN_VALID_TIME 1767225600 // 2026-01-01 UTC: anything earlier is not a real time
#define RTC_SYNC_INTERVAL_S 3600
#define SCHEDULER_TASK_STACK_SIZE 4096
#define SCHEDULER_TASK_PRIORITY 4

////////////////////////////////////////////////////////////////////////////////
/// OTA

#define OTA_QUEUE_LEN 8
#define OTA_QUEUE_TIMEOUT_MS 3000  // ota_write() blocks while the flash writer is busy
#define OTA_DATA_TIMEOUT_MS 10000  // abort when no data comes
#define OTA_WRITE_YIELD_MS 2
#define OTA_PROGRESS_STEP 4096
#define OTA_REBOOT_DELAY_MS 1000
#define OTA_TASK_STACK_SIZE 4096
#define OTA_TASK_PRIORITY 3

////////////////////////////////////////////////////////////////////////////////
/// BLE

#define BLE_PAIRING_WINDOW_S 120
#define BLE_AUTH_TIMEOUT_S 30      // unknown phone must pair within this time
#define BLE_MAX_BONDS 4
#define BLE_STATE_NOTIFY_MS 250    // lamp state notifications are coalesced: max 4 per second

////////////////////////////////////////////////////////////////////////////////
/// Bus

#define BUS_QUEUE_LEN 10
#define BUS_TIMEOUT_MS 500
#define BUS_EVENT_DATA_SIZE 64

////////////////////////////////////////////////////////////////////////////////
/// Input

#define INPUT_TASK_STACK_SIZE 3072
#define INPUT_TASK_PRIORITY 6
#define INPUT_POLL_MS 10
#define INPUT_DEBOUNCE_SAMPLES 3   // x INPUT_POLL_MS
#define INPUT_CLICK_TIMEOUT_MS 350 // max pause between clicks of a multi-click
#define INPUT_MAX_CLICKS 5         // no need to wait for more clicks after this
#define INPUT_HOLD_MS 600
#define INPUT_HOLD_REPEAT_MS 50

#define HOLD_BRIGHTNESS_STEP 3     // per INPUT_HOLD_REPEAT_MS: ~4 s for full range
#define CLICK_BRIGHTNESS_STEP 5
#define POWER_HOLD_MS 1500         // 4 buttons: Main hold time to turn on/off
#define RESET_HOLD_MS 3000         // 4 buttons: Reset hold time
#define RESET_HOLD_OFF_MS 10000    // 1 button/touch: hold time while the lamp is off
#define PAIRING_CLICKS 5           // clicks of the main button to open the BLE pairing window
#define PAIRING_BLINK_MS 300

////////////////////////////////////////////////////////////////////////////////
/// Main task

#define MAIN_TASK_STACK_SIZE 8192
#define MAIN_TASK_PRIORITY 5

////////////////////////////////////////////////////////////////////////////////
/// Surface

#define FPS_MAX 120
#define BRIGHTNESS_MIN 1 // 0 looks like the lamp is off
#define SURFACE_TASK_STACK_SIZE 8192
#define MAX_MATRIX_LEDS 1024
#define MAX_EFFECTS 64
#define MIN_MATRIX_SIZE 8
#define MAX_MATRIX_SIZE 128

#if CONFIG_FL_MATRIX_WIDTH * CONFIG_FL_MATRIX_HEIGHT > MAX_MATRIX_LEDS
    #error Too much LEDs in matrix!
#endif

// Rendering task runs on the second core when there is one, BLE/WiFi stacks live on the first
#if CONFIG_FREERTOS_UNICORE || SOC_CPU_CORES_NUM < 2
    #define SURFACE_TASK_CORE tskNO_AFFINITY
#else
    #define SURFACE_TASK_CORE APP_CPU_NUM
#endif

////////////////////////////////////////////////////////////////////////////////
/// WiFi

#define WIFI_HOSTNAME "firelamp"
#define WIFI_CLIENT_TASK_STACK_SIZE 4096
#define WIFI_CLIENT_TASK_PRIORITY 3
#define WIFI_RECONNECT_MIN_MS 2000
#define WIFI_RECONNECT_MAX_MS 60000
#define WIFI_FIRST_SESSION_DELAY_MS 5000  // periodic mode: first session after boot or settings change
#define WIFI_SESSION_CONNECT_MS 30000     // periodic mode: connection timeout
#define WIFI_SESSION_RETRY_MS (10 * 60 * 1000) // periodic mode: retry after a failed session
#define WIFI_SNTP_WAIT_MS 15000

////////////////////////////////////////////////////////////////////////////////
/// Webserver

#define HTTPD_STACK_SIZE 32768
#define MAX_POST_SIZE 4096

#endif /* EVLAMP_CONFIG_H_ */
