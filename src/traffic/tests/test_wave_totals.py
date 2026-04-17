"""Tests wave/sender coverage and per-sender byte totals."""

from collections import defaultdict
from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[2]))

from traffic.config import low as normal_traffic
from traffic.generator import generate_traffic


def test_each_wave_contains_all_expected_senders() -> None:
    """Expectation: every wave has sender IDs 0..senders_per_wave-1 present."""
    config = normal_traffic()
    events = generate_traffic(config)

    senders_by_wave: dict[int, set[int]] = defaultdict(set)
    for event in events:
        senders_by_wave[event.wave_id].add(event.sender_id)

    expected_waves = set(range(config.number_of_waves))
    expected_senders = set(range(config.senders_per_wave))

    assert set(senders_by_wave.keys()) == expected_waves
    for wave_id in expected_waves:
        assert senders_by_wave[wave_id] == expected_senders


def test_per_sender_bytes_match_configured_burst_size() -> None:
    """Expectation: each sender contributes exactly bytes_per_sender_per_wave per wave."""
    config = normal_traffic()
    events = generate_traffic(config)

    bytes_by_wave_sender: dict[tuple[int, int], int] = defaultdict(int)
    for event in events:
        bytes_by_wave_sender[(event.wave_id, event.sender_id)] += event.packet_size_bytes

    for wave_id in range(config.number_of_waves):
        for sender_id in range(config.senders_per_wave):
            assert bytes_by_wave_sender[(wave_id, sender_id)] == config.bytes_per_sender_per_wave
