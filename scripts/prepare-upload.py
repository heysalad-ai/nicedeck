#!/usr/bin/env python3
"""Switch only the selected NiceDeck into its ROM download port.

Refuses any port that does not identify itself as a NiceDeck over the protocol
handshake. Several USB serial devices are commonly attached at once -- other
ESP32 boards enumerate identically -- and flashing the wrong one would
overwrite somebody else's firmware. Set NICEDECK_SERIAL (or pass
--expect-serial) to also require a specific board's USB serial number.

A board already sitting in its ROM download port cannot answer the handshake.
That case needs --allow-rom, which is an explicit statement by the operator
that they have checked which board is on that port.
"""
import argparse
import json
import os
import re
import sys
import termios
import time
import serial
from serial.tools import list_ports

def serial_key(value):
    return re.sub(r"[^0-9a-z]", "", (value or "").lower())


def fingerprint(info):
    return (info.device, info.vid, info.pid, info.serial_number, info.product)


def is_deck(port):
    # Old firmware used OpenStreamDeck/ESP32S3_DEV names. Trust the protocol
    # handshake, not the USB descriptor, so existing decks can be upgraded.
    request_id = "prepare-upload-" + str(time.monotonic_ns())
    with serial.Serial(port, 115200, timeout=0.1, write_timeout=1) as connection:
        connection.dtr = True
        connection.write((json.dumps({"op": "hello", "id": request_id}) + "\n").encode())
        connection.flush()
        pending = bytearray()
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            pending.extend(connection.read(1024))
            if len(pending) > 65536:
                return False
            while b"\n" in pending:
                line, _, pending = pending.partition(b"\n")
                try:
                    event = json.loads(line)
                except (ValueError, UnicodeDecodeError):
                    continue
                if isinstance(event, dict) and event.get("id") == request_id:
                    return (event.get("event") == "hello"
                            and event.get("device") in ("NiceDeck", "OpenStreamDeck")
                            and type(event.get("protocol")) is int
                            and event.get("protocol") == 1)
    return False


def describe(info):
    return f"{info.device} (product {info.product!r}, serial {info.serial_number!r})"


def prepare(port, expect_serial=None, allow_rom=False):
    info = next((p for p in list_ports.comports() if p.device == port), None)
    if info is None:
        raise RuntimeError(f"Port {port} is not available.")
    if not is_deck(port):
        # The ROM download port does not speak this protocol, but neither does
        # any other board. Requiring an explicit override keeps an unrelated
        # ESP32 on a neighbouring port from being flashed by accident.
        if not allow_rom:
            raise RuntimeError(
                f"{describe(info)} did not answer the NiceDeck handshake, so it will not be flashed. "
                "If this really is your deck already in ROM download mode, confirm which board is on "
                "this port and re-run with --allow-rom. Otherwise select the deck's application port.")
        return port
    identity = serial_key(info.serial_number)
    if not identity:
        raise RuntimeError("Could not identify the board. Hold BOOT, tap RESET, release BOOT, and flash the download port.")
    if expect_serial and identity != serial_key(expect_serial):
        raise RuntimeError(
            f"{describe(info)} is a NiceDeck, but not the expected board {expect_serial!r}. "
            "Pass --expect-serial for the board you mean, or unset NICEDECK_SERIAL to flash any deck.")
    original = fingerprint(info)
    # Let macOS finish closing the hello connection before changing its baud.
    time.sleep(0.3)
    reset_error = None
    try:
        with serial.Serial(port, 1200, timeout=0.2):
            time.sleep(0.2)
    except (OSError, termios.error) as error:
        # TinyUSB may disappear while pyserial configures or closes the port.
        # A matching replacement below is required before accepting that reset.
        reset_error = error
    disappeared = False
    for _ in range(60):
        ports = list_ports.comports()
        disappeared |= not any(fingerprint(p) == original for p in ports)
        matches = [p for p in ports if serial_key(p.serial_number) == identity]
        if len(matches) == 1 and (disappeared or fingerprint(matches[0]) != original):
            candidate = matches[0].device
            if not is_deck(candidate):
                return candidate
            raise RuntimeError("The board restarted into the app instead of download mode; hold BOOT, tap RESET, release BOOT, then retry.")
        time.sleep(0.2)
    detail = f" USB reset reported: {reset_error}." if reset_error else ""
    raise RuntimeError("The download port did not appear." + detail
                       + " Hold BOOT, tap RESET, release BOOT, then retry on its new port.")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port")
    parser.add_argument("--expect-serial", default=os.environ.get("NICEDECK_SERIAL") or None,
                        help="require this USB serial number; defaults to $NICEDECK_SERIAL")
    parser.add_argument("--allow-rom", action="store_true",
                        help="allow a port that cannot answer the handshake, for a board already in ROM download mode")
    args = parser.parse_args()
    print(prepare(args.port, expect_serial=args.expect_serial, allow_rom=args.allow_rom))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, termios.error) as error:
        sys.exit(str(error))
