# OpenAce device UI tests

These tests drive the web interface of a physical OpenAce device and verify every
write through its HTTP API.

## Install

```bash
cd tests/device_ui
python3 -m venv .venv
.venv/bin/pip install -e .
.venv/bin/playwright install chromium
```

## Run

Reversible UI and API tests:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest
```

Run one area:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_gatas_connect.py
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_flash_persistence.py
```

Validate direct radio traffic with a second transmitting GATAS:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest \
  --run-radio-traffic tests/test_aircraft_tracker_radio.py
```

Use `--headed` to watch the browser. Playwright traces and screenshots are
retained for failures under `test-results/`.

The suite creates unique temporary aircraft and removes them afterward.
The flash persistence test temporarily changes the active aircraft category,
calls `SaveBr`, and uses the `GATAS_DEBUG`-only `CompareBr` endpoint to verify
the flash contents immediately. It restores and saves the original aircraft
record before completing; no device restart is required.
