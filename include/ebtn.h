#ifndef _EBTN_H
#define _EBTN_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void ebtn_pause() {
   extern esp_err_t button_pause();
   extern esp_err_t rotary_encoder_pause();

   rotary_encoder_pause();
   button_pause();
}

static inline void ebtn_start() {
   extern esp_err_t button_start();
   extern esp_err_t rotary_encoder_start();

   button_start();
   rotary_encoder_start();
}

#ifdef __cplusplus
}
#endif

#endif // _EBTN_H