from __future__ import annotations

import json
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class DeviceApiError(RuntimeError):
    def __init__(self, status: int | None, body: str, path: str) -> None:
        super().__init__(f"device request failed: status={status} path={path} body={body!r}")
        self.status = status
        self.body = body
        self.path = path


class DeviceApi:
    def __init__(self, base_url: str, timeout: float = 5) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def request(
        self,
        method: str,
        path: str,
        payload: Any | None = None,
        headers: dict[str, str] | None = None,
    ) -> Any:
        request_headers = {"Accept": "application/json"}
        if headers:
            request_headers.update(headers)

        body = None
        if payload is not None:
            body = json.dumps(payload, separators=(",", ":")).encode()
            request_headers["Content-Type"] = "application/json"

        request = Request(
            f"{self.base_url}{path}",
            data=body,
            headers=request_headers,
            method=method,
        )
        try:
            with urlopen(request, timeout=self.timeout) as response:
                content = response.read().decode()
        except HTTPError as error:
            content = error.read().decode(errors="replace")
            raise DeviceApiError(error.code, content, path) from error
        except URLError as error:
            raise DeviceApiError(None, str(error.reason), path) from error

        return json.loads(content) if content else None

    def get(self, path: str) -> Any:
        return self.request("GET", path)

    def post(self, path: str, payload: Any) -> Any:
        return self.request("POST", path, payload)

    def delete_aircraft(self, callsign: str) -> Any:
        return self.request(
            "POST",
            f"/api/Config/aircraft/{callsign}.json",
            {},
            {"X-Method": "DELETE"},
        )

    def save(self) -> Any:
        return self.post("/api/Config/SaveBr.json", {})

    def restart(self) -> None:
        try:
            self.post("/api/Config/Restart.json", {})
        except (DeviceApiError, TimeoutError, ConnectionError):
            # The watchdog can reset the connection before lwIP sends the body.
            pass

    def wait_ready(self, timeout: float = 30) -> dict:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                value = self.get("/api/Config.json")
                if isinstance(value, dict):
                    return value
            except DeviceApiError as error:
                last_error = error
            time.sleep(0.25)
        raise TimeoutError(f"device did not become ready: {last_error}")

    def wait_for_aircraft(self, callsign: str, timeout: float = 5) -> dict | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = self.get(f"/api/Config/aircraft/{callsign}.json")
            if isinstance(value, dict):
                return value
            time.sleep(0.1)
        return None

    def wait_for_active_aircraft(self, callsign: str, timeout: float = 5) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            config = self.get("/api/Config/config.json")
            if config.get("aircraftId") == callsign:
                return True
            time.sleep(0.1)
        return False
