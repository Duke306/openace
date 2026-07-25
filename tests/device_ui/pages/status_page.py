from __future__ import annotations

from playwright.sync_api import Page, expect


class StatusPage:
    def __init__(self, page: Page, device_url: str) -> None:
        self.page = page
        self.device_url = device_url

    def open(self) -> None:
        self.page.goto(f"{self.device_url}/#status", wait_until="domcontentloaded")
        expect(self.page.get_by_text("Connected", exact=True)).to_be_visible(timeout=10_000)
        expect(self.page.get_by_role("heading", name="GPS status", exact=True)).to_be_visible()
        expect(self.page.get_by_role("table")).to_be_visible()

