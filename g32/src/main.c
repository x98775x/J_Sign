/* BTT GD TFT35 V3.0 (GD32F205VC) hardware startup diagnostic.
 * Uses GD32 SPL and BTT's EXMC LCD bus, not STM32 Arduino HAL.
 * Expected: backlight ON, RED/GREEN/BLUE/WHITE horizontal bars,
 * USART1 PA2 TX (RX=PA3) -> TTL RX at 115200: GD32 START / LCD READY / ALIVE.
 */
#include "gd32f20x.h"
/* Include the SPL peripheral headers directly; don't rely on gd32f20x_libopt.h. */
#include "gd32f20x_rcu.h"
#include "gd32f20x_gpio.h"
#include "gd32f20x_usart.h"
#include "gd32f20x_exmc.h"
#include <stdint.h>

#define LCD_COMMAND (*(volatile uint16_t *)0x60FFFFFEu)
#define LCD_DATA    (*(volatile uint16_t *)0x61000000u)

static void sleep_ms(uint32_t ms) {
    /* Busy-wait that doesn't require SysTick/interrupts. */
    while (ms--) {
        for (volatile uint32_t i = 0; i < 18000u; ++i) { __asm volatile("nop"); }
    }
}

static void serial_init(void) {
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART1);
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);   /* PA2 = USART1 TX */
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_3);  /* PA3 = USART1 RX */
    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200u);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_enable(USART1);
}

static void serial_puts(const char *s) {
    while (*s) {
        while (RESET == usart_flag_get(USART1, USART_FLAG_TBE)) {}
        usart_data_transmit(USART1, (uint8_t)*s++);
    }
    while (RESET == usart_flag_get(USART1, USART_FLAG_TC)) {}
}

static void lcd_gpio_init(void) {
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_EXMC);

    /* 16-bit FSMC bus as on BTT's own GD32 lcd.c */
    const uint32_t dmask = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 |
        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7;
    const uint32_t emask = GPIO_PIN_2 | GPIO_PIN_7 | GPIO_PIN_8 |
        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init(GPIOD, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, dmask);
    gpio_init(GPIOE, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, emask);

    /* Backlight PD12 is a regular GPIO, not EXMC. */
    gpio_init(GPIOD, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
    gpio_bit_set(GPIOD, GPIO_PIN_12);
}

static void lcd_exmc_init(void) {
    exmc_norsram_parameter_struct p;
    exmc_norsram_timing_parameter_struct timing;
    exmc_norsram_timing_parameter_struct write_timing;
    timing.asyn_address_setuptime = 1u;
    timing.asyn_address_holdtime = 0u;
    timing.asyn_data_setuptime = 15u;
    timing.bus_latency = 0u;
    timing.syn_clk_division = 0u;
    timing.syn_data_latency = 0u;
    timing.asyn_access_mode = EXMC_ACCESS_MODE_A;
    write_timing = timing;
    write_timing.asyn_data_setuptime = 8u;

    p.norsram_region = EXMC_BANK0_NORSRAM_REGION0;
    p.write_mode = EXMC_ASYN_WRITE;
    p.extended_mode = DISABLE;
    p.asyn_wait = DISABLE;
    p.nwait_signal = DISABLE;
    p.memory_write = ENABLE;
    p.nwait_config = EXMC_NWAIT_CONFIG_BEFORE;
    p.wrap_burst_mode = DISABLE;
    p.nwait_polarity = EXMC_NWAIT_POLARITY_LOW;
    p.burst_mode = DISABLE;
    p.databus_width = EXMC_NOR_DATABUS_WIDTH_16B;
    p.memory_type = EXMC_MEMORY_TYPE_NOR;
    p.address_data_mux = DISABLE;
    p.read_write_timing = &timing;
    p.write_timing = &write_timing;
    exmc_norsram_init(&p);
    exmc_norsram_enable(EXMC_BANK0_NORSRAM_REGION0);
}

static void cmd(uint16_t c) { LCD_COMMAND = c; }
static void data(uint16_t d) { LCD_DATA = d; }

static void lcd_controller_init(void) {
    /* Controller sequence used by BTT for ILI9488/ST7796S 16-bit mode. */
    cmd(0x01); sleep_ms(150);
    cmd(0xC0); data(0x0C); data(0x02);
    cmd(0xC1); data(0x44);
    cmd(0xC5); data(0x00); data(0x16); data(0x80);
    cmd(0x36); data(0x28);
    cmd(0x3A); data(0x55);
    cmd(0xB0); data(0x00);
    cmd(0xB1); data(0xB0);
    cmd(0xB4); data(0x02);
    cmd(0xB6); data(0x02); data(0x02);
    cmd(0xE9); data(0x00);
    cmd(0xF7); data(0xA9); data(0x51); data(0x2C); data(0x82);
    cmd(0x11); sleep_ms(150);
    cmd(0x29); sleep_ms(50);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    cmd(0x2A);
    data(x0 >> 8); data(x0 & 255); data(x1 >> 8); data(x1 & 255);
    cmd(0x2B);
    data(y0 >> 8); data(y0 & 255); data(y1 >> 8); data(y1 & 255);
    cmd(0x2C);
}

static void lcd_bar(uint16_t y0, uint16_t y1, uint16_t color) {
    lcd_window(0, y0, 479, y1);
    for (uint32_t i = 0; i < 480u * (uint32_t)(y1-y0+1u); ++i)
        data(color);
}

int main(void) {
    /* Startup and clock initialization are supplied by GD32's SPL framework. */
    serial_init();
    serial_puts("GD32 START\r\n");
    lcd_gpio_init();
    serial_puts("BACKLIGHT ON\r\n");
    lcd_exmc_init();
    lcd_controller_init();
    serial_puts("LCD READY\r\n");
    lcd_bar(0, 79, 0xF800);
    lcd_bar(80, 159, 0x07E0);
    lcd_bar(160, 239, 0x001F);
    lcd_bar(240, 319, 0xFFFF);
    serial_puts("BARS COMPLETE\r\n");
    for (;;) {
        serial_puts("ALIVE\r\n");
        sleep_ms(1000);
    }
}
