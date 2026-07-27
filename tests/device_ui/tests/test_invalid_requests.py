from __future__ import annotations

import pytest

from pages.api_client import DeviceApi, DeviceApiError


def test_invalid_json_returns_400_without_mutation(api: DeviceApi) -> None:
    before = api.get("/api/Config/aircraft.json")

    with pytest.raises(DeviceApiError) as error:
        api.request(
            "POST",
            "/api/Config/aircraft/BROKEN.json",
            headers={"Content-Type": "application/json"},
        )

    # request() with no payload sends an empty body, which must be rejected.
    assert error.value.status == 400
    assert api.get("/api/Config/aircraft.json") == before

