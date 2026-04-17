"""Send generated traffic events over a TCP socket to a waiting C++ engine.

Usage:
    python -m traffic.socket_sender --scenario low --host 127.0.0.1 --port 9000

The C++ engine must be started first with --socket <port>; it listens and blocks
until this sender connects. Events are streamed at their original inter-packet
timing so the engine receives them in real time.
"""
from __future__ import annotations

import argparse
import socket
import time

from .config import ScenarioName, get_scenario
from .generator import generate_traffic


def send(scenario: ScenarioName, host: str, port: int) -> None:
    config = get_scenario(scenario)
    events = generate_traffic(config)
    # events are already sorted by packet_start_us

    with socket.create_connection((host, port)) as conn:
        print(f"Connected to {host}:{port} — sending {len(events)} packets")
        prev_us = events[0].packet_start_us if events else 0

        for evt in events:
            gap_us = evt.packet_start_us - prev_us
            if gap_us > 0:
                time.sleep(gap_us / 1_000_000)
            prev_us = evt.packet_start_us

            line = f"{evt.packet_size_bytes},{evt.traffic_class.value},{evt.priority_tag}\n"
            conn.sendall(line.encode())

    print("All packets sent.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Stream traffic events over TCP")
    parser.add_argument(
        "--scenario",
        choices=[s.value for s in ScenarioName],
        default=ScenarioName.LOW.value,
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    send(ScenarioName(args.scenario), args.host, args.port)


if __name__ == "__main__":
    main()
