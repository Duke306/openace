from __future__ import annotations

import argparse
import json
import select
import socket
import time


DEFAULT_ADVERTISEMENT_ADDRESS = "255.255.255.255"
DEFAULT_DISCOVERY_PORT = 63093
DEFAULT_GDL90_PORT = 4000
DEFAULT_INTERVAL = 5.0


class ForeFlightSimulator:
    def __init__(
        self,
        gdl90_port: int = DEFAULT_GDL90_PORT,
        advertisement_address: str = DEFAULT_ADVERTISEMENT_ADDRESS,
        discovery_port: int = DEFAULT_DISCOVERY_PORT,
        interval: float = DEFAULT_INTERVAL,
        bind_address: str = "",
    ) -> None:
        self.gdl90_port = gdl90_port
        self.advertisement_address = advertisement_address
        self.discovery_port = discovery_port
        self.interval = interval
        self.packet_count = 0
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.socket.bind((bind_address, gdl90_port))

    @property
    def advertisement(self) -> bytes:
        return json.dumps({"App": "ForeFlight", "GDL90": {"port": self.gdl90_port}}).encode()

    def advertise(self) -> None:
        self.socket.sendto(
            self.advertisement,
            (self.advertisement_address, self.discovery_port),
        )
        print(
            f"Advertised GDL90 port {self.gdl90_port} to "
            f"{self.advertisement_address}:{self.discovery_port}",
            flush=True,
        )

    def run(self) -> None:
        next_advertisement = 0.0
        try:
            while True:
                now = time.monotonic()
                if now >= next_advertisement:
                    self.advertise()
                    next_advertisement = now + self.interval

                readable, _, _ = select.select(
                    [self.socket],
                    [],
                    [],
                    max(0.0, next_advertisement - time.monotonic()),
                )
                if readable:
                    packet, sender = self.socket.recvfrom(65535)
                    self.packet_count += 1
                    print(
                        f"Received UDP packet {self.packet_count} from "
                        f"{sender[0]}:{sender[1]} ({len(packet)} bytes)",
                        flush=True,
                    )
        finally:
            self.socket.close()
            print(f"Received {self.packet_count} UDP packets in total", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Advertise a ForeFlight client and count received GDL90 UDP packets."
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_GDL90_PORT,
        help=f"local GDL90 UDP port to advertise and listen on (default: {DEFAULT_GDL90_PORT})",
    )
    parser.add_argument(
        "--address",
        default=DEFAULT_ADVERTISEMENT_ADDRESS,
        help="broadcast or device address to send the ForeFlight advertisement to",
    )
    parser.add_argument(
        "--discovery-port",
        type=int,
        default=DEFAULT_DISCOVERY_PORT,
        help=f"ForeFlight discovery port (default: {DEFAULT_DISCOVERY_PORT})",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=DEFAULT_INTERVAL,
        help=f"seconds between advertisements (default: {DEFAULT_INTERVAL:g})",
    )
    parser.add_argument(
        "--bind-address",
        default="",
        help="local address on which to listen (default: all interfaces)",
    )
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.discovery_port <= 65535:
        parser.error("--discovery-port must be between 1 and 65535")
    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    return args


def main() -> None:
    args = parse_args()
    simulator = ForeFlightSimulator(
        gdl90_port=args.port,
        advertisement_address=args.address,
        discovery_port=args.discovery_port,
        interval=args.interval,
        bind_address=args.bind_address,
    )
    try:
        simulator.run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
