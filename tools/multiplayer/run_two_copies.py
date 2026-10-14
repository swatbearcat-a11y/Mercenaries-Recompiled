"""Automated M2 run: two copies of the game connected on this PC (no person needed).

Starts a host and a client (each a New Game in its own input-isolated, muted process with a private
config, save and log folder, as run_puppet_replay.py does), connected over loopback through
mercenaries_recomp.ini [Multiplayer]. The host walks the scripted path through the opening C-17, then
the client walks a shorter one; each draws the other as a puppet. Screenshots come from the copy that
is watching. Then the client is killed without a goodbye, and the host must time it out and remove its
puppet. Both are stopped and the session is scored from the preview logs.

Usage (from the repo root):
  .venv\\Scripts\\python.exe tools\\multiplayer\\run_two_copies.py --name m2-net-01
  options: --show --port 27410 --interp-ms 100 --timeout 420

Output: artifacts/test-runs/<name>/{host,client}/ with each copy's logs and captures, and
artifacts/test-runs/<name>/summary.json.
"""
import argparse
import bisect
import json
import math
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_puppet_replay import FULL, ROOT, SCRIPT, Run, bmp_to_png, fields, vec  # noqa: E402

# The client's walk, after the host's: (label, seconds, lx, ly, rx, ry, capture-at-end).
CLIENT_SCRIPT = [
    ("forward", 2.5, 0, FULL, 0, 0, False),
    ("turn-left", 1.0, 0, 0, -FULL, 0, False),
    ("forward-2", 2.5, 0, FULL, 0, 0, False),
    ("stop", 1.0, 0, 0, 0, 0, False),
]


class Copy(Run):
    def __init__(self, args, role):
        super().__init__(args)
        self.role = role
        self.dir = ROOT / "artifacts/test-runs" / args.name / role
        self.pad = self.dir / "gamepad-command.txt"

    def developer_ini(self):
        return "[Developer]\ndeveloper_menu=0\nlogging=1\nplayer_state_log=1\nplayer_state_log_hz=10\n"

    def recomp_ini(self):
        if self.role == "host":
            return (f"[Multiplayer]\nmode=host\nbind=127.0.0.1\nport={self.args.port}\n"
                    f"interp_ms={self.args.interp_ms}\nname=host\n")
        return (f"[Multiplayer]\nmode=join\naddress=127.0.0.1\nport={self.args.port}\n"
                f"interp_ms={self.args.interp_ms}\nname=client\n")

    def capture_now(self):
        """Screenshot without moving: a 1 ms neutral hold, then the 200 ms capture delay."""
        self.send(1, capture=True)


def read_logs(copy):
    out = []
    for path in sorted((copy.dir / "preview-logs").glob("preview-*.log")):
        for raw in path.read_text(errors="replace").splitlines():
            m = re.match(r"^\[(\d+)\] \[([^\]]+)\] (.*)$", raw)
            if m:
                out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(p * len(values)))] if values else None


def path_distance(point, path):
    """Distance in x/z from a point to a polyline."""
    best = math.inf
    for (ax, az), (bx, bz) in zip(path, path[1:]):
        dx, dz = bx - ax, bz - az
        length = dx * dx + dz * dz
        u = 0.0 if length == 0 else max(0.0, min(1.0, ((point[0] - ax) * dx + (point[1] - az) * dz) / length))
        best = min(best, math.hypot(point[0] - ax - u * dx, point[1] - az - u * dz))
    return best


