#include "../lr2021.hpp"

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ETL. */
#include "etl/map.h"

/* GATAS. */
#include "ace/manchester.hpp"
#include "ace/coreutils.hpp"
#include "ace/measure.hpp"
#include "ace/models.hpp"
#include "ace/moreutils.hpp"

static constexpr lr20xx_system_dio_t Lr2021_IRQ_DIO = LR20XX_SYSTEM_DIO_8;

static lr20xx_radio_common_rx_path_t lr2021RxPathForFrequency(uint32_t freq_in_hz)
{
    return (freq_in_hz >= 1600000000U) ? LR20XX_RADIO_COMMON_RX_PATH_HF : LR20XX_RADIO_COMMON_RX_PATH_LF;
}

bool Lr2021::detectradio() const
{
    lr20xx_system_version_t version{};
    if (lr20xx_system_get_version(this, &version) != LR20XX_STATUS_OK)
    {
        GATAS_WARN("Failed to read LR20xx version");
        return false;
    }

    if (version.major != 0x01 || version.minor != 0x18)
    {
        GATAS_WARN("Expected Lr2021 v1.18, but found [%u.%u]", version.major, version.minor);
        return false;
    }
    GATAS_INFO(" found Lr2021 v%u.%u ", version.major, version.minor);
    return true;
}

void Lr2021::radioInit()
{
    // TODO: Check if it's reset for this radio, or all radio's
    lr20xx_system_reset(this);

    waitBusy(50);

    lr20xx_system_set_dio_function(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_DIO_FUNC_IRQ, LR20XX_SYSTEM_DIO_DRIVE_NONE);
    lr20xx_system_set_dio_irq_cfg(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_IRQ_NONE);
    lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK);

    lr20xx_system_set_tcxo_mode(this, LR20XX_SYSTEM_TCXO_CTRL_1_8V, 0);
    lr20xx_system_set_dio_function(this, LR20XX_SYSTEM_DIO_5, LR20XX_SYSTEM_DIO_FUNC_RF_SWITCH, LR20XX_SYSTEM_DIO_DRIVE_NONE);
    lr20xx_system_set_dio_function(this, LR20XX_SYSTEM_DIO_6, LR20XX_SYSTEM_DIO_FUNC_RF_SWITCH, LR20XX_SYSTEM_DIO_DRIVE_NONE);
    lr20xx_system_set_dio_rf_switch_cfg(this, LR20XX_SYSTEM_DIO_5, LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_LF | LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_HF);
    lr20xx_system_set_dio_rf_switch_cfg(this, LR20XX_SYSTEM_DIO_6, LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_LF | LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_LF | LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_HF | LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_HF);

    lr20xx_system_set_standby_mode(this, LR20XX_SYSTEM_STANDBY_MODE_RC);
    lr20xx_system_set_reg_mode(this, LR20XX_SYSTEM_REG_MODE_DCDC);
    lr20xx_system_calibrate(this, static_cast<lr20xx_system_calibration_mask_t>(
                                      LR20XX_SYSTEM_CALIB_LF_RC_MASK | LR20XX_SYSTEM_CALIB_HF_RC_MASK |
                                      LR20XX_SYSTEM_CALIB_PLL_MASK | LR20XX_SYSTEM_CALIB_AAF_MASK |
                                      LR20XX_SYSTEM_CALIB_MU_MASK | LR20XX_SYSTEM_CALIB_PA_OFF_MASK));
    waitBusy(25);
    checkAndClearDeviceErrors();

    const lr20xx_radio_common_front_end_calibration_value_t fe_calib[] = {
        {.rx_path = lr2021RxPathForFrequency(868000000), .frequency_in_hertz = 868000000},
    };
    lr20xx_radio_common_calibrate_front_end_helper(this, fe_calib, 1);
    waitBusy(100);
    checkAndClearDeviceErrors();

    standBy();

    lr20xx_radio_common_set_rx_tx_fallback_mode(this, LR20XX_RADIO_FALLBACK_STDBY_XOSC);
    lr20xx_radio_common_set_cad_params(this, &DEFAULT_CAD_PARAMS);
    lr20xx_radio_common_set_pa_cfg(this, &DEFAULT_HIGH_POWER_PA_CFG);
}

