from __future__ import annotations

from playwright.sync_api import Page, expect


class ModulesPage:
    def __init__(self, page: Page, device_url: str) -> None:
        self.page = page
        self.device_url = device_url

    def open(self) -> None:
        self.page.goto(f"{self.device_url}/#modules", wait_until="domcontentloaded")
        expect(self.page.get_by_text("Connected", exact=True)).to_be_visible(timeout=10_000)

    def assert_loaded(self) -> None:
        expect(self.page.get_by_role("heading", name="Modules", exact=True)).to_be_visible(timeout=10_000)
        expect(self.page.get_by_role("table")).to_be_visible()
