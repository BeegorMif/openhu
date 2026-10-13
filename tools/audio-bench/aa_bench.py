#!/usr/bin/env python3
"""Android Auto audio with the real phone: records the default sink's monitor and measures
- dropouts while the phone plays continuously (runs of digital silence inside music),
- pause latency: input.action media.pause -> audio stops at the sink,
- play latency: input.action media.play -> audio starts at the sink.
The phone's reaction time is in both latencies, the same for every OpenHU build: compare runs of two builds
with the same phone and player.

    aa_bench.py [--label NAME] [--steady 90] [--cycles 10]
Run as the session user (XDG_RUNTIME_DIR=/run/user/<uid>), music playing on the phone.
"""
import argparse
import array
import json
import statistics
import subprocess
import threading
import time

from websockets.sync.client import connect

RATE = 48000
CHUNK = RATE // 200  # 5 ms
SILENT = 4           # |sample| below: digital silence
SOUND = 200          # |sample| above: audio


class Recorder(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.samples = array.array("h")
        self.stamps = []  # (index after the chunk, monotonic time of its last sample)
        self.lock = threading.Lock()
        self.proc = subprocess.Popen(
            ["parec", "--raw", "--format=s16le", f"--rate={RATE}", "--channels=1", "--latency-msec=5",
             "-d", "@DEFAULT_MONITOR@"], stdout=subprocess.PIPE)

    def run(self):
        while True:
            data = self.proc.stdout.read(CHUNK * 2)
            if not data:
                break
            t = time.monotonic()
            with self.lock:
                self.samples.frombytes(data[: len(data) // 2 * 2])
                self.stamps.append((len(self.samples), t))

    def now_index(self):
        with self.lock:
            return len(self.samples)

    def time_of(self, idx):
        with self.lock:
            for end, t in self.stamps:
                if end > idx:
                    return t - (end - 1 - idx) / RATE
            end, t = self.stamps[-1]
            return t - (end - 1 - idx) / RATE

    def index_at(self, t):
        """first sample recorded at or after monotonic time t"""
        with self.lock:
            for end, ts in self.stamps:
                if ts >= t:
                    return max(end - CHUNK, end - CHUNK + int((t - (ts - (CHUNK - 1) / RATE)) * RATE))
            return len(self.samples)


class Api:
    def __init__(self):
        self.ws = connect("ws://127.0.0.1:5520", max_size=None)
        self.ws.send(json.dumps({"type": "subscribe", "topics": ["media.status", "projection.state"]}))
        self.id = 0
        self.state = {}
        self.lock = threading.Lock()
        self.replies = {}
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.ws:
            m = json.loads(raw)
            if "id" in m:
                self.replies[m["id"]] = m
            elif m.get("topic"):
                with self.lock:
                    self.state[m["topic"]] = m.get("data") or {}

    def call(self, method, params=None):
        self.id += 1
        i = self.id
        t = time.monotonic()
        self.ws.send(json.dumps({"type": "request", "id": i, "method": method, "params": params or {}}))
        while i not in self.replies:
            time.sleep(0.001)
        return t, self.replies.pop(i)

    def get(self, topic):
        with self.lock:
            return dict(self.state.get(topic, {}))


def wait(cond, timeout, what):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.05)
    raise SystemExit(f"timeout: {what}")


def first(rec, start, pred, hold=0):
    """first index >= start where pred holds for hold samples in a row (waits for the recording)"""
    i, run_start, deadline = start, None, time.monotonic() + 8
    while time.monotonic() < deadline:
        n = rec.now_index()
        with rec.lock:
            s = rec.samples
            while i < n:
                if pred(s[i]):
                    if run_start is None:
                        run_start = i
                    if i - run_start + 1 >= max(1, hold):
                        return run_start
                else:
                    run_start = None
                i += 1
        time.sleep(0.01)
    return None


def dropouts(rec, a, b):
    """runs of >= 1 ms digital silence between indices a and b"""
    gaps, run = [], 0
    with rec.lock:
        for x in rec.samples[a:b]:
            if abs(x) < SILENT:
                run += 1
            else:
                if run >= RATE // 1000:
                    gaps.append(run * 1000 / RATE)
                run = 0
    return gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="openhu")
    ap.add_argument("--steady", type=int, default=90)
    ap.add_argument("--cycles", type=int, default=10)
    o = ap.parse_args()

    api = Api()
    rec = Recorder()
    rec.start()
    time.sleep(0.5)

    wait(lambda: (api.call("bus.get", {"topic": "projection.state"})[1].get("result") or {}).get("connected"), 60,
         "Android Auto connected")
    time.sleep(3)
    # music on
    for _ in range(5):
        api.call("input.action", {"action": "media.play", "phase": "click"})
        if first(rec, rec.now_index(), lambda x: abs(x) > SOUND, 1) is not None:
            break
    time.sleep(3)

    # continuous playback
    a = rec.now_index()
    time.sleep(o.steady)
    b = rec.now_index()
    steady = dropouts(rec, a, b)

    pause, play, cycle_gaps = [], [], []
    for c in range(o.cycles):
        t, _ = api.call("input.action", {"action": "media.pause", "phase": "click"})
        i = first(rec, rec.index_at(t), lambda x: abs(x) < SILENT, RATE // 2)  # 500 ms silence
        pause.append((rec.time_of(i) - t) * 1000 if i is not None else None)
        time.sleep(2.5)
        t, _ = api.call("input.action", {"action": "media.play", "phase": "click"})
        i = first(rec, rec.index_at(t), lambda x: abs(x) > SOUND, 1)
        play.append((rec.time_of(i) - t) * 1000 if i is not None else None)
        time.sleep(1)
        a = rec.now_index()
        time.sleep(4)
        cycle_gaps += dropouts(rec, a, rec.now_index())
        print(f"  cycle {c + 1}: pause {pause[-1]:.0f} ms, play {play[-1]:.0f} ms", flush=True)

    rec.proc.terminate()

    def stats(v):
        v = [x for x in v if x is not None]
        return (f"median {statistics.median(v):.0f} ms, min {min(v):.0f}, max {max(v):.0f} (n={len(v)})"
                if v else "none")

    short = [g for g in steady if g < 100]
    print(f"{o.label}")
    print(f"  steady {o.steady} s: {len(short)} silences < 100 ms ({sum(short):.1f} ms), "
          f"{len(steady) - len(short)} longer (longest {max(steady, default=0):.0f} ms)")
    print(f"  pause -> silence: {stats(pause)}")
    print(f"  play  -> sound:   {stats(play)}")
    print(f"  after play (4 s x {o.cycles}): {len(cycle_gaps)} dropouts, {sum(cycle_gaps):.1f} ms")


if __name__ == "__main__":
    main()
