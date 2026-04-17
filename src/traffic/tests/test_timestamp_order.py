"""Tests that generated events are ordered by non-decreasing start timestamps."""

from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[2]))

from traffic.config import high as high_congestion
from traffic.generator import generate_traffic
from traffic.validate import validate_timestamp_order


def test_event_timestamps_are_non_decreasing() -> None:
    """Expectation: packet_start_us never decreases across generated event order."""
    events = generate_traffic(high_congestion())

    validate_timestamp_order(events)

    for previous, current in zip(events, events[1:]):
        assert previous.packet_start_us <= current.packet_start_us
