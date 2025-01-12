#include "PSM.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"

// Function declarations
void zc_interrupt_handler(void* arg);
void psm_timer_interrupt_handler(void* arg);
static void psm_calculate_skip_from_zc(psm_t *psm);
static void psm_update_control(psm_t* psm, bool force_disable);
static void calculate_skip(psm_t *psm);

esp_err_t psm_init(psm_t *psm) {
    ESP_RETURN_ON_FALSE(psm != NULL, ESP_ERR_INVALID_ARG, "PSM", "Invalid PSM handle");
    
    esp_rom_gpio_pad_select_gpio(psm->sense_pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(psm->sense_pin, GPIO_MODE_INPUT), "PSM",
                       "Failed to set sense pin direction");

    esp_rom_gpio_pad_select_gpio(psm->control_pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(psm->control_pin, GPIO_MODE_OUTPUT), "PSM",
                       "Failed to set control pin direction");

    psm->divider = psm->divider > 0 ? psm->divider : 1;

    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), "PSM",
                       "Failed to install ISR service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(psm->sense_pin, zc_interrupt_handler, (void*)psm), "PSM",
                       "Failed to add ISR handler");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(psm->sense_pin, psm->mode), "PSM",
                       "Failed to set interrupt type");

    return ESP_OK;
}

esp_err_t psm_deinit(psm_t *psm) {
    ESP_RETURN_ON_FALSE(psm != NULL, ESP_ERR_INVALID_ARG, "PSM", "Invalid handle");
    
    if (psm->psm_interval_timer) {
        ESP_RETURN_ON_ERROR(esp_timer_stop(psm->psm_interval_timer), "PSM", "Timer stop failed");
        ESP_RETURN_ON_ERROR(esp_timer_delete(psm->psm_interval_timer), "PSM", "Timer delete failed");
    }
    
    gpio_isr_handler_remove(psm->sense_pin);
    gpio_reset_pin(psm->sense_pin);
    gpio_reset_pin(psm->control_pin);
    
    return ESP_OK;
}

void onPSMInterrupt() {}

void zc_interrupt_handler(void* arg) {
    psm_t *psm = (psm_t*)arg;
    uint64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds

    if (psm->interrupt_min_time_diff > 0) {
        uint64_t time_diff = current_time - psm->interrupt_min_time_diff;
        if (time_diff < psm->last_millis) {
            return;
        }
    }

  psm_calculate_skip_from_zc(psm);

  if (psm->psm_interval_timer_initialized) {
    esp_timer_stop(psm->psm_interval_timer);
    esp_timer_start_once(psm->psm_interval_timer, psm->timer_interval_us);
  }
}

void psm_timer_interrupt_handler(void* arg) {
  psm_t *psm = (psm_t*)arg;
  esp_timer_stop(psm->psm_interval_timer);
  psm_update_control(psm, true);
}

void psm_set(psm_t *psm, unsigned int value) {
  if (value < psm->range) {
    psm->value = value;
  }
  else {
    psm->value = psm->range;
  }
}

long psm_get_counter(psm_t *psm) {
  return psm->counter;
}

void psm_reset_counter(psm_t *psm) {
  psm->counter = 0;
}

void psm_stop_after(psm_t *psm, long counter) {
  psm->stop_after = counter;
}

static void psm_calculate_skip_from_zc(psm_t *psm) {
  if (psm->divider_counter >= psm->divider - 1) {
    psm->divider_counter -= psm->divider - 1;
    calculate_skip(psm);
  }
  else {
    psm->divider_counter++;
  }
  psm_update_control(psm, false);
}

static void calculate_skip(psm_t *psm) {
  psm->a += psm->value;

  if (psm->a >= psm->range) {
    psm->a -= psm->range;
    psm->skip = false;
  }
  else {
    psm->skip = true;
  }

  if (psm->a > psm->range) {
    psm->a = 0;
    psm->skip = false;
  }

  if (!psm->skip) {
    psm->counter++;
  }

  if (!psm->skip
    && psm->stop_after > 0
    && psm->counter > psm->stop_after) {
    psm->skip = true;
  }
}

static void psm_update_control(psm_t* psm, bool force_disable) {
  if (force_disable || psm->skip) {
    gpio_set_level(psm->control_pin, 0);
  }
  else {
    gpio_set_level(psm->control_pin, 1);
  }
}

unsigned int psm_get_cps(psm_t *psm) {
  unsigned int range = psm->range;
  unsigned int value = psm->value;
  unsigned char divider = psm->divider;

  psm->range = 0xFFFF;
  psm->value = 1;
  psm->a = 0;
  psm->divider = 1;
  psm->skip = true;

  unsigned long stopAt = (esp_timer_get_time() / 1000) + 1000;

  while (esp_timer_get_time() / 1000 < stopAt) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  unsigned int result = psm->a;

  psm->range = range;
  psm->value = value;
  psm->a = 0;
  psm->divider = divider;

  return result;
}

unsigned long psm_get_last_millis(psm_t *psm) {
  return psm->last_millis;
}

unsigned char psm_get_divider(psm_t *psm) {
  return psm->divider;
}

void psm_set_divider(psm_t *psm, unsigned char divider) {
  psm->divider = divider > 0 ? divider : 1;
}

void psm_set_shift_divider_counter(psm_t *psm,char value) {
  psm->divider_counter += value;
}

void psm_init_timer(psm_t *psm, uint16_t delay) {
  uint32_t us = delay > 1000u ? delay : delay > 55u ? 5500u : 6600u;
  psm->timer_interval_us = us;

  esp_timer_create_args_t timer_args = {
    .callback = (esp_timer_cb_t)psm_timer_interrupt_handler,
    .arg = psm,
    .name = "psm_timer"
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &psm->psm_interval_timer));
  ESP_ERROR_CHECK(esp_timer_start_once(psm->psm_interval_timer, us));

  psm->psm_interval_timer_initialized = true;
}