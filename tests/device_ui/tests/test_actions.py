from __future__ import annotations

from playwright.sync_api import Page, expect

from pages.actions_page import ActionsPage


def test_actions_page_loads(page: Page, device_url: str) -> None:
    actions = ActionsPage(page, device_url)
    actions.open()


def test_firmware_upload_requires_confirmation(page: Page, device_url: str) -> None:
    actions = ActionsPage(page, device_url)
    actions.open()
    page.get_by_role("button", name="Upload firmware", exact=True).click()
    dialog = page.get_by_role("dialog", name="Start firmware mode?")
    expect(dialog).to_be_visible()
    dialog.get_by_role("button", name="Cancel", exact=True).click()
    expect(dialog).not_to_be_visible()