void Lr2021::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"deviceErrors\":" << statistics.deviceErrors;
    stream << ",\"spiNo\":" << spiHall->spiNum();
    stream << ",\"receivedPackets:k\":" << statistics.receivedPackets;
    stream << ",\"transmittedPackets:k\":" << statistics.transmittedPackets;
    stream << ",\"buzyWaitsTimeout:err\":" << statistics.buzyWaitsTimeout;
    stream << ",\"txQueueFull:err\":" << statistics.queueFull;
    stream << ",\"txQueueSize:k\":" << txQueue.size();
    stream << ",\"mode\":" << "\"" << GATAS::modulationToString(rxRadioParameters.frequency->mode) << "\"";
    stream << ",\"dataSource\":" << "\"" << GATAS::toString(rxRadioParameters.config->dataSource()) << "\"";
    stream << ",\"frequency:hz\":" << rxRadioParameters.hopFrequency;
    stream << ",\"powerdBm:dbm\":" << rxRadioParameters.frequency->powerdBm;
    stream << ",\"radio\":" << radioNo;
    stream << ",\"txEnabled:b\":" << txEnabled;
    stream << ",\"hasGpsFix:b\":" << hasGpsFix;
    stream << ",\"lastDeviceError:bin\":" << lastDeviceError;
    stream << ",\"currentDeviceError:bin\":" << currentDeviceError;
    stream << "}";
}

void Lr2021::configureRadio(const GATAS::RadioParameters &newParameters, uint8_t payloadLength)
{
    standBy();

    if (LOW_POWER_MODE)
    {
        lr20xx_radio_common_set_pa_cfg(this, &DEFAULT_LOW_POWER_PA_CFG);
    }
    else
    {
        lr20xx_radio_common_set_pa_cfg(this, &DEFAULT_HIGH_POWER_PA_CFG);
    }

    if (newParameters.config->pcId != rxRadioParameters.config->pcId || true)
    {
        GATAS_MEASURE("configureLr2021", 1600);

        if (newParameters.frequency->mode == GATAS::Modulation::GFSK)
        {
            lr20xx_radio_common_set_pkt_type(this, LR20XX_RADIO_COMMON_PKT_TYPE_FSK);

            auto mod = DEFAULT_MOD_PARAMS_GFSK;
            if (newParameters.frequency->channelBandwidth == 234300)
            {
                mod.bw = LR20XX_RADIO_FSK_COMMON_RX_BW_238_000_HZ;
            }
            else if (newParameters.frequency->channelBandwidth == 312000)
            {
                mod.bw = LR20XX_RADIO_FSK_COMMON_RX_BW_307_000_HZ;
            }
            else if (newParameters.frequency->channelBandwidth == 187200)
            {
                mod.bw = LR20XX_RADIO_FSK_COMMON_RX_BW_185_000_HZ;
            }
            else
            {
                GATAS_WARN("bw Not set");
            }

            if (newParameters.frequency->gaussBt == 10)
            {
                mod.pulse_shape = LR20XX_RADIO_FSK_PULSE_SHAPE_GAUSSIAN_BT_1_0;
            }

            mod.bitrate = newParameters.frequency->chipRate;
            mod.fdev_in_hz = newParameters.frequency->freqDiv;
            lr20xx_radio_fsk_set_modulation_params(this, &mod);

            auto pkt_params_gfsk = DEFAULT_PKG_PARAMS_GFSK;
            uint8_t syncLengthBits;
            const uint8_t *syncData;

            if (payloadLength)
            {
                syncLengthBits = newParameters.config->syncLength;
                syncData = newParameters.config->syncWord.data();
                pkt_params_gfsk.payload_length = payloadLength * (newParameters.config->manchester ? 2 : 1);

                if (newParameters.config->packetLength == 0)
                {
                    pkt_params_gfsk.header_mode = LR20XX_RADIO_FSK_HEADER_8BITS;
                }
            }
            else
            {
                syncLengthBits = newParameters.config->syncLength - newParameters.config->syncSkipInRxLength;
                syncData = newParameters.config->syncWord.data() + (newParameters.config->syncSkipInRxLength + 7) / 8;

                if (newParameters.config->packetLength == 0)
                {
                    pkt_params_gfsk.header_mode = LR20XX_RADIO_FSK_HEADER_8BITS;
                    pkt_params_gfsk.payload_length = GROUNDSTATION_RX_BASE;
                }
                else
                {
                    pkt_params_gfsk.payload_length = newParameters.config->packetLength * (newParameters.config->manchester ? 2 : 1);
                }
            }

            pkt_params_gfsk.pbl_length_in_bit = newParameters.config->txPreambleLength;
            pkt_params_gfsk.payload_length_unit = LR20XX_RADIO_FSK_PAYLOAD_LENGTH_IN_BYTE;
            lr20xx_radio_fsk_set_packet_params(this, &pkt_params_gfsk);
            lr20xx_radio_fsk_set_syncword(this, syncData, syncLengthBits, LR20XX_RADIO_FSK_SYNCWORD_MSBF);
        }
        else if (newParameters.frequency->mode == GATAS::Modulation::LORA)
        {
            lr20xx_radio_common_set_pkt_type(this, LR20XX_RADIO_COMMON_PKT_TYPE_LORA);

            auto pkg = DEFAULT_PKG_PARAMS_LORA;
            if (payloadLength == 0)
            {
                auto mod = DEFAULT_MOD_PARAMS_LORA;
                if (newParameters.frequency->channelBandwidth == 500000)
                {
                    mod.bw = LR20XX_RADIO_LORA_BW_500;
                }
                else if (newParameters.frequency->channelBandwidth == 250000)
                {
                    mod.bw = LR20XX_RADIO_LORA_BW_250;
                }
                else if (newParameters.frequency->channelBandwidth == 125000)
                {
                    mod.bw = LR20XX_RADIO_LORA_BW_125;
                }

                lr20xx_radio_lora_set_modulation_params(this, &mod);
                pkg.pld_len_in_bytes = newParameters.config->packetLength;
            }
            else
            {
                pkg.pld_len_in_bytes = payloadLength;
            }

            pkg.preamble_len_in_symb = newParameters.config->txPreambleLength;
            lr20xx_radio_lora_set_packet_params(this, &pkg);
            lr20xx_radio_lora_set_syncword(this, newParameters.config->syncWord.data()[0]);
        }
    }

    if (newParameters.hopFrequency != rxRadioParameters.hopFrequency || true)
    {
        lr20xx_radio_common_set_rf_freq(this, newParameters.hopFrequency + offsetHz);
        lr20xx_radio_common_set_rx_path(this, lr2021RxPathForFrequency(newParameters.hopFrequency + offsetHz),
                                        LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE);
    }

    checkAndClearDeviceErrors();
}

