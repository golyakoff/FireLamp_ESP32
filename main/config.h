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

#ifdef CONFIG_FL_WIFI_MODE_AP
    #define DEFAULT_WIFI_MODE WIFI_MODE_AP
#else
    #define DEFAULT_WIFI_MODE WIFI_MODE_STA
#endif

#if CONFIG_FL_WIFI_DHCP
    #define DEFAULT_WIFI_DHCP true
#else
    #define DEFAULT_WIFI_DHCP false
#endif

#define DEFAULT_WIFI_AP_MAXCONN 5

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
/// Bus

#define BUS_QUEUE_LEN 5
#define BUS_TIMEOUT_MS 500
#define BUS_EVENT_DATA_SIZE 64

////////////////////////////////////////////////////////////////////////////////
/// Main task

#define MAIN_TASK_STACK_SIZE 8192
#define MAIN_TASK_PRIORITY 5

////////////////////////////////////////////////////////////////////////////////
/// Surface

#define FPS_MAX 120
#define SURFACE_TASK_STACK_SIZE 8192
#define MAX_MATRIX_LEDS 1024
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
/// Webserver

#define HTTPD_STACK_SIZE 32768
#define MAX_POST_SIZE 4096

#endif /* EVLAMP_CONFIG_H_ */
