#include "../LR2021.hpp"

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

void LR2021::start()
{
    lr20xx_reset(this);
    getBus().subscribe(*this);
};

GATAS::PostConstruct LR2021::postConstruct()
{
    spiHall = static_cast<SpiModule *>(BaseModule::moduleByName(*this, SpiModule::NAME));

    if (spiHall == nullptr)
    {
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    rxDataFrameQueue = static_cast<RxDataFrameQueue *>(BaseModule::moduleByName(*this, RxDataFrameQueue::NAME));
    if (rxDataFrameQueue == nullptr)
    {
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(csPin);
    gpio_set_dir(csPin, GPIO_OUT);
    gpio_put(csPin, 1);

    // Busy Pin has PullUp from LR2021 so kept as input only
    gpio_init(busyPin);
    gpio_set_dir(busyPin, GPIO_IN);

    gpio_init(dio1Pin);
    gpio_set_dir(dio1Pin, GPIO_IN);

    // Read the device type
    char data[7];
    lr20xx_read_register(this, 0x0320, (uint8_t *)data, 6);
    data[6] = 0;

    // Note that even for a LR2021, the version might come back as SX1261
    // https://forum.lora-developers.semtech.com/t/lr20xx-device-id/1508
    if (strncmp(data, "SX126", 5) != 0)
    {
        GATAS_WARN("Expected SX126X, but found [%s] ", data);
        return GATAS::PostConstruct::HARDWARE_NOT_FOUND;
    }
    GATAS_INFO(" found [%s] (Sx1261 is normal for a LR2021) ", data);

    radioInit();

    if (xTaskCreate(LR2021Trampoline, NAMES[radioNo].cbegin(), configMINIMAL_STACK_SIZE + 128, this, tskIDLE_PRIORITY + 4, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }
    registerPinInterrupt(dio1Pin, GPIO_IRQ_EDGE_RISE, taskHandle, 0);

    GATAS_INFO("Initialised on cs:%d busy:%d dio1:%d ", csPin, busyPin, dio1Pin);

    // Make the SPI pins available to picotool
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(csPin), NAMES[radioNo].cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(busyPin), NAMES[radioNo].cbegin()));
    bi_decl(bi_1pin_with_name(static_cast<uint32_t>(dio1Pin), NAMES[radioNo].cbegin()));

    return GATAS::PostConstruct::OK;
}

void LR2021::enterDisabledState(uint8_t radioNo, const Configuration &config)
{
    auto pinMap = config.pinMap(NAMES[radioNo]);
    auto csPin = pinMap.at(GATAS::PinType::CS);
    gpio_init(csPin);
    gpio_set_dir(csPin, GPIO_OUT);
    gpio_put(csPin, 1);
}

void LR2021::getData(etl::string_stream &stream, const etl::string_view path) const
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

void LR2021::on_receive(const GATAS::RadioTxFrameMsg &msg)
{
    if (msg.radioNo == radioNo)
    {
        if (hasGpsFix && txEnabled)
        {
            TxPacket txPacket{
                .radioParameters = msg.radioParameters,
                .frame = etl::move(msg.frame),
                .length = msg.length};

            if (!txQueue.push(etl::move(txPacket)))
            {
                statistics.queueFull += 1;
            }
        }

        xTaskNotify(taskHandle, TaskState::HANDLETX, eSetBits);
    }
}

void LR2021::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    if (msg.moduleName == LR2021::NAMES[radioNo])
    {
        txEnabled = msg.config.valueByPath(true, LR2021::NAMES[radioNo], "txEnabled");
        offsetHz = msg.config.valueByPath(true, LR2021::NAMES[radioNo], "offset");
    }
    groundStation = msg.config.gaTasConfig().conspicuity.groundStation;
}

void LR2021::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
}

void LR2021::on_receive(const GATAS::RadioControlMsg &msg)
{
    if (msg.radioNo == radioNo)
    {
        newRxRadioParameters = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), msg.radioParameters);
        xTaskNotify(taskHandle, TaskState::HANDLE_RX_CONFIG, eSetBits);
    }
}

/**
 * Apply methods
 */

