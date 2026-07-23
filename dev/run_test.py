#!/usr/bin/env python3
# One run of one integration scenario, emitting a JSON verdict.
#
# This is a graph node's command (see build.py): the compositor and the test
# clients are dependencies, so by the time we run they are built. We start a
# fresh headless compositor in its own scratch dir (TMPDIR == XDG_RUNTIME_DIR
# == cwd), wait until it is fully up, run the scenario with the environment
# from dev/tests/lib.sh, and write {status, seconds, detail, ...} to --out.
#
# We ALWAYS exit 0: a scenario failure is recorded in the JSON, not in the
# process exit code, so the build graph does not abort and every test still
# runs. The aggregator node reads all the JSONs and produces the verdict that
# fails the build.

import argparse
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import time

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


def parse_header(path: str) -> dict:
    xfail = None
    args: list[str] = []
    extra_env: dict[str, str] = {}
    pre: list[str] = []
    expect_exit = False
    private_bus = False
    with open(path) as f:
        for line in f.read(2048).splitlines():
            m = re.match(r"\s*#\s*xfail:\s*(.*)", line)
            if m and xfail is None:
                xfail = m.group(1).strip() or "expected failure"
            m = re.match(r"\s*#\s*imway-args:\s*(.*)", line)
            if m:
                args = shlex.split(m.group(1))
            # raw shell run in the scratch dir before the compositor starts:
            # scenarios that need files in place at init time (xdg data dirs
            # for the icon store) stage them here
            m = re.match(r"\s*#\s*imway-pre:\s*(.*)", line)
            if m and m.group(1).strip():
                pre.append(m.group(1).strip())
            m = re.match(r"\s*#\s*imway-env:\s*(.*)", line)
            if m:
                for item in shlex.split(m.group(1)):
                    key, sep, value = item.partition("=")
                    if not sep or not key:
                        raise ValueError(f"{path}: bad imway-env item: {item}")
                    extra_env[key] = value
            if re.match(r"\s*#\s*expect-compositor-exit\s*$", line):
                expect_exit = True
            if re.match(r"\s*#\s*private-session-bus\s*$", line):
                private_bus = True
    return dict(xfail=xfail, args=args, env=extra_env, pre=pre,
                expect_exit=expect_exit, private_bus=private_bus)


def is_sock(p: str) -> bool:
    try:
        return stat.S_ISSOCK(os.stat(p).st_mode)
    except OSError:
        return False


def is_fifo(p: str) -> bool:
    try:
        return stat.S_ISFIFO(os.stat(p).st_mode)
    except OSError:
        return False


