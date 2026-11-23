"""Preflight by default. The explicit --write option installs the three verified segments."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def checked_run(port, *args):
    proc = subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", port, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    print(proc.stdout)
    if proc.returncode:
        raise SystemExit("设备操作失败，停止后续步骤。")
    return proc.stdout


def verify_files(folder):
    manifest = json.loads((folder / "manifest.json").read_text())
    offsets = {"bootloader.bin": 0, "partitions.bin": 0x8000, "app.bin": 0x10000}
    if (
        manifest["target"] != "esp32c3"
        or manifest["flash_bytes"] != 0x800000
        or set(manifest["images"]) != set(offsets)
    ):
        raise ValueError("固件清单与工牌布局不符")
    for name, offset in offsets.items():
        data = (folder / name).read_bytes()
        entry = manifest["images"][name]
        end = {0: 0x8000, 0x8000: 0x9000, 0x10000: 0x300000}[offset]
        if (
            not data
            or offset + len(data) > end
            or entry["offset"] != offset
            or hashlib.sha256(data).hexdigest() != entry["sha256"]
        ):
            raise ValueError("镜像边界或 SHA-256 校验失败：" + name)
    return offsets


def main():
    p = argparse.ArgumentParser(
        description="AI Passport ESP32-C3 8MB 安装与检查，esptool 4.x"
    )
    p.add_argument("--port", required=True)
    p.add_argument("--write", action="store_true")
    p.add_argument("--backup", type=Path, help="可选，备份完整原片至新的本地文件")
    opts = p.parse_args()
    folder = Path(__file__).resolve().parents[1] / "bin" / "flash"
    offsets = verify_files(folder)
    info = checked_run(opts.port, "flash_id")
    if not re.search(r"Detected flash size:\s*8MB", info, re.I):
        raise SystemExit("未识别为 8 MB Flash，已停止。")
    if opts.backup:
        # Reserve safely; do not clobber an existing user backup.
        with opts.backup.open("xb"):
            pass
        checked_run(opts.port, "read_flash", "0", "0x800000", str(opts.backup))
        if opts.backup.stat().st_size != 0x800000:
            raise SystemExit("备份不完整")
        print("备份 SHA-256", hashlib.sha256(opts.backup.read_bytes()).hexdigest())
    if opts.write:
        args = []
        for name, offset in offsets.items():
            args.extend([hex(offset), str(folder / name)])
        checked_run(
            opts.port,
            "write_flash",
            "--flash_mode",
            "dio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "8MB",
            *args,
        )
    else:
        print("预检通过。仅显式 --write 才写入固件。")


if __name__ == "__main__":
    main()
