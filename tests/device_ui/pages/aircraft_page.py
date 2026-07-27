from __future__ import annotations

from typing import Any

from playwright.sync_api import Page, expect


class AircraftPage:
    def __init__(self, page: Page, device_url: str) -> None:
        self.page = page
        self.device_url = device_url

    def open(self) -> None:
        self.page.goto(f"{self.device_url}/#session", wait_until="domcontentloaded")
        expect(self.page.get_by_text("Connected", exact=True)).to_be_visible(timeout=10_000)
        expect(self.page.get_by_role("button", name="Add", exact=True)).to_be_visible()

    def add(self, aircraft: dict[str, Any]) -> None:
        self.page.get_by_role("button", name="Add", exact=True).click()
        self._fill_form(aircraft)
        self.page.get_by_role("button", name="Save", exact=True).click()
        expect(self.page.get_by_role("button", name="Modify", exact=True)).to_be_visible(timeout=10_000)

    def select(self, callsign: str) -> None:
        selector = self.page.get_by_role("combobox", name="Aircraft:", exact=True)
        selector.select_option(callsign)
        expect(selector).to_have_value(callsign)

    def modify_selected(self, aircraft: dict[str, Any]) -> None:
        self.page.get_by_role("button", name="Modify", exact=True).click()
        # The editor renders before its asynchronous aircraft fetch completes.
        # Waiting only for form controls races _setFormData(), which can overwrite
        # values Playwright has already entered with the previous device record.
        callsign = self.page.get_by_role("textbox", name="Call Sign:", exact=True)
        expect(callsign).to_be_disabled()
        expect(callsign).to_have_value(aircraft["callSign"], timeout=10_000)
        self._fill_form(aircraft, include_callsign=False)
        self.page.get_by_role("button", name="Save", exact=True).click()
        expect(self.page.get_by_role("button", name="Modify", exact=True)).to_be_visible(timeout=10_000)

    def delete_selected(self, callsign: str) -> None:
        self.page.get_by_role("button", name="Remove", exact=True).click()
        dialog = self.page.get_by_role("dialog", name=f"Delete '{callsign}'?")
        expect(dialog).to_be_visible()
        dialog.get_by_role("button", name="Delete", exact=True).click()
        expect(dialog).not_to_be_visible(timeout=10_000)

    def _fill_form(self, aircraft: dict[str, Any], include_callsign: bool = True) -> None:
        if include_callsign:
            self.page.get_by_role("textbox", name="Call Sign:", exact=True).fill(aircraft["callSign"])

        self.page.get_by_role("combobox", name="Aircraft Type:", exact=True).select_option(
            label=aircraft["category"]
        )
        self.page.get_by_role(
            "combobox", name="Your official Transponder:", exact=True
        ).select_option(label=aircraft["addressType"])
        self.page.get_by_role("textbox", name="Transponder Code:", exact=True).fill(
            f"{aircraft['address'] & 0xFFFFFF:06X}"
        )

        modes = self._protocol_modes(aircraft.get("protocols", []))
        for protocol, mode in modes.items():
            self.page.locator(f"#protocol_{protocol}_{mode}").check()

        self.page.get_by_role("checkbox", name="No Track", exact=True).set_checked(
            bool(aircraft.get("noTrack", False))
        )
        self.page.get_by_role("checkbox", name="Ground Station", exact=True).set_checked(
            bool(aircraft.get("groundStation", False))
        )

    @staticmethod
    def _protocol_modes(protocols: list[Any]) -> dict[str, str]:
        result = {"OGN": "OFF", "FLARM": "OFF", "ADSL": "OFF", "FANET": "OFF"}
        for protocol in protocols:
            if isinstance(protocol, str):
                result[protocol] = "RX_TX"
            elif isinstance(protocol, dict):
                for name, config in protocol.items():
                    result[name] = config.get("mode", "RX_TX")
        return result