void Lr2021::listen()
{
    lr20xx_system_set_fs_mode(this);
    lr20xx_system_set_dio_irq_cfg(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_IRQ_RX_DONE);
    lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK);
    enablePinInterrupt(dio1Pin, DIO1_RX_DONE);
    lr20xx_radio_common_set_rx(this, LR20XX_MAX_TIMEOUT_IN_MS);
}

void Lr2021::sendGFSKPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length)
{
    (void)parameters;
    lr20xx_system_set_dio_irq_cfg(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_IRQ_TX_DONE);
    lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK);
    enablePinInterrupt(dio1Pin, DIO1_TX_DONE);

    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::max(static_cast<int8_t>(22), parameters.frequency->powerdBm);
    lr20xx_radio_common_set_tx_params(this, static_cast<int8_t>(powerdBm * 2), LR20XX_RADIO_COMMON_RAMP_208_US);
    lr20xx_radio_fifo_clear_tx(this);
    lr20xx_radio_fifo_write_tx(this, data, length);
    lr20xx_radio_common_set_tx(this, LR20XX_MAX_TIMEOUT_IN_MS);
}

void Lr2021::sendLORAPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length)
{
    auto mod = DEFAULT_MOD_PARAMS_LORA;
    switch (parameters.codingRate)
    {
    case 5:
        mod.cr = LR20XX_RADIO_LORA_CR_4_5;
        break;
    case 6:
        mod.cr = LR20XX_RADIO_LORA_CR_4_6;
        break;
    case 7:
        mod.cr = LR20XX_RADIO_LORA_CR_4_7;
        break;
    }

    if (parameters.frequency->channelBandwidth == 500000)
    {
        mod.bw = LR20XX_RADIO_LORA_BW_500;
    }
    else if (parameters.frequency->channelBandwidth == 250000)
    {
        mod.bw = LR20XX_RADIO_LORA_BW_250;
    }
    else if (parameters.frequency->channelBandwidth == 125000)
    {
        mod.bw = LR20XX_RADIO_LORA_BW_125;
    }

    lr20xx_radio_lora_set_modulation_params(this, &mod);

    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::max(static_cast<int8_t>(22), parameters.frequency->powerdBm);
    lr20xx_radio_common_set_tx_params(this, static_cast<int8_t>(powerdBm * 2), LR20XX_RADIO_COMMON_RAMP_208_US);

    lr20xx_system_set_dio_irq_cfg(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_IRQ_TX_DONE);
    lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK);
    enablePinInterrupt(dio1Pin, DIO1_TX_DONE);

    lr20xx_radio_fifo_clear_tx(this);
    lr20xx_radio_fifo_write_tx(this, data, length);
    lr20xx_radio_common_set_tx(this, LR20XX_MAX_TIMEOUT_IN_MS);
}

