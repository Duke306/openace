#include "../StaticGPS.hpp"

#include <cmath>
#include <cstring>

#include "../StaticGPSNmea.hpp"

#include "ace/coreutils.hpp"
#include "ace/lwiplock.hpp"

#include "etl/to_arithmetic.h"

#include "lwip/def.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/time.h"

namespace
{
    constexpr uint16_t NTP_PORT = 123;
    constexpr size_t NTP_PACKET_SIZE = 48;
    constexpr uint32_t NTP_TO_UNIX_EPOCH_SECONDS = 2'208'988'800UL;
}

float StaticGPS::configFloat(const Configuration &config, const etl::string_view key, const char *defaultValue)
{
    const auto text = config.strValueByPath(defaultValue, NAME, key);
    const auto value = etl::to_arithmetic<float>(text);
    if (!value || !std::isfinite(value.value()))
    {
        const auto fallback = etl::to_arithmetic<float>(etl::string_view(defaultValue));
        return fallback ? fallback.value() : 0.0F;
    }
    return value.value();
}

StaticGPS::StaticGPS(etl::imessage_bus &bus, float latitude_, float longitude_, float altitudeMeters_, const etl::string_view ntpServer_)
    : AbstractGnss(bus, NAME, GATAS::PinTypeMap{}, true, 0),
      latitude(latitude_),
      longitude(longitude_),
      altitudeMeters(altitudeMeters_),
      ntpServer(ntpServer_)
{
}

StaticGPS::StaticGPS(etl::imessage_bus &bus, const Configuration &config)
    : StaticGPS(bus,
                configFloat(config, "latitude", "52.8925725"),
                configFloat(config, "longitude", "4.7362312"),
                configFloat(config, "altitude", "0.0"),
                config.strValueByPath("pool.ntp.org", NAME, "ntpServer"))
{
}

GATAS::PostConstruct StaticGPS::postConstruct()
{
    if (!std::isfinite(latitude) || latitude < -90.0F || latitude > 90.0F ||
        !std::isfinite(longitude) || longitude < -180.0F || longitude > 180.0F ||
        !std::isfinite(altitudeMeters) || altitudeMeters < -1000.0F || altitudeMeters > 20000.0F ||
        ntpServer.empty())
    {
        setStatus("Config error");
        return GATAS::PostConstruct::CONFIG_ERROR;
    }

    rtc = static_cast<RtcModule *>(moduleByName(*this, RtcModule::NAME));
    if (rtc == nullptr)
    {
        setStatus("No RTC");
        return GATAS::PostConstruct::DEP_NOT_FOUND;
    }

    setStatus("Waiting NTP");
    return GATAS::PostConstruct::OK;
}

void StaticGPS::start()
{
    getBus().subscribe(*this);

    if (xTaskCreate(taskTrampoline, NAME.cbegin(), configMINIMAL_STACK_SIZE + 1024, this, tskIDLE_PRIORITY + 3, &staticTaskHandle) != pdPASS)
    {
        setStatus("Task error");
        return;
    }

    sendTimerHandle = xTimerCreate(NAME.cbegin(), TASK_DELAY_MS(SEND_INTERVAL_MS), pdTRUE, this, timerCallback);
    if (sendTimerHandle == nullptr || xTimerStart(sendTimerHandle, 0) != pdPASS)
    {
        setStatus("Timer error");
        return;
    }

    nextNtpAttemptUs = time_us_64();
    xTaskNotify(staticTaskHandle, NETWORK_CHANGED, eSetBits);
}

void StaticGPS::taskTrampoline(void *arg)
{
    static_cast<StaticGPS *>(arg)->task();
}

void StaticGPS::timerCallback(TimerHandle_t timer)
{
    auto *staticGps = static_cast<StaticGPS *>(pvTimerGetTimerID(timer));
    if (staticGps != nullptr && staticGps->staticTaskHandle != nullptr)
    {
        xTaskNotify(staticGps->staticTaskHandle, SEND_SENTENCES, eSetBits);
    }
}

void StaticGPS::task()
{
    while (true)
    {
        uint32_t notification = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notification, portMAX_DELAY);

        if ((notification & NTP_RESULT) != 0)
        {
            applyNtpResult();
        }

        if ((notification & NETWORK_CHANGED) != 0)
        {
            nextNtpAttemptUs = time_us_64();
        }

        if ((notification & NTP_FAILED) != 0)
        {
            staticStatistics.ntpErrors += 1;
            nextNtpAttemptUs = time_us_64() + NTP_RETRY_INTERVAL_US;
        }

        const uint64_t nowUs = time_us_64();
        if (ntpRequestActive && nowUs - ntpRequestStartedUs >= NTP_TIMEOUT_US)
        {
            cancelNtpRequest();
            staticStatistics.ntpErrors += 1;
            nextNtpAttemptUs = nowUs + NTP_RETRY_INTERVAL_US;
        }

        if (!ntpRequestActive && nowUs >= nextNtpAttemptUs)
        {
            beginNtpRequest();
        }

        if ((notification & SEND_SENTENCES) != 0)
        {
            publishSentences();
        }
    }
}

