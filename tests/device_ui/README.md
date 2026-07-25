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

Include save/restart persistence tests:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest --run-persistence
```

Run one area:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_aircraft_crud.py
```

Use `--headed` to watch the browser. Playwright traces and screenshots are
retained for failures under `test-results/`.

The suite creates unique temporary aircraft and removes them afterward. It does
not save reversible tests to flash. Persistence tests call `SaveBr` and restart
the device, and therefore require the explicit `--run-persistence` flag.

