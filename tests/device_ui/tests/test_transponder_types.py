from __future__ import annotations

import pytest
from playwright.sync_api import Page

from pages.aircraft_page import AircraftPage
from pages.api_client import DeviceApi


@pytest.mark.aircraft_mutation
@pytest.mark.parametrize("address_type", ["ICAO", "FLARM", "OGN", "ADSL"])
def test_official_transponder_preserves_complete_record(
    page: Page,
    device_url: str,
    api: DeviceApi,
    temporary_aircraft: dict,
    address_type: str,
) -> None:
    expected = {
        **temporary_aircraft,
        "addressType": address_type,
        "protocols": [
            {"OGN": {"mode": "RX"}},
            {"FLARM": {"mode": "RX_TX"}},
            {"ADSL": {"mode": "RX"}},
            {"FANET": {"mode": "RX"}},
        ],
    }
    ui = AircraftPage(page, device_url)
    ui.open()
    ui.select(temporary_aircraft["callSign"])
    assert api.wait_for_active_aircraft(temporary_aircraft["callSign"])
    ui.modify_selected(expected)

    assert api.get(f"/api/Config/aircraft/{temporary_aircraft['callSign']}.json") == expected

