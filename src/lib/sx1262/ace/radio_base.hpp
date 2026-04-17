#pragma once

/* System. */
#include <stdint.h>

/* FreeRTOS. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* PICO. */
#include "pico/binary_info.h"
#include "pico/stdlib.h"

/* Vendor. */
#include "etl/message_bus.h"
#include "etl/queue_spsc_atomic.h"

/* GaTas Libraries */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/messages.hpp"
#include "rxdataframequeue.hpp"

/**
 * Abstract base class for radio implementations (Sx1262, Lr2021, etc.)
 * Handles common logic: lifecycle, message routing, RX/TX state machine
 * Subclasses implement hardware-specific radio operations
 */
class RadioBase : public BaseModule, public etl::message_router<RadioBase, GATAS::RadioTxFrameMsg, GATAS::ConfigUpdatedMsg, GATAS::GpsStatsMsg, GATAS::RadioControlMsg>
{
protected:
    static constexpr uint8_t MANCHESTER = 2; // Used to clarify why we multiply by 2 for Manchester encoding
#if GATAS_DEBUG == 1
    static constexpr bool LOW_POWER_MODE = true;
#else
    static constexpr bool LOW_POWER_MODE = false;
#endif
    static constexpr uint8_t LOW_POWER_DBM = 0; // - 17 (0xEF) to +14 (0x0E) dBm by step of 1 dB if low power PA is selected

    static constexpr uint8_t GROUNDSTATION_RX_BASE = 202; // Largest frame is 25 byte -> 50 in manchester
    static constexpr uint8_t DEFAULT_RX_BASE = 54;

    enum TaskState : uint8_t
    {
        DIO1_TX_DONE = 1,
        DIO1_RX_DONE = 2,
        HANDLETX = 4,
        HANDLE_RX_CONFIG = 8,
    };

    struct TxPacket
    {
        GATAS::RadioParameters radioParameters;
        PoolOwnedPtr<GATAS::GlobalPoolConfiguration, const uint8_t> frame;
        size_t length = 0;
    };

    mutable struct
    {
        uint16_t deviceErrors = 0;
        uint32_t receivedPackets = 0;
        uint32_t transmittedPackets = 0;
        uint32_t buzyWaitsTimeout = 0;
        uint32_t queueFull = 0;
    } statistics;

    // Hardware access
    const uint8_t csPin;
    const uint8_t busyPin;
    const uint8_t dio1Pin;
    const uint8_t radioNo;

    // Configuration and state
    bool txEnabled;
    bool groundStation;
    uint32_t offsetHz;
    bool hasGpsFix = false;

    // Modules
    SpiModule *spiHall = nullptr;
    RxDataFrameQueue *rxDataFrameQueue = nullptr;

    // Task management
    TaskHandle_t taskHandle = nullptr;
    etl::queue_spsc_atomic<TxPacket, 4, etl::memory_model::MEMORY_MODEL_SMALL> txQueue;

    // RX configuration
    GATAS::RadioParameters rxRadioParameters{nullptr, nullptr, 868'000'000, 0};
    GATAS::RadioParameters newRxRadioParameters{nullptr, nullptr, 868'000'000, 0};

protected:
    // Task management
    static void radioTaskTrampoline(void *arg);
    void radioTask(void *arg);

    // Common helper
    void sendPacket(const TxPacket &txpacket);

    // Hardware-specific methods - must be implemented by subclasses
    virtual void radioInit() = 0;
    virtual void checkAndClearDeviceErrors() = 0;
    virtual void receiveGFSKPacket() = 0;
    virtual void receiveLORAPacket() = 0;
    virtual void sendGFSKPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length) = 0;
    virtual void sendLORAPacket(const GATAS::RadioParameters &parameters, const uint8_t *data, uint8_t length) = 0;
    virtual void configureRadio(const GATAS::RadioParameters &newParameters, uint8_t txPayloadLength) = 0;
    virtual void listen() = 0;
    virtual void standBy() = 0;
    virtual uint8_t receivedPacketLength() const = 0;
    virtual bool detectradio() const = 0;
    void waitBusy(uint16_t minimumDelay = 0) const;

public:
    static constexpr etl::array<etl::string_view, 4> COMMON_NAMES{"_Radio_0", "_Radio_1", "_Radio_2", "_Radio_3"};

    RadioBase(etl::imessage_bus &bus, const GATAS::PinTypeMap &pins, uint8_t radioNo_, bool txEnabled_, bool groundStation_, uint32_t offsetHz_, const etl::string_view &moduleName)
        : BaseModule(bus, moduleName),
          csPin(pins.at(GATAS::PinType::CS)),
          busyPin(pins.at(GATAS::PinType::BUSY)),
          dio1Pin(pins.at(GATAS::PinType::DIO1)),
          radioNo(radioNo_),
          txEnabled(txEnabled_),
          groundStation(groundStation_),
          offsetHz(offsetHz_)
    {
    }

    virtual ~RadioBase() = default;

    // Lifecycle - common implementation uses these virtual methods for radio-specific setup
    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;

    // Data access
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override = 0;

    // Message routing
    inline void sendToBus(const etl::imessage &message)
    {
        getBus().receive(message);
    };

    void on_receive_unknown(const etl::imessage &msg)
    {
        (void)msg;
    }

    // Common message handlers - same for all radio implementations
    void on_receive(const GATAS::ConfigUpdatedMsg &msg);
    void on_receive(const GATAS::RadioTxFrameMsg &msg);
    void on_receive(const GATAS::RadioControlMsg &msg);
    void on_receive(const GATAS::GpsStatsMsg &msg);

    // Accessor methods
    inline uint8_t cs() const
    {
        return csPin;
    }
    inline uint8_t busy() const
    {
        return busyPin;
    }
    inline SpiModule *spi()
    {
        return spiHall;
    }
    virtual uint8_t radio() const
    {
        return radioNo;
    }
};
