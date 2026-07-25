---
name: openace-device-testing
description: Test a physical OpenAce device through its HTTP API, including reading and modifying aircraft or module configuration, saving volatile configuration to flash, erasing saved flash configuration, reboot persistence checks, regression testing, and reproducing user-reported device issues. Use for hardware-in-the-loop, integration, persistence, factory-default, configuration, or device regression testing of OpenAce firmware.
---

# OpenAce Device Testing

Test configuration changes safely and leave an evidence trail that another run can reproduce.

## Establish the test target

1. Work from the OpenAce repository and obey its `AGENTS.md`.
2. Prefix every shell command with `rtk`.
3. Obtain the device base URL from the user or existing test context. Never guess a device IP.
4. Use the literal, user-confirmed device URL in commands. If automation needs a variable, use a task-specific name such as `OPENACE_DEVICE`; never use `HOME` or another common system variable.

   ```bash
   rtk curl -fsS http://192.168.4.1/api/Config.json
   ```
5. Record firmware version, board, power arrangement, network path, test time, and the exact starting configuration relevant to the test.
6. Confirm the target is a disposable test device before any flash erase. Treat `EraseBr`, restart, USB boot, radio transmission changes, Wi-Fi changes, and hardware pin changes as important actions.

## Understand configuration state

OpenAce has three configuration sources:

- `Ram`: the current volatile configuration.
- `Flash`: the persisted configuration loaded on boot.
- `Default`: the firmware-embedded defaults.

Read the current source and persistent-store size:

```bash
rtk curl -fsS http://DEVICE/api/Config.json
```

Configuration writes first update RAM and set `config._dirty` to `true`. They are not proven persistent until `SaveBr` succeeds and a reboot loads the same values.

Successful writes return `/ok.json`. Invalid, incomplete, oversized, or rejected
writes return HTTP 400 with `/error.json`. Always perform a GET verification as
well, because transport success does not prove persistence or runtime behavior.

## Snapshot before mutation

Read and save every path that the test will modify. Do not claim a complete backup unless every required top-level path was captured; `/api/Config.json` contains metadata, not the entire configuration document.

Common paths:

```bash
rtk curl -fsS http://DEVICE/api/Config/config.json
rtk curl -fsS http://DEVICE/api/Config/hardware.json
rtk curl -fsS http://DEVICE/api/Config/modules.json
rtk curl -fsS http://DEVICE/api/Config/aircraft.json
rtk curl -fsS http://DEVICE/api/Config/MODULE_NAME.json
```

Store snapshots and test evidence outside source directories containing firmware code. Redact Wi-Fi passwords, service PINs, and other secrets before placing evidence in reports or commits.

Prefer a reversible probe value and change one variable at a time. Avoid connectivity, pin-map, transmitter, and active-aircraft changes unless the test specifically requires them.

## Modify configuration

POST valid JSON to a configuration path. A top-level module or configuration object is replaced/updated through the same path scheme.

Modify a module:

```bash
rtk curl -fsS -X POST -H 'Content-Type: application/json' \
  --data '{"filterAbove":1600,"filterBelow":1500}' \
  http://DEVICE/api/Config/ADSBDecoder.json
rtk curl -fsS http://DEVICE/api/Config/ADSBDecoder.json
```

Modify general configuration:

```bash
rtk curl -fsS -X POST -H 'Content-Type: application/json' \
  --data '{"pilotname":"TEST","aircraftId":"XX-XXX"}' \
  http://DEVICE/api/Config/config.json
rtk curl -fsS http://DEVICE/api/Config/config.json
```

Add or update aircraft data using the callsign as the path:

```bash
rtk curl -fsS -X POST -H 'Content-Type: application/json' \
  --data '{"callSign":"TEST-1","address":11259375,"addressType":"ADSL","category":"Light","privacy":0,"noTrack":0,"groundStation":0,"protocols":[]}' \
  http://DEVICE/api/Config/aircraft/TEST-1.json
rtk curl -fsS http://DEVICE/api/Config/aircraft/TEST-1.json
```

