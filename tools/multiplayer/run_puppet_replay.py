"""Automated M1 puppet-replay run (no person needed).

Starts a New Game in a hidden (or, with --show, visible), input-isolated, muted game process with
a private config and save folder, walks a scripted path through the opening C-17 with the test
gamepad file, lets the puppet (developer option puppet_replay) replay it, captures screenshots,
stops the game and scores the replay from the preview logs. With --show the window opens without
taking focus, and keyboard, mouse and controllers still don't reach the game.

Usage (from the repo root):
  .venv\\Scripts\\python.exe tools\\multiplayer\\run_puppet_replay.py --name m1-replay-01
  options: --show --delay-ms 3000 --yaw-mode 1 --correct-cm 200 --timeout 420

Output: artifacts/test-runs/<name>/ with preview-logs, gamepad-*.bmp/png captures and summary.json.
"""
import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build/mercenaries/bin/Release/mercenaries_recomp.exe"
GAME = ROOT / "game_files/mercenaries-retail"
LOG_LINE = re.compile(r"^\[(\d+)\] \[([^\]]+)\] (.*)$")
FULL = 32767
SW_SHOWNOACTIVATE = 4  # the window's first ShowWindow takes this from STARTUPINFO

# Scripted walk: (label, seconds, lx, ly, rx, ry, capture-at-end). Full stick = run.
SCRIPT = [
    ("forward", 3.0, 0, FULL, 0, 0, False),
    ("stop", 1.5, 0, 0, 0, 0, False),
    ("turn-right", 1.2, 0, 0, FULL, 0, True),
    ("forward-2", 3.0, 0, FULL, 0, 0, True),
    ("strafe-left", 2.0, -FULL, 0, 0, 0, False),
    ("backward", 2.0, 0, -FULL, 0, 0, False),
    ("stop-2", 1.0, 0, 0, 0, 0, True),
]


def fields(text):
    return dict(re.findall(r"(\w+)=(\S+)", text))


def vec(text):
    return [float(v) for v in text.split(",")]


