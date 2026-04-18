from __future__ import annotations

from .config import ScenarioName, TrafficConfig, get_scenario
from .models.classifier import classify_packets
from .models.incast_wave import generate_wave_starts
from .models.packetizer import packetize_wave_starts
from .schema import TrafficEvents


def generate_traffic(config: TrafficConfig) -> TrafficEvents:
    wave_starts = generate_wave_starts(
        senders_per_wave=config.senders_per_wave,
        number_of_waves=config.number_of_waves,
        first_wave_start_us=config.first_wave_start_us,
        wave_interval_us=config.wave_interval_us,
        max_start_offset_us=config.max_start_offset_us,
        seed=config.seed,
    )
    packet_events = packetize_wave_starts(
        wave_starts=wave_starts,
        bytes_per_sender_per_wave=config.bytes_per_sender_per_wave,
        packet_size_bytes=config.packet_size_bytes,
    )
    return classify_packets(
        packet_events=packet_events,
        control_packet_every_n=config.control_packet_every_n,
        control_priority_tag=config.control_priority_tag,
        bulk_priority_tag=config.bulk_priority_tag,
    )


def generate_traffic_for_scenario(name: ScenarioName) -> TrafficEvents:
    return generate_traffic(get_scenario(name))
