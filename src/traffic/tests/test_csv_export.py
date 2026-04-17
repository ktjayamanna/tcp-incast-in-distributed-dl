"""Tests CSV export schema and content for generated traffic traces."""

from __future__ import annotations

import csv
from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[2]))

from traffic.config import ScenarioName, low
from traffic.io.csv_export import TRACE_COLUMNS, generate_and_export_csv


def test_generate_and_export_csv_writes_expected_header_and_rows(tmp_path: Path) -> None:
    """Expectation: export writes canonical columns and one row per generated event."""
    config = low()
    output_path = generate_and_export_csv(
        config=config,
        scenario_name=ScenarioName.LOW,
        output_dir=tmp_path,
    )

    assert output_path.exists()

    with output_path.open("r", newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        rows = list(reader)

    assert reader.fieldnames == list(TRACE_COLUMNS)
    assert len(rows) > 0


def test_generate_and_export_csv_filename_contains_scenario_and_seed(tmp_path: Path) -> None:
    """Expectation: filename is deterministic and encodes scenario/config identity."""
    config = low()
    output_path = generate_and_export_csv(
        config=config,
        scenario_name=ScenarioName.LOW,
        output_dir=tmp_path,
    )

    filename = output_path.name
    assert "low" in filename
    assert f"seed{config.seed}" in filename
    assert filename.endswith(".csv")
