#include "../radio_base.hpp"

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* GATAS. */
#include "ace/manchester.hpp"
#include "ace/coreutils.hpp"
#include "ace/measure.hpp"
#include "ace/models.hpp"
#include "ace/moreutils.hpp"


void RadioBase::start()
{
    getBus().subscribe(*this);
};

GATAS::PostConstruct RadioBase::postConstruct()
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

    // Busy Pin has PullUp from SX1262 so kept as input only
    gpio_init(busyPin);
    gpio_set_dir(busyPin, GPIO_IN);

    gpio_init(dio1Pin);
    gpio_set_dir(dio1Pin, GPIO_IN);

    if (!detectradio()) {
        return GATAS::PostConstruct::HARDWARE_NOT_FOUND;
    }

    radioInit();

    if (xTaskCreate(radioTaskTrampoline, COMMON_NAMES[radioNo].cbegin(), configMINIMAL_STACK_SIZE + 128, this, tskIDLE_PRIORITY + 4, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }
    registerPinInterrupt(dio1Pin, GPIO_IRQ_EDGE_RISE, taskHandle, 0);

    GATAS_INFO("Initialised on cs:%d busy:%d dio1:%d ", csPin, busyPin, dio1Pin);

    return GATAS::PostConstruct::OK;
}

// Common message handlers - identical for all radio implementations
void RadioBase::on_receive(const GATAS::RadioTxFrameMsg &msg)
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

void RadioBase::on_receive(const GATAS::ConfigUpdatedMsg &msg)
{
    // Note: Subclasses must override NAMES array and pass it appropriately
    // This handler uses the module name from BaseModule to check
    const etl::string_view currentName = BaseModule::name();
    if (msg.moduleName == currentName)
    {
        txEnabled = msg.config.valueByPath(txEnabled, currentName, "txEnabled");
        offsetHz = msg.config.valueByPath(offsetHz, currentName, "offset");
    }
    groundStation = msg.config.gaTasConfig().conspicuity.groundStation;
}

void RadioBase::on_receive(const GATAS::GpsStatsMsg &msg)
{
    hasGpsFix = msg.gpsStats.gpsFix.hasFix;
}

void RadioBase::on_receive(const GATAS::RadioControlMsg &msg)
{
    if (msg.radioNo == radioNo)
    {
        newRxRadioParameters = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), msg.radioParameters);
        xTaskNotify(taskHandle, TaskState::HANDLE_RX_CONFIG, eSetBits);
    }
}

// Common helper: dispatch packet sending based on modulation type
void RadioBase::sendPacket(const TxPacket &txPacket)
{
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

// Common trampoline for task creation
void RadioBase::radioTaskTrampoline(void *arg)
{
    RadioBase *radio = static_cast<RadioBase *>(arg);
    radio->radioTask(arg);
}

// Common RX/TX state machine - identical logic for all radio implementations
void RadioBase::radioTask(void *arg)
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
                    keepTransmittingUntill = CoreUtils::timeUs32Raw() + 55000; // 55ms is longest packet expect (LORA)
                    configureRadio(txPacket.radioParameters, txPacket.length);
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
                    configureRadio(rxRadioParameters, 0);
                    listen();
                }
                doListen = false;
            }
        }
    }
}

void RadioBase::waitBusy(uint16_t minimumDelay) const
{
    constexpr uint8_t checkInterval = 100;

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