class Run:
    def __init__(self, args):
        self.args = args
        self.dir = ROOT / "artifacts/test-runs" / args.name
        self.pad = self.dir / "gamepad-command.txt"
        self.seq = 0
        self.process = None

    # --- setup and process -------------------------------------------------------------------
    def developer_ini(self):
        return ("[Developer]\n"
                "developer_menu=0\nlogging=1\nplayer_state_log=1\nplayer_state_log_hz=10\n"
                f"puppet_replay=1\npuppet_replay_delay_ms={self.args.delay_ms}\n"
                f"puppet_correct_cm={self.args.correct_cm}\npuppet_yaw_mode={self.args.yaw_mode}\n")

    def recomp_ini(self):
        """Contents for the run's mercenaries_recomp.ini, or None to let the game create it."""
        return None

    def prepare(self):
        if self.dir.exists():
            sys.exit(f"Use a new run name: {self.dir} exists")
        self.dir.mkdir(parents=True)
        (self.dir / "developer.ini").write_text(self.developer_ini(), encoding="ascii")
        if self.recomp_ini():
            (self.dir / "mercenaries_recomp.ini").write_text(self.recomp_ini(), encoding="ascii")
        self.pad.write_text("0 1 1 0 0 0 0 0 0 0\n", encoding="ascii")  # seq 0 is ignored
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith("MERCENARIES_")}
        env.update({
            "MERCENARIES_CONFIG_ROOT": str(self.dir),
            "MERCENARIES_PREVIEW_LOG_DIR": str(self.dir / "preview-logs"),
            "MERCENARIES_TEST_SAVE_DIR": str(self.dir / "saves"),
            "MERCENARIES_TEST_ISOLATE_INPUT": "1",
            "MERCENARIES_TEST_MUTE_HOST_AUDIO": "1",
            "MERCENARIES_TEST_EXACT_NEW_GAME_SEQUENCE": "1",
            "MERCENARIES_TEST_EXACT_START_DELAY_MS": "45000",
            "MERCENARIES_TEST_EXACT_A1_DELAY_MS": "65000",
            "MERCENARIES_TEST_EXACT_A2_DELAY_MS": "70000",
            "MERCENARIES_TEST_EXACT_A3_DELAY_MS": "75000",
            "MERCENARIES_TEST_EXACT_PULSE_MS": "300",
            "MERCENARIES_TEST_SKIP_MOVIE_AFTER_POLLS": "30",
            "MERCENARIES_TEST_AUTO_Y": "1",
            "MERCENARIES_TEST_AUTO_Y_AFTER_USE_ONLY": "1",
            "MERCENARIES_TEST_AUTO_Y_ONCE": "1",
            "MERCENARIES_TEST_AUTO_Y_DELAY_MS": "600",
            "MERCENARIES_TEST_AUTO_Y_PULSE_MS": "500",
            "MERCENARIES_TEST_AUTO_Y_STOP_AFTER_STANDUP": "1",
            "MERCENARIES_TEST_GAMEPAD_FILE": str(self.pad),
            "MERCENARIES_CAPTURE_TEST_GAMEPAD_PREFIX": str(self.dir / "gamepad"),
            "MERCENARIES_DUMP_CRASH_GUEST_PATH": str(self.dir / "crash-guest.bin"),
        })
        if not self.args.show:
            env["MERCENARIES_HIDE_WINDOW"] = "1"
        (self.dir / "settings.json").write_text(json.dumps(
            {k: v for k, v in env.items() if k.startswith("MERCENARIES_")}, indent=2), encoding="utf-8")
        return env

    def launch(self, env):
        out = open(self.dir / "stdout.log", "wb")
        err = open(self.dir / "stderr.log", "wb")
        startup = None
        if self.args.show:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = SW_SHOWNOACTIVATE
        self.process = subprocess.Popen([str(EXE), str(GAME)], cwd=str(ROOT), env=env, stdout=out, stderr=err,
                                        startupinfo=startup)
        (self.dir / "pid.txt").write_text(str(self.process.pid), encoding="ascii")
        print(f"launched pid {self.process.pid}; run dir {self.dir}")

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=30)

    def alive(self):
        return self.process is not None and self.process.poll() is None

    # --- logs and controller ------------------------------------------------------------------
    def events(self):
        out = []
        for path in sorted((self.dir / "preview-logs").glob("preview-*.log")):
            for raw in path.read_text(errors="replace").splitlines():
                m = LOG_LINE.match(raw)
                if m:
                    out.append((int(m.group(1)), m.group(2), m.group(3)))
        return out

    def player_samples(self):
        return [(t, fields(body)) for t, cat, body in self.events()
                if cat == "player-state" and "ctrl=" in body]

    def send(self, hold_ms, lx=0, ly=0, rx=0, ry=0, buttons=0, pressure=0, capture=False):
        self.seq += 1
        temp = self.pad.with_suffix(".tmp")
        temp.write_text(f"{self.seq} 1 {hold_ms} {buttons:02X} {pressure:02X} {lx} {ly} {rx} {ry} "
                        f"{1 if capture else 0}\n", encoding="ascii")
        os.replace(temp, self.pad)
        return self.seq

    def hold(self, seconds, lx=0, ly=0, rx=0, ry=0, capture=False):
        """Hold a stick state for `seconds`, refreshing the 2 s command limit; capture at the end.

        The game saves a capture 200 ms after the command's hold expires and drops it if a newer
        command arrives first, so a capture ends with the stick released for half a second.
        """
        end = time.monotonic() + seconds
        while True:
            left = end - time.monotonic()
            if left <= 0:
                break
            last = left <= 0.6
            self.send(max(1, int(min(left, 0.6) * 1000)) if last else 1000, lx, ly, rx, ry,
                      capture=capture and last)
            time.sleep(min(left, 0.5))
        if capture:
            time.sleep(0.5)

    def wait_for(self, predicate, timeout, what):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if not self.alive():
                raise RuntimeError(f"game exited while waiting for {what}")
            value = predicate()
            if value:
                return value
            time.sleep(1.0)
        raise RuntimeError(f"timed out waiting for {what}")

    # --- scenario -----------------------------------------------------------------------------
    def gameplay_ready(self):
        samples = [f for _, f in self.player_samples() if f.get("state") == "C2CBD863"]
        return len(samples) >= 50  # ~5 s of normal gameplay at 10 Hz

    def stderr_has(self, text):
        path = self.dir / "stderr.log"
        return path.exists() and text in path.read_text(errors="replace")

    def position(self):
        samples = self.player_samples()
        return vec(samples[-1][1]["pos"]) if samples else None

    def ensure_control(self):
        """Confirm the stick moves the player; press Y (use/stand up) and retry if not."""
        for attempt in range(4):
            time.sleep(1.0 if attempt == 0 else 6.0)
            before = self.position()
            self.hold(1.5, ly=FULL)
            self.hold(0.8)
            time.sleep(1.0)
            after = self.position()
            moved = before and after and math.dist(before[::2], after[::2])
            print(f"control check {attempt}: moved {moved or 0:.2f} m")
            if moved and moved > 0.5:
                return True
            self.send(600, pressure=0x08)  # Y
        return False

    def run_script(self, script=SCRIPT):
        timeline = []
        for label, seconds, lx, ly, rx, ry, capture in script:
            start = self.player_samples()[-1][0]
            self.hold(seconds, lx, ly, rx, ry, capture=capture)
            timeline.append({"phase": label, "log_ms": start, "seconds": seconds})
            print(f"phase {label} done")
        return timeline

    # --- analysis -----------------------------------------------------------------------------
    def analyse(self, timeline):
        events = self.events()
        replay = [(t, fields(body)) for t, cat, body in events if cat == "puppet-replay"]
        puppet_events = [(t, body) for t, cat, body in events if cat == "puppet"]
        active = [(t, f) for t, f in replay if f.get("actor", "00000000") != "00000000"]
        errors = sorted(float(f["err"]) for _, f in active if float(f["err"]) >= 0)
        # Replaying the player's own path, the puppet's target is the player whenever the player
        # stands still there; the two can't overlap, so score those samples separately.
        free = sorted(float(f["err"]) for _, f in active if float(f["err"]) >= 0 and f.get("occupied") != "1")
        yaw = sorted(abs(float(f["yaw_err"])) for _, f in active)
        teleports = max((int(f["teleports"]) for _, f in replay), default=0)
        stuck_teleports = max((int(f.get("stuck_teleports", 0)) for _, f in replay), default=0)

        def pct(values, p):
            return values[min(len(values) - 1, int(p * len(values)))] if values else None

        def path_length(points):
            return sum(math.dist(a, b) for a, b in zip(points, points[1:]))

        puppet_path = [vec(f["pos"])[::2] for _, f in active]
        target_path = [vec(f["rec"])[::2] for _, f in active]
        # Motion between consecutive samples without a teleport, where the recording moves.
        ratios, directions = [], []
        for (_, a), (_, b) in zip(active, active[1:]):
            dt = float(b["t"]) - float(a["t"])
            if dt <= 0 or a["teleports"] != b["teleports"]:
                continue
            pv = [(q - p) / dt for p, q in zip(vec(a["pos"])[::2], vec(b["pos"])[::2])]
            tv = [(q - p) / dt for p, q in zip(vec(a["rec"])[::2], vec(b["rec"])[::2])]
            ts, ps = math.hypot(*tv), math.hypot(*pv)
            if ts < 0.5:
                continue
            ratios.append(ps / ts)
            if ps > 0.1:
                cos = max(-1.0, min(1.0, (pv[0] * tv[0] + pv[1] * tv[1]) / (ps * ts)))
                directions.append(math.degrees(math.acos(cos)))
        ratios.sort()
        directions.sort()
        gaits = [(f["gait"], f["rec_gait"]) for _, f in active if "rec_gait" in f]
        summary = {
            "spawned": any("spawned" in body for _, body in puppet_events),
            "puppet_events": [f"[{t}] {body}" for t, body in puppet_events],
            "ai_zero": all(f.get("ai") == "00000000" for _, f in active) if active else None,
            "active_samples": len(active),
            "error_m": {"median": pct(errors, 0.5), "p90": pct(errors, 0.9), "max": errors[-1] if errors else None},
            "within_1m": sum(e <= 1.0 for e in errors) / len(errors) if errors else None,
            "unoccupied_samples": len(free),
            "unoccupied_error_m": {"median": pct(free, 0.5), "p90": pct(free, 0.9), "max": free[-1] if free else None},
            "unoccupied_within_1m": sum(e <= 1.0 for e in free) / len(free) if free else None,
            "yaw_error_deg": {"median": pct(yaw, 0.5), "p90": pct(yaw, 0.9)},
            "teleports": teleports,
            "stuck_teleports": stuck_teleports,
            "moving_samples": len(ratios),
            "speed_ratio": {"median": pct(ratios, 0.5), "p10": pct(ratios, 0.1), "p90": pct(ratios, 0.9)},
            "direction_error_deg": {"median": pct(directions, 0.5), "p90": pct(directions, 0.9)},
            "gait_match": sum(a == b for a, b in gaits) / len(gaits) if gaits else None,
            "puppet_path_m": path_length(puppet_path),
            "target_path_m": path_length(target_path),
            "fed_controls": sum(int(f.get("fed", 0)) for _, f in active),
            "control_ids": sorted({i for _, f in active for i in f.get("ids", "-").split(",") if i != "-"}),
            "timeline": timeline,
        }
        (self.dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        return summary


def bmp_to_png(src, dst):
    data = src.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    step = struct.unpack_from("<H", data, 28)[0] // 8
    bottom_up, height = height > 0, abs(height)
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(height):
        sy = height - 1 - y if bottom_up else y
        row = data[offset + sy * stride: offset + sy * stride + width * step]
        rgb = bytearray(width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::step], row[1::step], row[0::step]
        rows.append(b"\0" + bytes(rgb))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    dst.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", required=True)
    parser.add_argument("--show", action="store_true",
                        help="show the game window (it doesn't take focus; your input still doesn't reach it)")
    parser.add_argument("--delay-ms", type=int, default=3000)
    parser.add_argument("--yaw-mode", type=int, default=1, choices=(0, 1, 2))
    parser.add_argument("--correct-cm", type=int, default=200)
    parser.add_argument("--timeout", type=int, default=420, help="seconds to wait for gameplay")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name):
        sys.exit("run name: letters, digits, . _ - only")
    run = Run(args)
    env = run.prepare()
    run.launch(env)
    timeline, summary = [], None
    try:
        run.wait_for(run.gameplay_ready, args.timeout, "normal gameplay")
        print("gameplay reached")
        # The opening briefing keeps the player seated until its "Stand Up" prompt; the
        # automatic Y (MERCENARIES_TEST_AUTO_Y_AFTER_USE_ONLY) then stands them up. Any manual
        # gamepad command before that would suppress the automatic route, so wait for it.
        run.wait_for(lambda: run.stderr_has("Stand Up prompt ready"), 150, "the stand-up prompt")
        print("stand-up prompt ready; waiting for the stand-up")
        time.sleep(10.0)
        if not run.ensure_control():
            raise RuntimeError("the stick never moved the player")
        timeline = run.run_script()
        run.hold(args.delay_ms / 1000.0 + 2.0)
        run.send(300, capture=True)
        time.sleep(2.0)
    except Exception as error:  # keep the logs: analyse what happened
        print(f"run problem: {error}")
        timeline.append({"error": str(error)})
    finally:
        run.stop()
        summary = run.analyse(timeline)
        for bmp in sorted(run.dir.glob("gamepad-*.bmp")):
            bmp_to_png(bmp, bmp.with_suffix(".png"))
    print(json.dumps({k: v for k, v in summary.items() if k not in ("timeline", "puppet_events")}, indent=2))
    for line in summary["puppet_events"][:10]:
        print(line)


if __name__ == "__main__":
    main()
