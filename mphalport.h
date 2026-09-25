#ifndef MPHALPORT_H
#define MPHALPORT_H

#include <stdint.h>
#include <stddef.h>
#include "py/mpprint.h"

// HAL delays
#define mp_hal_delay_us(us) mp_hal_delay_us_stub(us)
#define mp_hal_delay_ms(ms) mp_hal_delay_ms_stub(ms)

void mp_hal_delay_us_stub(uint32_t us);
void mp_hal_delay_ms_stub(uint32_t ms);

// Ticks
uint32_t mp_hal_ticks_ms(void);
uint32_t mp_hal_ticks_us(void);

// I/O
int mp_hal_stdin_rx_chr(void);
void mp_hal_stdout_tx_strn(const char *str, size_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);

// Interrupt control
void mp_hal_set_interrupt_char(int c);
int mp_hal_get_interrupt_char(void);

#endif // MPHALPORT_H