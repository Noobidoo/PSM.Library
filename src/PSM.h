#ifndef PSM_H
#define PSM_H

#include <esp_err.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "external.h"

typedef struct {
    gpio_num_t sense_pin;
    gpio_num_t control_pin;
    gpio_int_type_t mode;
    esp_timer_handle_t psm_interval_timer;
    unsigned int range;
    unsigned int value;
    unsigned int divider;
    unsigned int divider_counter;
    unsigned int interrupt_min_time_diff;
    volatile int timer_interval_us;
    volatile unsigned int a;
    volatile bool skip;
    volatile long counter;
    volatile long stop_after;
    volatile uint64_t last_millis;
    volatile bool psm_interval_timer_initialized;
} psm_t;

EXTERN psm_t* psmRef;

// Function declarations
esp_err_t psm_init(const gpio_num_t zc_pin, const gpio_num_t dimmer_pin, int range, int mode, int divider, int interrupt_min_time_diff);
esp_err_t psm_deinit();
void psm_set(unsigned int value);
long psm_get_counter();
void psm_reset_counter();
void psm_stop_after(long counter);
unsigned int psm_get_cps();
int psm_get_value(void)
void psm_set_divider(unsigned char divider);
void psm_init_timer(uint16_t delay);
void psm_set_shift_divider_counter(char value);

#endif
