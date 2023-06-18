/*
 * Basic encoder example using ESP-IDF GPIO functions (gpio_get_level)
 * See menuconfig for library options.
 */
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <button.h>
#include <encoder.h>

// Define encoder button (see button examples for button setup)
static button_t btn_enc = {.pin = GPIO_NUM_32, .active_low = true};

// Rotary encoder with button
static rotary_encoder_t enc = {
    .btn = &btn_enc,
    .pin_a = GPIO_NUM_33,
    .pin_b = GPIO_NUM_34,
    .active_low = true,
};

static QueueHandle_t enc_event_queue;

static void encoder_task(void* pvParameter) {
   enc_event_queue = xQueueCreate(5, sizeof(rotary_encoder_event_t));

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