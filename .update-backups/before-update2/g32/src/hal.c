#include "gd32f20x.h"
#include "gd32f20x_rcu.h"
#include "gd32f20x_gpio.h"
#include "gd32f20x_usart.h"
#include "gd32f20x_exmc.h"
#include "gd32f20x_fmc.h"
#include "hal.h"

#define APP_VECTOR_TABLE 0x08003000u   /* BTT bootloader owns 0x08000000..0x08002FFF */

#define LCD_COMMAND (*(volatile uint16_t *)0x60FFFFFEu)
#define LCD_DATA    (*(volatile uint16_t *)0x61000000u)

/* ------------------------------------------------------------------ */
/* Time                                                               */
/* ------------------------------------------------------------------ */
static volatile uint32_t g_ms;

uint32_t millis(void) { return g_ms; }

void delay_ms(uint32_t ms) {
    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < ms) { __NOP(); }
}

/* ------------------------------------------------------------------ */
/* Encoder: sampled every 1 ms from SysTick                           */
/* ------------------------------------------------------------------ */
static volatile int     enc_acc;
static volatile uint8_t btn_evt;

static void enc_poll(void) {
    /* Gray-code quadrature; reject invalid changes and only emit once per detent. */
    static uint8_t prev = 3u;
    static int8_t quarter = 0;
    static uint8_t btn_cnt = 0, btn_state = 1;
    const uint8_t a = (SET == gpio_input_bit_get(GPIOA, GPIO_PIN_8)) ? 1u : 0u;
    const uint8_t b = (SET == gpio_input_bit_get(GPIOC, GPIO_PIN_9)) ? 1u : 0u;
    const uint8_t k = (SET == gpio_input_bit_get(GPIOC, GPIO_PIN_8)) ? 1u : 0u;
    const uint8_t now = (uint8_t)((a << 1) | b);
    /* Old A-falling/B-high counted as positive; invert to match clockwise forward. */
    static const int8_t step[16] = {
         0, -1,  1, 0,
         1,  0,  0,-1,
        -1,  0,  0, 1,
         0,  1, -1, 0
    };
    const int8_t q = step[(prev << 2) | now];
    if (q) {
        if ((quarter > 0 && q < 0) || (quarter < 0 && q > 0)) quarter = 0;
        quarter += q;
        if (quarter >= 4) { enc_acc--; quarter = 0; }
        else if (quarter <= -4) { enc_acc++; quarter = 0; }
    }
    prev = now;
    if (k != btn_state) {
        if (++btn_cnt >= 15u) {
            btn_state = k;
            btn_cnt = 0;
            if (btn_state == 0u) btn_evt = 1u;
        }
    } else btn_cnt = 0;
}

void SysTick_Handler(void) {
    g_ms++;
    enc_poll();
}

int hal_enc_delta(void) {
    __disable_irq();
    int d = enc_acc;
    enc_acc = 0;
    __enable_irq();
    return d;
}

bool hal_enc_click(void) {
    if (btn_evt) { btn_evt = 0; return true; }
    return false;
}

bool hal_enc_button_down(void) {
    return RESET == gpio_input_bit_get(GPIOC, GPIO_PIN_8);
}

/* ------------------------------------------------------------------ */
/* Calibration storage: page at 0x0803F000 (linker script stops the app at 0x0803F000;  */
/* BTT's own parameter page is 0x0803F800, left alone).                                 */
/* ------------------------------------------------------------------ */
#define CAL_ADDR  0x0803F000u
#define CAL_MAGIC 0x4A53434Cu     /* "JSCL" */

static uint32_t cal_checksum(const uint32_t *w) {
    return w[0] ^ w[1] ^ w[2] ^ w[3] ^ 0xA5A5A5A5u;
}

bool hal_cal_load(uint16_t tx[3], uint16_t ty[3]) {
    const uint32_t *w = (const uint32_t *)CAL_ADDR;
    if (w[0] != CAL_MAGIC || w[4] != cal_checksum(w)) return false;
    for (int i = 0; i < 3; ++i) {
        tx[i] = (uint16_t)(w[1 + i] >> 16);
        ty[i] = (uint16_t)(w[1 + i] & 0xFFFFu);
    }
    return true;
}

void hal_cal_save(const uint16_t tx[3], const uint16_t ty[3]) {
    uint32_t w[5];
    w[0] = CAL_MAGIC;
    for (int i = 0; i < 3; ++i) w[1 + i] = ((uint32_t)tx[i] << 16) | ty[i];
    w[4] = cal_checksum(w);

    __disable_irq();                       /* flash stalls the CPU while it erases */
    fmc_unlock();
    fmc_page_erase(CAL_ADDR);
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    const uint16_t *hw = (const uint16_t *)w;
    for (int i = 0; i < 10; ++i) {         /* GD32F20x programs 16 bits at a time */
        fmc_halfword_program(CAL_ADDR + (uint32_t)i * 2u, hw[i]);
        fmc_flag_clear(FMC_FLAG_BANK0_END);
        fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
        fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    }
    fmc_lock();
    __enable_irq();
}

