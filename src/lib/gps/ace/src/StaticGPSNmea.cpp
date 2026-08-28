#include "../StaticGPSNmea.hpp"

#include <cmath>
#include <ctime>

#include "ace/coreutils.hpp"

#include "etl/string.h"
#include "etl/string_stream.h"

namespace
{
    bool finishSentence(GATAS::NMEAString &sentence)
    {
        if (sentence.empty() || sentence.front() != '$')
        {
            return false;
        }

        const size_t bodySize = sentence.size();
        CoreUtils::addChecksumToNMEA(sentence);
        if (sentence.size() != bodySize + 5)
        {
            return false;
        }

        // GPSSentenceMsg carries the sentence without its line terminator;
        // DataPort adds CRLF when it writes the NMEA stream.
        sentence.resize(sentence.size() - 2);
        return true;
    }

    bool coordinate(float value, bool latitude, etl::istring &text, char &hemisphere)
    {
        const float maximum = latitude ? 90.0F : 180.0F;
        if (!std::isfinite(value) || value < -maximum || value > maximum)
        {
            return false;
        }

        hemisphere = latitude ? (value < 0.0F ? 'S' : 'N') : (value < 0.0F ? 'W' : 'E');
        const float absolute = std::fabs(value);
        int degrees = static_cast<int>(absolute);
        float minutes = std::round((absolute - static_cast<float>(degrees)) * 60.0F * 100000.0F) / 100000.0F;
        if (minutes >= 60.0F)
        {
            minutes = 0.0F;
            degrees += 1;
        }

        text.clear();
        etl::string_stream stream(text);
        stream << etl::format_spec{}.width(latitude ? 2 : 3).fill('0') << degrees
               << etl::format_spec{}.precision(5).width(8).fill('0') << minutes;
        return text.size() == (latitude ? 10U : 11U);
    }
}

etl::optional<StaticGPSNmea::Sentences> StaticGPSNmea::create(float latitude, float longitude, float altitudeMeters, uint64_t epochMs)
{
    if (!std::isfinite(altitudeMeters) || altitudeMeters < -1000.0F || altitudeMeters > 20000.0F)
    {
        return etl::nullopt;
    }

    etl::string<10> latitudeText;
    etl::string<11> longitudeText;
    char latitudeHemisphere = 'N';
    char longitudeHemisphere = 'E';
    if (!coordinate(latitude, true, latitudeText, latitudeHemisphere) ||
        !coordinate(longitude, false, longitudeText, longitudeHemisphere))
    {
        return etl::nullopt;
    }

    const time_t seconds = static_cast<time_t>(epochMs / 1000);
    struct tm utc = {};
    if (gmtime_r(&seconds, &utc) == nullptr)
    {
        return etl::nullopt;
    }

    etl::string<9> timeText;
    etl::string_stream timeStream(timeText);
    const uint32_t centiseconds = static_cast<uint32_t>((epochMs % 1000) / 10);
    timeStream << etl::format_spec{}.width(2).fill('0') << utc.tm_hour
               << etl::format_spec{}.width(2).fill('0') << utc.tm_min
               << etl::format_spec{}.width(2).fill('0') << utc.tm_sec
               << GATAS::RESET_FORMAT << "."
               << etl::format_spec{}.width(2).fill('0') << centiseconds;

    etl::string<6> dateText;
    etl::string_stream dateStream(dateText);
    dateStream << etl::format_spec{}.width(2).fill('0') << utc.tm_mday
               << etl::format_spec{}.width(2).fill('0') << utc.tm_mon + 1
               << etl::format_spec{}.width(2).fill('0') << (utc.tm_year + 1900) % 100;

    Sentences result;

    etl::string_stream gll(result.gll);
    gll << "$GPGLL," << latitudeText << "," << etl::string_view(&latitudeHemisphere, 1)
        << "," << longitudeText << "," << etl::string_view(&longitudeHemisphere, 1)
        << "," << timeText << ",A";
    if (!finishSentence(result.gll))
    {
        return etl::nullopt;
    }

    etl::string_stream rmc(result.rmc);
    rmc << "$GPRMC," << timeText << ",A," << latitudeText << "," << etl::string_view(&latitudeHemisphere, 1)
        << "," << longitudeText << "," << etl::string_view(&longitudeHemisphere, 1)
        << ",0.000,0.00," << dateText << ",,";
    if (!finishSentence(result.rmc))
    {
        return etl::nullopt;
    }

    result.vtg = "$GPVTG,0.00,T,,M,0.000,N,0.000,K";
    if (!finishSentence(result.vtg))
    {
        return etl::nullopt;
    }

    etl::string_stream gga(result.gga);
    gga << "$GPGGA," << timeText << "," << latitudeText << "," << etl::string_view(&latitudeHemisphere, 1)
        << "," << longitudeText << "," << etl::string_view(&longitudeHemisphere, 1)
        << ",1,08,1.0," << etl::format_spec{}.precision(1) << altitudeMeters
        << GATAS::RESET_FORMAT << ",M,0.0,M,,";
    if (!finishSentence(result.gga))
    {
        return etl::nullopt;
    }

    result.gsa = "$GPGSA,A,3,,,,,,,,,,,,,1.0,1.0,1.0";
    if (!finishSentence(result.gsa))
    {
        return etl::nullopt;
    }

    return result;
}
