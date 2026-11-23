import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "desktop"))
import exporter as pc


class Device:
    ident = "00000002-12345678-00000fa0"
    device = "1234abcdef12"

    def __init__(self):
        self.data = {self.ident: bytes(500)}
        self.buf = io.BytesIO()
        self.damage = False
        self.session = False
        self.interrupt = False
        self.commands = []

    def write(self, b):
        op, *args = b.decode().split()
        self.commands.append(op)
        r = b"OK\n"
        if op == "PPR1":
            r = f"PASSPORT1 {self.device} {self.ident[:17]} 5000 READY\n".encode()
        elif op == "BEGIN":
            self.session = True
        elif op == "END":
            self.session = False
        elif not self.session:
            r = b"ERR SESSION\n"
        elif op == "LIST":
            r = (
                "".join(f"CLIP {key} {len(data)}\n" for key, data in self.data.items())
                + "END\n"
            ).encode()
        elif op == "GET":
            raw = self.data[args[0]]
            r = f"DATA {len(raw)} {hashlib.sha256(raw).hexdigest()}\n".encode()
            if self.interrupt:
                r += raw[:3]
            else:
                r += (bytes([1]) * len(raw) if self.damage else raw) + b"END\n"
        elif op == "ACK":
            if hashlib.sha256(self.data[args[0]]).hexdigest() == args[1]:
                del self.data[args[0]]
            else:
                r = b"ERR CHECKSUM\n"
        self.buf = io.BytesIO(r)

    def read_until(self, end, limit):
        return self.buf.readline(limit)

    def read(self, n):
        return self.buf.read(n)


class RecorderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dev = Device()
        self.link = pc.Link(self.dev)

    def test_copy_without_deleting_and_explicit_reclaim(self):
        device, ids = pc.run_export(self.link, self.root)
        folder = pc.explorer_files(self.root, device, ids)
        self.assertEqual(len(list(folder.glob("*.wav"))), 1)
        self.assertIn(self.dev.ident, self.dev.data)
        pc.run_export(self.link, self.root, reclaim=True)
        self.assertEqual(self.dev.commands.count("GET"), 1)
        self.assertFalse(self.dev.data)

    def test_old_and_wideband_clips_share_export(self):
        import wave
        ident = self.dev.ident[:17] + "w" + self.dev.ident[18:]
        packet = bytes.fromhex("2cc13bc067e5376ce0f4a7166ff55badc21ca7a45b90add6778765de24ca7e762adaa987d8893bf00000")
        self.dev.data[ident] = packet * 5
        device, ids = pc.run_export(self.link, self.root, reclaim=True)
        self.assertFalse(self.dev.data)
        self.assertEqual(set(ids), {ident, self.dev.ident})
        files = pc.explorer_files(self.root, device, ids)
        with wave.open(str(next(files.glob("*w*.wav"))), "rb") as wav:
            self.assertEqual((wav.getframerate(), wav.getnframes()), (16000, 1600))

    def test_checksum_failure_preserves_only_copy(self):
        self.dev.damage = True
        with self.assertRaises(RuntimeError):
            pc.run_export(self.link, self.root, reclaim=True)
        self.assertNotIn("ACK", self.dev.commands)
        self.assertFalse(list(self.root.glob("*/clip.json")))

    def test_interrupted_bytes_can_retry(self):
        self.dev.interrupt = True
        with self.assertRaises(ConnectionError):
            pc.run_export(self.link, self.root, reclaim=True)
        self.dev.interrupt = False
        pc.run_export(self.link, self.root, reclaim=True)
        self.assertEqual(len(list(self.root.glob("*/clip.json"))), 1)

    def test_local_damage_prevents_reclaim(self):
        pc.run_export(self.link, self.root)
        next(self.root.glob("*/voice.wav")).write_bytes(b"damage")
        with self.assertRaises(RuntimeError):
            pc.run_export(self.link, self.root, reclaim=True)
        self.assertNotIn("ACK", self.dev.commands)

    def test_different_device_bytes_block_stale_ack(self):
        pc.run_export(self.link, self.root)
        self.dev.data[self.dev.ident] = b"x" * 500
        with self.assertRaisesRegex(RuntimeError, "CHECKSUM"):
            pc.run_export(self.link, self.root, reclaim=True)
        self.assertTrue(self.dev.data)

    def test_incomplete_frame_raw_rescue(self):
        self.dev.data[self.dev.ident] = b"123"
        with self.assertRaises(RuntimeError):
            pc.run_export(self.link, self.root, reclaim=True)
        pc.run_export(self.link, self.root, rescue=True)
        self.assertEqual(next(self.root.glob("*.recovered.g729")).read_bytes(), b"123")
        self.assertTrue(self.dev.data)

    def test_unclocked_boot_and_empty(self):
        self.dev.data = {"00000001-0fffffff-00000020": bytes(20)}
        pc.run_export(self.link, self.root)
        self.assertIsNone(pc.load_json(next(self.root.glob("*/clip.json")))["started"])
        self.dev.data.clear()
        self.assertEqual(pc.run_export(self.link, self.root)[1], [])

    def test_bad_names_are_rejected_before_fetch(self):
        self.dev.data = {"../../unsafe": bytes(10)}
        with self.assertRaises(ValueError):
            pc.run_export(self.link, self.root)
        self.assertNotIn("GET", self.dev.commands)

    def test_partial_export_still_exposes_good_recordings(self):
        self.dev.data["00000002-12345678-00001111"] = b"x"
        with self.assertRaises(pc.PartialExport) as raised:
            pc.run_export(self.link, self.root)
        self.assertEqual(raised.exception.saved, [self.dev.ident])
        self.assertEqual(len(self.dev.data), 2)


if __name__ == "__main__":
    unittest.main()
