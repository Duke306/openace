from __future__ import annotations

from playwright.sync_api import Page, expect


class ActionsPage:
    def __init__(self, page: Page, device_url: str) -> None:
        self.page = page
        self.device_url = device_url

    def open(self) -> None:
        self.page.goto(f"{self.device_url}/#actions", wait_until="domcontentloaded")
        expect(self.page.get_by_text("Connected", exact=True)).to_be_visible(timeout=10_000)
        expect(self.page.get_by_role("heading", name="Device actions")).to_be_visible()

    def restart(self) -> None:
        self.page.get_by_role("button", name="Restart device", exact=True).click()
        dialog = self.page.get_by_role("dialog", name="Restart GATAS?")
        expect(dialog).to_be_visible()
        dialog.get_by_role("button", name="Restart", exact=True).click()

