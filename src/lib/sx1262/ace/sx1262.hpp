#pragma once

#include "radio_base.hpp"
#include "../usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x.h"
#include "../usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x_hal.h"

/**
 * Sx1262 radio driver implementation
 * Hardware-specific implementation for Semtech SX1262 transceiver
 * https://www.waveshare.com/wiki/SX1262_XXXM_LoRaWAN/GNSS_HAT
 */
class Sx1262 : public RadioBase
{
private:
    // SX1262-specific PA configurations
    static constexpr sx126x_pa_cfg_params_s DEFAULT_HIGH_POWER_PA_CFG =
        {
            .pa_duty_cycle = 0x04,
            .hp_max = 0x07,
            .device_sel = 0x00,
            .pa_lut = 0x01,
        };

    // ************************************************************************************
    // 13.1.14.1 SetPaConfig
    // Table 13-21: PA Operating Modes with Optimal Settings
    static constexpr sx126x_pa_cfg_params_s DEFAULT_LOW_POWER_PA_CFG =
        {
            .pa_duty_cycle = 0x02,
            .hp_max = 0x02,
            .device_sel = 0x00,
            .pa_lut = 0x01,
        };

    // ************************************************************************************
    // GFSK

    // GFSK packet is setup such that we get the last byte from teh syncword as the first byte of the packet
    // 13.4.6 SetPacketParams
    static constexpr sx126x_pkt_params_gfsk_t DEFAULT_PKG_PARAMS_GFSK =
        {
            .preamble_len_in_bits = 0,                                    // SET per protocol
            .preamble_detector = SX126X_GFSK_PREAMBLE_DETECTOR_MIN_8BITS, // SET per protocol
            .sync_word_len_in_bits = 0,                                   // SET per protocol
            .address_filtering = SX126X_GFSK_ADDRESS_FILTERING_DISABLE,
            .header_type = SX126X_GFSK_PKT_FIX_LEN,
            .pld_len_in_bytes = 0,           // SET per protocol
            .crc_type = SX126X_GFSK_CRC_OFF, // Manchester decoding used. so no CRC possible
            .dc_free = SX126X_GFSK_DC_FREE_OFF};

    // Verified, looks ok
    // 13.4.5 SetModulationParams
    static constexpr sx126x_mod_params_gfsk_t DEFAULT_MOD_PARAMS_GFSK =
        {
            .br_in_bps = 100'000,                         // 50kbps*2 (Manchester) = 100000
            .fdev_in_hz = 50'000,                         //
            .pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_05, // Gaussian BT 0.5
            .bw_dsb_param = SX126X_GFSK_BW_234300         //
        };

    // ************************************************************************************
    // LORA

    // 13.1.8 SetCAD
    // CAD is only used by LORA
    static constexpr sx126x_cad_params_t DEFAULT_CAD_PARAMS =
        {
            .cad_symb_nb = SX126X_CAD_16_SYMB,
            .cad_detect_peak = 0x14,
            .cad_detect_min = 0X0A,
            .cad_exit_mode = SX126X_CAD_ONLY,
            .cad_timeout = 0,
        };

    // 13.67
    static constexpr sx126x_pkt_params_lora_t DEFAULT_PKG_PARAMS_LORA =
        {
            .preamble_len_in_symb = 12,
            .header_type = SX126X_LORA_PKT_EXPLICIT,
            .pld_len_in_bytes = GROUNDSTATION_RX_BASE, // Based on Uplink ADSL Size so they can be teh same size, that's all
            .crc_is_on = true,
            .invert_iq_is_on = false,
        };

    static constexpr sx126x_mod_params_lora_t DEFAULT_MOD_PARAMS_LORA =
        {
            .sf = SX126X_LORA_SF7,
            .bw = SX126X_LORA_BW_250,
            .cr = SX126X_LORA_CR_4_5,
            .ldro = 0,
        };

    static constexpr GATAS::LinkLayerConfig PROTOCOL_NONE{1, GATAS::DataSource::NONE, false, 0, 16, 64, 0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};

    // SX1262-specific state
    sx126x_errors_mask_t lastDeviceError = 0;
    sx126x_errors_mask_t currentDeviceError = 0;

public:
    static constexpr etl::array<etl::string_view, 4> NAMES{"Sx1262_0", "Sx1262_1", "Sx1262_2", "Sx1262_3"};
    static constexpr etl::array<etl::string_view, 4> COMMON_NAMES{"_Radio_0", "_Radio_1", "_Radio_2", "_Radio_3"};

    Sx1262(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t radioNo_, bool txEnabled_, bool groundStation_, uint32_t offsetHz_)
        : RadioBase(bus, pins, radioNo_, txEnabled_, groundStation_, offsetHz_, COMMON_NAMES[radioNo_])
    {
    }

    Sx1262(etl::imessage_bus &bus, const Configuration &config, uint8_t radioNo_)
        : Sx1262(bus,
                 config.pinMap(NAMES[radioNo_]),
                 radioNo_,
                 config.valueByPath(true, NAMES[radioNo_], "txEnabled"),
                 config.gaTasConfig().conspicuity.groundStation,
                 config.valueByPath(true, NAMES[radioNo_], "offset"))
    {
    }

    virtual ~Sx1262() = default;

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

    // Static entry point for disabled state
    static void enterDisabledState(uint8_t radioNo, const Configuration &config);

private:
    sx126x_irq_mask_t getIrqStatus();
    bool isTxDone();
};
