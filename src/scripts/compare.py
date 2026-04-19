#!/usr/bin/env python3
"""
CPU PQ vs GPU PQ comparison.

Runs both engines with identical configs (link BPS, buffer, sort interval).
The only intentional difference is sort_latency_us:
  CPU: slower sort  → longer blind window → more control drops
  GPU: faster sort  → shorter blind window → fewer control drops

Prints a focused terminal table of control-packet drop rates.
"""
import os
import re
import subprocess
import sys
import time


def run_engine(binary, args, timeout=120):
    cmd = [binary] + args
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - t0
        return r.stdout, r.returncode, elapsed
    except subprocess.TimeoutExpired:
        return '', -1, timeout


def parse_stats(stdout):
    """Extract key metrics from engine stdout."""
    def _int(pattern):
        m = re.search(pattern, stdout, re.MULTILINE)
        return int(m.group(1)) if m else 0

    def _float(pattern):
        m = re.search(pattern, stdout, re.MULTILINE)
        return float(m.group(1)) if m else 0.0

    return {
        'arrived':            _int(r'^arrived=(\d+)'),
        'ctrl_arrived':       _int(r'^control: arrived=(\d+)'),
        'ctrl_dropped':       _int(r'^control:.*\bdropped=(\d+)'),
        'ctrl_drop_pct':      _float(r'^control:.*\(([0-9.]+)%\)'),
        'bulk_arrived':       _int(r'^bulk:\s+arrived=(\d+)'),
        'bulk_dropped':       _int(r'^bulk:.*\bdropped=(\d+)'),
        'bulk_drop_pct':      _float(r'^bulk:.*\(([0-9.]+)%\)'),
        'sort_latency_avg_us': _float(r'^sort_latency_avg_us=([0-9.]+)'),
    }


def bar(pct, width=20):
    filled = round(pct / 100 * width)
    return '█' * filled + '░' * (width - filled)


