#include "../lr2021.hpp"

/* FreeRTOS. */
#include "FreeRTOS.h"

/* PICO. */
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* GATAS. */
#include "ace/basemodule.hpp"
#include "ace/coreutils.hpp"

static uint8_t lr20xx_buzy_wait(uint8_t busyPin)
{
    constexpr uint8_t checkInterval = 100;
    constexpr uint32_t timeoutUs = 250'000;

    uint8_t countdown = checkInterval;
    auto timeoutTime = CoreUtils::timeUs32Raw() + timeoutUs;
    while (gpio_get(busyPin))
    {
        if (--countdown == 0)
        {
            countdown = checkInterval;
            if (CoreUtils::isUsReachedRaw(timeoutTime))
            {
                return 1;
            }
        }
    }
    return 0;
}

lr20xx_hal_status_t lr20xx_hal_write(const void *context, const uint8_t *command, const uint16_t command_length,
                                     const uint8_t *data, const uint16_t data_length)
{
    Lr2021 *lr2021 = (Lr2021 *)context;
    SpiModule *spi = lr2021->spi();

    lr20xx_hal_status_t ret = LR20XX_HAL_STATUS_OK;
    if (lr20xx_buzy_wait(lr2021->busy()))
    {
        GATAS_WARN("hal write, Wait busy timeout");
        ret = LR20XX_HAL_STATUS_ERROR;
    }
    else
    {
        spi->cs_select(lr2021->cs());
        spi_write_blocking(spi->spiNum() ? spi1 : spi0, command, command_length);
        if (data_length != 0)
        {
            spi_write_blocking(spi->spiNum() ? spi1 : spi0, data, data_length);
        }
    }
    spi->cs_deselect(lr2021->cs());
    return ret;
}

lr20xx_hal_status_t lr20xx_hal_read(const void *context, const uint8_t *command, const uint16_t command_length,
                                    uint8_t *data, const uint16_t data_length)
{
    Lr2021 *lr2021 = (Lr2021 *)context;
    SpiModule *spi = lr2021->spi();

    lr20xx_hal_status_t ret = LR20XX_HAL_STATUS_OK;
    if (lr20xx_buzy_wait(lr2021->busy()))
    {
        GATAS_WARN("hal read, Wait busy timeout");
        ret = LR20XX_HAL_STATUS_ERROR;
    }
    else
    {
        spi->cs_select(lr2021->cs());
        int length = spi_write_blocking(spi->spiNum() ? spi1 : spi0, command, command_length);
        if (length != command_length)
        {
            GATAS_WARN("lr20xx_hal_read write error");
            ret = LR20XX_HAL_STATUS_ERROR;
        }
        else
        {
            length = spi_read_blocking(spi->spiNum() ? spi1 : spi0, 0, data, data_length);
            if (length != data_length)
            {
                GATAS_WARN("lr20xx_hal_read read error");
                ret = LR20XX_HAL_STATUS_ERROR;
            }
        }
    }
    spi->cs_deselect(lr2021->cs());
    return ret;
}

lr20xx_hal_status_t lr20xx_hal_direct_read(const void *context, uint8_t *data, const uint16_t data_length)
{
    Lr2021 *lr2021 = (Lr2021 *)context;
    SpiModule *spi = lr2021->spi();

    if (lr20xx_buzy_wait(lr2021->busy()))
    {
        GATAS_WARN("hal direct read, Wait busy timeout");
        return LR20XX_HAL_STATUS_ERROR;
    }

    spi->cs_select(lr2021->cs());
    int length = spi_read_blocking(spi->spiNum() ? spi1 : spi0, 0, data, data_length);
    spi->cs_deselect(lr2021->cs());

    return (length == data_length) ? LR20XX_HAL_STATUS_OK : LR20XX_HAL_STATUS_ERROR;
}

lr20xx_hal_status_t lr20xx_hal_direct_read_fifo(const void *context, const uint8_t *command,
                                                 const uint16_t command_length, uint8_t *data,
                                                 const uint16_t data_length)
{
    return lr20xx_hal_read(context, command, command_length, data, data_length);
}

lr20xx_hal_status_t lr20xx_hal_reset(const void *context)
{
    (void)context;
    GATAS_INFO("Lr2021 Reset called");
    return LR20XX_HAL_STATUS_OK;
}

lr20xx_hal_status_t lr20xx_hal_wakeup(const void *context)
{
    (void)context;
    GATAS_INFO("Lr2021 wakeup called");
    return LR20XX_HAL_STATUS_OK;
}
