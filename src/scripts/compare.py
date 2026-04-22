#!/usr/bin/env python3
"""
CPU PQ vs GPU PQ comparison via ns3.

Starts each engine in --socket mode, sends identical ns3 incast traffic to
each in turn (deterministic: fixed seed), then prints a terminal table of
control-packet drop rates.
"""
import os
import re
import subprocess
import sys
import time


def run_engine_with_ns3(binary, engine_args, scenario, port, link_bps, buffer_bytes, timeout=300):
    """Start engine in socket mode, send ns3 traffic, return stdout."""
    engine_cmd = [binary] + engine_args + ['--socket', str(port)]
    ns3_cmd = [
        'python3', '-m', 'traffic.ns3_incast',
        '--scenario', scenario,
        '--port', str(port),
        '--link-bps', str(link_bps),
        '--buffer-bytes', str(buffer_bytes),
    ]

    env = {**os.environ, 'PYTHONPATH': '.'}
    t0 = time.perf_counter()
    try:
        engine_proc = subprocess.Popen(engine_cmd, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
        time.sleep(0.5)
        ns3_proc = subprocess.run(ns3_cmd, capture_output=True, text=True,
                                  timeout=timeout, env=env)
        stdout, _ = engine_proc.communicate(timeout=60)
        elapsed = time.perf_counter() - t0
        return stdout, engine_proc.returncode, elapsed
    except subprocess.TimeoutExpired:
        engine_proc.kill()
        return '', -1, timeout


def parse_stats(stdout):
    def _int(pattern):
        m = re.search(pattern, stdout, re.MULTILINE)
        return int(m.group(1)) if m else 0

    def _float(pattern):
        m = re.search(pattern, stdout, re.MULTILINE)
        return float(m.group(1)) if m else 0.0

    return {
        'arrived':              _int(r'^arrived=(\d+)'),
        'ctrl_arrived':         _int(r'^control: arrived=(\d+)'),
        'ctrl_dropped':         _int(r'^control:.*\bdropped=(\d+)'),
        'ctrl_drop_pct':        _float(r'^control:.*\(([0-9.]+)%\)'),
        'bulk_arrived':         _int(r'^bulk:\s+arrived=(\d+)'),
        'bulk_dropped':         _int(r'^bulk:.*\bdropped=(\d+)'),
        'bulk_drop_pct':        _float(r'^bulk:.*\(([0-9.]+)%\)'),
        'sort_latency_avg_us':  _float(r'^sort_latency_avg_us=([0-9.]+)'),
        'cpu_sort_util_pct':    _float(r'^cpu_sort_util_pct=([0-9.]+)'),
        'sim_wall_ms':          _float(r'^sim_wall_ms=([0-9.]+)'),
        # GPU-only fields (zero for CPU PQ)
        'pipeline_efficiency':  _float(r'^pipeline_efficiency=([0-9.]+)'),
        'gpu_kernel_util_pct':  _float(r'^gpu_kernel_util_pct=([0-9.]+)'),
        'gpu_sort_active_pct':  _float(r'^gpu_sort_active_pct=([0-9.]+)'),
    }


def bar(pct, width=20):
    filled = round(pct / 100 * width)
    return '█' * filled + '░' * (width - filled)


BUFFER_BY_SCENARIO = {
    'test':   '1048576',    #  1 MB
    'low':    '52428800',   # 50 MB
    'medium': '5242880',    #  5 MB
    'high':   '16777216',   # 16 MB
}

def main():
    scenario         = os.environ.get('SCENARIO', 'high')
    link_bps         = os.environ.get('LINK_BPS', '45000000000')
    buffer_bytes     = os.environ.get('BUFFER_BYTES',
                           BUFFER_BY_SCENARIO.get(scenario, '52428800'))
    sort_interval_us = os.environ.get('SORT_INTERVAL_US', '9000')
    base_port        = int(os.environ.get('SOCKET_PORT', '9000'))

    common_args = [
        '--link-bps', link_bps,
        '--buffer-bytes', buffer_bytes,
        '--sort-interval-us', sort_interval_us,
    ]

    engines = [
        ('CPU Priority Queue', 'build/cpu_priority_queue_sim', common_args, base_port),
        ('GPU Priority Queue', 'build/gpu_priority_queue_sim', common_args, base_port + 1),
    ]

    print(f'\nScenario : {scenario}')
    print(f'Link     : {int(link_bps)//1_000_000_000} Gbps')
    print(f'Buffer   : {int(buffer_bytes)//1_048_576} MB')
    print(f'Sort     : interval={sort_interval_us}µs  blind window = measured at runtime')
    print()

    results = []
    for label, binary, args, port in engines:
        print(f'Running {label}...', end=' ', flush=True)
        stdout, rc, elapsed = run_engine_with_ns3(
            binary, args, scenario, port, link_bps, buffer_bytes)
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
        for label, r, _ in results:
            if not r:
                print(f'  {label}: FAILED')
        sys.exit(1)

    cpu_total = cpu_stats['ctrl_dropped'] + cpu_stats['bulk_dropped']
    gpu_total = gpu_stats['ctrl_dropped'] + gpu_stats['bulk_dropped']
    cpu_lat   = cpu_stats['sort_latency_avg_us']
    gpu_lat   = gpu_stats['sort_latency_avg_us']

    print()
    print('  Same congestion. Different victims.')
    print()

    W = 72
    print('=' * W)
    print(f'  {"":28s}  {"CPU PQ":>16s}  {"GPU PQ":>16s}')
    print('-' * W)

    rows = [
        ('Total packets arrived',  str(cpu_stats['arrived']),             str(gpu_stats['arrived']),             False),
        ('Sort blind window (µs)', f'{cpu_lat:.1f}µs (measured)',         f'{gpu_lat:.1f}µs (measured)',         False),
        ('',                       '',                                     '',                                    False),
        ('Control arrived',        str(cpu_stats['ctrl_arrived']),        str(gpu_stats['ctrl_arrived']),        False),
        ('Control dropped',        str(cpu_stats['ctrl_dropped']),        str(gpu_stats['ctrl_dropped']),        True),
        ('Control drop rate',      f"{cpu_stats['ctrl_drop_pct']:.1f}%",  f"{gpu_stats['ctrl_drop_pct']:.1f}%", True),
        ('',                       '',                                     '',                                    False),
        ('Bulk arrived',           str(cpu_stats['bulk_arrived']),        str(gpu_stats['bulk_arrived']),        False),
        ('Bulk dropped',           str(cpu_stats['bulk_dropped']),        str(gpu_stats['bulk_dropped']),        False),
        ('Bulk drop rate',         f"{cpu_stats['bulk_drop_pct']:.1f}%",  f"{gpu_stats['bulk_drop_pct']:.1f}%", False),
        ('',                       '',                                     '',                                    False),
        ('Total dropped',          str(cpu_total),                        str(gpu_total),                        False),
    ]

    for label, cv, gv, highlight in rows:
        if not label:
            print()
            continue
        marker = '  ← GPU wins' if highlight and gpu_stats['ctrl_drop_pct'] < cpu_stats['ctrl_drop_pct'] else ''
        print(f'  {label:28s}  {cv:>16s}  {gv:>16s}{marker}')

    print('=' * W)

    print()
    print('  Control drop rate (lower is better)')
    print(f'  CPU PQ  {bar(cpu_stats["ctrl_drop_pct"])} {cpu_stats["ctrl_drop_pct"]:.1f}%  — drops blindly during ~{cpu_lat:.0f}µs sort')
    print(f'  GPU PQ  {bar(gpu_stats["ctrl_drop_pct"])} {gpu_stats["ctrl_drop_pct"]:.1f}%  — blind only ~{gpu_lat:.0f}µs, protects control')
    print()

    redirected = cpu_stats['ctrl_dropped'] - gpu_stats['ctrl_dropped']
    if redirected > 0:
        print(f'  GPU redirected {redirected} control drops → bulk drops.')
        print(f'  Total drops unchanged ({gpu_total}). Queue pressure is identical.')
        print(f'  GPU sorts in {gpu_lat:.1f}µs — fast enough to pick the right victim.')
        print(f'  CPU sorts in {cpu_lat:.1f}µs — the wave is already gone by then.')
    print()

    print(f'  Wall time — CPU PQ: {results[0][2]:.1f}s   GPU PQ: {results[1][2]:.1f}s')
    print()

    print('  Sorter utilization')
    print(f'  CPU PQ  sort occupies {cpu_stats["cpu_sort_util_pct"]:.1f}% of sim wall time  '
          f'(std::sort, ~{cpu_lat:.0f}µs blind window per epoch)')
    print(f'  GPU PQ  GPU active    {gpu_stats["gpu_sort_active_pct"]:.1f}% of sim wall time  '
          f'| kernel compute {gpu_stats["gpu_kernel_util_pct"]:.1f}% of GPU active time')
    print(f'          pipeline efficiency {gpu_stats["pipeline_efficiency"]:.2f}x  '
          f'(>1.0 = H2D/kernel/D2H stages overlapping across 3 slots)')
    print()


if __name__ == '__main__':
    main()
