#include <assert.h>
#include "input.h"
#include "bus.h"
int fake_level = 0; int64_t fake_now_us = 0;
static int n = 0; static input_event_t last; static event_type_t last_t;
esp_err_t bus_send_event(event_type_t type, void *data, size_t size) { last_t = type; memcpy(&last, data, sizeof(last)); n++; return ESP_OK; }
#include "input.c"
static void run_ms(int ms) { for (int i = 0; i < ms / INPUT_POLL_MS; i++) { fake_now_us += INPUT_POLL_MS * 1000; for (int b = 0; b < INPUT_BTN_MAX; b++) process(b, fake_now_us); } }
int main(void)
{
    input_init(INPUT_MODE_FOUR_BUTTONS, 1);
    // all GPIO stubs share one level; pressed_level differs for Reset (0), so set Reset "released" = 1
    // -> test only that a click is reported right after release, without the multi-click wait
    fake_level = 1; run_ms(100); fake_level = 0; run_ms(40);
    assert(n >= 1 && last_t == EVENT_BUTTON_CLICKED && last.clicks == 1);
    printf("input (4 buttons): click reported %d ms after release\n", 40);
    return 0;
}
