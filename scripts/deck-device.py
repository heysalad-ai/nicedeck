#!/usr/bin/env python3
"""Read/configure NiceDeck over USB without triggering any HID action.

Requires pyserial. Screenshot output is PPM (can be converted by macOS sips).
Close the Mac editor before using this tool so only one process owns USB CDC.
"""
import argparse
import base64
import copy
import glob
import json
from pathlib import Path
import sys
import time
import uuid

import serial


class Deck:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.2, write_timeout=15)
        self.serial.dtr = True
        self.pending = bytearray()
        request_id = self.send("hello")
        hello = self.wait(lambda e: e.get("event") == "hello" and e.get("id") == request_id)
        if (hello.get("device") not in ("NiceDeck", "OpenStreamDeck")
                or type(hello.get("protocol")) is not int or hello.get("protocol") != 1):
            self.serial.close()
            raise RuntimeError("The connected device is not NiceDeck protocol 1 (or its OpenStreamDeck predecessor)")
        self.hello = hello

    def send(self, op, **fields):
        request_id = str(uuid.uuid4())
        payload = json.dumps(dict(op=op, id=request_id, **fields), separators=(",", ":")).encode() + b"\n"
        self.serial.write(payload)
        self.serial.flush()
        return request_id

    def events(self, timeout=15):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            chunk = self.serial.read(max(1, min(self.serial.in_waiting, 16384)))
            self.pending.extend(chunk)
            if len(self.pending) > 600000:
                raise RuntimeError("Device response exceeded the protocol limit")
            while b"\n" in self.pending:
                line, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                try:
                    event = json.loads(line)
                except (UnicodeDecodeError, ValueError):
                    continue  # Ignore ROM boot diagnostics.
                if isinstance(event, dict):
                    yield event

    def wait(self, predicate, timeout=15):
        for event in self.events(timeout):
            if predicate(event):
                return event
        raise TimeoutError("Timed out waiting for device response")

    def request(self, op, event_type="ack", **fields):
        request_id = self.send(op, **fields)
        result = self.wait(lambda e: e.get("event") == event_type and e.get("id") == request_id, 30)
        if result.get("ok") is False:
            raise RuntimeError(result.get("error", "Device rejected request"))
        return result

    def config(self):
        return self.request("get_config", "config")["config"]

    def screenshot(self, path):
        request_id = self.send("screenshot")
        pixels = None
        seen = None
        for event in self.events(45):
            if event.get("id") != request_id:
                continue
            if event.get("event") == "screenshot":
                width, height = int(event["width"]), int(event["height"])
                if (width, height, event.get("format")) != (240, 320, "rgb565be"):
                    raise RuntimeError("Unexpected screenshot dimensions or format")
                pixels = base64.b64decode(event["data"], validate=True)
                if len(pixels) != width * height * 2:
                    raise RuntimeError("Incomplete screenshot")
                rgb = bytearray()
                for i in range(0, len(pixels), 2):
                    value = (pixels[i] << 8) | pixels[i + 1]
                    rgb.extend((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
                Path(path).write_bytes(f"P6\n{width} {height}\n255\n".encode() + rgb)
                return dict(screenshot=str(path), width=width, height=height)
            if event.get("event") == "screenshot_begin":
                width, height = int(event["width"]), int(event["height"])
                if width != 240 or height != 320 or event.get("encoding") != "rgb565be":
                    raise RuntimeError("Unexpected screenshot dimensions or encoding")
                pixels = bytearray(width * height * 2)
                seen = bytearray(len(pixels))
            elif event.get("event") == "screenshot_chunk" and pixels is not None:
                offset = int(event["offset"])
                data = base64.b64decode(event["data"], validate=True)
                if offset < 0 or offset + len(data) > len(pixels):
                    raise RuntimeError("Screenshot chunk out of range")
                pixels[offset:offset + len(data)] = data
                seen[offset:offset + len(data)] = b"\1" * len(data)
            elif event.get("event") == "screenshot_end" and pixels is not None:
                if not all(seen):
                    raise RuntimeError("Incomplete screenshot")
                rgb = bytearray()
                for i in range(0, len(pixels), 2):
                    value = (pixels[i] << 8) | pixels[i + 1]
                    rgb.extend((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
                Path(path).write_bytes(f"P6\n{width} {height}\n255\n".encode() + rgb)
                return dict(screenshot=str(path), width=width, height=height)
            elif event.get("event") in ("error", "ack") and event.get("ok") is False:
                raise RuntimeError(event.get("error", "Screenshot failed"))
        raise TimeoutError("Screenshot did not complete")

    def smoke_test(self):
        original = self.config()
        invalid = copy.deepcopy(original)
        invalid["brightness"] = 101
        request_id = self.send("config", config=invalid)
        rejected = self.wait(lambda e: e.get("id") == request_id and e.get("event") == "ack")
        assert rejected.get("ok") is False, "Invalid config was accepted"
        assert self.config() == original, "Rejected config altered device state"
        modified = copy.deepcopy(original)
        modified["pages"][0]["name"] = "USB verified"
        try:
            self.request("config", config=modified)
            assert self.config() == modified, "Configuration readback differed"
            self.request("page", page=len(modified["pages"]) - 1)
            status = self.request("status", "status")
            assert status["page"] == len(modified["pages"]) - 1, "Page selection failed"
            assert status.get("touch") is True, "Touch controller not detected"
            assert status.get("hid") is True, "USB HID not ready"
        finally:
            self.request("config", config=original)
            self.request("page", page=0)
        return {"passed": ["identity", "invalid_config_rejected", "config_roundtrip", "page_selection", "touch_detected", "hid_ready", "original_restored"], "status": status}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB serial port; auto-detect if exactly one exists")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    sub.add_parser("get-config").add_argument("path")
    sub.add_parser("sync").add_argument("path")
    sub.add_parser("screenshot").add_argument("path")
    sub.add_parser("smoke-test")
    args = parser.parse_args()
    ports = glob.glob("/dev/cu.usbmodem*")
    port = args.port or (ports[0] if len(ports) == 1 else None)
    if not port:
        parser.error("Specify --port; there is not exactly one USB serial device")
    deck = Deck(port)
    try:
        if args.command == "status":
            result = deck.request("status", "status")
        elif args.command == "get-config":
            Path(args.path).write_text(json.dumps(deck.config(), indent=2) + "\n")
            result = {"saved": args.path}
        elif args.command == "sync":
            result = deck.request("config", config=json.loads(Path(args.path).read_text()))
        elif args.command == "screenshot":
            result = deck.screenshot(args.path)
        else:
            result = deck.smoke_test()
        print(json.dumps(result, indent=2))
    finally:
        deck.serial.close()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, TimeoutError, OSError, AssertionError, ValueError) as error:
        print(f"NiceDeck: {error}", file=sys.stderr)
        sys.exit(1)
