from __future__ import annotations

import time

import pytest
from playwright.sync_api import Page

from pages.aircraft_page import AircraftPage
from pages.api_client import DeviceApi


AIRCRAFT_TYPES = [
    "Light",
    "Small",
    "Large",
    "Aerobatic",
    "Helicopter",
    "Glider",
    "Balloon",
    "Sky Diver",
    "Ultra Light",
    "Unmanned aerial vehicle",
    "Surface Emergency Vehicle",
    "Surface Vehicle",
    "Point Obstacle",
    "Gyrocopter",
    "Hang Glider",
    "Para Glider",
    "Drop Plane",
]

DEVICE_SETTLE_SECONDS = 2


@pytest.mark.aircraft_mutation
def test_aircraft_type_preserves_complete_record(
    page: Page,
    device_url: str,
    api: DeviceApi,
    temporary_aircraft: dict,
) -> None:
    ui = AircraftPage(page, device_url)
    ui.open()
    ui.select(temporary_aircraft["callSign"])
    assert api.wait_for_active_aircraft(temporary_aircraft["callSign"])
    time.sleep(DEVICE_SETTLE_SECONDS)

    for category in AIRCRAFT_TYPES:
        expected = {
            **temporary_aircraft,
            "category": category,
            "protocols": [
                {"OGN": {"mode": "RX"}},
                {"FLARM": {"mode": "RX_TX"}},
                {"ADSL": {"mode": "RX"}},
                {"FANET": {"mode": "RX"}},
            ],
        }
        ui.modify_selected(expected)
        time.sleep(DEVICE_SETTLE_SECONDS)

        assert api.get(f"/api/Config/aircraft/{temporary_aircraft['callSign']}.json") == expected

    # Give the device time to finish background configuration work before the
    # fixture deletes the aircraft and restores the original active aircraft.
    time.sleep(DEVICE_SETTLE_SECONDS)