/* ------------------------------------------------------------------ */
/* UART bridge (USART1 PA2/PA3) with interrupt-driven RX ring buffer  */
/* ------------------------------------------------------------------ */
#define RXBUF_SIZE 256u
static volatile uint8_t  rxbuf[RXBUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

void USART1_IRQHandler(void) {
    if (RESET != usart_flag_get(USART1, USART_FLAG_RBNE) ||
        RESET != usart_flag_get(USART1, USART_FLAG_ORERR)) {
        uint8_t c = (uint8_t)usart_data_receive(USART1);   /* also clears ORERR */
        uint16_t next = (uint16_t)((rx_head + 1u) % RXBUF_SIZE);
        if (next != rx_tail) { rxbuf[rx_head] = c; rx_head = next; }
    }
}

static void uart_init(void) {
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART1);
    gpio_init(GPIOA, GPIO_MODE_AF_PP,        GPIO_OSPEED_50MHZ, GPIO_PIN_2);  /* TX */
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING,  GPIO_OSPEED_50MHZ, GPIO_PIN_3);  /* RX */
    usart_deinit(USART1);
    usart_baudrate_set(USART1, 460800u);
    usart_word_length_set(USART1, USART_WL_8BIT);
    usart_stop_bit_set(USART1, USART_STB_1BIT);
    usart_parity_config(USART1, USART_PM_NONE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_enable(USART1);
    usart_interrupt_enable(USART1, USART_INT_RBNE);
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

static void uart_putc(char c) {
    while (RESET == usart_flag_get(USART1, USART_FLAG_TBE)) {}
    usart_data_transmit(USART1, (uint8_t)c);
}

void hal_uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
    uart_putc('\r');
    uart_putc('\n');
}

int hal_uart_getc(void) {
    if (rx_tail == rx_head) return -1;
    uint8_t c = rxbuf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) % RXBUF_SIZE);
    return c;
}

/* ------------------------------------------------------------------ */
/* LCD                                                                */
/* ------------------------------------------------------------------ */
static void lcd_cmd(uint16_t c)  { LCD_COMMAND = c; }
static void lcd_data(uint16_t d) { LCD_DATA = d; }

static void lcd_gpio_init(void) {
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_EXMC);

    const uint32_t dmask = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 |
        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7;
    const uint32_t emask = GPIO_PIN_2 | GPIO_PIN_7 | GPIO_PIN_8 |
        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init(GPIOD, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, dmask);
    gpio_init(GPIOE, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, emask);

    gpio_init(GPIOD, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);   /* backlight */
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

static void lcd_controller_init(void) {
    /* Same ILI9488 sequence as the hardware diagnostic that produced the colour bars. */
    lcd_cmd(0x01); delay_ms(150);
    lcd_cmd(0xC0); lcd_data(0x0C); lcd_data(0x02);
    lcd_cmd(0xC1); lcd_data(0x44);
    lcd_cmd(0xC5); lcd_data(0x00); lcd_data(0x16); lcd_data(0x80);
    lcd_cmd(0x36); lcd_data(0x28);
    lcd_cmd(0x3A); lcd_data(0x55);
    lcd_cmd(0xB0); lcd_data(0x00);
    lcd_cmd(0xB1); lcd_data(0xB0);
    lcd_cmd(0xB4); lcd_data(0x02);
    lcd_cmd(0xB6); lcd_data(0x02); lcd_data(0x02);
    lcd_cmd(0xE9); lcd_data(0x00);
    lcd_cmd(0xF7); lcd_data(0xA9); lcd_data(0x51); lcd_data(0x2C); lcd_data(0x82);
    lcd_cmd(0x11); delay_ms(150);
    lcd_cmd(0x29); delay_ms(50);
}

void hal_lcd_init(void) {
    lcd_gpio_init();
    lcd_exmc_init();
    lcd_controller_init();
}

void hal_lcd_flush(int x1, int y1, int x2, int y2, const uint16_t *px) {
    lcd_cmd(0x2A);
    lcd_data((uint16_t)(x1 >> 8)); lcd_data((uint16_t)(x1 & 255));
    lcd_data((uint16_t)(x2 >> 8)); lcd_data((uint16_t)(x2 & 255));
    lcd_cmd(0x2B);
    lcd_data((uint16_t)(y1 >> 8)); lcd_data((uint16_t)(y1 & 255));
    lcd_data((uint16_t)(y2 >> 8)); lcd_data((uint16_t)(y2 & 255));
    lcd_cmd(0x2C);
    uint32_t n = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);
    while (n--) LCD_DATA = *px++;
}

