#include "PSM.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"

psm_t* psmRef = NULL;

// Function declarations
void zc_interrupt_handler();
void psm_timer_interrupt_handler(void* arg);
static void psm_calculate_skip_from_zc(void);
static void psm_update_control(bool force_disable);
static void calculate_skip(void);

esp_err_t psm_init(const gpio_num_t zc_pin, const gpio_num_t dimmer_pin, int range, int mode, int divider, int interrupt_min_time_diff) {
    psmRef = malloc(sizeof(psm_t));
    psmRef->sense_pin = zc_pin;
    psmRef->control_pin = dimmer_pin;
    psmRef->range = range;
    psmRef->mode = mode;
    psmRef->divider = divider;
    psmRef->interrupt_min_time_diff = interrupt_min_time_diff;
    psmRef->psm_interval_timer_initialized = false;
    psmRef->last_millis = 0;
    psmRef->divider_counter = 0;
    psmRef->a = 0;
    psmRef->skip = 0;
    psmRef->counter = 0;
    psmRef->stop_after = 0;
    

    ESP_RETURN_ON_FALSE(psmRef != NULL, ESP_ERR_NO_MEM, "PSM", "Failed to allocate memory for PSM handle");
 
    esp_rom_gpio_pad_select_gpio(psmRef->sense_pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(psmRef->sense_pin, GPIO_MODE_INPUT), "PSM",
                       "Failed to set sense pin direction");

    esp_rom_gpio_pad_select_gpio(psmRef->control_pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(psmRef->control_pin, GPIO_MODE_OUTPUT), "PSM",
                       "Failed to set control pin direction");

    psmRef->divider = psmRef->divider > 0 ? psmRef->divider : 1;

    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3&ESP_INTR_FLAG_EDGE), "PSM",
                       "Failed to install ISR service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(psmRef->sense_pin, zc_interrupt_handler, NULL), "PSM",
                       "Failed to add ISR handler");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(psmRef->sense_pin, psmRef->mode), "PSM",
                       "Failed to set interrupt type");

    return ESP_OK;
}

esp_err_t psm_deinit(void) {
    ESP_RETURN_ON_FALSE(psmRef != NULL, ESP_ERR_INVALID_ARG, "PSM", "Invalid handle");
    
    if (psmRef->psm_interval_timer) {
        ESP_RETURN_ON_ERROR(esp_timer_stop(psmRef->psm_interval_timer), "PSM", "Timer stop failed");
        ESP_RETURN_ON_ERROR(esp_timer_delete(psmRef->psm_interval_timer), "PSM", "Timer delete failed");
    }
    
    gpio_isr_handler_remove(psmRef->sense_pin);
    gpio_reset_pin(psmRef->sense_pin);
    gpio_reset_pin(psmRef->control_pin);
    
    return ESP_OK;
}

void onPSMInterrupt() {}

void zc_interrupt_handler() {
    uint64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds

    if (psmRef->interrupt_min_time_diff > 0 && current_time - psmRef->interrupt_min_time_diff < psmRef->last_millis) {
        if (current_time >= psmRef->last_millis) {
            return;
        }
    }
  psmRef->last_millis = current_time;
  psm_calculate_skip_from_zc();

  if (psmRef->psm_interval_timer_initialized) {
    esp_timer_stop(psmRef->psm_interval_timer);
    esp_timer_start_once(psmRef->psm_interval_timer, psmRef->timer_interval_us);
  }
}

void psm_timer_interrupt_handler(void* arg) {
  esp_timer_stop(psmRef->psm_interval_timer);
  psm_update_control(true);
}

void psm_set( unsigned int value) {
  if (value < psmRef->range) {
    psmRef->value = value;
  }
  else {
    psmRef->value = psmRef->range;
  }
}

int psm_get_value(void) {
  return psmRef->value;
}

long psm_get_counter(void) {
  return psmRef->counter;
}

void psm_reset_counter(void) {
  psmRef->counter = 0;
}

void psm_stop_after( long counter) {
  psmRef->stop_after = counter;
}

static void psm_calculate_skip_from_zc(void) {

  if (psmRef->divider_counter >= psmRef->divider - 1) {
    psmRef->divider_counter -= psmRef->divider - 1;
    calculate_skip();
  }
  else {
    psmRef->divider_counter++;
  }
  psm_update_control(false);
}

static void calculate_skip(void) {
  psmRef->a += psmRef->value;

  if (psmRef->a >= psmRef->range) {
    psmRef->a -= psmRef->range;
    psmRef->skip = false;
  }
  else {
    psmRef->skip = true;
  }

  if (psmRef->a > psmRef->range) {
    psmRef->a = 0;
    psmRef->skip = false;
  }

  if (!psmRef->skip) {
    psmRef->counter++;
  }

  if (!psmRef->skip
    && psmRef->stop_after > 0
    && psmRef->counter > psmRef->stop_after) {
    psmRef->skip = true;
  }
}

static void psm_update_control(bool force_disable) {
  if (force_disable || psmRef->skip) {
    gpio_set_level(psmRef->control_pin, 0);

  }
  else {
    gpio_set_level(psmRef->control_pin, 1);

  }
}

unsigned int psm_get_cps(void) {
  unsigned int range = psmRef->range;
  unsigned int value = psmRef->value;
  unsigned char divider = psmRef->divider;

  psmRef->range = 0xFFFF;
  psmRef->value = 1;
  psmRef->a = 0;
  psmRef->divider = 1;
  psmRef->skip = true;

  unsigned long stopAt = (esp_timer_get_time() / 1000) + 1000;

  while (esp_timer_get_time() / 1000 < stopAt) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  unsigned int result = psmRef->a;

  psmRef->range = range;
  psmRef->value = value;
  psmRef->a = 0;
  psmRef->divider = divider;

  return result;
}

unsigned long psm_get_last_millis(void) {
  return psmRef->last_millis;
}

unsigned char psm_get_divider(void) {
  return psmRef->divider;
}

void psm_set_divider( unsigned char divider) {
  psmRef->divider = divider > 0 ? divider : 1;
}

void psm_set_shift_divider_counter(char value) {
  psmRef->divider_counter += value;
}

void psm_init_timer(uint16_t delay) {
  uint32_t us = delay > 1000u ? delay : delay > 55u ? 5500u : 6600u;
  psmRef->timer_interval_us = us;

  esp_timer_create_args_t timer_args = {
    .callback = (esp_timer_cb_t)psm_timer_interrupt_handler,
    .name = "psm_timer"
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &psmRef->psm_interval_timer));
  ESP_ERROR_CHECK(esp_timer_start_once(psmRef->psm_interval_timer, us));

  psmRef->psm_interval_timer_initialized = true;
}