#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment


def run_simulation(method_name, make_target, scenario, link_bps, buffer_bytes, trace_dir):
    env = os.environ.copy()
    env['PYTHONPATH'] = '.'
    try:
        result = subprocess.run(
            ['make', make_target,
             f'SCENARIO={scenario}',
             f'LINK_BPS={link_bps}',
             f'BUFFER_BYTES={buffer_bytes}',
             f'TRACE_DIR={trace_dir}'],
            cwd='.',
            capture_output=True,
            text=True,
            timeout=600,
            env=env,
        )
        return {
            'method': method_name,
            'status': 'Success' if result.returncode == 0 else 'Failed',
            'return_code': result.returncode,
            'stdout': result.stdout[-2000:] if result.stdout else '',
            'stderr': result.stderr[-500:] if result.stderr else '',
        }
    except subprocess.TimeoutExpired:
        return {
            'method': method_name,
            'status': 'Timeout',
            'return_code': -1,
            'stdout': '',
            'stderr': 'Exceeded 600 s timeout',
        }


def save_to_excel(results, output_path):
    wb = Workbook()
    ws = wb.active
    ws.title = "Results"

    headers = ['Method', 'Status', 'Return Code', 'Output', 'Errors']
    fill = PatternFill(start_color="4472C4", end_color="4472C4", fill_type="solid")
    font = Font(bold=True, color="FFFFFF")
    for col, h in enumerate(headers, 1):
        cell = ws.cell(row=1, column=col, value=h)
        cell.fill = fill
        cell.font = font
        cell.alignment = Alignment(horizontal="center")

    for row, r in enumerate(results, 2):
        ws.cell(row=row, column=1).value = r['method']
        ws.cell(row=row, column=2).value = r['status']
        ws.cell(row=row, column=3).value = r['return_code']
        ws.cell(row=row, column=4).value = r['stdout']
        ws.cell(row=row, column=5).value = r['stderr']

    for col, width in zip('ABCDE', [25, 12, 12, 60, 40]):
        ws.column_dimensions[col].width = width

    wb.save(output_path)
    print(f"Results saved to: {output_path}")


def main():
    scenario    = os.environ.get('SCENARIO', 'high')
    link_bps    = os.environ.get('LINK_BPS', '40000000000')
    buffer_bytes = os.environ.get('BUFFER_BYTES', '52428800')  # 50 MB for all scenarios
    trace_dir   = os.environ.get('TRACE_DIR', 'data/traces')
    output_file = os.environ.get('OUTPUT_FILE', 'results.xlsx')

    methods = [
        ('CPU FIFO',           'run-cpu-fifo'),
        ('CPU Priority Queue', 'run-cpu-priority-queue'),
        ('GPU Priority Queue', 'run-gpu-priority-queue'),
    ]

    results = []
    for name, target in methods:
        print(f"\nRunning {name}...")
        r = run_simulation(name, target, scenario, link_bps, buffer_bytes, trace_dir)
        results.append(r)
        print(f"  {r['status']}")

    save_to_excel(results, output_file)


if __name__ == '__main__':
    main()
