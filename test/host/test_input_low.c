// Host test of input.c with an active low button (button to GND)
#include <assert.h>
#include "input.h"
#include "bus.h"
int fake_level = 1; int64_t fake_now_us = 0;
static int n = 0; static input_event_t last; static event_type_t last_t;
esp_err_t bus_send_event(event_type_t type, void *data, size_t size) { last_t = type; memcpy(&last, data, sizeof(last)); n++; return ESP_OK; }
#include "input.c"
static void run_ms(int ms) { for (int i = 0; i < ms / INPUT_POLL_MS; i++) { fake_now_us += INPUT_POLL_MS * 1000; process(INPUT_BTN_MAIN, fake_now_us); } }
int main(void)
{
    input_init(INPUT_MODE_ONE_BUTTON, 0, 0);
    // idle line is high (pull-up): nothing happens
    run_ms(1000);
    assert(n == 0);
    // pressed = low
    fake_level = 0; run_ms(100); fake_level = 1; run_ms(500);
    assert(n == 1 && last_t == EVENT_BUTTON_CLICKED && last.clicks == 1);
    printf("input (active low): all tests passed\n");
    return 0;
}
