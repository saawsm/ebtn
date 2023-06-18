/*
 * Button example using custom polling.
 * See menuconfig for library options.
 */
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <button.h>

static const char* BUTTON_STATE_NAMES[] = {
    [BUTTON_PRESSED] = "pressed",   //
    [BUTTON_RELEASED] = "released", //
    [BUTTON_CLICKED] = "clicked",   //
    [BUTTON_PRESSED_LONG] = "long press",
};

static uint8_t poll_state(gpio_num_t pin) {
   // return (pcf.data_in & (1 << pin)) > 0; // e.g. Use a port expander
   return gpio_get_level(pin); // Read pin state
}

static void prepoll_callback() {
   // ESP_ERROR_CHECK(pcf8574_read_port(&pcf)); // fetch new port state
}

// Define buttons
static button_t btn1 = {.pin = GPIO_NUM_32, .active_low = true, .poll_state_callback = poll_state};
static button_t btn2 = {.pin = GPIO_NUM_33, .active_low = true, .poll_state_callback = poll_state};
static button_t btn3 = {.pin = GPIO_NUM_34, .active_low = true, .poll_state_callback = poll_state};
static button_t btn4 = {.pin = GPIO_NUM_35, .active_low = true, .poll_state_callback = poll_state};

static QueueHandle_t btn_event_queue;

static void button_task(void* pvParameter) {
   btn_event_queue = xQueueCreate(5, sizeof(button_event_t));

   // Button states are fetched using the poll_state_callback function for each button.
   // For performance a single "prepoll" function is invoked before any button state polling takes place.
   // (e.g. Allowing for updated port expander readings)
   button_set_prepoll_callback(prepoll_callback);

   ESP_ERROR_CHECK(button_init(btn_event_queue)); // Init button library with event queue
   ESP_ERROR_CHECK(button_add(&btn1));            // Add buttons...
   ESP_ERROR_CHECK(button_add(&btn2));
   ESP_ERROR_CHECK(button_add(&btn3));
   ESP_ERROR_CHECK(button_add(&btn4));

   button_event_t e;
   while (true) {
      xQueueReceive(btn_event_queue, &e, portMAX_DELAY); // Block until button event is available

      uint8_t idx = 0;
      if (e.sender == &btn1) {
         idx = 1;
      } else if (e.sender == &btn2) {
         idx = 2;
      } else if (e.sender == &btn3) {
         idx = 3;
      } else if (e.sender == &btn4) {
         idx = 4;
      }

      if (e.count > 1) {
         printf("Button %u was %s, %u times\n", idx, BUTTON_STATE_NAMES[e.type], e.count);
      } else {
         printf("Button %u was %s, %u times - delta %" PRIu32 " ms\n", idx, BUTTON_STATE_NAMES[e.type], e.count, e.delta_ms);
      }
   }

   ESP_ERROR_CHECK(button_free()); // Cleanup button library
   vQueueDelete(btn_event_queue);
}

void app_main() {
   xTaskCreate(button_task, "button_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}