def direction(sender, receiver, sender_player):
    """How the receiver draws the sender: its mp-puppet samples for that player against the sender's own path."""
    sender_logs, receiver_logs = read_logs(sender), read_logs(receiver)
    path = [vec(fields(body)["pos"])[::2] for _, cat, body in sender_logs if cat == "player-state" and "pos=" in body]
    own = [(t, vec(fields(body)["pos"])) for t, cat, body in receiver_logs if cat == "player-state" and "pos=" in body]
    own_times = [t for t, _ in own]
    samples = [(t, fields(body)) for t, cat, body in receiver_logs
               if cat == "mp-puppet" and body.startswith(f"player={sender_player} ")]
    active = [(t, f) for t, f in samples if f.get("actor", "00000000") != "00000000"]
    free, deviations, behind, age = [], [], [], []
    for t, f in active:
        rec = vec(f["rec"])
        deviations.append(path_distance(rec[::2], path) if len(path) > 1 else None)
        if own:
            i = min(bisect.bisect_left(own_times, t), len(own) - 1)
            standing_on = math.dist(own[i][1][::2], rec[::2]) < 1.2
        else:
            standing_on = False
        if f.get("occupied") != "1" and not standing_on:
            free.append(float(f["err"]))
        behind.append(float(f["behind_ms"]))
        age.append(float(f["age_ms"]))
    deviations = [d for d in deviations if d is not None]
    events = [body for _, cat, body in receiver_logs if cat in ("puppet", "multiplayer") and not body.startswith("t=")]
    return {
        "samples": len(active),
        "updates": int(active[-1][1]["updates"]) if active else 0,
        "spawned": any(f"name=mp_player_{sender_player}" in e and e.startswith("spawned") for e in events),
        "error_free_m": {"median": pct(free, 0.5), "p90": pct(free, 0.9), "max": max(free) if free else None,
                         "count": len(free)},
        "target_off_sender_path_m": {"median": pct(deviations, 0.5), "p90": pct(deviations, 0.9),
                                     "max": max(deviations) if deviations else None},
        "behind_ms": {"median": pct(behind, 0.5), "p10": pct(behind, 0.1), "p90": pct(behind, 0.9)},
        "update_age_ms": {"median": pct(age, 0.5), "p90": pct(age, 0.9)},
        "teleports": max((int(f["teleports"]) for _, f in active), default=0),
        "stuck_teleports": max((int(f["stuck_teleports"]) for _, f in active), default=0),
        "events": events[:20],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", required=True)
    parser.add_argument("--show", action="store_true",
                        help="show both game windows (they don't take focus; your input still doesn't reach them)")
    parser.add_argument("--port", type=int, default=27410)
    parser.add_argument("--interp-ms", type=int, default=100)
    parser.add_argument("--timeout", type=int, default=420, help="seconds to wait for gameplay")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name):
        sys.exit("run name: letters, digits, . _ - only")
    # Run's own options, unused here.
    args.delay_ms, args.correct_cm, args.yaw_mode = 3000, 200, 1
    top = ROOT / "artifacts/test-runs" / args.name
    if top.exists():
        sys.exit(f"Use a new run name: {top} exists")
    host, client = Copy(args, "host"), Copy(args, "client")
    envs = [host.prepare(), client.prepare()]
    host.launch(envs[0])
    time.sleep(2.0)
    client.launch(envs[1])
    timeline = []
    try:
        for copy in (host, client):
            copy.wait_for(copy.gameplay_ready, args.timeout, f"{copy.role} gameplay")
        print("both in gameplay")
        for copy in (host, client):
            copy.wait_for(lambda: copy.stderr_has("Stand Up prompt ready"), 150, f"{copy.role} stand-up prompt")
        time.sleep(10.0)
        for copy in (host, client):
            if not copy.ensure_control():
                raise RuntimeError(f"the stick never moved the {copy.role} player")
        timeline.append({"host": host.run_script(), "at": "host walks"})
        client.capture_now()
        time.sleep(1.0)
        timeline.append({"client": client.run_script(CLIENT_SCRIPT), "at": "client walks"})
        host.capture_now()
        time.sleep(3.0)
        client.capture_now()
        host.capture_now()
        time.sleep(2.0)
        # The client vanishes without saying goodbye: the host must time it out and remove its puppet.
        client.stop()
        left = time.monotonic()
        host.wait_for(lambda: any(cat == "puppet" and body.startswith("removed name=mp_player_1")
                                  for _, cat, body in host.events()), 30, "the host removing the client's puppet")
        timeline.append({"at": "client killed", "puppet_removed_after_s": round(time.monotonic() - left, 1)})
        print(f"host removed the client's puppet {time.monotonic() - left:.1f} s after the client was killed")
        time.sleep(2.0)
    except Exception as error:  # keep the logs: analyse what happened
        print(f"run problem: {error}")
        timeline.append({"error": str(error)})
    finally:
        host.stop()
        client.stop()
        for copy in (host, client):
            for bmp in sorted(copy.dir.glob("gamepad-*.bmp")):
                bmp_to_png(bmp, bmp.with_suffix(".png"))
    summary = {
        "host_drawn_by_client": direction(host, client, 0),
        "client_drawn_by_host": direction(client, host, 1),
        "timeline": timeline,
    }
    (top / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "events"} for k, v in summary.items()
                      if k != "timeline"}, indent=2))
    for line in summary["host_drawn_by_client"]["events"][:8]:
        print("client:", line)
    for line in summary["client_drawn_by_host"]["events"][:8]:
        print("host:", line)


if __name__ == "__main__":
    main()
