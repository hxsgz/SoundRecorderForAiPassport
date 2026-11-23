"""Explicit opt-in Windows report notifications; independent of USB export."""

import time
import hashlib
import json


def watch(share, state, notify, once=False):
    state.mkdir(parents=True, exist_ok=True)
    previous = ""
    while True:
        try:
            if not (share / "reports").is_dir():
                raise OSError("日报目录暂不可用")
            for file in sorted((share / "reports").glob("*.json")):
                raw = file.read_bytes()
                marker = state / hashlib.sha256(raw).hexdigest()
                if not marker.exists():
                    report = json.loads(raw)
                    notify(
                        "语音日报 · " + report["day"],
                        report["text"][:165] + " · " + file.name,
                        True,
                    )
                    marker.touch()
            status = share / "state" / "status.json"
            failures = (
                json.loads(status.read_text(encoding="utf-8"))["failures"]
                if status.exists()
                else {}
            )
            if failures:
                detail = "；".join(str(v["error"]) for v in failures.values())
                if detail != previous:
                    notify("NAS 处理等待重试", detail, True)
                previous = detail
            else:
                previous = ""
        except (OSError, ValueError, KeyError) as exc:
            if str(exc) != previous:
                notify("日报通知", str(exc), True)
            previous = str(exc)
        if once:
            return
        time.sleep(20)
