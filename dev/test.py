#!/usr/bin/env python3
# Integration test runner. Builds nothing — dev/build.sh builds the compositor
# and the test clients.
#
# Each test run gets a fresh headless compositor in its own scratch dir
# (TMPDIR == XDG_RUNTIME_DIR == cwd). The runner starts the compositor, waits
# from Python until it is fully up, then hands the scenario shell script the
# environment it needs (see dev/tests/lib.sh) and runs it.
#
# Every test runs N times (default 3) with the whole shuffled schedule spread
# across a thread pool. A test that was OK in all runs is OK, FAIL in all runs
# is FAIL, and anything in between is FLAKY. Exit code is 0 only when there are
# no FAIL/FLAKY results (XFAIL and SKIP are fine).

import argparse
import glob
import os
import random
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# single-run outcomes
PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


@dataclass
class Test:
    name: str          # e.g. "headless_feat_pointer"
    path: str          # scenario .sh path
    client: str        # client binary path ("" if none)
    xfail: str | None  # reason string if the test is expected to fail


@dataclass
class RunResult:
    status: str        # PASS | FAIL | SKIP
    seconds: float
    detail: str        # short reason for FAIL
    artifacts: str     # kept scratch dir on failure, else ""


def discover(tests_dir: str, bindir: str, pattern: str) -> list[Test]:
    out = []
    for path in sorted(glob.glob(os.path.join(tests_dir, "headless_*.sh"))):
        name = os.path.basename(path)[:-3]
        if pattern and not glob.fnmatch.fnmatch(name, pattern):
            continue
        client = os.path.join(bindir, "tests", name.replace("headless_", "client_", 1))
        if not (os.path.isfile(client) and os.access(client, os.X_OK)):
            client = ""
        xfail = None
        with open(path) as f:
            for line in f.read(2048).splitlines():
                m = re.match(r"\s*#\s*xfail:\s*(.*)", line)
                if m:
                    xfail = m.group(1).strip() or "expected failure"
                    break
        out.append(Test(name, path, client, xfail))
    return out


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
    # snapshot every thread's backtrace of a (probably stuck) process, then
    # detach so it can still finish on its own
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


