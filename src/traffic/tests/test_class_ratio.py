"""Tests that CONTROL/BULK class split follows the configured ratio target."""

from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[2]))

from traffic.config import low as normal_traffic
from traffic.generator import generate_traffic
from traffic.models.classifier import TrafficClass


def test_control_ratio_is_near_configured_target() -> None:
    """Expectation: CONTROL share is close to 1 / control_packet_every_n."""
    config = normal_traffic()
    events = generate_traffic(config)

    control_count = sum(1 for event in events if event.traffic_class == TrafficClass.CONTROL)
    total_count = len(events)

    actual_ratio = control_count / total_count
    expected_ratio = 1.0 / config.control_packet_every_n

    assert abs(actual_ratio - expected_ratio) <= 0.01
