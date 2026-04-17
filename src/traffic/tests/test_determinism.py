"""Tests that generation is deterministic for the same seed and config."""

from dataclasses import replace
from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[2]))

from traffic.config import low as normal_traffic
from traffic.generator import generate_traffic


def test_same_seed_produces_identical_events() -> None:
    """Expectation: identical config + seed yields exactly identical event output."""
    config = normal_traffic()

    first_run = generate_traffic(config)
    second_run = generate_traffic(config)

    assert first_run == second_run


def test_different_seed_changes_output() -> None:
    """Expectation: changing seed changes at least part of generated traffic timing."""
    config = normal_traffic()
    different_seed_config = replace(config, seed=config.seed + 1)

    first_run = generate_traffic(config)
    second_run = generate_traffic(different_seed_config)

    assert first_run != second_run