def dump_stacks(pid: int, path: str) -> None:
    try:
        cp = subprocess.run(
            ["gdb", "-p", str(pid), "-batch", "-nx",
             "-ex", "set pagination off",
             "-ex", "thread apply all bt full",
             "-ex", "detach", "-ex", "quit"],
            timeout=30, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        out = cp.stdout
    except Exception as e:
        out = f"gdb dump failed: {e}\n"
    with open(path, "w") as f:
        f.write(out)


def tail(path: str, n: int = 25) -> str:
    try:
        with open(path, errors="replace") as f:
            return "".join(f.readlines()[-n:])
    except OSError:
        return ""


def last_line(s: str) -> str:
    lines = [l for l in s.strip().splitlines() if l.strip()]
    return lines[-1] if lines else ""


def collect(rt: str, shell_out: str) -> dict[str, str]:
    # embed the diagnostic files the compositor/scenario left behind, so the
    # verdict is self-contained and the scratch dir can always be removed
    arts: dict[str, str] = {}
    if shell_out.strip():
        arts["scenario.out"] = shell_out[-4000:]
    for fn, n in (("imway.log", 200), ("client.log", 20),
                  ("final-state.txt", 40), ("gdb-stacks.txt", 120)):
        text = tail(os.path.join(rt, fn), n)
        if text.strip():
            arts[fn] = text
    return arts


def run(imway: str, scenario: str, client: str, meta: dict,
        timeout: float) -> dict:
    name = os.path.basename(scenario)[:-3]
    rt = tempfile.mkdtemp(prefix=f"imway-{name}-")
    log = os.path.join(rt, "imway.log")
    ctl = os.path.join(rt, "ctl")
    sock = os.path.join(rt, "imway-test")
    client_log = os.path.join(rt, "client.log")

    env = dict(os.environ)
    bus_pid = 0

    if meta["private_bus"]:
        try:
            dbus_daemon = shutil.which("dbus-daemon")
            if not dbus_daemon:
                for candidate in ("/usr/bin/dbus-daemon", "/ix/realm/pg/bin/dbus-daemon"):
                    if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                        dbus_daemon = candidate
                        break
            if not dbus_daemon:
                raise FileNotFoundError("dbus-daemon")
            bus_config = os.path.join(rt, "session-bus.conf")
            with open(bus_config, "w") as f:
                f.write(f"""<!DOCTYPE busconfig PUBLIC '-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN'
 'http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd'>
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir={rt}</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
""")
            bus = subprocess.run(
                [dbus_daemon, f"--config-file={bus_config}", "--fork",
                 "--print-address=1", "--print-pid=1"],
                timeout=5, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            lines = bus.stdout.strip().splitlines()
            env["DBUS_SESSION_BUS_ADDRESS"] = lines[0]
            bus_pid = int(lines[1])
        except Exception as e:
            stderr = getattr(e, "stderr", "") or ""
            shutil.rmtree(rt, ignore_errors=True)
            return dict(status=FAIL, seconds=0.0,
                        detail=f"private session bus failed: {e} {stderr.strip()}",
                        artifacts={})

    def stop_bus() -> None:
        nonlocal bus_pid
        if bus_pid:
            try:
                os.kill(bus_pid, 15)
            except ProcessLookupError:
                pass
            bus_pid = 0

    env.update(
        XDG_RUNTIME_DIR=rt,
        WAYLAND_DISPLAY="imway-test",
        IMWAY_CTL=ctl,
        IMWAY_LOG=log,
        IMWAY_CLIENT=client,
        IMWAY_CLIENT_LOG=client_log,
        # hermetic audio: the host's sndiod/pulseaudio must not be a hidden
        # input of every scenario (mixer presence shifts frame counts); both
        # point at addresses that fail fast, imway-env can override
        AUDIODEVICE="snd@127.0.0.1,9/0",
        PULSE_SERVER="unix:/nonexistent-imway-test",
    )
    env.update(meta["env"])

    started = time.monotonic()

    def fail(detail: str) -> dict:
        arts = collect(rt, "")
        shutil.rmtree(rt, ignore_errors=True)
        return dict(status=FAIL, seconds=time.monotonic() - started, detail=detail, artifacts=arts)

    for cmd in meta["pre"]:
        cp = subprocess.run(["/bin/sh", "-c", cmd], cwd=rt, env=env, timeout=30,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if cp.returncode != 0:
            stop_bus()
            return fail(f"imway-pre failed (rc={cp.returncode}): {cmd}\n{cp.stdout}")

    logf = open(log, "w")
    proc = subprocess.Popen(
        [imway, "--device", "headless", "--socket", "imway-test", "--control", ctl] + meta["args"],
        cwd=rt, env=env, stdout=logf, stderr=subprocess.STDOUT,
    )
    env["IMWAY_PID"] = str(proc.pid)

    ready = False
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            logf.close()
            stop_bus()
            return fail(f"compositor exited during startup (rc={proc.returncode})")
        if is_sock(sock) and is_fifo(ctl) and "control FIFO:" in tail(log, 200):
            ready = True
            break
        time.sleep(0.05)
    if not ready:
        proc.kill()
        proc.wait()
        logf.close()
        stop_bus()
        return fail("compositor did not come up")

    shell_out = ""
    rc = 0
    try:
        cp = subprocess.run(
            ["timeout", "30s", "bash", scenario], cwd=rt, env=env, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        rc, shell_out = cp.returncode, cp.stdout
    except subprocess.TimeoutExpired as e:
        shell_out = (e.stdout.decode() if isinstance(e.stdout, bytes) else e.stdout) or ""
        rc = -1
        shell_out += f"\n[runner] scenario timed out after {timeout}s\n"

    if rc not in (0, 127) and proc.poll() is None and is_fifo(ctl):
        final_state = os.path.join(rt, "final-state.txt")
        try:
            fd = os.open(ctl, os.O_WRONLY | os.O_NONBLOCK)
        except OSError:
            fd = -1
        if fd >= 0:
            try:
                os.write(fd, f"dump {final_state}\n".encode())
                for _ in range(20):
                    if os.path.exists(final_state):
                        break
                    time.sleep(0.05)
            except OSError:
                pass
            finally:
                os.close(fd)

    died = proc.poll() is not None
    expected_exit_missing = False

    if meta["expect_exit"] and not died:
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline and proc.poll() is None:
            time.sleep(0.05)
        died = proc.poll() is not None
        expected_exit_missing = not died

    terminated_by_runner = not died
    if terminated_by_runner:
        proc.terminate()
    hung = False

    patience = time.monotonic() + 3.0
    while time.monotonic() < patience and proc.poll() is None:
        time.sleep(0.05)

    if proc.poll() is None:
        dump_stacks(proc.pid, os.path.join(rt, "gdb-stacks.txt"))
        end = time.monotonic() + 5.0
        while time.monotonic() < end and proc.poll() is None:
            time.sleep(0.05)
        if proc.poll() is None:
            hung = True
            proc.kill()

    comp_rc = proc.wait()
    logf.close()

    def finish(status: str, detail: str = "") -> dict:
        stop_bus()
        # embed the evidence into the record on failure, then always drop the
        # scratch dir — the JSON is self-contained
        arts = collect(rt, shell_out) if status == FAIL else {}
        shutil.rmtree(rt, ignore_errors=True)
        return dict(status=status, seconds=time.monotonic() - started, detail=detail, artifacts=arts)

    if rc == 127:
        return finish(SKIP)
    if rc != 0:
        detail = f"scenario rc={rc}: {last_line(shell_out)}"
        milestone = last_line(tail(client_log, 5))
        if milestone:
            detail += f" [client: {milestone}]"
        return finish(FAIL, detail)
    if expected_exit_missing:
        return finish(FAIL, "compositor did not exit when expected")
    if died and not meta["expect_exit"]:
        return finish(FAIL, "compositor died mid-test")
    if hung:
        return finish(FAIL, "compositor hung after quit")
    # an expected death may carry a deliberate nonzero code (the gpu policy
    # exits 1); the scenario asserts the semantics itself
    allowed_rc = (0, 1, None, 143) if terminated_by_runner or meta["expect_exit"] else (0, None)
    if comp_rc not in allowed_rc:
        return finish(FAIL, f"compositor exit rc={comp_rc}")
    return finish(PASS)


def main() -> int:
    ap = argparse.ArgumentParser(description="one integration scenario run -> JSON")
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--imway", required=True)
    ap.add_argument("--client", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--run", type=int, default=0)
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    name = os.path.basename(args.scenario)[:-3]
    client = args.client if (args.client and os.path.isfile(args.client)) else ""

    try:
        meta = parse_header(args.scenario)
        res = run(args.imway, args.scenario, client, meta, args.timeout)
    except Exception as e:  # never let a runner bug abort the graph
        meta = {"xfail": None}
        res = dict(status=FAIL, seconds=0.0, detail=f"runner error: {e}", artifacts={})

    record = dict(name=name, run=args.run, xfail=meta.get("xfail"), **res)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(record, f)
    # always succeed: the verdict lives in the JSON, not the exit code
    return 0


if __name__ == "__main__":
    sys.exit(main())
