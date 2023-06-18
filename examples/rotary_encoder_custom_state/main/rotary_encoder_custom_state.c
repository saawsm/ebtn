/*
 * Encoder example using custom polling.
 * See menuconfig for library options.
 */
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <button.h>
#include <encoder.h>

static uint8_t poll_state(gpio_num_t pin) {
   // return (pcf.data_in & (1 << pin)) > 0; // e.g. Use a port expander
   return gpio_get_level(pin); // Read pin state
}

static void prepoll_callback() {
   // ESP_ERROR_CHECK(pcf8574_read_port(&pcf)); // fetch new port state
}

// Define encoder button (see button examples for button setup)
static button_t btn_enc = {.pin = GPIO_NUM_32, .active_low = true};

// Rotary encoder with button
static rotary_encoder_t enc = {
    .btn = &btn_enc,
    .pin_a = GPIO_NUM_33,
    .pin_b = GPIO_NUM_34,
    .active_low = true,
    .poll_state_callback = poll_state,
};

static QueueHandle_t enc_event_queue;

static void encoder_task(void* pvParameter) {
   enc_event_queue = xQueueCreate(5, sizeof(rotary_encoder_event_t));

   // Encoder pin states are fetched using the poll_state_callback function for each encoder.
   // For performance a single "prepoll" function is invoked before any encoder state polling takes place.
   // (e.g. Allowing for updated port expander readings)
   // Note: Since encoders are typically polled faster than buttons, this callback could be used for both encoders and buttons that share the same port expander (making
   // button_set_prepoll_callback unnecessary).
   rotary_encoder_set_prepoll_callback(prepoll_callback);

   ESP_ERROR_CHECK(rotary_encoder_init(enc_event_queue)); // Init rotary encoder library
   ESP_ERROR_CHECK(rotary_encoder_add(&enc));             // Add encoders...

   int16_t pos = 0;

   rotary_encoder_event_t e;
   while (true) {
      xQueueReceive(enc_event_queue, &e, portMAX_DELAY); // Block until encoder event is available

      pos += e.dir;

      printf("rotate: %d (%d)\n", e.dir, pos);
   }

   ESP_ERROR_CHECK(rotary_encoder_free()); // Cleanup rotary encoder library
   vQueueDelete(enc_event_queue);
}

void app_main() {
   xTaskCreate(encoder_task, "encoder_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}