import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch, MagicMock

spec = importlib.util.spec_from_file_location("prepare_upload", Path(__file__).resolve().parents[1] / "scripts/prepare-upload.py")
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


def device(path="/dev/cu.app", serial="102030A0B0C0", product="ESP32S3_DEV"):
    return SimpleNamespace(device=path, serial_number=serial, product=product, vid=0x303A, pid=0x1001)


class UploadTests(unittest.TestCase):
    def test_hello_probe_settles_before_1200_baud_reset(self):
        app, rom = device(), device("/dev/cu.rom", "10:20:30:a0:b0:c0")
        events = []
        def probe(port):
            events.append(("probe", port))
            return port == app.device
        def reset(*args, **kwargs):
            events.append(("reset", args[0]))
            return MagicMock()
        with patch.object(upload.list_ports, "comports", side_effect=[[app], [rom]]), \
             patch.object(upload, "is_deck", side_effect=probe), \
             patch.object(upload.serial, "Serial", side_effect=reset), \
             patch.object(upload.time, "sleep", side_effect=lambda duration: events.append(("sleep", duration))):
            self.assertEqual(upload.prepare(app.device), rom.device)
        self.assertEqual(events[:3], [("probe", app.device), ("sleep", 0.3), ("reset", app.device)])

    def test_reset_disconnect_is_accepted_only_for_same_board_rom(self):
        app = device()
        rom = device("/dev/cu.rom", "10:20:30:a0:b0:c0")
        other = device("/dev/cu.other", "112233445566")
        for error in (OSError(6, "Device not configured"), upload.termios.error(6, "Device not configured")):
            with self.subTest(error=type(error).__name__), \
                 patch.object(upload.list_ports, "comports", side_effect=[[app], [other], [other, rom]]), \
                 patch.object(upload, "is_deck", side_effect=[True, False]) as probe, \
                 patch.object(upload.serial, "Serial", side_effect=error), patch.object(upload.time, "sleep"):
                self.assertEqual(upload.prepare(app.device), rom.device)
                self.assertEqual([call.args[0] for call in probe.call_args_list], [app.device, rom.device])

    def test_reset_disconnect_without_same_board_rom_is_clean_error(self):
        app = device()
        other = device("/dev/cu.other", "112233445566")
        with patch.object(upload.list_ports, "comports", side_effect=[[app]] + [[other]] * 60), \
             patch.object(upload, "is_deck", return_value=True) as probe, \
             patch.object(upload.serial, "Serial", side_effect=upload.termios.error(6, "Device not configured")), \
             patch.object(upload.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "download port did not appear.*USB reset reported.*Device not configured"):
                upload.prepare(app.device)
            probe.assert_called_once_with(app.device)

    def test_same_vid_pid_and_product_uses_protocol_and_serial(self):
        app = device()
        rom = device("/dev/cu.rom", "10:20:30:a0:b0:c0")
        unrelated = device("/dev/cu.other", "112233445566")
        with patch.object(upload.list_ports, "comports", side_effect=[[app], [app, unrelated], [rom, unrelated]]), \
             patch.object(upload, "is_deck", side_effect=[True, False]) as probe, \
             patch.object(upload.serial, "Serial") as connection, patch.object(upload.time, "sleep"):
            self.assertEqual(upload.prepare(app.device), rom.device)
            self.assertEqual([call.args[0] for call in probe.call_args_list], [app.device, rom.device])
            connection.assert_called_once_with(app.device, 1200, timeout=0.2)

    def test_unchanged_app_port_cannot_be_mistaken_for_download_port(self):
        app = device()
        with patch.object(upload.list_ports, "comports", return_value=[app]), \
             patch.object(upload, "is_deck", return_value=True), \
             patch.object(upload.serial, "Serial"), patch.object(upload.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "download port did not appear"):
                upload.prepare(app.device)

    def test_missing_serial_prevents_reset(self):
        app = device(serial=None)
        with patch.object(upload.list_ports, "comports", return_value=[app]), \
             patch.object(upload, "is_deck", return_value=True), patch.object(upload.serial, "Serial") as connection:
            with self.assertRaisesRegex(RuntimeError, "Could not identify"):
                upload.prepare(app.device)
            connection.assert_not_called()

    def test_unidentified_port_is_refused(self):
        # An unrelated ESP32 on a neighbouring port answers nothing, and looks
        # exactly like a deck already in ROM download mode. Refusing by default
        # is what stops the wrong board being flashed.
        other = device("/dev/cu.other", "123456ABCDEF", "USB JTAG_serial debug unit")
        with patch.object(upload.list_ports, "comports", return_value=[other]), \
             patch.object(upload, "is_deck", return_value=False), patch.object(upload.serial, "Serial") as connection:
            with self.assertRaisesRegex(RuntimeError, "did not answer the NiceDeck handshake"):
                upload.prepare(other.device)
            connection.assert_not_called()

    def test_rom_port_is_left_for_esptool_when_explicitly_allowed(self):
        rom = device("/dev/cu.rom")
        with patch.object(upload.list_ports, "comports", return_value=[rom]), \
             patch.object(upload, "is_deck", return_value=False), patch.object(upload.serial, "Serial") as connection:
            self.assertEqual(upload.prepare(rom.device, allow_rom=True), rom.device)
            connection.assert_not_called()

    def test_a_different_deck_is_refused_when_a_serial_is_expected(self):
        other = device("/dev/cu.otherdeck", "AABBCCDDEEFF")
        with patch.object(upload.list_ports, "comports", return_value=[other]), \
             patch.object(upload, "is_deck", return_value=True), patch.object(upload.serial, "Serial") as connection:
            with self.assertRaisesRegex(RuntimeError, "not the expected board"):
                upload.prepare(other.device, expect_serial="102030A0B0C0")
            connection.assert_not_called()

    def test_expected_serial_matches_regardless_of_punctuation(self):
        app, rom = device(), device("/dev/cu.rom", "10:20:30:a0:b0:c0")
        with patch.object(upload.list_ports, "comports", side_effect=[[app]] + [[rom]] * 60), \
             patch.object(upload, "is_deck", side_effect=[True, False]), \
             patch.object(upload.serial, "Serial"), patch.object(upload.time, "sleep"):
            self.assertEqual(upload.prepare(app.device, expect_serial="10:20:30:A0:B0:C0"), rom.device)

    def test_wrong_board_never_selected(self):
        app = device()
        other = device("/dev/cu.other", "112233445566")
        with patch.object(upload.list_ports, "comports", side_effect=[[app]] + [[other]] * 60), \
             patch.object(upload, "is_deck", return_value=True), \
             patch.object(upload.serial, "Serial"), patch.object(upload.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "download port did not appear"):
                upload.prepare(app.device)

    def test_restarted_app_is_not_download_mode(self):
        app, restarted = device(), device("/dev/cu.new")
        with patch.object(upload.list_ports, "comports", side_effect=[[app], [restarted]]), \
             patch.object(upload, "is_deck", return_value=True), \
             patch.object(upload.serial, "Serial"), patch.object(upload.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "restarted into the app"):
                upload.prepare(app.device)

    def test_protocol_probe_handles_fragmented_json_and_boot_logs(self):
        connection = MagicMock()
        chunks = []
        def send(payload):
            request = json.loads(payload)
            response = json.dumps(dict(event="hello", id=request["id"], device="OpenStreamDeck", protocol=1)).encode() + b"\n"
            chunks.extend([b"boot log\n" + response[:20], response[20:]])
        connection.write.side_effect = send
        connection.read.side_effect = lambda _: chunks.pop(0)
        connection.__enter__.return_value = connection
        with patch.object(upload.serial, "Serial", return_value=connection):
            self.assertTrue(upload.is_deck("/dev/cu.app"))


if __name__ == "__main__":
    unittest.main()