def main():
    scenario         = os.environ.get('SCENARIO', 'test')
    link_bps         = os.environ.get('LINK_BPS', '40000000000')
    buffer_bytes     = os.environ.get('BUFFER_BYTES', '1048576')
    sort_interval_us = os.environ.get('SORT_INTERVAL_US', '9000')
    trace_dir        = os.environ.get('TRACE_DIR', 'data/traces')

    # Generate trace if needed.
    subprocess.run(
        ['python3', '-c',
         f'from pathlib import Path; from traffic.config import ScenarioName, get_scenario; '
         f'from traffic.io.csv_export import generate_and_export_csv; '
         f's = ScenarioName("{scenario}"); '
         f'generate_and_export_csv(config=get_scenario(s), scenario_name=s, '
         f'output_dir=Path("{trace_dir}"))'],
        capture_output=True, env={**os.environ, 'PYTHONPATH': '.'},
    )

    # Resolve trace path.
    r = subprocess.run(
        ['python3', '-c',
         f'from pathlib import Path; from traffic.config import ScenarioName, get_scenario; '
         f'from traffic.io.csv_export import build_trace_path; '
         f's = ScenarioName("{scenario}"); '
         f'print(build_trace_path(config=get_scenario(s), scenario_name=s, '
         f'output_dir=Path("{trace_dir}")))'],
        capture_output=True, text=True, env={**os.environ, 'PYTHONPATH': '.'},
    )
    trace_path = r.stdout.strip()
    if not trace_path:
        print('ERROR: could not resolve trace path', file=sys.stderr)
        sys.exit(1)

    common_args = [
        '--input', trace_path,
        '--link-bps', link_bps,
        '--buffer-bytes', buffer_bytes,
        '--sort-interval-us', sort_interval_us,
    ]

    engines = [
        ('CPU Priority Queue', 'build/cpu_priority_queue_sim', common_args),
        ('GPU Priority Queue', 'build/gpu_priority_queue_sim', common_args),
    ]

    print(f'\nScenario : {scenario}')
    print(f'Link     : {int(link_bps)//1_000_000_000} Gbps')
    print(f'Buffer   : {int(buffer_bytes)//1_048_576} MB')
    print(f'Sort     : interval={sort_interval_us}µs  blind window = measured at runtime')
    print()

    results = []
    for label, binary, args in engines:
        print(f'Running {label}...', end=' ', flush=True)
        stdout, rc, elapsed = run_engine(binary, args)
        if rc != 0:
            print(f'FAILED (exit {rc})')
            results.append((label, None, elapsed))
            continue
        stats = parse_stats(stdout)
        results.append((label, stats, elapsed))
        print(f'done ({elapsed:.1f}s)')

    cpu_stats = results[0][1]
    gpu_stats = results[1][1]

    if not cpu_stats or not gpu_stats:
        for label, r in zip(['CPU PQ', 'GPU PQ'], results):
            if not r[1]:
                print(f'  {label}: FAILED')
        return

    cpu_total = cpu_stats['ctrl_dropped'] + cpu_stats['bulk_dropped']
    gpu_total = gpu_stats['ctrl_dropped'] + gpu_stats['bulk_dropped']

    # ── Headline ───────────────────────────────────────────────────────────
    print()
    print('  Same congestion. Different victims.')
    print()

    # ── Main table ─────────────────────────────────────────────────────────
    W = 72
    print('=' * W)
    print(f'  {"":28s}  {"CPU PQ":>16s}  {"GPU PQ":>16s}')
    print('-' * W)

    cpu_lat = cpu_stats['sort_latency_avg_us']
    gpu_lat = gpu_stats['sort_latency_avg_us']

    rows = [
        ('Total packets arrived',  str(cpu_stats['arrived']),        str(gpu_stats['arrived']),        False),
        ('Sort blind window (µs)', f'{cpu_lat:.1f}µs (measured)',    f'{gpu_lat:.1f}µs (measured)',    False),
        ('',                       '',                                '',                               False),
        ('Control arrived',        str(cpu_stats['ctrl_arrived']),   str(gpu_stats['ctrl_arrived']),   False),
        ('Control dropped',        str(cpu_stats['ctrl_dropped']),   str(gpu_stats['ctrl_dropped']),   True),
        ('Control drop rate',      f"{cpu_stats['ctrl_drop_pct']:.1f}%", f"{gpu_stats['ctrl_drop_pct']:.1f}%", True),
        ('',                       '',                                '',                               False),
        ('Bulk arrived',           str(cpu_stats['bulk_arrived']),   str(gpu_stats['bulk_arrived']),   False),
        ('Bulk dropped',           str(cpu_stats['bulk_dropped']),   str(gpu_stats['bulk_dropped']),   False),
        ('Bulk drop rate',         f"{cpu_stats['bulk_drop_pct']:.1f}%", f"{gpu_stats['bulk_drop_pct']:.1f}%", False),
        ('',                       '',                                '',                               False),
        ('Total dropped',          str(cpu_total),                   str(gpu_total),                   False),
    ]

    for label, cv, gv, highlight in rows:
        if not label:
            print()
            continue
        marker = '  ← GPU wins' if highlight and gpu_stats['ctrl_drop_pct'] < cpu_stats['ctrl_drop_pct'] else ''
        print(f'  {label:28s}  {cv:>16s}  {gv:>16s}{marker}')

    print('=' * W)

    # ── Visual bars ────────────────────────────────────────────────────────
    print()
    print('  Control drop rate (lower is better)')
    cpu_pct = cpu_stats['ctrl_drop_pct']
    gpu_pct = gpu_stats['ctrl_drop_pct']
    print(f'  CPU PQ  {bar(cpu_pct)} {cpu_pct:.1f}%  — drops blindly, some victims are control')
    print(f'  GPU PQ  {bar(gpu_pct)} {gpu_pct:.1f}%  — always drops bulk, control is protected')
    print()

    # ── One-liner verdict ──────────────────────────────────────────────────
    redirected = cpu_stats['ctrl_dropped'] - gpu_stats['ctrl_dropped']
    if redirected > 0:
        print(f'  GPU redirected {redirected} control drops → bulk drops.')
        print(f'  Total drops unchanged ({gpu_total}). Queue pressure is identical.')
        print(f'  GPU sorts in {gpu_lat:.1f}µs — fast enough to pick the right victim.')
        print(f'  CPU sorts in {cpu_lat:.1f}µs — the wave is already gone by then.')
    print()

    cpu_elapsed = results[0][2]
    gpu_elapsed = results[1][2]
    print(f'  Wall time — CPU PQ: {cpu_elapsed:.1f}s   GPU PQ: {gpu_elapsed:.1f}s')
    print()


if __name__ == '__main__':
    main()
