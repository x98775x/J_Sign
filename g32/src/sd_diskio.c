/* SD card over hardware SPI0 (PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI) as a read-only FatFs disk. */
#include "gd32f20x.h"
#include "gd32f20x_rcu.h"
#include "gd32f20x_gpio.h"
#include "gd32f20x_spi.h"
#include "diskio.h"
#include "hal.h"

#define SD_CS_L() gpio_bit_reset(GPIOA, GPIO_PIN_4)
#define SD_CS_H() gpio_bit_set(GPIOA, GPIO_PIN_4)

#define CMD0   0
#define CMD8   8
#define CMD16  16
#define CMD17  17
#define CMD55  55
#define CMD58  58
#define ACMD41 41

static bool    sd_ready = false;
static bool    sd_block_addr = false;     /* SDHC/SDXC use block numbers, old cards byte offsets */

static void spi_setup(uint32_t psc) {
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_SPI0);
    gpio_init(GPIOA, GPIO_MODE_AF_PP,       GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_7);
    gpio_init(GPIOA, GPIO_MODE_IPU,         GPIO_OSPEED_50MHZ, GPIO_PIN_6);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP,      GPIO_OSPEED_50MHZ, GPIO_PIN_4);
    SD_CS_H();

    spi_i2s_deinit(SPI0);
    spi_parameter_struct sp;
    spi_struct_para_init(&sp);
    sp.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    sp.device_mode          = SPI_MASTER;
    sp.frame_size           = SPI_FRAMESIZE_8BIT;
    sp.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    sp.nss                  = SPI_NSS_SOFT;
    sp.endian               = SPI_ENDIAN_MSB;
    sp.prescale             = psc;
    spi_init(SPI0, &sp);
    spi_enable(SPI0);
}

static void spi_speed(uint32_t psc) {
    spi_disable(SPI0);
    SPI_CTL0(SPI0) = (SPI_CTL0(SPI0) & ~SPI_CTL0_PSC) | psc;
    spi_enable(SPI0);
}

static uint8_t xfer(uint8_t b) {
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE)) {}
    spi_i2s_data_transmit(SPI0, b);
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE)) {}
    return (uint8_t)spi_i2s_data_receive(SPI0);
}

static bool wait_ready(uint32_t ms) {
    uint32_t t0 = millis();
    do { if (xfer(0xFF) == 0xFF) return true; } while ((millis() - t0) < ms);
    return false;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg) {
    if (cmd != CMD0 && !wait_ready(500)) return 0xFF;
    xfer((uint8_t)(0x40 | cmd));
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    xfer(cmd == CMD0 ? 0x95 : (cmd == CMD8 ? 0x87 : 0x01));
    uint8_t r = 0xFF;
    for (int i = 0; i < 10; ++i) { r = xfer(0xFF); if (!(r & 0x80)) break; }
    return r;
}

static void sd_deselect(void) { SD_CS_H(); xfer(0xFF); }

static bool sd_card_init(void) {
    sd_ready = false;
    spi_setup(SPI_PSC_256);                       /* < 400 kHz while identifying the card */
    SD_CS_H();
    for (int i = 0; i < 10; ++i) xfer(0xFF);      /* 80 clocks */

    SD_CS_L();
    uint8_t r = 0xFF;
    for (int tries = 0; tries < 5 && r != 0x01; ++tries) r = sd_cmd(CMD0, 0);
    if (r != 0x01) { sd_deselect(); return false; }

    bool v2 = false;
    r = sd_cmd(CMD8, 0x1AAu);
    if (r == 0x01) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; ++i) ocr[i] = xfer(0xFF);
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) { sd_deselect(); return false; }
        v2 = true;
    }

    uint32_t t0 = millis();
    do {
        sd_cmd(CMD55, 0);
        r = sd_cmd(ACMD41, v2 ? 0x40000000u : 0u);
    } while (r != 0x00 && (millis() - t0) < 1500);
    if (r != 0x00) { sd_deselect(); return false; }

    sd_block_addr = false;
    if (v2) {
        if (sd_cmd(CMD58, 0) == 0x00) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; ++i) ocr[i] = xfer(0xFF);
            sd_block_addr = (ocr[0] & 0x40u) != 0;
        }
    }
    if (!sd_block_addr) sd_cmd(CMD16, 512);
    sd_deselect();

    spi_speed(SPI_PSC_8);                         /* 15 MHz */
    sd_ready = true;
    return true;
}

static bool sd_read_sector(uint8_t *buf, uint32_t lba) {
    SD_CS_L();
    if (sd_cmd(CMD17, sd_block_addr ? lba : lba * 512u) != 0x00) { sd_deselect(); return false; }
    uint32_t t0 = millis();
    uint8_t tok;
    do { tok = xfer(0xFF); } while (tok == 0xFF && (millis() - t0) < 250);
    if (tok != 0xFE) { sd_deselect(); return false; }
    for (int i = 0; i < 512; ++i) buf[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);                       /* CRC, ignored */
    sd_deselect();
    return true;
}

void sd_invalidate(void) { sd_ready = false; }    /* force a fresh card init on the next mount */

/* ---- FatFs glue ---- */
DSTATUS disk_status(BYTE pdrv) {
    return (pdrv == 0 && sd_ready) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return sd_card_init() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !sd_ready) return RES_NOTRDY;
    while (count--) {
        if (!sd_read_sector(buff, (uint32_t)sector)) return RES_ERROR;
        buff += 512;
        sector++;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv; (void)buff; (void)sector; (void)count;
    return RES_WRPRT;                              /* read-only on purpose */
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv; (void)buff;
    return (cmd == CTRL_SYNC) ? RES_OK : RES_PARERR;
}
