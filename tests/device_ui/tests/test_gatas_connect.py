from __future__ import annotations

import time

import pytest

from pages.api_client import DeviceApi


def test_gatas_connect_provides_adsb_traffic_to_aircraft_tracker(api: DeviceApi) -> None:
    configured_modules = api.get("/api/Config/modules.json")
    assert isinstance(configured_modules, str)

    enabled_modules = {name.strip() for name in configured_modules.split(",")}
    if "GatasConnect" not in enabled_modules:
        pytest.skip("GatasConnect is not enabled on this device")

    deadline = time.monotonic() + 15
    last_tracker_data: dict | None = None
    while time.monotonic() < deadline:
        tracker_data = api.get("/api/AircraftTracker.json")
        assert isinstance(tracker_data, dict)
        last_tracker_data = tracker_data

        aircraft = tracker_data.get("aircraft", {})
        data_sources = aircraft.get("ds", [])
        if "ADSB" in data_sources:
            return

        time.sleep(0.5)

    pytest.fail(
        "GatasConnect is enabled, but AircraftTracker reported no ADSB aircraft "
        f"within 15 seconds; last response: {last_tracker_data!r}"
    )
