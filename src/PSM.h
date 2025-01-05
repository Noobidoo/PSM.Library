#ifndef PSM_H
#define PSM_H

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_timer.h"


typedef struct {
    gpio_num_t sense_pin;
    gpio_num_t control_pin;
    unsigned int range;
    gpio_int_type_t mode;
    unsigned char divider;
    unsigned char divider_counter;
    unsigned char interrupt_min_time_diff;
    volatile int timer_interval_us;
    volatile unsigned int a;
    volatile bool skip;
    volatile long counter;
    volatile long stop_after;
    volatile uint64_t last_millis;
    volatile esp_timer_handle_t* psm_interval_timer;
    volatile bool psm_interval_timer_initialized;
    volatile bool psm_interval_timer_initialized;
} psm_t;

// Function declarations
esp_err_t psm_init(psm_t *psm);
void psm_set(psm_t *psm, unsigned int value);
long psm_get_counter(psm_t *psm);
void psm_reset_counter(psm_t *psm);
void psm_stop_after(psm_t *psm, long counter);
unsigned int psm_get_cps(psm_t *psm);
void psm_set_divider(psm_t *psm, unsigned char divider);
void psm_init_timer(psm_t *psm, unsigned int timer_interval_us, esp_timer_cb_t timer_callback);

#endif