Delete aircraft data through the POST tunnel required by the embedded web server:

```bash
rtk curl -fsS -X POST -H 'X-Method: DELETE' --data '{}' \
  http://DEVICE/api/Config/aircraft/TEST-1.json
rtk curl -fsS http://DEVICE/api/Config/aircraft.json
```

For any module, first GET `/api/Config/MODULE_NAME.json`, modify only fields relevant to the case, POST valid JSON, then GET the same path. Use names and schemas from the running device or `src/pico/gatas_default_config.json`; do not invent fields.

## Save RAM configuration to flash

1. Verify the intended RAM value and `config._dirty`.
2. Save with a non-empty JSON body:

   ```bash
   rtk curl -fsS -X POST -H 'Content-Type: application/json' --data '{}' \
     http://DEVICE/api/Config/SaveBr.json
   ```

3. GET `/api/Config/config.json` and `/api/Config.json`. Expect `_dirty` to be false and a nonzero persistent-store size.
4. Reboot only when temporary network loss is acceptable:

   ```bash
   rtk curl -fsS -X POST -H 'Content-Type: application/json' --data '{}' \
     http://DEVICE/api/Config/Restart.json
   ```

5. Wait for the device to return without using a blocking wait longer than 60 seconds. Reconnect and GET the tested path.
6. Pass persistence only when the exact value survives reboot and `/api/Config.json` reports `Flash` (or the expected source for the tested boot sequence).

## Erase saved flash configuration

Perform this only when the user has authorized destructive testing on the identified test device. Erase removes volatile and persistent configuration stores. The in-memory JSON may remain observable until reboot, so an immediate GET is not proof of either success or failure.

```bash
rtk curl -fsS -X POST -H 'Content-Type: application/json' --data '{}' \
  http://DEVICE/api/Config/EraseBr.json
rtk curl -fsS -X POST -H 'Content-Type: application/json' --data '{}' \
  http://DEVICE/api/Config/Restart.json
```

After reconnection:

1. GET `/api/Config.json`; expect `configuration` to be `Default` and `pStoreSize` to be zero.
2. GET the tested configuration paths and compare them with `src/pico/gatas_default_config.json` for the firmware under test.
3. Confirm identity separately: `EraseBr` erases the configuration stores used here but does not imply that the binary identity store or firmware image was erased.
4. Restore required test configuration explicitly, verify it in RAM, save to flash, reboot, and verify again.

## Run a regression case

Use this sequence:

1. Translate the report into one observable claim.
2. Record prerequisites and the smallest exact reproduction steps.
3. Capture the baseline GET responses.
4. Change one input and verify the RAM result.
5. If persistence is implicated, save, reboot, and verify.
6. If defaults or corruption recovery are implicated, use an authorized erase/reboot and verify.
7. Repeat enough times to distinguish deterministic failures from timing or connectivity issues.
8. Restore the starting state, unless the agreed test ends at factory defaults.
9. Run the relevant desktop regression tests when code changes are involved:

   ```bash
   rtk run sh src/lib/config/config_tests/urun.sh
   ```

Keep desktop tests and physical-device observations separate in the result.

## Report results

Report:

- device, board, firmware/build identifier, and timestamp;
- starting configuration source and relevant snapshot;
- exact requests or UI actions, with secrets redacted;
- expected and actual results;
- response bodies and post-action GET verification;
- whether the behavior occurred before save, after save, after reboot, or after erase;
- reproduction count;
- restoration status;
- verdict: `PASS`, `FAIL`, `INCONCLUSIVE`, or `BLOCKED`;
- suspected subsystem and the next narrow test.

Never mark a test passed solely because a POST returned HTTP success. Never erase, restart, enter USB boot, or alter a safety-relevant transmitter setting on an unidentified or operational device.
