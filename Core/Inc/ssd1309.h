/*
 * SSD1309 OLED 128x64, SPI (TX DMA), неблокирующий драйвер.
 *
 * ВАЖНО (STM32H7 / D-Cache):
 * 1) Если D-Cache включён, перед HAL_SPI_Transmit_DMA() обязательно чистить DCache
 *    для исходного буфера (tx). Драйвер делает Clean автоматически.
 * 2) Память буфера должна быть ДОСТУПНА DMA. Не размещайте framebuffer в DTCM.
 *
 * Callback политика:
 * - Драйвер НЕ переопределяет HAL_*Callback.
 * - Вы маршрутизируете callbacks и вызываете SSD1309_OnSpiTxCplt / SSD1309_OnSpiError.
 */

#ifndef SSD1309_DRIVER_H
#define SSD1309_DRIVER_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1309_WIDTH     128u
#define SSD1309_HEIGHT     64u
#define SSD1309_FB_SIZE  (SSD1309_WIDTH * SSD1309_HEIGHT / 8u) /* 1024 bytes */

typedef enum {
    SSD1309_COLOR_BLACK = 0,
    SSD1309_COLOR_WHITE = 1
} SSD1309_Color_t;

typedef struct {
    SPI_HandleTypeDef *hspi;

    GPIO_TypeDef *cs_port;  uint16_t cs_pin;
    GPIO_TypeDef *dc_port;  uint16_t dc_pin;
    GPIO_TypeDef *rst_port; uint16_t rst_pin;

    uint8_t col_offset; /* часто 0 или 2 для модулей 132->128 */
    uint8_t invert;     /* 0 normal, 1 invert */
} SSD1309_Config_t;

typedef struct {
    SSD1309_Config_t cfg;

    /* framebuffer aligned(32) чтобы корректно чистить DCache по линиям */
    uint8_t fb[SSD1309_FB_SIZE] __attribute__((aligned(32)));

    volatile uint8_t ready;
    volatile uint8_t busy;
    volatile uint8_t dirty;

    uint32_t t0_ms;
    uint8_t init_step;

    uint8_t init_len;
    uint8_t init_pos;

    uint8_t page;

    uint8_t init_seq[32] __attribute__((aligned(32)));
    uint8_t tx_cmd[8]    __attribute__((aligned(32)));
    uint8_t tx_len;
    uint8_t phase;
} SSD1309_t;

void SSD1309_Init(SSD1309_t *d, const SSD1309_Config_t *cfg);
void SSD1309_BeginAsync(SSD1309_t *d);

/* Вызывать регулярно (например, в while(1)) */
void SSD1309_Task(SSD1309_t *d);

/* Работа с framebuffer */
void SSD1309_Clear(SSD1309_t *d);
void SSD1309_DrawPixel(SSD1309_t *d, uint16_t x, uint16_t y, SSD1309_Color_t c);
void SSD1309_DrawChar8x8(SSD1309_t *d, uint16_t x, uint16_t y, char ch, SSD1309_Color_t c);
void SSD1309_DrawString8x8(SSD1309_t *d, uint16_t x, uint16_t y, const char *s, SSD1309_Color_t c);

/* Асинхронное обновление дисплея */
void SSD1309_UpdateAsync(SSD1309_t *d);

static inline bool SSD1309_IsReady(const SSD1309_t *d) { return d->ready != 0u; }
static inline bool SSD1309_IsBusy(const SSD1309_t *d)  { return d->busy  != 0u; }

/* Маршрутизация из ваших HAL callbacks */
void SSD1309_OnSpiTxCplt(SSD1309_t *d, SPI_HandleTypeDef *hspi);
void SSD1309_OnSpiError(SSD1309_t *d, SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* SSD1309_DRIVER_H */