void Lr2021::receiveGFSKPacket()
{
    lr20xx_radio_fsk_packet_status_t pkt_status{};
    lr20xx_radio_fsk_get_packet_status(this, &pkt_status);

    if (pkt_status.packet_length_bytes > 0)
    {
        statistics.receivedPackets += 1;
        uint8_t receivedFrameLength = receivedPacketLength();

        if (receivedFrameLength >= 4)
        {
            if (rxRadioParameters.config->dataSource() == GATAS::DataSource::ADSLO_HDR && (receivedFrameLength % 4 != 0))
            {
                GATAS_WARN("Dropping invalid ADSL HDR packet");
                return;
            }

            if (auto frameData = static_cast<uint8_t *>(getGlobalPool().alloc(receivedFrameLength)))
            {
                GATAS::DataFrame rxFrame{
                    .epochSeconds = CoreUtils::secondsSinceEpoch(),
                    .frequency = rxRadioParameters.hopFrequency,
                    .config = rxRadioParameters.config,
                    .frame = {getGlobalPool(), frameData},
                    .length = receivedFrameLength,
                    .rssidBm = pkt_status.rssi_avg_in_dbm,
                };

                lr20xx_radio_fifo_read_rx(this, rxFrame.frame.get(), receivedFrameLength);
                rxDataFrameQueue->push(etl::move(rxFrame));
            }
        }
        else
        {
            GATAS_WARN("Incorrect frame length received %d", receivedFrameLength);
        }
    }
    else
    {
        checkAndClearDeviceErrors();
        GATAS_INFO("pkt_status.packet_length_bytes %d", pkt_status.packet_length_bytes);
    }
}

void Lr2021::receiveLORAPacket()
{
    lr20xx_system_stat1_t stat1{};
    lr20xx_system_stat2_t stat2{};
    lr20xx_system_irq_mask_t irq_mask = 0;
    lr20xx_system_get_status(this, &stat1, &stat2, &irq_mask);
    if (stat1.command_status == LR20XX_SYSTEM_CMD_STATUS_DATA)
    {
        lr20xx_radio_lora_packet_status_t pkt_status{};
        lr20xx_radio_lora_get_packet_status(this, &pkt_status);

        statistics.receivedPackets += 1;
        uint8_t receivedFrameLength = receivedPacketLength();

        if (receivedFrameLength >= 4)
        {
            if (auto frameData = static_cast<uint8_t *>(getGlobalPool().alloc(receivedFrameLength)))
            {
                GATAS::DataFrame rxFrame{
                    .epochSeconds = CoreUtils::secondsSinceEpoch(),
                    .frequency = rxRadioParameters.hopFrequency,
                    .config = rxRadioParameters.config,
                    .frame = {getGlobalPool(), frameData},
                    .length = receivedFrameLength,
                    .rssidBm = pkt_status.rssi_signal_pkt_in_dbm,
                };

                lr20xx_radio_fifo_read_rx(this, rxFrame.frame.get(), receivedFrameLength);
                rxDataFrameQueue->push(etl::move(rxFrame));
            }
        }
        else
        {
            GATAS_WARN("Incorrect frame length received %d", receivedFrameLength);
        }
    }
    else
    {
        checkAndClearDeviceErrors();
    }
}

uint8_t Lr2021::receivedPacketLength() const
{
    uint16_t rx_packet_length = 0;
    lr20xx_radio_common_get_rx_packet_length(this, &rx_packet_length);
    return static_cast<uint8_t>(rx_packet_length);
}

void Lr2021::standBy()
{
    GATAS_MEASURE("standBy", 2000);
    lr20xx_system_set_standby_mode(this, LR20XX_SYSTEM_STANDBY_MODE_XOSC);
}

void Lr2021::checkAndClearDeviceErrors()
{
    GATAS_MEASURE("checkAndClearDeviceErrors", 600);
    lr20xx_status_t status = lr20xx_system_get_errors(this, &currentDeviceError);
    if (status == LR20XX_STATUS_OK && currentDeviceError != 0)
    {
        statistics.deviceErrors += 1;
        lastDeviceError = currentDeviceError;
        lr20xx_system_set_dio_irq_cfg(this, Lr2021_IRQ_DIO, LR20XX_SYSTEM_IRQ_NONE);
        lr20xx_system_clear_irq_status(this, LR20XX_SYSTEM_IRQ_ALL_MASK);
        lr20xx_system_clear_errors(this);
        GATAS_WARN("Device Error: %b", currentDeviceError);
    }
}

lr20xx_system_irq_mask_t Lr2021::getIrqStatus()
{
    lr20xx_system_irq_mask_t mask = 0;
    lr20xx_system_get_and_clear_irq_status(this, &mask);
    return mask;
}

bool Lr2021::isTxDone()
{
    const lr20xx_system_irq_mask_t mask = getIrqStatus();
    return (mask & LR20XX_SYSTEM_IRQ_TX_DONE) != 0;
}
