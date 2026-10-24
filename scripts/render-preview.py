#!/usr/bin/env python3
"""Render the deck's LVGL screens off-device, from a real configuration.

Uses the same LVGL build, theme, layout and icon code as the firmware, so the
output shows what the panel will draw. It shows nothing about touch behaviour.

  python3 scripts/render-preview.py --config ~/Library/Application\\ Support/NiceDeck/config.json
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / 'build' / 'deck-preview'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--config', type=Path,
                    default=Path.home() / 'Library/Application Support/NiceDeck/config.json')
parser.add_argument('--out', type=Path, default=ROOT / 'build' / 'ui-preview')
parser.add_argument('--pressed', type=int, default=-1,
                    help='draw this tile index in its pressed state')
args = parser.parse_args()

if not BINARY.exists():
    raise SystemExit(f'{BINARY} not built; run scripts/build-preview.sh')

config = json.loads(args.config.read_text())
args.out.mkdir(parents=True, exist_ok=True)
pages = config['pages']
outputs = []
for index, page in enumerate(pages):
    tiles = [f"{b['label']}|{b['color']}|{b['icon']}|"
             f"{(b['actions'][0]['value'] if b.get('actions') else '')}"
             for b in page['buttons']]
    ppm = args.out / f"page-{index}-{page['id']}.ppm"
    command = [str(BINARY), str(ppm), page['name'], str(index), str(len(pages))]
    if args.pressed >= 0:
        command += ['--pressed', str(args.pressed)]
    command += tiles
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    png = ppm.with_suffix('.png')
    subprocess.run(['sips', '-s', 'format', 'png', str(ppm), '--out', str(png)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ppm.unlink()
    outputs.append(png)

print('\n'.join(str(p) for p in outputs))
