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
        'arrived':       _int(r'^arrived=(\d+)'),
        'ctrl_arrived':  _int(r'^control: arrived=(\d+)'),
        'ctrl_dropped':  _int(r'^control:.*\bdropped=(\d+)'),
        'ctrl_drop_pct': _float(r'^control:.*\(([0-9.]+)%\)'),
        'bulk_arrived':  _int(r'^bulk:\s+arrived=(\d+)'),
        'bulk_dropped':  _int(r'^bulk:.*\bdropped=(\d+)'),
        'bulk_drop_pct': _float(r'^bulk:.*\(([0-9.]+)%\)'),
    }


def bar(pct, width=20):
    filled = round(pct / 100 * width)
    return '█' * filled + '░' * (width - filled)


def main():
    scenario         = os.environ.get('SCENARIO', 'test')
    link_bps         = os.environ.get('LINK_BPS', '40000000000')
    buffer_bytes     = os.environ.get('BUFFER_BYTES', '1048576')
    cpu_latency_us   = os.environ.get('CPU_SORT_LATENCY_US', '25')
    gpu_latency_us   = os.environ.get('GPU_SORT_LATENCY_US', '5')
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
        ('CPU Priority Queue', 'build/cpu_priority_queue_sim',
         common_args + ['--sort-latency-us', cpu_latency_us]),
        ('GPU Priority Queue', 'build/gpu_priority_queue_sim',
         common_args + ['--sort-latency-us', gpu_latency_us]),
    ]

    print(f'\nScenario : {scenario}')
    print(f'Link     : {int(link_bps)//1_000_000_000} Gbps')
    print(f'Buffer   : {int(buffer_bytes)//1024} KB')
    print(f'Sort lag : CPU={cpu_latency_us}µs  GPU={gpu_latency_us}µs  interval={sort_interval_us}µs')
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

    # ── Print comparison table ─────────────────────────────────────────────
    print()
    print('=' * 72)
    print(f'{"":30s}  {"CPU PQ":>16s}  {"GPU PQ":>16s}')
    print('=' * 72)

    labels_and_keys = [
        ('Total packets arrived',   'arrived',       False),
        ('Control arrived',         'ctrl_arrived',  False),
        ('Control dropped',         'ctrl_dropped',  False),
        ('Control drop rate',       'ctrl_drop_pct', True),
        ('Bulk dropped',            'bulk_dropped',  False),
        ('Bulk drop rate',          'bulk_drop_pct', True),
    ]

    cpu_stats = results[0][1]
    gpu_stats = results[1][1]

    for label, key, is_pct in labels_and_keys:
        cv = cpu_stats[key] if cpu_stats else None
        gv = gpu_stats[key] if gpu_stats else None
        if is_pct:
            cs = f'{cv:.1f}%' if cv is not None else 'ERR'
            gs = f'{gv:.1f}%' if gv is not None else 'ERR'
        else:
            cs = str(cv) if cv is not None else 'ERR'
            gs = str(gv) if gv is not None else 'ERR'
        print(f'  {label:28s}  {cs:>16s}  {gs:>16s}')

    print('=' * 72)

    # ── Visual drop-rate bars ──────────────────────────────────────────────
    if cpu_stats and gpu_stats:
        print()
        print('  Control drop rate (lower is better)')
        cpu_pct = cpu_stats['ctrl_drop_pct']
        gpu_pct = gpu_stats['ctrl_drop_pct']
        print(f'  CPU PQ  {bar(cpu_pct)} {cpu_pct:.1f}%')
        print(f'  GPU PQ  {bar(gpu_pct)} {gpu_pct:.1f}%')
        print()

        if cpu_pct > 0:
            improvement = (cpu_pct - gpu_pct) / cpu_pct * 100
            print(f'  GPU PQ reduces control drop rate by {improvement:.0f}% vs CPU PQ')
        print()

    cpu_elapsed = results[0][2]
    gpu_elapsed = results[1][2]
    print(f'  Wall time — CPU PQ: {cpu_elapsed:.1f}s   GPU PQ: {gpu_elapsed:.1f}s')
    print()


if __name__ == '__main__':
    main()
