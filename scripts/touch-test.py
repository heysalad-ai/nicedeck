#!/usr/bin/env python3
"""No-action touch diagnostics. Close the NiceDeck Mac app first.

  python3 scripts/touch-test.py guided        # movement check, then alignment check
  python3 scripts/touch-test.py motion        # count contacts while nothing touches the glass
  python3 scripts/touch-test.py targets       # five-point alignment check
  python3 scripts/touch-test.py stream        # record raw and mapped samples to JSON
  python3 scripts/touch-test.py status        # read counters without resetting them
  python3 scripts/touch-test.py exit          # leave test mode and re-enable shortcuts

While test mode is active the firmware runs a screen with no event handlers and
refuses to start any action sequence, so nothing here can send a keystroke or a
Mac command.

The alignment check VERIFIES that touch lands where it is drawn. It reports the
measured error for every target and never derives, applies or stores a
correction; the device reports calibrationApplied=false to make that explicit.

Test mode survives a serial disconnect so an interrupted run does not silently
re-enable shortcuts. A power cycle also leaves it.
"""
import argparse
import importlib.util
import json
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location('deck_device', Path(__file__).with_name('deck-device.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

SUPPORTED = ('1.1.0',)


def summarise(report):
    return (f"contacts={report['contacts']} downSamples={report['downSamples']} "
            f"readErrors={report['readErrors']} bootPresses={report['bootPresses']} "
            f"interval={report['minIntervalMs']}-{report['maxIntervalMs']}ms "
            f"lastReject={report['lastReject']}")


def alignment_errors(report):
    rows = []
    for index, point in enumerate(report['points'], start=1):
        if point['measured']:
            rows.append(f"  target {index}: expected ({point['expectedX']},{point['expectedY']}) "
                        f"measured ({point['x']},{point['y']}) error ({point['dx']:+d},{point['dy']:+d}) px")
        else:
            rows.append(f"  target {index}: not reached")
    return '\n'.join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=['guided', 'motion', 'targets', 'stream', 'status', 'exit'])
    parser.add_argument('--port', required=True, help='USB serial port of your NiceDeck')
    parser.add_argument('--seconds', type=float, default=30.0, help='stream duration')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()

    deck = module.Deck(args.port)
    try:
        firmware = deck.hello.get('firmware')
        if firmware not in SUPPORTED:
            raise SystemExit(f"This tool needs NiceDeck firmware {' or '.join(SUPPORTED)}; "
                             f"the device reports {firmware}.")

        if args.mode == 'guided':
            deck.request('touch_test', 'touch_test', mode='motion')
            input('Shortcuts are disabled. Move the case and the cable WITHOUT touching the '
                  'glass for about 20 seconds, then press Return here. ')
            motion = deck.request('touch_test', 'touch_test', mode='status')
            print('Movement check:', summarise(motion))

            deck.request('touch_test', 'touch_test', mode='targets')
            input('Now tap each of the five crosshairs in turn. Press Return here when the '
                  'screen says the alignment check is complete, or when you give up. ')
            targets = deck.request('touch_test', 'touch_test', mode='status')
            print(f"Alignment check: {targets['hits']} of {len(targets['points'])} targets reached "
                  f"in {targets['attempts']} attempts")
            print(alignment_errors(targets))
            print('Measurement only; no calibration was applied '
                  f"(calibrationApplied={targets['calibrationApplied']}).")

            input('Press Return to leave test mode and re-enable shortcuts. ')
            deck.request('touch_test', 'touch_test', mode='exit')
            result = {'motion': motion, 'alignment': targets}

        elif args.mode == 'stream':
            deck.request('touch_test', 'touch_test', mode='stream')
            print(f'Recording raw samples for {args.seconds:.0f}s. Touch the screen now.')
            samples = []
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                for event in deck.events(timeout=min(2.0, max(0.1, end - time.monotonic()))):
                    if event.get('event') == 'touch_sample':
                        samples.append(event)
            deck.request('touch_test', 'touch_test', mode='stop_stream')
            final = deck.request('touch_test', 'touch_test', mode='status')
            contacts = sum(1 for i, s in enumerate(samples)
                           if s.get('down') and (i == 0 or not samples[i - 1].get('down')))
            print(f'{len(samples)} samples, {contacts} contacts, final: {summarise(final)}')
            result = {'samples': samples, 'final': final}

        else:
            result = deck.request('touch_test', 'touch_test', mode=args.mode)
            print(summarise(result) if args.mode != 'exit' else 'Test mode left; shortcuts re-enabled.')
            if args.mode in ('targets', 'status') and any(p['measured'] for p in result['points']):
                print(alignment_errors(result))

        text = json.dumps(result, indent=2)
        if args.output:
            args.output.write_text(text + '\n')
            print('Saved', args.output)
        else:
            print(text)
    finally:
        deck.serial.close()


if __name__ == '__main__':
    main()
