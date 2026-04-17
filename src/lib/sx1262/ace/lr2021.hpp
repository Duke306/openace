#pragma once

#include "radio_base.hpp"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_hal.h"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_radio_common.h"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_radio_fsk.h"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_radio_lora.h"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_system.h"
#include "../usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc/lr20xx_radio_fifo.h"

/* System. */
#include <stdint.h>

/**
 * Lr2021 radio driver implementation
 * Hardware-specific implementation for Semtech LR2021 transceiver
 */
class Lr2021 : public RadioBase
{
private:
    static constexpr lr20xx_system_dio_t Lr2021_IRQ_DIO = LR20XX_SYSTEM_DIO_8;
    static constexpr uint32_t LR20XX_MAX_TIMEOUT_IN_MS = 0x00FFFFFF;

    // LR2021-specific PA configurations
    static constexpr lr20xx_radio_common_pa_cfg_t DEFAULT_HIGH_POWER_PA_CFG =
        {
            .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF,
            .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
            .pa_lf_duty_cycle = 0x04,
            .pa_lf_slices = 0x07,
            .pa_hf_duty_cycle = 0x10,
        };

    static constexpr lr20xx_radio_common_pa_cfg_t DEFAULT_LOW_POWER_PA_CFG =
        {
            .pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF,
            .pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM,
            .pa_lf_duty_cycle = 0x02,
            .pa_lf_slices = 0x02,
            .pa_hf_duty_cycle = 0x10,
        };

    // LR2021-specific packet parameters for GFSK
    static constexpr lr20xx_radio_fsk_pkt_params_t DEFAULT_PKG_PARAMS_GFSK =
        {
            .pbl_length_in_bit = 0,
            .preamble_detector = LR20XX_RADIO_FSK_PREAMBLE_DETECTOR_8_BITS,
            .long_preamble_enabled = false,
            .address_filtering = LR20XX_RADIO_FSK_ADDRESS_FILTERING_DISABLED,
            .header_mode = LR20XX_RADIO_FSK_HEADER_IMPLICIT,
            .payload_length_unit = LR20XX_RADIO_FSK_PAYLOAD_LENGTH_IN_BYTE,
            .payload_length = 0,
            .crc = LR20XX_RADIO_FSK_CRC_OFF,
            .whitening = LR20XX_RADIO_FSK_WHITENING_OFF,
        };

    static constexpr lr20xx_radio_fsk_mod_params_t DEFAULT_MOD_PARAMS_GFSK =
        {
            .bitrate_unit = LR20XX_RADIO_FSK_MOD_PARAMS_BR_IN_BPS,
            .bitrate = 100'000,
            .pulse_shape = LR20XX_RADIO_FSK_PULSE_SHAPE_GAUSSIAN_BT_0_5,
            .bw = LR20XX_RADIO_FSK_COMMON_RX_BW_238_000_HZ,
            .fdev_in_hz = 50'000,
        };

    // LR2021-specific CAD parameters for LORA
    static constexpr lr20xx_radio_common_cad_params_t DEFAULT_CAD_PARAMS =
        {
            .timeout = 0,
            .threshold = 0x14,
            .exit_mode = LR20XX_RADIO_COMMON_CAD_EXIT_MODE_FALLBACK,
            .tx_rx_timeout = 0,
        };

    static constexpr lr20xx_radio_lora_pkt_params_t DEFAULT_PKG_PARAMS_LORA =
        {
            .preamble_len_in_symb = 12,
            .pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
            .pld_len_in_bytes = GROUNDSTATION_RX_BASE,
            .crc = LR20XX_RADIO_LORA_CRC_ENABLED,
            .iq = LR20XX_RADIO_LORA_IQ_STANDARD,
        };

    static constexpr lr20xx_radio_lora_mod_params_t DEFAULT_MOD_PARAMS_LORA =
        {
            .sf = LR20XX_RADIO_LORA_SF7,
            .bw = LR20XX_RADIO_LORA_BW_250,
            .cr = LR20XX_RADIO_LORA_CR_4_5,
            .ppm = LR20XX_RADIO_LORA_NO_PPM};

    static constexpr GATAS::LinkLayerConfig PROTOCOL_NONE{1, GATAS::DataSource::NONE, false, 0, 16, 64, 0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};

    // LR2021-specific state
    lr20xx_system_errors_t lastDeviceError = 0;
    lr20xx_system_errors_t currentDeviceError = 0;

public:
    static constexpr etl::array<etl::string_view, 4> NAMES{"Lr2021_0", "Lr2021_1", "Lr2021_2", "Lr2021_3"};

    Lr2021(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t radioNo_, bool txEnabled_, bool groundStation_, uint32_t offsetHz_)
        : RadioBase(bus, pins, radioNo_, txEnabled_, groundStation_, offsetHz_, COMMON_NAMES[radioNo_])
    {
    }

    Lr2021(etl::imessage_bus &bus, const Configuration &config, uint8_t radioNo_)
        : Lr2021(bus,
                 config.pinMap(NAMES[radioNo_]),
                 radioNo_,
                 config.valueByPath(true, NAMES[radioNo_], "txEnabled"),
                 config.gaTasConfig().conspicuity.groundStation,
                 config.valueByPath(true, NAMES[radioNo_], "offset"))
    {
    }

    virtual ~Lr2021() = default;

    // Data access
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    // Hardware-specific implementations
    virtual void radioInit() override;
    virtual void checkAndClearDeviceErrors() override;
    virtual void receiveGFSKPacket() override;
    virtual void receiveLORAPacket() override;
    virtual void sendGFSKPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length) override;
    virtual void sendLORAPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length) override;
    virtual void configureRadio(const GATAS::RadioParameters &newParameters, uint8_t txPayloadLength) override;
    virtual void listen() override;
    virtual void standBy() override;
    virtual uint8_t receivedPacketLength() const override;
    virtual bool detectradio() const override;

private:
    lr20xx_system_irq_mask_t getIrqStatus();
    bool isTxDone();
};
