from __future__ import annotations

import pytest
from playwright.sync_api import Page

from pages.aircraft_page import AircraftPage
from pages.api_client import DeviceApi


@pytest.mark.aircraft_mutation
@pytest.mark.parametrize("mode", ["OFF", "RX", "TX", "RX_TX"])
def test_each_ogn_protocol_mode(
    page: Page,
    device_url: str,
    api: DeviceApi,
    temporary_aircraft: dict,
    mode: str,
) -> None:
    expected = {**temporary_aircraft}
    expected["protocols"] = [] if mode == "OFF" else [{"OGN": {"mode": mode}}]

    ui = AircraftPage(page, device_url)
    ui.open()
    ui.select(temporary_aircraft["callSign"])
    assert api.wait_for_active_aircraft(temporary_aircraft["callSign"])
    ui.modify_selected(expected)

    assert api.get(f"/api/Config/aircraft/{temporary_aircraft['callSign']}.json") == expected