void LR2021::radioInit()
{
    // .365
    waitBusy(50);

    lr20xx_set_dio_irq_params(this, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    lr20xx_clear_irq_status(this, SX126X_IRQ_ALL);

    // Must be called before any calibration
    lr20xx_set_dio3_as_tcxo_ctrl(this, SX126X_TCXO_CTRL_1_6V, 5000.f / 15.625);
    lr20xx_set_dio2_as_rf_sw_ctrl(this, true);

    // Start Calibration

    // 9.4 Standby (STDBY) Mode DCDC must be set in SX126X_STANDBY_CFG_RC mode
    // 13.1.12 Calibrate Function must happen in SX126X_STANDBY_CFG_RC
    lr20xx_set_standby(this, SX126X_STANDBY_CFG_RC);
    lr20xx_set_reg_mode(this, SX126X_REG_MODE_DCDC);
    lr20xx_cal(this, SX126X_CAL_ALL);
    waitBusy(25);
    checkAndClearDeviceErrors();
    lr20xx_cal_img_in_mhz(this, 868 - 2, 868 + 2); // 13.1.13 CalibrateImage
    waitBusy(100);
    checkAndClearDeviceErrors();

    // Calibration done, use stanby
    standBy();

    // 15.2.2 Better Resistance of the LR2021 Tx to Antenna Mismatch
    uint8_t clamp;
    lr20xx_read_register(this, 0x08D8, &clamp, 1);
    clamp |= 0x1E;
    lr20xx_write_register(this, 0x08D8, &clamp, 1);

    // TX Base at 0x00  RX Base at 0x80
    lr20xx_set_buffer_base_address(this, 0x00, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE);

    lr20xx_set_rx_tx_fallback_mode(this, SX126X_FALLBACK_STDBY_XOSC);
    lr20xx_set_cad_params(this, &DEFAULT_CAD_PARAMS);

    lr20xx_set_pa_cfg(this, &DEFAULT_HIGH_POWER_PA_CFG);
    lr20xx_set_ocp_value(this, (uint8_t)(120.0 / 2.5));

    // Add RX gain register to retention
    const uint16_t reg = 0x08AC;
    lr20xx_add_registers_to_retention_list(this, &reg, 1);

    // Set boosted gain once
    lr20xx_cfg_rx_boosted(this, true);
}

void LR2021::configureLR2021(const GATAS::RadioParameters &newParameters, uint8_t payloadLength)
{
    // 9.8 Transceiver Circuit Modes Graphical Illustration
    standBy();

    // TX Base at 0x00  RX Base at 0x80
    lr20xx_set_buffer_base_address(this, 0x00, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE);

    if (LOW_POWER_MODE)
    {
        lr20xx_set_pa_cfg(this, &DEFAULT_LOW_POWER_PA_CFG);
    }
    else
    {
        lr20xx_set_pa_cfg(this, &DEFAULT_HIGH_POWER_PA_CFG);
    }

    if (newParameters.config->pcId != rxRadioParameters.config->pcId || true)
    {
        GATAS_MEASURE("configureLR2021", 1600 /* 500 */);

        // This routines takes about 250us
        if (newParameters.frequency->mode == GATAS::Modulation::GFSK)
        {
            lr20xx_set_pkt_type(this, SX126X_PKT_TYPE_GFSK);

            auto mod = DEFAULT_MOD_PARAMS_GFSK;
            if (newParameters.frequency->channelBandwidth == 234300)
            {
                mod.bw_dsb_param = SX126X_GFSK_BW_234300;
            }
            else if (newParameters.frequency->channelBandwidth == 312000)
            {
                mod.bw_dsb_param = SX126X_GFSK_BW_312000;
            }
            else if (newParameters.frequency->channelBandwidth == 187200)
            {
                mod.bw_dsb_param = SX126X_GFSK_BW_187200;
            }
            else
            {
                GATAS_WARN("bw_dsb_param Not set");
            }

            if (newParameters.frequency->gaussBt == 10)
            {
                mod.pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_1;
            }

            mod.br_in_bps = newParameters.frequency->chipRate;
            mod.fdev_in_hz = newParameters.frequency->freqDiv;

            lr20xx_set_gfsk_mod_params(this, &mod);

            // preamble_len_in_bits -> transmitted preamble length: number of bits sent as preamble coded as 0x55.
            // preamble_detector -> the packet controller will only become active if a certain number of preamble bits have been successfully received by the rad
            // The user can select a value ranging from “Preamble detector length off” - where the radio will not perform any gating and will try to lock directly on the following Sync Word
            auto pkt_params_gfsk = DEFAULT_PKG_PARAMS_GFSK;
            uint8_t syncLengthBits;
            const uint8_t *syncData;

            // payloadLength will be !=0 to indicate actual data needs to be send
            if (payloadLength)
            {
                // TX Config
                syncLengthBits = newParameters.config->syncLength;
                syncData = newParameters.config->syncWord.data();
                pkt_params_gfsk.pld_len_in_bytes = payloadLength * (newParameters.config->manchester ? 2 : 1);

                // Variable payload test (0 is true)
                if (newParameters.config->packetLength == 0) // When newParameters.config->packetLength == 0, this means variable payload
                {
                    pkt_params_gfsk.header_type = SX126X_GFSK_PKT_VAR_LEN;
                }
            }
            else
            {
                // RX Config
                syncLengthBits = newParameters.config->syncLength - newParameters.config->syncSkipInRxLength;
                syncData = newParameters.config->syncWord.data() + (newParameters.config->syncSkipInRxLength + 7) / 8;

                // Variable payload test (0 is true)
                if (newParameters.config->packetLength == 0)
                {
                    pkt_params_gfsk.header_type = SX126X_GFSK_PKT_VAR_LEN;
                    // Maximum payload size in variable mode
                    pkt_params_gfsk.pld_len_in_bytes = GROUNDSTATION_RX_BASE;
                }
                else
                {
                    pkt_params_gfsk.pld_len_in_bytes = newParameters.config->packetLength * (newParameters.config->manchester ? 2 : 1);
                }
            }

            pkt_params_gfsk.preamble_detector = SX126X_GFSK_PREAMBLE_DETECTOR_MIN_8BITS;   // Reception (bit set anyways) Must be smaller than sync 6.2.2.1
            pkt_params_gfsk.preamble_len_in_bits = newParameters.config->txPreambleLength; // In addition to this length, there is also 16 bit preamble specific for nRF905 added to the syncword. This will works fine for an LR2021
            pkt_params_gfsk.sync_word_len_in_bits = syncLengthBits;
            lr20xx_set_gfsk_pkt_params(this, &pkt_params_gfsk);
            lr20xx_set_gfsk_sync_word(this, syncData, (syncLengthBits + 7) / 8);
        }
        else if (newParameters.frequency->mode == GATAS::Modulation::LORA)
        {
            lr20xx_set_pkt_type(this, SX126X_PKT_TYPE_LORA);

            auto pkg = DEFAULT_PKG_PARAMS_LORA;
            // Only set lr20xx_set_lora_mod_params in RX path
            // payloadLength will be !=0 to indicate actual data needs to be send
            if (payloadLength == 0)
            {
                auto mod = DEFAULT_MOD_PARAMS_LORA;
                if (newParameters.frequency->channelBandwidth == 500000)
                {
                    mod.bw = SX126X_LORA_BW_250;
                }
                else if (newParameters.frequency->channelBandwidth == 250000)
                {
                    mod.bw = SX126X_LORA_BW_250;
                }
                else if (newParameters.frequency->channelBandwidth == 125000)
                {
                    mod.bw = SX126X_LORA_BW_125;
                }

                // These Setting are handled in sendLORAPacket because codingrate is set dynamically packet
                lr20xx_set_lora_mod_params(this, &mod);

                pkg.pld_len_in_bytes = newParameters.config->packetLength;
            }
            else
            {
                pkg.pld_len_in_bytes = payloadLength;
            }

            pkg.preamble_len_in_symb = newParameters.config->txPreambleLength;
            lr20xx_set_lora_pkt_params(this, &pkg);

            lr20xx_set_lora_sync_word(this, newParameters.config->syncWord.data()[0]);
        }
    }

    if (newParameters.hopFrequency != rxRadioParameters.hopFrequency || true)
    {
        lr20xx_set_rf_freq(this, newParameters.hopFrequency + offsetHz);
    }

    // GATAS_INFO("Radio %d changed frequency from %ld to %ld", radioNo, lastParameters.hopFrequency, newParameters.hopFrequency);
    checkAndClearDeviceErrors();
}

void LR2021::listen()
{
    // https://forum.lora-developers.semtech.com/t/LR2021-reduced-rx-sensitivity-packet-recepqtion-fails/162/12
    // Need to call SetFs() and then RxBoosted() periodically to fix a issue with receiver gain
    lr20xx_set_fs(this);
    lr20xx_cfg_rx_boosted(this, true);

    lr20xx_set_dio_irq_params(this, SX126X_IRQ_RX_DONE, SX126X_IRQ_RX_DONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    lr20xx_clear_irq_status(this, SX126X_IRQ_ALL);
    enablePinInterrupt(dio1Pin, DIO1_RX_DONE);
    lr20xx_set_rx(this, SX126X_MAX_TIMEOUT_IN_MS);
}

void LR2021::sendGFSKPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length)
{
    (void)parameters;
    lr20xx_set_dio_irq_params(this, SX126X_IRQ_TX_DONE, SX126X_IRQ_TX_DONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    lr20xx_clear_irq_status(this, SX126X_IRQ_ALL);
    enablePinInterrupt(dio1Pin, DIO1_TX_DONE);

    // 22Dbm is the max power for LR2021.
    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::max(static_cast<int8_t>(22), parameters.frequency->powerdBm);
    lr20xx_set_tx_params(this, powerdBm, SX126X_RAMP_200_US);
    lr20xx_write_buffer(this, 0x00, data, length);
    // 13.1.14 SetTx
    lr20xx_set_tx(this, SX126X_MAX_TIMEOUT_IN_MS);
}

void LR2021::sendLORAPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length)
{

    // printBufferHex(etl::span(data, length));
    // putchar('\n');
    auto mod = DEFAULT_MOD_PARAMS_LORA;
    switch (parameters.codingRate)
    {
    case 5:
        mod.cr = SX126X_LORA_CR_4_5;
        break;
    case 6:
        mod.cr = SX126X_LORA_CR_4_6;
        break;
    case 7:
        mod.cr = SX126X_LORA_CR_4_7;
        break;
    }

    if (parameters.frequency->channelBandwidth == 500000)
    {
        mod.bw = SX126X_LORA_BW_500;
    }
    else if (parameters.frequency->channelBandwidth == 250000)
    {
        mod.bw = SX126X_LORA_BW_250;
    }
    else if (parameters.frequency->channelBandwidth == 125000)
    {
        mod.bw = SX126X_LORA_BW_125;
    }

    // These Setting are handled in sendLORAPacket because codingrate is set dynamically packet
    lr20xx_set_lora_mod_params(this, &mod);

    int8_t powerdBm = LOW_POWER_MODE ? LOW_POWER_DBM : etl::max(static_cast<int8_t>(22), parameters.frequency->powerdBm);
    lr20xx_set_tx_params(this, powerdBm, SX126X_RAMP_200_US);

    // Wait until CAD done
    // disablePinInterrupt(dio1Pin); // We are just waiting for CAD
    // Might be here a solution?? https://github.com/antirez/freakwan/tree/main/techo-port
    lr20xx_set_dio_irq_params(this, SX126X_IRQ_TX_DONE, SX126X_IRQ_TX_DONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    lr20xx_clear_irq_status(this, SX126X_IRQ_ALL);
    enablePinInterrupt(dio1Pin, DIO1_TX_DONE);

    // 13.1.14 SetTx
    lr20xx_write_buffer(this, 0x00, data, length);
    lr20xx_set_tx(this, SX126X_MAX_TIMEOUT_IN_MS);
}

void LR2021::receiveGFSKPacket()
{
    // 13.5.3 GetPacketStatus
    lr20xx_pkt_status_gfsk_t pkt_status;
    lr20xx_get_gfsk_pkt_status(this, &pkt_status);
    if (pkt_status.rx_status.pkt_received && pkt_status.rx_status.abort_error == 0)
    {
        statistics.receivedPackets += 1;
        uint8_t receivedFrameLength = receivedPacketLength();

        if (receivedFrameLength >= 4)
        {
            // ADSL OHR has a lot of false packets, we eurly detect them and drop them
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
                    .rssidBm = pkt_status.rssi_avg,
                };

                lr20xx_read_buffer(this, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE, rxFrame.frame.get(), receivedFrameLength);
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
        // If we get here, there is some mis configuration going on
        checkAndClearDeviceErrors();
        GATAS_INFO("pkt_status.rx_status.pkt_received %d %d %d %d %d %d",
                   pkt_status.rx_status.pkt_received, pkt_status.rx_status.abort_error, pkt_status.rx_status.length_error,
                   pkt_status.rx_status.crc_error, pkt_status.rx_status.pkt_sent, pkt_status.rx_status.adrs_error);
    }
}

void LR2021::receiveLORAPacket()
{
    // 13.5.3 GetPacketStatus
    lr20xx_chip_status_t chip_status;
    lr20xx_get_status(this, &chip_status);
    if (chip_status.cmd_status == SX126X_CMD_STATUS_DATA_AVAILABLE)
    {
        lr20xx_pkt_status_lora_t pkt_status;
        lr20xx_get_lora_pkt_status(this, &pkt_status);

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
                    .rssidBm = pkt_status.signal_rssi_pkt_in_dbm,
                };

                lr20xx_read_buffer(this, groundStation ? GROUNDSTATION_RX_BASE : DEFAULT_RX_BASE, rxFrame.frame.get(), receivedFrameLength);
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
        // If we get here, there is some mis configuration going on
        checkAndClearDeviceErrors();
    }
}

uint8_t LR2021::receivedPacketLength() const
{
    lr20xx_rx_buffer_status_t rx_buffer_status;
    lr20xx_get_rx_buffer_status(this, &rx_buffer_status);
    return rx_buffer_status.pld_len_in_bytes;
}

void LR2021::waitBusy(uint16_t minimumDelay) const
{
    constexpr uint8_t checkInterval = 100; // loop cycles per check

    uint8_t countdown = checkInterval;
    while (gpio_get(busyPin))
    {
        vTaskDelay(TASK_DELAY_MS(2));
        if (--countdown == 0)
        {
            statistics.buzyWaitsTimeout += 1;
            return;
        }
    }

    vTaskDelay(TASK_DELAY_MS(minimumDelay));
}

void LR2021::standBy()
{
    GATAS_MEASURE("standBy", 2000 /* 150 */);

    // .715
    lr20xx_set_standby(this, SX126X_STANDBY_CFG_XOSC);
}

void LR2021::checkAndClearDeviceErrors()
{
    GATAS_MEASURE("checkAndClearDeviceErrors", 600 /* 200 */);
    lr20xx_status_t status = lr20xx_get_device_errors(this, &currentDeviceError);
    if (status == SX126X_STATUS_OK && currentDeviceError != 0)
    {
        statistics.deviceErrors += 1;
        lastDeviceError = currentDeviceError;
        lr20xx_set_dio_irq_params(this, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
        lr20xx_clear_irq_status(this, SX126X_IRQ_ALL);
        lr20xx_clear_device_errors(this);
        GATAS_WARN("Device Error: %b", currentDeviceError);
    }
}

lr20xx_irq_mask_t LR2021::getIrqStatus()
{
    lr20xx_irq_mask_t mask;
    lr20xx_get_irq_status(this, &mask);
    return mask;
}

bool LR2021::isTxDone()
{
    const lr20xx_irq_mask_t mask = getIrqStatus();
    if (mask & SX126X_IRQ_TX_DONE)
    {
        lr20xx_clear_irq_status(this, SX126X_IRQ_TX_DONE);
        return true;
    }
    return false;
}

void LR2021::sendPacket(const TxPacket &txPacket)
{
    // GATAS_INFO("Radio %d TX %s timeMs:%d", radioNo, GATAS::toString(command.txPacket.radioParameters.config->dataSource), CoreUtils::msInSecond());
    // TODO: Sometimes configuration can take a few msmeven we don;t change protocol

    if (txPacket.radioParameters.frequency->mode == GATAS::Modulation::GFSK)
    {
        GATAS_MEASURE("sendGFSKPacket", 800);
        if (txPacket.radioParameters.config->manchester)
        {
            if (txPacket.length > GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH)
            {
                GATAS_WARN("Frame too long for manchester encoding %d", txPacket.length);
                return;
            }
            uint8_t manchesterFrame[GATAS::RADIO_MAX_TX_GFSK_FRAME_LENGTH * MANCHESTER];
            manchesterEncode(manchesterFrame, txPacket.frame, txPacket.length);
            sendGFSKPacket(txPacket.radioParameters, manchesterFrame, txPacket.length * MANCHESTER);
        }
        else
        {
            sendGFSKPacket(txPacket.radioParameters, txPacket.frame, txPacket.length);
        }
    }
    else if (txPacket.radioParameters.frequency->mode == GATAS::Modulation::LORA)
    {
        GATAS_MEASURE("sendLORAPacket", 100);
        sendLORAPacket(txPacket.radioParameters, txPacket.frame, txPacket.length);
    }
}

void LR2021::LR2021Trampoline(void *arg)
{
    LR2021 *LR2021 = static_cast<LR2021 *>(arg);
    LR2021->LR2021Task(arg);
}

void LR2021::LR2021Task(void *arg)
{
    (void)arg;
    SpiModule *aceSpi = static_cast<SpiModule *>(BaseModule::moduleByName(*this, SpiModule::NAME));
    uint32_t keepTransmittingUntill = 0;
    bool doListen = false;
    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, TASK_DELAY_MS(2000));

        if (notifyValue)
        {
            // When a new configuration mark it with a boolean as it needs to be processed later
            if (notifyValue & TaskState::HANDLE_RX_CONFIG)
            {
                rxRadioParameters = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), newRxRadioParameters);
                // GATAS_INFO("%8ld New Config ds:%s", CoreUtils::timeUs32Raw() / 1000, GATAS::toString(rxRadioParameters.config->dataSource()));
                doListen = true;
            }

            // After TX, go back to RX
            if (notifyValue & TaskState::DIO1_TX_DONE)
            {
                statistics.transmittedPackets += 1;
                doListen = true;
                keepTransmittingUntill = 0;
            }

            // When in TX mode, the transceiver cannot be reconfigured and we need to wait for the TX to finish
            if (keepTransmittingUntill)
            {
                // Keep listening
                if (!CoreUtils::isUsReachedRaw(keepTransmittingUntill))
                {
                    // TX still in progress — normal, skip queue and wait for DIO1_TX_DONE
                    continue;
                }
                // 55ms elapsed without DIO1_TX_DONE — hardware likely stuck, fall through to recover
                GATAS_WARN("TX timeout - no DIO1_TX_DONE received within 55ms");
                keepTransmittingUntill = 0;
                doListen = true;
            }

            // When a packet is received, receive it and directly reconfigure the transceiver.. then send it to the bus
            if (notifyValue & TaskState::DIO1_RX_DONE)
            {
                // GATAS_INFO("Radio %d Packet RX: %s timeMs:%d", radioNo, GATAS::toString(rxRadioParameters.config->dataSource), CoreUtils::msInSecond());
                bool _;
                if (auto guard = aceSpi->getLock(_))
                {
                    standBy();
                    if (rxRadioParameters.frequency->mode == GATAS::Modulation::GFSK)
                    {
                        GATAS_MEASURE("Receive GFSK Packet Radio:", 1800, rxRadioParameters.hopFrequency);
                        receiveGFSKPacket();
                        doListen = true;
                    }
                    else if (rxRadioParameters.frequency->mode == GATAS::Modulation::LORA)
                    {
                        GATAS_MEASURE("Receive Lora Packet Radio:", 0, radioNo);
                        receiveLORAPacket();
                        doListen = true;
                    }
                }
            };

            // Only in TX
            if (TxPacket txPacket; txQueue.pop(txPacket))
            {
                bool _;
                if (auto guard = aceSpi->getLock(_))
                {
                    GATAS_MEASURE("Send Radio:", 1500, radioNo);
                    // GATAS_INFO("%8ld TX Packet ds:%s", CoreUtils::timeUs32Raw() / 1000, GATAS::toString(txPacket.radioParameters.config->dataSource()));
                    keepTransmittingUntill = CoreUtils::timeUs32Raw() + 55000; // 55ms is longest packet expect (LORA)
                    configureLR2021(txPacket.radioParameters, txPacket.length);
                    sendPacket(txPacket);
                    continue; // Need to wait for TX done
                }
            }

            // When set, instruct to start listening again
            if (doListen)
            {
                bool _;
                if (auto guard = aceSpi->getLock(_))
                {
                    configureLR2021(rxRadioParameters, 0);
                    listen();
                }
                doListen = false;
            }
        }
    }
}
