from __future__ import annotations

import pytest
from playwright.sync_api import Page

from pages.aircraft_page import AircraftPage
from pages.api_client import DeviceApi


@pytest.mark.aircraft_mutation
def test_add_and_remove_aircraft(
    page: Page,
    device_url: str,
    api: DeviceApi,
    temporary_callsign: str,
    original_general_config: dict,
) -> None:
    aircraft = {
        "callSign": temporary_callsign,
        "category": "Small",
        "addressType": "OGN",
        "address": 0x123456,
        "noTrack": False,
        "groundStation": False,
        "heightAboveGps": 0,
        "protocols": [{"OGN": {"mode": "RX"}}],
    }
    ui = AircraftPage(page, device_url)

    try:
        ui.open()
        ui.add(aircraft)
        assert api.wait_for_aircraft(temporary_callsign, timeout=5) == aircraft

        ui.select(temporary_callsign)
        assert api.wait_for_active_aircraft(temporary_callsign)
        ui.delete_selected(temporary_callsign)
        assert api.get(f"/api/Config/aircraft/{temporary_callsign}.json") is None
    finally:
        if api.get(f"/api/Config/aircraft/{temporary_callsign}.json") is not None:
            api.delete_aircraft(temporary_callsign)
        api.post("/api/Config/config.json", original_general_config)
        if not original_general_config.get("_dirty", True):
            api.save()
