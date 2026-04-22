from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


@dataclass(frozen=True)
class TrafficConfig:
    senders_per_wave: int
    number_of_waves: int
    first_wave_start_us: int
    wave_interval_us: int
    max_start_offset_us: int
    seed: int
    bytes_per_sender_per_wave: int
    packet_size_bytes: int
    control_packet_every_n: int
    control_priority_tag: int = 46
    bulk_priority_tag: int = 0


class ScenarioName(Enum):
    TEST   = "test"
    LOW    = "low"
    MEDIUM = "medium"
    HIGH   = "high"


def test() -> TrafficConfig:
    """500 senders × 3 waves — fits in seconds on CPU PQ, 1 MB buffer forces drops."""
    return TrafficConfig(
        senders_per_wave=500,
        number_of_waves=3,
        first_wave_start_us=0,
        wave_interval_us=9_000,
        max_start_offset_us=20,
        seed=42,
        bytes_per_sender_per_wave=4_500,
        packet_size_bytes=1_500,
        control_packet_every_n=25,
    )


def low() -> TrafficConfig:
    """128 senders — small network segment. GPU loses badly to CPU sort."""
    return TrafficConfig(
        senders_per_wave=128,
        number_of_waves=10,
        first_wave_start_us=0,
        wave_interval_us=2_000,
        max_start_offset_us=20,
        seed=1,
        bytes_per_sender_per_wave=1_500,
        packet_size_bytes=1_500,
        control_packet_every_n=25,
    )


def medium() -> TrafficConfig:
    """5 000 senders × 3 waves — rack aggregation. GPU approaches CPU sort performance."""
    return TrafficConfig(
        senders_per_wave=5_000,
        number_of_waves=3,
        first_wave_start_us=0,
        wave_interval_us=5_000,
        max_start_offset_us=20,
        seed=2,
        bytes_per_sender_per_wave=3_000,
        packet_size_bytes=1_500,
        control_packet_every_n=25,
    )


def high() -> TrafficConfig:
    """10 000 senders — core / datacenter switch. GPU wins decisively."""
    return TrafficConfig(
        senders_per_wave=10_000,
        number_of_waves=3,
        first_wave_start_us=0,
        wave_interval_us=9_000,
        max_start_offset_us=20,
        seed=3,
        bytes_per_sender_per_wave=4_500,
        packet_size_bytes=1_500,
        control_packet_every_n=25,
    )


SCENARIOS = {
    ScenarioName.TEST:   test,
    ScenarioName.LOW:    low,
    ScenarioName.MEDIUM: medium,
    ScenarioName.HIGH:   high,
}

# Simulation buffer size per scenario (bytes).
# Sized so each wave overflows the buffer ~2-3× to produce meaningful drop rates.
# Burst volume per wave: test=2.2MB, low=192KB, medium=14.6MB, high=43.9MB.
SCENARIO_BUFFER_BYTES = {
    ScenarioName.TEST:   1_048_576,     #  1 MB — 2.2× burst → ~49% drops (tight)
    ScenarioName.LOW:   52_428_800,     # 50 MB — burst is 192 KB → 0% drops (baseline)
    ScenarioName.MEDIUM: 5_242_880,     #  5 MB — 2.9× burst → ~50% drops
    ScenarioName.HIGH:  16_777_216,     # 16 MB — 2.7× burst → ~48% drops
}


def get_scenario(name: ScenarioName) -> TrafficConfig:
    return SCENARIOS[name]()


def get_buffer_bytes(name: ScenarioName) -> int:
    return SCENARIO_BUFFER_BYTES[name]
