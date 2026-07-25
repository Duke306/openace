from __future__ import annotations

import pytest

from pages.api_client import DeviceApi


@pytest.mark.persistence
@pytest.mark.aircraft_mutation
def test_aircraft_survives_save_and_restart(
    api: DeviceApi,
    temporary_aircraft: dict,
    original_general_config: dict,
) -> None:
    if original_general_config.get("_dirty", True):
        pytest.skip("persistence restoration requires a clean starting configuration")

    callsign = temporary_aircraft["callSign"]
    api.save()
    status_before = api.get("/api/Config.json")
    assert status_before["pStoreSize"] > 0

    api.restart()
    api.wait_ready(timeout=30)

    assert api.get(f"/api/Config/aircraft/{callsign}.json") == temporary_aircraft
