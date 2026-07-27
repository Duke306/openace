from __future__ import annotations

import time

import pytest

from pages.api_client import DeviceApi


RADIO_DATA_SOURCES = frozenset({"Flarm", "ADSL", "ADSL Hdr", "Fanet", "OGN"})
MAX_RADIO_DISTANCE_M = 100_000
OBSERVATION_SECONDS = 15
POLL_INTERVAL_SECONDS = 0.5


@pytest.mark.radio_traffic
def test_aircraft_tracker_receives_only_nearby_radio_traffic(api: DeviceApi) -> None:
    deadline = time.monotonic() + OBSERVATION_SECONDS
    observed_radio_aircraft: set[tuple[str, str]] = set()
    last_tracker_data: dict | None = None

    while time.monotonic() < deadline:
        tracker_data = api.get("/api/AircraftTracker.json")
        assert isinstance(tracker_data, dict)
        last_tracker_data = tracker_data

        aircraft = tracker_data.get("aircraft:aoa")
        assert isinstance(aircraft, dict), f"invalid AircraftTracker aircraft data: {tracker_data!r}"

        addresses = aircraft.get("hex")
        data_sources = aircraft.get("ds")
        distances = aircraft.get("dis")
        assert isinstance(addresses, list)
        assert isinstance(data_sources, list)
        assert isinstance(distances, list)
        assert len(addresses) == len(data_sources) == len(distances), (
            "AircraftTracker aircraft arrays are not aligned: "
            f"hex={len(addresses)}, ds={len(data_sources)}, dis={len(distances)}"
        )

        for address, data_source, distance in zip(addresses, data_sources, distances, strict=True):
            if data_source not in RADIO_DATA_SOURCES:
                continue

            assert isinstance(address, str)
            assert isinstance(distance, int) and not isinstance(distance, bool), (
                f"radio aircraft {address} ({data_source}) has invalid distance {distance!r}"
            )
            assert 0 <= distance <= MAX_RADIO_DISTANCE_M, (
                f"radio aircraft {address} ({data_source}) is at invalid distance "
                f"{distance} m; expected 0..{MAX_RADIO_DISTANCE_M} m"
            )
            observed_radio_aircraft.add((address, data_source))

        time.sleep(POLL_INTERVAL_SECONDS)

    assert observed_radio_aircraft, (
        "AircraftTracker reported no direct radio traffic within "
        f"{OBSERVATION_SECONDS} seconds. Start a second transmitting GATAS and retry; "
        f"last response: {last_tracker_data!r}"
    )
