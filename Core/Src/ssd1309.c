#include "ssd1309.h"
#include "font8x8_basic.h"
#include <string.h>

/* Внутренние фазы */
enum { PHASE_IDLE = 0, PHASE_INIT = 1, PHASE_PAGE_CMD = 2, PHASE_PAGE_DATA = 3 };

static inline void cs_low (SSD1309_t *d){ HAL_GPIO_WritePin(d->cfg.cs_port,  d->cfg.cs_pin,  GPIO_PIN_RESET); }
static inline void cs_high(SSD1309_t *d){ HAL_GPIO_WritePin(d->cfg.cs_port,  d->cfg.cs_pin,  GPIO_PIN_SET);   }
static inline void dc_cmd (SSD1309_t *d){ HAL_GPIO_WritePin(d->cfg.dc_port,  d->cfg.dc_pin,  GPIO_PIN_RESET); }
static inline void dc_data(SSD1309_t *d){ HAL_GPIO_WritePin(d->cfg.dc_port,  d->cfg.dc_pin,  GPIO_PIN_SET);   }
static inline void rst_low(SSD1309_t *d){ HAL_GPIO_WritePin(d->cfg.rst_port, d->cfg.rst_pin, GPIO_PIN_RESET); }
static inline void rst_high(SSD1309_t *d){HAL_GPIO_WritePin(d->cfg.rst_port, d->cfg.rst_pin, GPIO_PIN_SET);   }

/* DCache clean helper: округление по 32 байта (cache line) */
static void dcache_clean(const void *addr, uint32_t len)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        uintptr_t a = (uintptr_t)addr;
        uintptr_t a32 = a & ~((uintptr_t)31);
        uint32_t l32 = (uint32_t)(len + (uint32_t)(a - a32) + 31U) & ~31U;
        SCB_CleanDCache_by_Addr((uint32_t *)a32, (int32_t)l32);
    }
#else
    (void)addr; (void)len;
#endif
}

static void build_init_seq(SSD1309_t *d)
{
    uint8_t i = 0;

    /* базовая, совместимая с SSD1309 последовательность */
    d->init_seq[i++] = 0xAE;                 /* display OFF */
    d->init_seq[i++] = 0xD5; d->init_seq[i++] = 0x80; /* clock div */
    d->init_seq[i++] = 0xA8; d->init_seq[i++] = 0x3F; /* multiplex 1/64 */
    d->init_seq[i++] = 0xD3; d->init_seq[i++] = 0x00; /* display offset */
    d->init_seq[i++] = 0x40;                 /* start line */

    /* addressing: page */
    d->init_seq[i++] = 0x20; d->init_seq[i++] = 0x02;

    d->init_seq[i++] = 0xA1;                 /* seg remap */
    d->init_seq[i++] = 0xC8;                 /* COM scan dec */
    d->init_seq[i++] = 0xDA; d->init_seq[i++] = 0x12; /* COM pins */
    d->init_seq[i++] = 0x81; d->init_seq[i++] = 0x7F; /* contrast */
    d->init_seq[i++] = 0xD9; d->init_seq[i++] = 0xF1; /* precharge */
    d->init_seq[i++] = 0xDB; d->init_seq[i++] = 0x40; /* VCOMH */
    d->init_seq[i++] = 0xA4;                 /* resume RAM */
    d->init_seq[i++] = (d->cfg.invert ? 0xA7 : 0xA6); /* invert/normal */

    /* ВАЖНО: 0x8D (charge pump) НЕ обязателен для SSD1309 и на части модулей мешает.
       Поэтому убрано. */

    d->init_seq[i++] = 0xAF;                 /* display ON */

    d->init_len = i;
    d->init_pos = 0;
}

static void start_init_chunk(SSD1309_t *d)
{
    if (d->busy) return;

    if (d->init_pos >= d->init_len) {
        cs_high(d);
        d->ready = 1u;
        d->phase = PHASE_IDLE;
        d->init_step = 5u; /* done */
        return;
    }

    uint8_t rem = (uint8_t)(d->init_len - d->init_pos);
    d->tx_len = (rem > (uint8_t)sizeof(d->tx_cmd)) ? (uint8_t)sizeof(d->tx_cmd) : rem;

    memcpy(d->tx_cmd, &d->init_seq[d->init_pos], d->tx_len);
    dcache_clean(d->tx_cmd, d->tx_len);

    dc_cmd(d);
    cs_low(d);

    if (HAL_SPI_Transmit_DMA(d->cfg.hspi, d->tx_cmd, d->tx_len) == HAL_OK) {
        d->busy = 1u;
        d->phase = PHASE_INIT;
    } else {
        cs_high(d); /* повторим позже в SSD1309_Task() */
    }
}

static void start_page_cmd(SSD1309_t *d)
{
    if (d->busy) return;

    const uint8_t col = d->cfg.col_offset;

    d->tx_cmd[0] = (uint8_t)(0xB0u | (d->page & 0x0Fu));
    d->tx_cmd[1] = (uint8_t)(0x00u | (col & 0x0Fu));
    d->tx_cmd[2] = (uint8_t)(0x10u | ((col >> 4) & 0x0Fu));
    d->tx_len = 3u;

    dcache_clean(d->tx_cmd, d->tx_len);

    dc_cmd(d);
    cs_low(d);

    if (HAL_SPI_Transmit_DMA(d->cfg.hspi, d->tx_cmd, d->tx_len) == HAL_OK) {
        d->busy = 1u;
        d->phase = PHASE_PAGE_CMD;
    } else {
        cs_high(d);
    }
}

