#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "models.hpp"

TEST_CASE("DataSource short strings stay compact and stable", "[models]")
{
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::FLARM)) == "fl");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::ADSLM)) == "am");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::ADSLO_HDR)) == "ah");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::ADSLFLARM)) == "af");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::ADSLOGN)) == "ao");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::FANET)) == "fa");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::ADSB)) == "ab");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::OGN)) == "og");
    REQUIRE(std::string_view(GATAS::toShortString(GATAS::DataSource::NONE)) == "un");
}

TEST_CASE("GATAS Connect output modes select their transports", "[models]")
{
    const GATAS::GatasConnectOutput udp = GATAS::GatasConnectOutput::UDP;
    const GATAS::GatasConnectOutput bluetooth = GATAS::GatasConnectOutput::Bluetooth;
    const GATAS::GatasConnectOutput combined = GATAS::GatasConnectOutput::UDPAndBluetooth;

    REQUIRE(udp.usesUDP());
    REQUIRE_FALSE(udp.usesBluetooth());
    REQUIRE_FALSE(bluetooth.usesUDP());
    REQUIRE(bluetooth.usesBluetooth());
    REQUIRE(combined.usesUDP());
    REQUIRE(combined.usesBluetooth());

    REQUIRE(udp.withBluetooth() == combined);
    REQUIRE(bluetooth.withBluetooth() == bluetooth);
    REQUIRE(combined.withBluetooth() == combined);
}
