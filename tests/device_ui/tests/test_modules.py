from __future__ import annotations

from playwright.sync_api import Page

from pages.modules_page import ModulesPage


def test_modules_page_loads(page: Page, device_url: str) -> None:
    modules = ModulesPage(page, device_url)
    modules.open()
    modules.assert_loaded()