static void start_page_data(SSD1309_t *d)
{
    if (d->busy) return;

    uint8_t *p = &d->fb[(uint32_t)d->page * SSD1309_WIDTH];

    /* КЛЮЧЕВО: чистим DCache для области, которую DMA прочитает */
    dcache_clean(p, SSD1309_WIDTH);

    dc_data(d);
    cs_low(d);

    if (HAL_SPI_Transmit_DMA(d->cfg.hspi, p, SSD1309_WIDTH) == HAL_OK) {
        d->busy = 1u;
        d->phase = PHASE_PAGE_DATA;
    } else {
        cs_high(d);
    }
}

void SSD1309_Init(SSD1309_t *d, const SSD1309_Config_t *cfg)
{
    memset(d, 0, sizeof(*d));
    d->cfg = *cfg;

    d->ready = 0u;
    d->busy = 0u;
    d->dirty = 0u;
    d->phase = PHASE_IDLE;
    d->init_step = 0u;

    /* важно: CS держим high в idle */
    cs_high(d);
    dc_cmd(d);
    rst_high(d);
}

void SSD1309_BeginAsync(SSD1309_t *d)
{
    d->ready = 0u;
    d->busy = 0u;
    d->dirty = 0u;
    d->phase = PHASE_IDLE;
    d->init_step = 0u;

    cs_high(d);
    dc_cmd(d);
    rst_high(d);
}

void SSD1309_Task(SSD1309_t *d)
{
    uint32_t now = HAL_GetTick();

    /* Асинхронная инициализация без блокировок */
    if (!d->ready) {
        switch (d->init_step) {
        case 0u:
            cs_high(d);
            dc_cmd(d);
            rst_low(d);
            d->t0_ms = now;
            d->init_step = 1u;
            break;

        case 1u:
            if ((now - d->t0_ms) >= 20u) { /* чуть дольше reset */
                rst_high(d);
                d->t0_ms = now;
                d->init_step = 2u;
            }
            break;

        case 2u:
            if ((now - d->t0_ms) >= 120u) { /* чуть дольше после reset */
                build_init_seq(d);
                d->init_step = 4u;
                start_init_chunk(d);
            }
            break;

        case 4u:
            if (!d->busy) start_init_chunk(d);
            break;

        default:
            break;
        }
        return;
    }

    /* Обновление экрана по dirty */
    if (d->dirty && !d->busy) {
        d->page = 0u;
        start_page_cmd(d);
    }
}

void SSD1309_UpdateAsync(SSD1309_t *d)
{
    d->dirty = 1u;

    if (d->ready && !d->busy) {
        d->page = 0u;
        start_page_cmd(d);
    }
}

/* ===== graphics ===== */

void SSD1309_Clear(SSD1309_t *d)
{
    memset(d->fb, 0x00, sizeof(d->fb));
    d->dirty = 1u;
}

void SSD1309_DrawPixel(SSD1309_t *d, uint16_t x, uint16_t y, SSD1309_Color_t c)
{
    if (x >= SSD1309_WIDTH || y >= SSD1309_HEIGHT) return;

    uint32_t index = (uint32_t)x + ((uint32_t)(y >> 3) * SSD1309_WIDTH);
    uint8_t mask = (uint8_t)(1u << (y & 7u));

    if (c == SSD1309_COLOR_WHITE) d->fb[index] |= mask;
    else                         d->fb[index] &= (uint8_t)~mask;

    d->dirty = 1u;
}

void SSD1309_DrawChar8x8(SSD1309_t *d, uint16_t x, uint16_t y, char ch, SSD1309_Color_t c)
{
    uint8_t uch = (uint8_t)ch;
    if (uch < 0x20u || uch > 0x7Fu) uch = 0x20u;

    const uint8_t *glyph = font8x8_basic[uch - 0x20u];

    for (uint8_t row = 0; row < 8u; row++) {
        uint8_t bits = glyph[row];
        for (uint8_t col = 0; col < 8u; col++) {
            if (bits & (1u << (7u - col))) {
                SSD1309_DrawPixel(d, (uint16_t)(x + col), (uint16_t)(y + row), c);
            }
        }
    }
}

void SSD1309_DrawString8x8(SSD1309_t *d, uint16_t x, uint16_t y, const char *s, SSD1309_Color_t c)
{
    while (*s) {
        SSD1309_DrawChar8x8(d, x, y, *s++, c);
        x = (uint16_t)(x + 8u);
        if (x > (SSD1309_WIDTH - 8u)) break;
    }
}

/* ===== callbacks routing target ===== */

void SSD1309_OnSpiTxCplt(SSD1309_t *d, SPI_HandleTypeDef *hspi)
{
    if (!d || hspi != d->cfg.hspi) return;

    /* КРИТИЧНО: поднимаем CS после каждого DMA трансфера (многие модули требуют CS toggle) */
    cs_high(d);

    d->busy = 0u;

    switch (d->phase) {
    case PHASE_INIT:
        d->init_pos = (uint8_t)(d->init_pos + d->tx_len);
        start_init_chunk(d);
        break;

    case PHASE_PAGE_CMD:
        start_page_data(d);
        break;

    case PHASE_PAGE_DATA:
        d->page++;
        if (d->page < 8u) {
            start_page_cmd(d);
        } else {
            d->phase = PHASE_IDLE;
            d->dirty = 0u;
        }
        break;

    default:
        d->phase = PHASE_IDLE;
        break;
    }
}

void SSD1309_OnSpiError(SSD1309_t *d, SPI_HandleTypeDef *hspi)
{
    if (!d || hspi != d->cfg.hspi) return;

    cs_high(d);
    d->busy = 0u;
    d->phase = PHASE_IDLE;
    /* dirty не сбрасываем: можно повторить UpdateAsync() */
}
