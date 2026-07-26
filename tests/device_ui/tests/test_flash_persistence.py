from __future__ import annotations

import pytest

from pages.api_client import DeviceApi


@pytest.mark.persistence
@pytest.mark.aircraft_mutation
def test_save_writes_volatile_configuration_to_flash(
    api: DeviceApi,
    original_general_config: dict,
) -> None:
    callsign = original_general_config["aircraftId"]
    aircraft_path = f"/api/Config/aircraft/{callsign}.json"
    original_aircraft = api.get(aircraft_path)
    assert isinstance(original_aircraft, dict)

    modified_aircraft = {
        **original_aircraft,
        "category": "Small" if original_aircraft["category"] != "Small" else "Light",
    }

    try:
        api.post(aircraft_path, modified_aircraft)
        assert api.get(aircraft_path) == modified_aircraft

        api.save()
        assert api.persistent_matches_volatile(), (
            "SaveBr data in flash does not match volatile configuration"
        )
        assert api.get("/api/Config.json")["pStoreSize"] > 0
    finally:
        api.post(aircraft_path, original_aircraft)
        api.save()
        assert api.persistent_matches_volatile(), (
            "restored configuration in flash does not match volatile configuration"
        )
