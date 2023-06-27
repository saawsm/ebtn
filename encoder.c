#include "encoder.h"

#include <esp_check.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

static const char* TAG = "ebtn-encoder";

#define SEMAPHORE_TAKE()                                                                                                                                       \
   do {                                                                                                                                                        \
      if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(CONFIG_EBTN_POLLING_INTERVAL_US_ENC / 1000U))) {                                                                \
         ESP_LOGE(TAG, "Could not take mutex");                                                                                                                \
         return ESP_ERR_TIMEOUT;                                                                                                                               \
      }                                                                                                                                                        \
   } while (0)

#define SEMAPHORE_GIVE()                                                                                                                                       \
   do {                                                                                                                                                        \
      if (!xSemaphoreGive(mutex)) {                                                                                                                            \
         ESP_LOGE(TAG, "Could not give mutex");                                                                                                                \
         return ESP_FAIL;                                                                                                                                      \
      }                                                                                                                                                        \
   } while (0)

static esp_timer_handle_t timer;
static QueueHandle_t _queue;
static SemaphoreHandle_t mutex;

static ebtn_prepoll_cb_t _prepoll_callback = NULL;

static rotary_encoder_t* encoders[CONFIG_EBTN_MAX_COUNT_ENC] = {NULL};

static const uint8_t valid_states[] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

inline static void encoder_poll(rotary_encoder_t* enc) {
   if (!enc->poll_state_callback)
      return;

   const uint8_t state = ((enc->poll_state_callback(enc->pin_a) ^ enc->active_low) << 1) | (enc->poll_state_callback(enc->pin_b) ^ enc->active_low);

   enc->internal.code = ((enc->internal.code << 2) | state) & 0xf;

   if (valid_states[enc->internal.code]) {
      enc->internal.store = (enc->internal.store << 4) | enc->internal.code;

      rotary_encoder_event_t evt = {.sender = enc};

      if ((enc->internal.store & 0xff) == 0x2b) {
         evt.dir = ROT_COUNTERCLOCKWISE;
         xQueueSendToBack(_queue, &evt, 0);

      } else if ((enc->internal.store & 0xff) == 0x17) {
         evt.dir = ROT_CLOCKWISE;
         xQueueSendToBack(_queue, &evt, 0);
      }
   }
}

static void poll(void* arg) {
   if (_prepoll_callback)
      _prepoll_callback();

   if (!xSemaphoreTake(mutex, 0))
      return;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_ENC; i++) {
      if (encoders[i])
         encoder_poll(encoders[i]);
   }

   xSemaphoreGive(mutex);
}

static const esp_timer_create_args_t timer_args = {
    .arg = NULL,
    .name = "poll_encoders",
    .dispatch_method = ESP_TIMER_TASK,
    .callback = poll,
};

static uint8_t gpio_encoder_poll_state(gpio_num_t pin) {
   return gpio_get_level(pin);
}

esp_err_t rotary_encoder_init(QueueHandle_t queue) {
   ESP_RETURN_ON_FALSE(queue, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   _queue = queue;

   mutex = xSemaphoreCreateMutex();
   ESP_RETURN_ON_FALSE(mutex, ESP_ERR_NO_MEM, TAG, "Failed to create mutex");

   ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer), TAG, "Failed to create encoder timer");

   return esp_timer_start_periodic(timer, CONFIG_EBTN_POLLING_INTERVAL_US_ENC);
}

esp_err_t rotary_encoder_free() {
   SEMAPHORE_TAKE();

   esp_timer_stop(timer);
   esp_timer_delete(timer);
   timer = NULL;

   _queue = NULL;

   SEMAPHORE_GIVE();

   vSemaphoreDelete(mutex);
   return ESP_OK;
}

void rotary_encoder_set_prepoll_callback(ebtn_prepoll_cb_t prepoll_callback) {
   _prepoll_callback = prepoll_callback;
}

esp_err_t rotary_encoder_add(rotary_encoder_t* enc) {
   ESP_RETURN_ON_FALSE(enc, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   SEMAPHORE_TAKE();

   esp_err_t ret = ESP_ERR_NO_MEM;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_ENC; i++) {
      ESP_GOTO_ON_FALSE(encoders[i] != enc, ESP_ERR_INVALID_STATE, end, TAG, "Encoder already added");

      if (encoders[i])
         continue;

      enc->internal.code = 0;
      enc->internal.store = 0;

      if (!enc->poll_state_callback) {
         esp_rom_gpio_pad_select_gpio(enc->pin_a);
         ret = gpio_set_direction(enc->pin_a, GPIO_MODE_INPUT);
         if (ret != ESP_OK)
            break;

         esp_rom_gpio_pad_select_gpio(enc->pin_b);
         ret = gpio_set_direction(enc->pin_b, GPIO_MODE_INPUT);
         if (ret != ESP_OK)
            break;

         if (enc->internal_pull) {
            ret = gpio_set_pull_mode(enc->pin_a, enc->active_low ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
            if (ret != ESP_OK)
               break;

            ret = gpio_set_pull_mode(enc->pin_b, enc->active_low ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
            if (ret != ESP_OK)
               break;
         }

         enc->poll_state_callback = gpio_encoder_poll_state;
      }

      encoders[i] = enc;
      ret = ESP_OK;
      break;
   }

end:
   SEMAPHORE_GIVE();
   return ret;
}

esp_err_t rotary_encoder_remove(rotary_encoder_t* enc) {
   ESP_RETURN_ON_FALSE(enc, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   SEMAPHORE_TAKE();

   esp_err_t err = ESP_ERR_INVALID_ARG;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_ENC; i++) {
      if (encoders[i] != enc)
         continue;

      encoders[i] = NULL;

      err = ESP_OK;
      break;
   }

   SEMAPHORE_GIVE();

   return err;
}