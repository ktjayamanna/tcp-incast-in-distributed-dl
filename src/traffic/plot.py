from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from traffic.config import ScenarioName, get_scenario
from traffic.generator import generate_traffic
from traffic.models.classifier import TrafficClass


def _scenario_from_string(value: str) -> ScenarioName:
    try:
        return ScenarioName(value)
    except ValueError as exc:
        allowed = ", ".join(item.value for item in ScenarioName)
        raise argparse.ArgumentTypeError(
            f"Invalid scenario '{value}'. Choose one of: {allowed}"
        ) from exc


def _plot_event_timeline(events, config, out_dir: Path) -> None:
    times = np.array([event.packet_start_us for event in events], dtype=np.int64)
    zoom_waves = min(3, config.number_of_waves)
    zoom_end_us = config.first_wave_start_us + zoom_waves * config.wave_interval_us
    zoom_times = times[(times >= config.first_wave_start_us) & (times <= zoom_end_us)]
    bin_width_us = max(1, min(100, max(1, config.max_start_offset_us // 3)))
    bin_edges = np.arange(
        config.first_wave_start_us,
        zoom_end_us + bin_width_us,
        bin_width_us,
        dtype=np.int64,
    )
    plt.figure(figsize=(10, 4))
    plt.hist(zoom_times, bins=bin_edges, color="#2f7ed8")
    for wave_idx in range(zoom_waves + 1):
        x = config.first_wave_start_us + wave_idx * config.wave_interval_us
        plt.axvline(x, color="#7a7a7a", linestyle="--", linewidth=0.8, alpha=0.6)
    plt.xlabel("packet_start_us")
    plt.ylabel("event_count")
    plt.title(
        f"Input Signal Timeline (First {zoom_waves} Waves, {bin_width_us}us bins)"
    )
    plt.tight_layout()
    plt.savefig(out_dir / "input_signal_timeline.png", dpi=150)
    plt.close()


def _plot_events_per_wave(events, config, out_dir: Path) -> None:
    counts = np.zeros(config.number_of_waves, dtype=np.int64)
    for event in events:
        counts[event.wave_id] += 1

    zoom_waves = min(30, config.number_of_waves)
    wave_axis = np.arange(zoom_waves)

    plt.figure(figsize=(10, 4))
    plt.plot(wave_axis, counts[:zoom_waves], linewidth=1.5)
    plt.xlabel("wave_id")
    plt.ylabel("event_count")
    plt.title(f"Events Per Wave (First {zoom_waves})")
    plt.tight_layout()
    plt.savefig(out_dir / "events_per_wave.png", dpi=150)
    plt.close()


def _plot_control_ratio_by_wave(events, config, out_dir: Path) -> None:
    total_counts = np.zeros(config.number_of_waves, dtype=np.int64)
    control_counts = np.zeros(config.number_of_waves, dtype=np.int64)

    for event in events:
        total_counts[event.wave_id] += 1
        if event.traffic_class == TrafficClass.CONTROL:
            control_counts[event.wave_id] += 1

    ratios = np.divide(
        control_counts,
        total_counts,
        out=np.zeros_like(control_counts, dtype=float),
        where=total_counts > 0,
    )
    target_ratio = 1.0 / config.control_packet_every_n
    zoom_waves = min(30, config.number_of_waves)
    wave_axis = np.arange(zoom_waves)

    plt.figure(figsize=(10, 4))
    plt.plot(wave_axis, ratios[:zoom_waves], label="observed", linewidth=1.4)
    plt.axhline(target_ratio, color="#7a7a7a", linestyle="--", linewidth=1.2, label="target")
    plt.xlabel("wave_id")
    plt.ylabel("control_fraction")
    plt.title(f"Control Ratio Per Wave (First {zoom_waves})")
    plt.legend(loc="best")
    plt.tight_layout()
    plt.savefig(out_dir / "control_ratio_by_wave.png", dpi=150)
    plt.close()


def _plot_sender_start_offsets(events, config, out_dir: Path) -> None:
    first_packet_events = [event for event in events if event.packet_index_for_sender == 0]
    if not first_packet_events:
        return

    wave_base_start: dict[int, int] = {}
    for event in first_packet_events:
        if event.wave_id not in wave_base_start:
            wave_base_start[event.wave_id] = event.packet_start_us
        else:
            wave_base_start[event.wave_id] = min(
                wave_base_start[event.wave_id], event.packet_start_us
            )

    wave_ids = np.array([event.wave_id for event in first_packet_events], dtype=np.int64)
    offsets = np.array(
        [event.packet_start_us - wave_base_start[event.wave_id] for event in first_packet_events],
        dtype=np.int64,
    )
    zoom_waves = min(20, config.number_of_waves)
    in_zoom = wave_ids < zoom_waves
    zoom_wave_ids = wave_ids[in_zoom]
    zoom_offsets = offsets[in_zoom]

    plt.figure(figsize=(10, 4))
    plt.scatter(zoom_wave_ids, zoom_offsets, s=8, alpha=0.6)
    plt.xlabel("wave_id")
    plt.ylabel("sender_start_offset_us")
    plt.title(f"Input Signal Sync Quality (First {zoom_waves} Waves)")
    plt.scatter([], [], s=24, alpha=0.6, label="Darker spots mean overlap")
    plt.legend(loc="lower right", bbox_to_anchor=(1.0, -0.22), ncol=1, frameon=True, fontsize=8)
    plt.tight_layout()
    plt.savefig(out_dir / "input_signal_sender_offsets.png", dpi=150, bbox_inches="tight")
    plt.close()


def _write_plot_guide(out_dir: Path, config) -> None:
    target_control_ratio = 1.0 / config.control_packet_every_n
    content = f"""Plot Guide
==========

This file explains how to read each generated plot, what expected behavior looks like for the current traffic generator, and what subtle differences can indicate.

1) input_signal_timeline.png
- What it shows:
  High-resolution event histogram over packet start time, zoomed to early waves.
- How to read it:
  X-axis is packet start time (microseconds). Y-axis is number of packets in a small time bin.
  Dashed vertical lines mark wave boundaries.
- Expected behavior:
  Repeating narrow burst peaks right after each wave boundary.
- Subtle differences and meaning:
  Wider peaks: more sender start-time spread (higher jitter).
  Peaks shifted far from boundaries: timing offset bug.
  Uneven peak heights: inconsistent per-wave volume.
  Missing peaks: missing waves or filtering bug.

2) events_per_wave.png
- What it shows:
  Packet count per wave (first zoom window).
- How to read it:
  X-axis is wave ID. Y-axis is total packets emitted in that wave.
- Expected behavior:
  Nearly flat line for this generator (same bytes and packetization each wave).
- Subtle differences and meaning:
  Small oscillation: expected if packet remainder behavior differs by config.
  Step/drift trend: configuration changed over time or state leak.
  Outlier spikes/dips: likely logic bug or partial wave generation.

3) input_signal_sender_offsets.png
- What it shows:
  Sender start offset inside each wave (first packet only).
- How to read it:
  Each dot is one sender in one wave.
  X-axis is wave ID.
  Y-axis is offset (microseconds) from the earliest sender in that same wave.
  Dot transparency alpha is 0.6. Darker regions mean multiple points overlap.
- Expected behavior:
  Offsets should stay within configured jitter range [0, max_start_offset_us].
  For this config, that upper bound is {config.max_start_offset_us} us.
- Subtle differences and meaning:
  Tight band near 0: highly synchronized senders.
  Wide vertical spread: less synchronization / more jitter.
  Occasional dots above expected max: offset calculation bug.
  Changing spread across wave IDs: non-stationary RNG/config issue.

4) control_ratio_by_wave.png
- What it shows:
  Observed control-packet fraction by wave vs configured target.
- How to read it:
  Solid line is observed ratio; dashed line is target ratio.
- Expected behavior:
  Observed line close to target ({target_control_ratio:.4f}) in each wave.
- Subtle differences and meaning:
  Constant bias above/below target: classifier rule mismatch.
  Large wave-to-wave swings: low packet counts or unstable classification logic.
  Drift over waves: stateful bug in classifier indexing.
"""
    (out_dir / "plots.txt").write_text(content)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate sanity plots for traffic output.")
    parser.add_argument(
        "--scenario",
        type=_scenario_from_string,
        default=ScenarioName.LOW,
        help="Scenario name.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("src/traffic/data/plots"),
        help="Directory where plot images are written.",
    )
    args, _ = parser.parse_known_args()

    config = get_scenario(args.scenario)
    events = generate_traffic(config)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    _plot_event_timeline(events, config, args.out_dir)
    _plot_events_per_wave(events, config, args.out_dir)
    _plot_sender_start_offsets(events, config, args.out_dir)
    _plot_control_ratio_by_wave(events, config, args.out_dir)
    _write_plot_guide(args.out_dir, config)

    print(f"Wrote 4 input-sanity plots and plots.txt to: {args.out_dir}")


if __name__ == "__main__":
    main()