void StaticGPS::publishSentences()
{
    // Until NTP or another source has established epoch time, emitting 1970
    // would falsely claim a valid GNSS time and date.
    const uint64_t epochMs = CoreUtils::msSinceEpoch();
    if (epochMs < 1'000'000'000'000ULL)
    {
        staticStatistics.invalidTime += 1;
        return;
    }

    auto sentences = StaticGPSNmea::create(latitude, longitude, altitudeMeters, epochMs);
    if (!sentences)
    {
        setStatus("NMEA error");
        return;
    }

    publishSentence(sentences->gll);
    publishSentence(sentences->rmc);
    publishSentence(sentences->vtg);
    publishSentence(sentences->gga);
    publishSentence(sentences->gsa);
}

void StaticGPS::beginNtpRequest()
{
    cancelNtpRequest();

    LwipLock lock;
    ntpPcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (ntpPcb == nullptr)
    {
        staticStatistics.ntpErrors += 1;
        nextNtpAttemptUs = time_us_64() + NTP_RETRY_INTERVAL_US;
        return;
    }

    udp_recv(ntpPcb, ntpReceiveCallback, this);
    ntpRequestActive = true;
    ntpRequestStartedUs = time_us_64();
    staticStatistics.ntpRequests += 1;

    ip_addr_t address;
    const err_t error = dns_gethostbyname(ntpServer.c_str(), &address, dnsCallback, this);
    if (error == ERR_OK)
    {
        sendNtpRequest(&address);
    }
    else if (error != ERR_INPROGRESS)
    {
        udp_remove(ntpPcb);
        ntpPcb = nullptr;
        ntpRequestActive = false;
        staticStatistics.ntpErrors += 1;
        nextNtpAttemptUs = time_us_64() + NTP_RETRY_INTERVAL_US;
    }
}

void StaticGPS::dnsCallback(const char *name, const ip_addr_t *address, void *arg)
{
    (void)name;
    auto *staticGps = static_cast<StaticGPS *>(arg);
    if (staticGps == nullptr || !staticGps->ntpRequestActive)
    {
        return;
    }

    if (address == nullptr)
    {
        staticGps->ntpRequestActive = false;
        if (staticGps->ntpPcb != nullptr)
        {
            udp_remove(staticGps->ntpPcb);
            staticGps->ntpPcb = nullptr;
        }
        xTaskNotify(staticGps->staticTaskHandle, NTP_FAILED, eSetBits);
        return;
    }

    staticGps->sendNtpRequest(address);
}

void StaticGPS::sendNtpRequest(const ip_addr_t *address)
{
    if (!ntpRequestActive || ntpPcb == nullptr || address == nullptr)
    {
        return;
    }

    pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_SIZE, PBUF_RAM);
    if (packet == nullptr)
    {
        return;
    }

    uint8_t request[NTP_PACKET_SIZE] = {};
    request[0] = 0x1B; // NTP v3, client mode
    if (pbuf_take(packet, request, sizeof(request)) != ERR_OK)
    {
        pbuf_free(packet);
        return;
    }
    if (udp_connect(ntpPcb, address, NTP_PORT) == ERR_OK)
    {
        udp_send(ntpPcb, packet);
    }
    pbuf_free(packet);
}

