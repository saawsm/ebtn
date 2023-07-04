#include "button.h"

#include <esp_check.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

static const char* TAG = "ebtn-button";

#define SEMAPHORE_TAKE()                                                                                                                                       \
   do {                                                                                                                                                        \
      if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(CONFIG_EBTN_POLLING_INTERVAL_MS_BTN))) {                                                                        \
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

static uint32_t time_ms = 0;

static button_t* buttons[CONFIG_EBTN_MAX_COUNT_BTN] = {NULL};
static button_t* lastPressed[CONFIG_EBTN_MAX_COUNT_BTN_GROUPS] = {NULL};

static esp_timer_handle_t timer;
static QueueHandle_t _queue;
static SemaphoreHandle_t mutex;

static ebtn_prepoll_cb_t _prepoll_callback = NULL;

inline static void poll_button(button_t* btn) {
   if (!btn->poll_state_callback)
      return;

   const uint8_t pressed = btn->poll_state_callback(btn->pin) ^ btn->active_low;
   const uint32_t delta = time_ms - btn->internal.last_changed_ms; // milliseconds since button state changed

   button_event_t evt = {.sender = btn, .delta_ms = 0, .count = 1};

   if (btn->internal.state != pressed) { // button state changed (released -> pressed, or pressed -> released)
      btn->internal.state = pressed;
      btn->internal.last_changed_ms = time_ms;

      if (!btn->internal.state) { // released transition (pressed -> released)

         evt.type = BUTTON_RELEASED;
         evt.delta_ms = delta;
         xQueueSendToBack(_queue, &evt, 0);

         if (delta < CONFIG_EBTN_CLICK_MAX_MS) { // button was tapped
            btn->internal.click_count++;         // increment consecutive click counter
            btn->internal.previous_delta_ms = delta;

         } else if (btn->internal.long_press_pending && lastPressed[btn->group] == btn) { // slow press->release
                                                                                          // fire event immediately, since button was held for awhile
            evt.type = BUTTON_CLICKED;
            evt.delta_ms = delta;
            xQueueSendToBack(_queue, &evt, 0);

            btn->internal.click_count = 0;
         }

      } else { // pressed transition (released -> pressed)
         evt.type = BUTTON_PRESSED;
         evt.delta_ms = delta;
         xQueueSendToBack(_queue, &evt, 0);

         lastPressed[btn->group] = btn;
         btn->internal.long_press_pending = true;
      }
   } else if (btn->internal.state) { // pressed

      // if button is held for >EBTN_LONG_PRESS_MIN_MS fire long pressed event
      if (delta > CONFIG_EBTN_LONG_PRESS_MIN_MS && btn->internal.long_press_pending) {
         evt.type = BUTTON_PRESSED_LONG;
         evt.count = btn->internal.click_count + 1;
         xQueueSendToBack(_queue, &evt, 0);

         btn->internal.long_press_pending = false;
      }

   } else if (delta > CONFIG_EBTN_CLICK_MAX_MS && btn->internal.click_count > 0) {
      // when button is released and after CLICK_MAX_MS, process any recorded consecutive fast clicks (e.g. double or triple clicks)
      if (btn->internal.long_press_pending && lastPressed[btn->group] == btn) {
         evt.type = BUTTON_CLICKED;
         evt.count = btn->internal.click_count;
         evt.delta_ms = (evt.count == 1) ? btn->internal.previous_delta_ms : 0;
         xQueueSendToBack(_queue, &evt, 0);
      }

      btn->internal.click_count = 0;
   }
}

static void poll(void* arg) {
   if (_prepoll_callback)
      _prepoll_callback();

   if (!xSemaphoreTake(mutex, 0))
      return;

   time_ms += CONFIG_EBTN_POLLING_INTERVAL_MS_BTN;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_BTN; i++) {
      if (buttons[i])
         poll_button(buttons[i]);
   }

   xSemaphoreGive(mutex);
}

static const esp_timer_create_args_t timer_args = {
    .arg = NULL,
    .name = "poll_buttons",
    .dispatch_method = ESP_TIMER_TASK,
    .callback = poll,
};

static uint8_t gpio_button_poll_state(gpio_num_t pin) {
   return gpio_get_level(pin);
}

esp_err_t button_init(QueueHandle_t queue) {
   ESP_RETURN_ON_FALSE(queue, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   _queue = queue;
   time_ms = 0;

   mutex = xSemaphoreCreateMutex();
   ESP_RETURN_ON_FALSE(mutex, ESP_ERR_NO_MEM, TAG, "Failed to create mutex");

   ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer), TAG, "Failed to create button timer");

   return button_start();
}

esp_err_t button_free() {
   SEMAPHORE_TAKE();

   button_pause();
   esp_timer_delete(timer);
   timer = NULL;

   _queue = NULL;

   SEMAPHORE_GIVE();

   vSemaphoreDelete(mutex);
   return ESP_OK;
}

esp_err_t button_pause() {
   return esp_timer_stop(timer);
}

esp_err_t button_start() {
   return esp_timer_start_periodic(timer, CONFIG_EBTN_POLLING_INTERVAL_MS_BTN * 1000);
}

void button_set_prepoll_callback(ebtn_prepoll_cb_t prepoll_callback) {
   _prepoll_callback = prepoll_callback;
}

esp_err_t button_add(button_t* btn) {
   ESP_RETURN_ON_FALSE(btn, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");
   ESP_RETURN_ON_FALSE(btn->group < CONFIG_EBTN_MAX_COUNT_BTN_GROUPS, ESP_ERR_INVALID_STATE, TAG, "Invalid button group");

   SEMAPHORE_TAKE();

   esp_err_t ret = ESP_ERR_NO_MEM;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_BTN; i++) {
      ESP_GOTO_ON_FALSE(buttons[i] != btn, ESP_ERR_INVALID_STATE, end, TAG, "Button already added");

      if (buttons[i])
         continue;

      btn->internal.state = 0;
      btn->internal.last_changed_ms = 0;
      btn->internal.click_count = 0;
      btn->internal.long_press_pending = true;
      btn->internal.previous_delta_ms = 0;

      if (!btn->poll_state_callback) {
         esp_rom_gpio_pad_select_gpio(btn->pin);
         ret = gpio_set_direction(btn->pin, GPIO_MODE_INPUT);
         if (ret != ESP_OK)
            break;

         if (btn->internal_pull) {
            ret = gpio_set_pull_mode(btn->pin, btn->active_low ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
            if (ret != ESP_OK)
               break;
         }

         btn->poll_state_callback = gpio_button_poll_state;
      }

      buttons[i] = btn;
      ret = ESP_OK;
      break;
   }

end:
   SEMAPHORE_GIVE();
   return ret;
}

esp_err_t button_remove(button_t* btn) {
   ESP_RETURN_ON_FALSE(btn, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   SEMAPHORE_TAKE();

   esp_err_t err = ESP_ERR_INVALID_ARG;

   for (uint8_t i = 0; i < CONFIG_EBTN_MAX_COUNT_BTN; i++) {
      if (buttons[i] != btn)
         continue;

      buttons[i] = NULL;

      err = ESP_OK;
      break;
   }

   SEMAPHORE_GIVE();

   return err;
}