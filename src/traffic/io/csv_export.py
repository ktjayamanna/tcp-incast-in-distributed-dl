from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterable

from traffic.config import ScenarioName, TrafficConfig
from traffic.generator import generate_traffic
from traffic.validate import validate_generated_traffic


TRACE_COLUMNS = (
    "packet_start_us",
    "packet_size_bytes",
    "traffic_class",
    "priority_tag",
)

DEFAULT_TRACE_DIR = Path("src/data/traces")


def build_trace_path(
    *,
    config: TrafficConfig,
    scenario_name: ScenarioName | None = None,
    output_dir: Path = DEFAULT_TRACE_DIR,
) -> Path:
    label = scenario_name.value if scenario_name is not None else "custom"
    filename = f"{label}_seed{config.seed}_senders{config.senders_per_wave}_waves{config.number_of_waves}.csv"
    return output_dir / filename


def export_events_to_csv(events: Iterable, output_path: Path) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(TRACE_COLUMNS)
        for e in events:
            writer.writerow([e.packet_start_us, e.packet_size_bytes,
                             e.traffic_class.value, e.priority_tag])
    return output_path


def generate_and_export_csv(
    *,
    config: TrafficConfig,
    scenario_name: ScenarioName | None = None,
    output_dir: Path = DEFAULT_TRACE_DIR,
    validate: bool = True,
) -> Path:
    events = generate_traffic(config)
    if validate:
        validate_generated_traffic(events, config)
    output_path = build_trace_path(config=config, scenario_name=scenario_name, output_dir=output_dir)
    return export_events_to_csv(events, output_path)
