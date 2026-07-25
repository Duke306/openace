from __future__ import annotations

from playwright.sync_api import Page

from pages.status_page import StatusPage


def test_status_page_loads(page: Page, device_url: str) -> None:
    StatusPage(page, device_url).open()