void StaticGPS::ntpReceiveCallback(void *arg, udp_pcb *pcb, pbuf *packet, const ip_addr_t *address, uint16_t port)
{
    (void)address;
    auto *staticGps = static_cast<StaticGPS *>(arg);
    if (staticGps == nullptr || packet == nullptr)
    {
        if (packet != nullptr)
        {
            pbuf_free(packet);
        }
        return;
    }

    uint8_t response[NTP_PACKET_SIZE] = {};
    const bool valid = staticGps->ntpRequestActive && port == NTP_PORT &&
                       packet->tot_len >= NTP_PACKET_SIZE &&
                       pbuf_copy_partial(packet, response, sizeof(response), 0) == sizeof(response) &&
                       (response[0] & 0x07) == 4 && // server mode
                       (response[0] >> 6) != 3 &&  // clock is synchronized
                       response[1] != 0;           // valid stratum
    pbuf_free(packet);

    if (!valid)
    {
        return;
    }

    uint32_t ntpSecondsNetwork = 0;
    uint32_t ntpFractionNetwork = 0;
    std::memcpy(&ntpSecondsNetwork, response + 40, sizeof(ntpSecondsNetwork));
    std::memcpy(&ntpFractionNetwork, response + 44, sizeof(ntpFractionNetwork));
    const uint32_t ntpSeconds = lwip_ntohl(ntpSecondsNetwork);
    const uint32_t ntpFraction = lwip_ntohl(ntpFractionNetwork);
    const uint64_t unixSeconds = ntpSeconds >= NTP_TO_UNIX_EPOCH_SECONDS
                                     ? static_cast<uint64_t>(ntpSeconds - NTP_TO_UNIX_EPOCH_SECONDS)
                                     : (1ULL << 32) + ntpSeconds - NTP_TO_UNIX_EPOCH_SECONDS;

    const uint64_t receiveUs = time_us_64();
    const uint64_t roundTripMs = (receiveUs - staticGps->ntpRequestStartedUs) / 1'000ULL;
    const uint64_t unixMs = unixSeconds * 1'000ULL +
                            ((static_cast<uint64_t>(ntpFraction) * 1'000ULL) >> 32) +
                            roundTripMs / 2;

    taskENTER_CRITICAL();
    staticGps->ntpEpochAtReceiveMs = unixMs;
    staticGps->ntpReceivedAtUs = receiveUs;
    staticGps->ntpResultPending = true;
    taskEXIT_CRITICAL();

    staticGps->ntpRequestActive = false;
    if (pcb != nullptr)
    {
        udp_remove(pcb);
        staticGps->ntpPcb = nullptr;
    }
    xTaskNotify(staticGps->staticTaskHandle, NTP_RESULT, eSetBits);
}

void StaticGPS::cancelNtpRequest()
{
    LwipLock lock;
    if (ntpPcb != nullptr)
    {
        udp_remove(ntpPcb);
        ntpPcb = nullptr;
    }
    ntpRequestActive = false;
}

void StaticGPS::applyNtpResult()
{
    uint64_t epochAtReceiveMs = 0;
    uint64_t receivedAtUs = 0;
    taskENTER_CRITICAL();
    if (ntpResultPending)
    {
        epochAtReceiveMs = ntpEpochAtReceiveMs;
        receivedAtUs = ntpReceivedAtUs;
        ntpResultPending = false;
    }
    taskEXIT_CRITICAL();

    if (epochAtReceiveMs == 0)
    {
        return;
    }

    const uint64_t nowUs = time_us_64();
    const uint64_t epochNowMs = epochAtReceiveMs + (nowUs - receivedAtUs) / 1'000ULL;
    // NTP gives the current fractional UTC second. Feeding that phase through
    // RtcModule keeps PicoRtc's PPS state and CoreUtils' software PPS aligned.
    rtc->ppsEvent(static_cast<int32_t>((epochNowMs % 1'000ULL) * 1'000ULL));
    CoreUtils::setOffsetMsSinceEpoch(epochNowMs);
    staticStatistics.ntpSyncs += 1;
    nextNtpAttemptUs = nowUs + NTP_REFRESH_INTERVAL_US;
    setStatus("Configured");
}

void StaticGPS::on_receive(const GATAS::WifiConnectionStateMsg &msg)
{
    if (msg.wifiMode == GATAS::WifiMode::CLIENT && staticTaskHandle != nullptr)
    {
        xTaskNotify(staticTaskHandle, NETWORK_CHANGED, eSetBits);
    }
}

void StaticGPS::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

bool StaticGPS::configureGnss()
{
    return true;
}

void StaticGPS::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"latitude\":" << etl::format_spec{}.precision(7) << latitude;
    stream << ",\"longitude\":" << longitude;
    stream << ",\"altitude:m\":" << altitudeMeters;
    stream << ",\"status\":\"" << getStatus() << "\"";
    stream << ",\"totalReceived:k\":" << getTotalReceived();
    stream << ",\"ntpServer\":\"" << ntpServer << "\"";
    stream << ",\"ntpRequests:k\":" << staticStatistics.ntpRequests;
    stream << ",\"ntpSyncs:k\":" << staticStatistics.ntpSyncs;
    stream << ",\"ntpErrors:err\":" << staticStatistics.ntpErrors;
    stream << ",\"waitingForTime:k\":" << staticStatistics.invalidTime;
    stream << "}";
}
