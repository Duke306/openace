#pragma once

#include <stdint.h>

#include "ace/constants.hpp"

#include "etl/optional.h"

namespace StaticGPSNmea
{
    struct Sentences
    {
        GATAS::NMEAString gll;
        GATAS::NMEAString rmc;
        GATAS::NMEAString vtg;
        GATAS::NMEAString gga;
        GATAS::NMEAString gsa;
    };

    /**
     * Build one 2 Hz sample of valid NMEA sentences for a stationary position.
     * Altitude is expressed in metres above mean sea level; geoid separation is
     * reported as zero because a static configuration has no geoid model input.
     */
    etl::optional<Sentences> create(float latitude, float longitude, float altitudeMeters, uint64_t epochMs);
}