def run_once(imway: str, t: Test, timeout: float, keep: bool) -> RunResult:
    rt = tempfile.mkdtemp(prefix=f"imway-{t.name}-")
    log = os.path.join(rt, "imway.log")
    ctl = os.path.join(rt, "ctl")
    sock = os.path.join(rt, "imway-test")
    client_log = os.path.join(rt, "client.log")

    env = dict(os.environ)
    env.update(
        XDG_RUNTIME_DIR=rt,
        WAYLAND_DISPLAY="imway-test",
        IMWAY_CTL=ctl,
        IMWAY_LOG=log,
        IMWAY_CLIENT=t.client,
        IMWAY_CLIENT_LOG=client_log,
    )

    started = time.monotonic()

    def fail(detail: str) -> RunResult:
        return RunResult(FAIL, time.monotonic() - started, detail, rt)

    logf = open(log, "w")
    proc = subprocess.Popen(
        [imway, "--device", "headless", "--socket", "imway-test", "--control", ctl],
        cwd=rt, env=env, stdout=logf, stderr=subprocess.STDOUT,
    )
    env["IMWAY_PID"] = str(proc.pid)

    # ready = socket bound, FIFO open, init-complete marker logged
    ready = False
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            logf.close()
            return fail(f"compositor exited during startup (rc={proc.returncode})")
        if is_sock(sock) and is_fifo(ctl) and "control FIFO:" in tail(log, 200):
            ready = True
            break
        time.sleep(0.05)
    if not ready:
        proc.kill()
        proc.wait()
        logf.close()
        return fail("compositor did not come up")

    # run the scenario
    shell_out = ""
    rc = 0
    try:
        cp = subprocess.run(
            ["bash", t.path], cwd=rt, env=env, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        rc, shell_out = cp.returncode, cp.stdout
    except subprocess.TimeoutExpired as e:
        shell_out = (e.stdout.decode() if isinstance(e.stdout, bytes) else e.stdout) or ""
        rc = -1
        shell_out += f"\n[runner] scenario timed out after {timeout}s\n"

    # teardown: clean shutdown is part of every test. SIGTERM (the compositor
    # breaks its event loop on it) rather than a FIFO "quit" — writing to the
    # control FIFO races the compositor's reopen cycle and can be silently
    # dropped, leaving the compositor running.
    died = proc.poll() is not None
    if not died:
        proc.terminate()
    hung = False

    # wait a short patience for a clean exit
    patience = time.monotonic() + 3.0
    while time.monotonic() < patience and proc.poll() is None:
        time.sleep(0.05)

    if proc.poll() is None:
        # slow shutdown: capture every thread's stack before it (maybe) finishes
        dump_stacks(proc.pid, os.path.join(rt, "gdb-stacks.txt"))
        end = time.monotonic() + 5.0
        while time.monotonic() < end and proc.poll() is None:
            time.sleep(0.05)
        if proc.poll() is None:
            hung = True
            proc.kill()

    comp_rc = proc.wait()
    logf.close()

    def finish(status: str, detail: str = "") -> RunResult:
        if status in (PASS, SKIP) and not keep:
            shutil.rmtree(rt, ignore_errors=True)
            art = ""
        else:
            with open(os.path.join(rt, "scenario.out"), "w") as f:
                f.write(shell_out)
            art = rt
        return RunResult(status, time.monotonic() - started, detail, art)

    if rc == 127:
        return finish(SKIP)
    if rc != 0:
        return finish(FAIL, f"scenario rc={rc}: {last_line(shell_out)}")
    if died:
        return finish(FAIL, "compositor died mid-test")
    if hung:
        return finish(FAIL, "compositor hung after quit")
    if comp_rc not in (0, None):
        return finish(FAIL, f"compositor exit rc={comp_rc}")
    return finish(PASS)


def last_line(s: str) -> str:
    lines = [l for l in s.strip().splitlines() if l.strip()]
    return lines[-1] if lines else ""


def main() -> int:
    ap = argparse.ArgumentParser(description="imway integration test runner")
    ap.add_argument("--imway", default=os.environ.get("IMWAY", "build-boot/imway"),
                    help="compositor binary (default build-boot/imway)")
    ap.add_argument("--bindir", default=os.environ.get("B", "build-boot"),
                    help="build dir holding tests/ (default build-boot)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--filter", default="", help="glob over test names")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--keep", action="store_true", help="keep scratch dirs")
    ap.add_argument("--allow-flaky", action="store_true",
                    help="FLAKY results warn instead of failing the run")
    args = ap.parse_args()

    imway = args.imway if os.path.isabs(args.imway) else os.path.join(ROOT, args.imway)
    bindir = args.bindir if os.path.isabs(args.bindir) else os.path.join(ROOT, args.bindir)
    if not (os.path.isfile(imway) and os.access(imway, os.X_OK)):
        print(f"no {imway} — run dev/build.sh first", file=sys.stderr)
        return 2

    tests = discover(os.path.join(ROOT, "dev", "tests"), bindir, args.filter)
    if not tests:
        print("no tests matched", file=sys.stderr)
        return 2

    # schedule: every test, args.runs times, all shuffled together
    schedule = [t for t in tests for _ in range(args.runs)]
    random.Random(args.seed).shuffle(schedule)

    results: dict[str, list[RunResult]] = {t.name: [] for t in tests}
    lock = threading.Lock()
    done = 0
    total = len(schedule)

    def work(t: Test) -> tuple[str, RunResult]:
        nonlocal done
        r = run_once(imway, t, args.timeout, args.keep)
        with lock:
            done += 1
            sys.stderr.write(f"\r  {done}/{total} runs ")
            sys.stderr.flush()
        return t.name, r

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, r in pool.map(work, schedule):
            results[name].append(r)
    sys.stderr.write("\r" + " " * 24 + "\r")

    by_name = {t.name: t for t in tests}
    n_ok = n_fail = n_flaky = n_skip = n_xfail = n_xpass = 0
    lines = []
    details = []

    for t in tests:
        runs = results[t.name]
        real = [r for r in runs if r.status != SKIP]
        if not real:
            agg = SKIP
        elif all(r.status == PASS for r in real):
            agg = "OK"
        elif all(r.status == FAIL for r in real):
            agg = "FAIL"
        else:
            agg = "FLAKY"

        label = agg
        if t.xfail and agg == "FAIL":
            label, n_xfail = "XFAIL", n_xfail + 1
        elif t.xfail and agg == "OK":
            label, n_xpass = "XPASS", n_xpass + 1
        elif agg == "OK":
            n_ok += 1
        elif agg == "FAIL":
            n_fail += 1
        elif agg == "FLAKY":
            n_flaky += 1
        else:
            n_skip += 1

        secs = max((r.seconds for r in runs), default=0.0)
        note = f"  [xfail: {t.xfail}]" if t.xfail else ""
        lines.append(f"  {label:<6} {t.name} ({secs:.1f}s){note}")

        if agg in ("FAIL", "FLAKY"):
            for i, r in enumerate(runs):
                if r.status == FAIL:
                    details.append(f"--- {t.name} run {i + 1}: {r.detail}")
                    if r.artifacts:
                        details.append(f"    artifacts: {r.artifacts}")
                        details.append(indent(tail(os.path.join(r.artifacts, "scenario.out"), 12)))
                        details.append(indent(tail(os.path.join(r.artifacts, "imway.log"), 8)))
                        stacks = os.path.join(r.artifacts, "gdb-stacks.txt")
                        if os.path.exists(stacks):
                            details.append("    gdb thread stacks (shutdown hang):")
                            details.append(indent(tail(stacks, 60)))

    print("\n".join(lines))
    if details:
        print("\n" + "\n".join(details))

    summary = (f"\n{n_ok} ok, {n_flaky} flaky, {n_fail} fail, {n_skip} skip"
               + (f", {n_xfail} xfail" if n_xfail else "")
               + (f", {n_xpass} xpass" if n_xpass else ""))
    print(summary)
    if n_xpass:
        print("warning: XPASS — a test marked xfail now passes; drop the marker")

    if n_fail or (n_flaky and not args.allow_flaky):
        return 1
    if n_flaky:
        print("warning: FLAKY results (allowed)")
    return 0


def indent(s: str) -> str:
    return "".join("      " + l + "\n" for l in s.splitlines())


if __name__ == "__main__":
    sys.exit(main())
