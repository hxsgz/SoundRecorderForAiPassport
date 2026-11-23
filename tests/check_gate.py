"""Exercise the firmware gate on a 3+ second speech WAV and ESC-50 fixtures.

Build the host DLL with tests/build_gate.cmd. In the fixture directory use the
following ESC-50 recordings (karolpiczak/ESC-50, audio/) with category names:
engine.wav 1-18527-A-44, car_horn.wav 1-17124-A-43, wind.wav 1-137296-A-16,
rain.wav 1-17367-A-10, vacuum_cleaner.wav 1-100210-A-36,
train.wav 1-119125-A-45. The test repeats each 5-second sample four times.
"""
import argparse
import ctypes
from pathlib import Path

import av
import numpy as np


def read_pcm(path):
    with av.open(str(path)) as source:
        rs = av.AudioResampler(format="s16", layout="mono", rate=8000)
        parts = [out.to_ndarray().ravel() for frame in source.decode(audio=0) for out in rs.resample(frame)]
        parts.extend(out.to_ndarray().ravel() for out in rs.resample(None))
    return np.concatenate(parts).astype(float)


def scaled(pcm, rms):
    return pcm * rms / np.sqrt(np.mean(pcm ** 2))


def intervals(dll, signal):
    ctx = ctypes.create_string_buffer(dll.test_gate_size())
    samples = np.clip(signal, -32768, 32767).astype(np.int16)
    start, last_voice, current, result = None, 0, 0, []
    for i in range(0, len(samples) // 80 - 1, 2):
        trigger = continuing = False
        for half in (i, i + 1):
            frame = samples[half*80:half*80+80]
            trigger |= dll.gate_frame(ctx, frame.ctypes.data)
            continuing |= bool(dll.test_continuing(ctx))
        if start is None:
            if trigger:
                start = max(current, i - 46)
                last_voice = i + 2
        else:
            if continuing:
                last_voice = i + 2
            if i + 2 - last_voice >= 1000:
                result.append((start, last_voice))
                current, start = i + 2, None
                dll.test_reset_votes(ctx)
    if start is not None:
        result.append((start, last_voice))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("speech", type=Path)
    parser.add_argument("environment", type=Path)
    args = parser.parse_args()
    dll = ctypes.CDLL(str(args.dll.resolve()))
    dll.gate_frame.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
    dll.gate_frame.restype = ctypes.c_bool
    dll.test_reset_votes.argtypes = (ctypes.c_void_p,)
    dll.test_continuing.argtypes = (ctypes.c_void_p,)
    dll.test_gate_size.restype = ctypes.c_size_t
    noise = np.random.default_rng(12).normal(0, 2000, 8000 * 60)
    speech = read_pcm(args.speech)
    pad = np.zeros(8000)
    cases = {"silence": np.zeros_like(noise), "wideband": noise,
             "below_threshold": np.clip(scaled(speech, 12), -90, 90)}
    for path in sorted(args.environment.glob("*.wav")):
        cases[path.stem] = np.tile(scaled(read_pcm(path), 1700), 4)
    engine = scaled(read_pcm(args.environment / "engine.wav"), 600)
    cases["speech"] = scaled(speech, 2200)
    cases["quiet_speech"] = scaled(speech, 350)
    cases["speech_over_engine"] = np.resize(engine, len(speech)) + scaled(speech, 1500)
    cases["two_second_phrase"] = np.concatenate((scaled(speech[8000:24000], 1200), np.zeros(8000 * 8)))
    rng = np.random.default_rng(23)
    rub = np.concatenate([np.hanning(n) * rng.normal(0, 4500, n)
                          for n in (320, 1200, 1760, 480)])
    cases["synthetic_rubs"] = np.tile(np.concatenate((rub, pad)), 12)
    results = {}
    for name, pcm in cases.items():
        clips = intervals(dll, np.concatenate((pad, pcm, pad)))
        results[name] = sum(end - start for start, end in clips) / 100
        print(f"{name:20} {len(pcm)/8000:5.1f}s  span before splice {results[name]:5.2f}s  clips {len(clips)}")
    for name in ("silence", "below_threshold", "synthetic_rubs"):
        assert results[name] == 0, name
    for name in ("speech", "quiet_speech", "speech_over_engine"):
        assert results[name] > 0.6 * len(speech) / 8000, name
    assert 1.5 < results["two_second_phrase"] <= 3.2
    assert results["wideband"] < 2.0
    phrase = scaled(speech[8000:24000], 1200)
    for name in ("engine", "car_horn", "train", "vacuum_cleaner", "wind", "rain", "synthetic_rubs"):
        assert results[name] == 0, name
        after = intervals(dll, np.concatenate((pad, phrase, cases[name], pad)))
        assert len(after) == 1 and after[0][1] <= 330, (name, after)
    def conversation(gap):
        return intervals(dll, np.concatenate((pad, phrase, np.zeros(8000 * gap),
                                              scaled(phrase, 350), np.zeros(8000 * 12))))
    joined, split = conversation(8), conversation(11)
    # The fixtures have different amounts of recorded silence at the end.
    blocks = scaled(phrase, 350).reshape(-1, 160)
    audible_end = 1100 + 2 * (1 + np.flatnonzero(np.mean(blocks**2, axis=1) > 65**2)[-1])
    assert len(joined) == 1 and abs(joined[0][1] - audible_end) <= 20, joined
    assert len(split) == 2 and split[0][1] <= 310, split
    long_clip = intervals(dll, np.concatenate((pad, np.tile(phrase, 35), pad)))
    assert len(long_clip) == 1 and long_clip[0][1] - long_clip[0][0] > 6900, long_clip
    print("PASS quiet second phrase, 8 s / 11 s pauses, trimmed ends, 70 s continuous conversation")


if __name__ == "__main__":
    main()
