from __future__ import annotations

import pytest
from playwright.sync_api import Page, expect

from pages.aircraft_page import AircraftPage
from pages.api_client import DeviceApi


@pytest.mark.aircraft_mutation
def test_reload_displays_configured_aircraft(
    page: Page,
    device_url: str,
    api: DeviceApi,
    temporary_aircraft: dict,
    original_general_config: dict,
) -> None:
    configured_callsign = original_general_config["aircraftId"]
    temporary_callsign = temporary_aircraft["callSign"]
    assert configured_callsign != temporary_callsign
    assert api.get("/api/Config/config.json")["aircraftId"] == configured_callsign

    ui = AircraftPage(page, device_url)
    ui.open()

    selector = page.get_by_role("combobox", name="Aircraft:", exact=True)
    expect(selector.locator(f'option[value="{temporary_callsign}"]')).to_have_count(1)

    page.reload(wait_until="domcontentloaded")
    expect(page.get_by_text("Connected", exact=True)).to_be_visible(timeout=10_000)
    expect(selector).to_have_value(configured_callsign)
