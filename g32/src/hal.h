/* Hardware layer for BTT TFT35 V3.0.x (GD32F205VCT6) - GD32 SPL, no Arduino. */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_W 480
#define LINK_BAUD 1000000u   /* GD32 <-> ESP32 UART speed (also set in esp32/src/main.cpp) */
#define SCREEN_H 320

void     hal_init(void);                 /* vector table, SysTick, GPIO, UART, encoder, touch pins */
uint32_t millis(void);
void     delay_ms(uint32_t ms);

/* LCD (EXMC 16-bit parallel, ILI9488) */
void hal_lcd_init(void);
void hal_lcd_flush(int x1, int y1, int x2, int y2, const uint16_t *px);

/* UART bridge to ESP32: USART1, TX=PA2, RX=PA3, LINK_BAUD 8N1 */
void hal_uart_puts(const char *s);       /* sends the string + "\r\n" */
int  hal_uart_getc(void);                /* -1 when nothing waiting */
void hal_uart_write(const uint8_t *p, uint32_t n);   /* queued, sent by interrupt */
uint32_t hal_uart_tx_free(void);         /* bytes that can still be queued */

/* EC11 encoder (A=PA8, B=PC9, BTN=PC8) - sampled in the 1 kHz SysTick */
int  hal_enc_delta(void);                /* accumulated detents since last call, then cleared */
bool hal_enc_click(void);                /* true once per button press */
bool hal_enc_button_down(void);          /* raw level: true while the knob is held in */

/* XPT2046 resistive touch (software SPI) */
bool hal_touch_pen(void);                /* true while a finger is down */
bool hal_touch_read(uint16_t *rx, uint16_t *ry);   /* raw 12-bit values, false if noisy */
void hal_touch_peek(uint16_t *rx, uint16_t *ry);   /* raw averaged values, no validation (debug) */

/* Touch calibration kept in internal flash (one 2 KB page just below the BTT parameter page) */
bool hal_cal_load(uint16_t tx[3], uint16_t ty[3]);
void hal_cal_save(const uint16_t tx[3], const uint16_t ty[3]);

#ifdef __cplusplus
}
#endif
#endif
