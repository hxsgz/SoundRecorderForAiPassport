"""USB record export. A committed clip directory is the unit of local and NAS delivery."""

import argparse
import atexit
from drive import ExplorerDrive
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time
import wave
from datetime import datetime, timedelta, timezone

HERE = Path(__file__).resolve().parent
ID = re.compile(r"[0-9a-f]{8}-[0-9a-f]{8}[-w][0-9a-f]{8}")
BOOT = re.compile(r"[0-9a-f]{8}-[0-9a-f]{8}")
DEVICE = re.compile(r"[0-9a-f]{12}")
SHA = re.compile(r"[0-9a-f]{64}")
CAPACITY = 5 * 1024 * 1024


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def sync_file(path):
    with path.open("rb+") as stream:
        os.fsync(stream.fileno())


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    sync_file(path)


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def raw_name(ident):
    return "voice.spxraw" if ident[17] == "w" else "voice.g729"


def decode(raw, wav):
    import av

    wide = raw.suffix == ".spxraw"
    rate, packet_size, frame_samples = (16000, 42, 320) if wide else (8000, 10, 80)
    size = raw.stat().st_size
    if size % packet_size or not size:
        raise ValueError("片段长度不完整，可使用 --rescue 导出原始数据")
    decoder = av.CodecContext.create("speex" if wide else "g729", "r")
    decoder.sample_rate = rate
    decoder.layout = "mono"
    convert = av.AudioResampler(format="s16", layout="mono", rate=rate)
    samples = 0
    with raw.open("rb") as source, wave.open(str(wav), "wb") as target:
        target.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        while packet := source.read(packet_size):
            for frame in decoder.decode(av.Packet(packet)):
                if frame.samples != frame_samples or frame.sample_rate != rate:
                    raise ValueError("解码格式异常，工牌保留原件")
                for pcm in convert.resample(frame):
                    target.writeframesraw(bytes(pcm.planes[0])[: pcm.samples * 2])
                    samples += pcm.samples
    if samples != (size // packet_size) * frame_samples:
        raise ValueError("解码长度异常，保留工牌原件")
    sync_file(wav)
    return samples / rate


class Link:
    def __init__(self, port):
        self.port = port

    def send(self, cmd):
        self.port.write((cmd + "\n").encode("ascii"))

    def line(self):
        data = self.port.read_until(b"\n", 240)
        if not data.endswith(b"\n"):
            raise ConnectionError("USB 超时或已拔线，重新连接可重试")
        return data.decode("ascii").strip()

    def expect(self, cmd, reply="OK"):
        self.send(cmd)
        result = self.line()
        if result != reply:
            raise RuntimeError(f"{cmd.split()[0]} 返回 {result}")

    def hello(self):
        self.send("PPR1")
        for _ in range(12):
            line = self.line()
            if line.startswith("PASSPORT1 "):
                _, device, boot, ms, state = line.split()
                if (
                    not DEVICE.fullmatch(device)
                    or not BOOT.fullmatch(boot)
                    or state not in ("READY", "UNMOUNTED")
                ):
                    break
                return (
                    device,
                    boot,
                    datetime.now(timezone.utc) - timedelta(milliseconds=int(ms)),
                    state,
                )
        raise ConnectionError("此端口没有返回录音固件标识")

    def clips(self):
        self.send("LIST")
        while True:
            line = self.line()
            if line == "END":
                return
            fields = line.split()
            if len(fields) != 3 or fields[0] != "CLIP" or not ID.fullmatch(fields[1]):
                raise ValueError(f"无效清单行 {line!r}")
            size = int(fields[2])
            if not 0 <= size <= CAPACITY:
                raise ValueError("无效录音大小")
            yield fields[1], size

    def fetch(self, ident, size, raw):
        self.send("GET " + ident)
        fields = self.line().split()
        if (
            len(fields) != 3
            or fields[:2] != ["DATA", str(size)]
            or not SHA.fullmatch(fields[2])
        ):
            raise ValueError("设备返回的大小或摘要无效")
        with raw.open("xb") as dest:
            remain = size
            while remain:
                chunk = self.port.read(min(16384, remain))
                if not chunk:
                    raise ConnectionError("录音传输中断")
                dest.write(chunk)
                remain -= len(chunk)
            dest.flush()
            os.fsync(dest.fileno())
        if self.line() != "END" or sha(raw) != fields[2]:
            raise ValueError("传输校验失败，原件保留在工牌上")
        return fields[2]


def verified(folder):
    m = load_json(folder / "clip.json")
    if (
        sha(folder / raw_name(m["key"][-26:])) != m["raw_sha256"]
        or sha(folder / "voice.wav") != m["wav_sha256"]
    ):
        raise ValueError(f"存档校验失败，请保留工牌原件并检查 {folder}")
    return m


def archive_one(link, root, device, boot, anchor, ident, size, rescue):
    name = raw_name(ident)
    key = device + "_" + ident
    final = root / key
    if final.exists() and not rescue:
        m = verified(final)
        if m["key"] != key or (final / name).stat().st_size != size:
            raise ValueError("已有归档与设备清单不一致")
        return m["raw_sha256"]
    stage = Path(tempfile.mkdtemp(prefix=".pending-", dir=root))
    try:
        digest = link.fetch(ident, size, stage / name)
        if rescue:
            destination = root / (key + "." + digest + ".recovered" + Path(name).suffix)
            os.replace(stage / name, destination)
            if sha(destination) != digest:
                raise ValueError("救援副本校验失败")
            return digest
        duration = decode(stage / name, stage / "voice.wav")
        started = None
        if ident[:17] == boot:
            started = (anchor + timedelta(milliseconds=int(ident[18:], 16))).isoformat()
        write_json(
            stage / "clip.json",
            {
                "key": key,
                "device": device,
                "boot": ident[:17],
                "uptime_ms": int(ident[18:], 16),
                "started": started,
                "exported": datetime.now(timezone.utc).isoformat(),
                "duration_s": duration,
                "raw_sha256": digest,
                "wav_sha256": sha(stage / "voice.wav"),
            },
        )
        verified(stage)
        stage.rename(final)
        return digest
    finally:
        if stage.exists():
            shutil.rmtree(stage)


class PartialExport(RuntimeError):
    def __init__(self, device, saved, errors):
        super().__init__("部分录音保留在工牌上：" + " | ".join(errors))
        self.device = device
        self.saved = saved


def run_export(link, archive, rescue=False, init=None, reclaim=False):
    device, boot, anchor, state = link.hello()
    link.expect("BEGIN")
    try:
        if state == "UNMOUNTED":
            if init != device:
                raise RuntimeError(
                    f"存储未挂载。确认没有待恢复数据后才可使用 --initialize {device} 初始化录音区"
                )
            link.expect("INIT " + device)
        local = datetime.now().astimezone()
        link.send(f"CLOCK {local.hour * 3600 + local.minute * 60 + local.second}")
        result = link.line()
        if result not in ("OK", "ERR COMMAND"):
            print("工牌校时失败，继续导出录音", flush=True)
        records = list(link.clips())
        errors = []
        saved = []
        for ident, size in records:
            try:
                digest = archive_one(
                    link, archive, device, boot, anchor, ident, size, rescue
                )
            except (ValueError, OSError) as exc:
                if isinstance(exc, ConnectionError):
                    raise
                errors.append(f"{ident} {exc}")
                continue
            if digest and reclaim:
                link.expect("ACK " + ident + " " + digest)
            saved.append(ident)
            print(f"已保存 {ident}  {size / (2100 if ident[17] == 'w' else 1000):.1f} 秒", flush=True)
        if errors:
            raise PartialExport(device, saved, errors)
        return device, [ident for ident, _ in records]
    finally:
        with contextlib.suppress(Exception):
            link.expect("END")


def explorer_files(root, device, records):
    folder = root / "USB" / device
    folder.mkdir(parents=True, exist_ok=True)
    for ident in records:
        entry = root / (device + "_" + ident)
        m = verified(entry)
        stamp = (
            datetime.fromisoformat(m["started"])
            .astimezone()
            .strftime("%Y%m%dT%H%M%S%z")
            if m["started"]
            else "UNSYNC"
        )
        dest = folder / (stamp + "_" + m["key"] + "_" + m["wav_sha256"][:16] + ".wav")
        if dest.exists() and sha(dest) == m["wav_sha256"]:
            continue
        with tempfile.NamedTemporaryFile(
            dir=folder, prefix=".copy-", delete=False
        ) as f:
            temp = Path(f.name)
        try:
            shutil.copyfile(entry / "voice.wav", temp)
            sync_file(temp)
            if sha(temp) != m["wav_sha256"]:
                raise ValueError("盘符副本校验失败")
            os.replace(temp, dest)
        finally:
            temp.unlink(missing_ok=True)
    return folder


def notify(title, message, enabled):
    print(f"{title} · {message}", flush=True)
    if enabled and os.name == "nt":
        from win_notice import show

        show(title, message)


def open_port(name):
    import serial

    port = serial.Serial(port=None, baudrate=115200, timeout=2, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = name
    port.open()
    port.reset_input_buffer()
    return port


def candidates(name):
    from serial.tools import list_ports

    if name:
        return [name]
    return [
        p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x303A, 0x1001)
    ]


@contextlib.contextmanager
def archive_lock(root):
    lock = (root / ".export.lock").open("a+b")
    try:
        if os.name == "nt":
            import msvcrt

            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        lock.close()


def main():
    parser = argparse.ArgumentParser(description="AI Passport USB 录音盘与本地归档")
    default_settings = HERE / "settings.json"
    if getattr(sys, "frozen", False):
        exe_dir = Path(sys.executable).resolve().parent
        default_settings = exe_dir / "settings.json"
        if not default_settings.exists():
            default_settings = exe_dir.parent / "desktop" / "settings.json"
    parser.add_argument("--settings", type=Path, default=default_settings)
    parser.add_argument("--port")
    parser.add_argument("--once", action="store_true")
    parser.add_argument(
        "--watch-reports",
        type=Path,
        help="独立监听指定 NAS 数据目录中的日报，此模式不打开 USB",
    )
    parser.add_argument(
        "--self-test", action="store_true", help="离线检查解码、写盘和资源管理器盘符"
    )
    parser.add_argument(
        "--reclaim", action="store_true", help="本地校验后显式释放工牌空间"
    )
    parser.add_argument(
        "--rescue", action="store_true", help="保存可读原始数据，不回收"
    )
    parser.add_argument(
        "--initialize",
        metavar="DEVICE",
        help="仅对未挂载录音区显式格式化，将丢失该区所有数据",
    )
    args = parser.parse_args()
    if args.self_test:
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            raw = folder / "test.g729"
            raw.write_bytes(bytes(100))
            decode(raw, folder / "test.wav")
            wide = folder / "test.spxraw"
            # One 20 ms frame from the fixed-point quality-5 wideband encoder.
            wide.write_bytes(bytes.fromhex("2cc13bc067e5376ce0f4a7166ff55badc21ca7a45b90add6778765de24ca7e762adaa987d8893bf00000"))
            if decode(wide, folder / "wide.wav") != 0.02:
                raise ValueError("16 kHz 导出自检失败")
            drive = ExplorerDrive(folder)
            try:
                if os.name == "nt":
                    if not drive.letter or sha(Path(drive.letter + "/test.wav")) != sha(
                        folder / "test.wav"
                    ):
                        raise OSError("盘符不可用，仍可从本地归档目录取文件")
                print("PASS: Speex 16 kHz / G.729 / WAV / local disk / Explorer", flush=True)
            finally:
                drive.close()
        return 0
    if not args.settings.exists() and args.settings.name != "settings.json":
        parser.error(f"找不到配置文件 {args.settings}")
    cfg = load_json(
        args.settings if args.settings.exists() else HERE / "settings.example.json"
    )
    root = Path(cfg["archive"]).expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)
    if args.watch_reports:
        from report_watch import watch

        watch(args.watch_reports, root / ".notifications", notify, args.once)
        return 0
    enabled = cfg.get("windows_notifications", True)
    once = args.once or args.initialize or args.rescue or args.reclaim
    drives = {}
    previous = {}
    print("等待 USB 数据线连接。归档目录", root, flush=True)
    atexit.register(lambda: [d.close() for d in drives.values()])
    while True:
        names = candidates(args.port or cfg.get("port"))
        for name in list(drives):
            if name not in names:
                drives.pop(name).close()
        failed = False
        if args.reclaim and len(names) > 1 and not args.port:
            parser.error("同时检测到多台设备，请用 --port 选择要回收的工牌")
        for name in names:
            if name in drives:
                continue
            try:
                with archive_lock(root), open_port(name) as port:
                    try:
                        device, ids = run_export(
                            Link(port), root, args.rescue, args.initialize, args.reclaim
                        )
                    except PartialExport as exc:
                        if not exc.saved:
                            raise
                        device, ids = exc.device, exc.saved
                        notify("部分片段需要重试", str(exc), enabled)
                        failed = True
                folder = root
                if not args.rescue:
                    folder = explorer_files(root, device, ids)
                if not once:
                    drives[name] = ExplorerDrive(folder, cfg.get("drive_letter", "R"))
                view = drives[name].letter if name in drives else None
                previous.pop(name, None)
                notify(
                    "USB 录音已就绪",
                    f"{len(ids)} 段，可从 {view or folder} 复制",
                    enabled,
                )
            except Exception as exc:
                failed = True
                if previous.get(name) != str(exc):
                    notify("USB 导出等待重试", str(exc), enabled)
                    previous[name] = str(exc)
        if once:
            if not names:
                print("没有检测到 ESP32-C3 USB 串口", file=sys.stderr)
            return int(failed or not names)
        time.sleep(3)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError, ImportError) as exc:
        sys.exit(str(exc))