/* ------------------------------------------------------------------ */
/* XPT2046 touch, software SPI mode 3 (pins from BTT pin_TFT35_V3_0.h) */
/*   CS=PE6  SCK=PE5  MISO=PE4  MOSI=PE3  PEN(IRQ)=PC13                */
/* ------------------------------------------------------------------ */
#define T_CS_H()   gpio_bit_set(GPIOE, GPIO_PIN_6)
#define T_CS_L()   gpio_bit_reset(GPIOE, GPIO_PIN_6)
#define T_SCK_H()  gpio_bit_set(GPIOE, GPIO_PIN_5)
#define T_SCK_L()  gpio_bit_reset(GPIOE, GPIO_PIN_5)
#define T_MOSI_H() gpio_bit_set(GPIOE, GPIO_PIN_3)
#define T_MOSI_L() gpio_bit_reset(GPIOE, GPIO_PIN_3)
#define T_MISO()   (SET == gpio_input_bit_get(GPIOE, GPIO_PIN_4))

#define XPT_CMD_RDX 0xD0   /* same as BTT for non-MKS boards */
#define XPT_CMD_RDY 0x90

static void touch_init(void) {
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOE);
    gpio_init(GPIOE, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6);
    gpio_init(GPIOE, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_4);
    gpio_init(GPIOC, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    T_CS_H();
    T_SCK_H();
}

static void t_delay(void) {
    for (volatile int i = 0; i < 40; ++i) { __NOP(); }
}

static uint8_t t_xfer(uint8_t tx) {
    uint8_t rx = 0;
    for (int i = 0; i < 8; ++i) {
        T_SCK_L();
        if (tx & 0x80) T_MOSI_H(); else T_MOSI_L();
        tx <<= 1;
        t_delay();
        T_SCK_H();
        t_delay();
        rx <<= 1;
        if (T_MISO()) rx |= 1u;
    }
    return rx;
}

static uint16_t t_read_ad(uint8_t cmd) {
    T_CS_L();
    t_xfer(cmd);
    uint16_t v = t_xfer(0xFF);
    v = (uint16_t)((v << 8) | t_xfer(0xFF));
    T_CS_H();
    return (uint16_t)(v >> 4);
}

#define READ_TIMES 5
#define LOST_VAL   1
#define ERR_RANGE  50

static uint16_t t_average(uint8_t cmd) {
    uint16_t buf[READ_TIMES], tmp, sum = 0;
    for (int i = 0; i < READ_TIMES; ++i) buf[i] = t_read_ad(cmd);
    for (int i = 0; i < READ_TIMES - 1; ++i)
        for (int j = i + 1; j < READ_TIMES; ++j)
            if (buf[i] > buf[j]) { tmp = buf[i]; buf[i] = buf[j]; buf[j] = tmp; }
    for (int i = LOST_VAL; i < READ_TIMES - LOST_VAL; ++i) sum = (uint16_t)(sum + buf[i]);
    return (uint16_t)(sum / (READ_TIMES - 2 * LOST_VAL));
}

static uint16_t t_repeated(uint8_t cmd) {
    uint16_t a = t_average(cmd), b = t_average(cmd);
    if ((b <= a && a < b + ERR_RANGE) || (a <= b && b < a + ERR_RANGE)) return (uint16_t)((a + b) / 2);
    return 0;
}

bool hal_touch_pen(void) {
    return RESET == gpio_input_bit_get(GPIOC, GPIO_PIN_13);   /* active low */
}

bool hal_touch_read(uint16_t *rx, uint16_t *ry) {
    uint16_t x = t_repeated(XPT_CMD_RDX);
    uint16_t y = t_repeated(XPT_CMD_RDY);
    if (x == 0 || y == 0) return false;
    *rx = x;
    *ry = y;
    return true;
}

/* ------------------------------------------------------------------ */
/* Everything else                                                    */
/* ------------------------------------------------------------------ */
void hal_init(void) {
    /* The BTT bootloader runs first. Point interrupts at OUR vector table and
       clear anything it may have left enabled or pending. */
    __disable_irq();
    SCB->VTOR = APP_VECTOR_TABLE;
    for (int i = 0; i < 8; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    /* Encoder pins: A=PA8, B=PC9, BTN=PC8, all pulled up. */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOA, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_8);
    gpio_init(GPIOC, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_8 | GPIO_PIN_9);

    uart_init();
    touch_init();

    SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 0);
    __enable_irq();
}
