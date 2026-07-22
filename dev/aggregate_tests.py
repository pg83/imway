#!/usr/bin/env python3
# The final test node: read every per-run JSON verdict, aggregate the runs of
# each test, print the summary and write it to --out, and exit nonzero when
# any test FAILed or was FLAKY. This is the node that makes `./build test`
# fail. It depends on every per-run node, so the build restores all their JSON
# outputs next to it before this runs.

import argparse
import glob
import json
import os
import sys

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


def load(results_dir: str) -> dict[str, list[dict]]:
    by_name: dict[str, list[dict]] = {}
    for path in sorted(glob.glob(os.path.join(results_dir, "*.json"))):
        with open(path) as f:
            rec = json.load(f)
        by_name.setdefault(rec["name"], []).append(rec)
    for runs in by_name.values():
        runs.sort(key=lambda r: r.get("run", 0))
    return by_name


def indent(s: str) -> str:
    return "".join("      " + l + "\n" for l in s.splitlines())


def main() -> int:
    ap = argparse.ArgumentParser(description="aggregate per-run JSON verdicts")
    ap.add_argument("--results", required=True, help="dir of *.json run records")
    ap.add_argument("--out", required=True, help="verdict summary file")
    ap.add_argument("--allow-flaky", action="store_true")
    args = ap.parse_args()

    by_name = load(args.results)
    if not by_name:
        sys.stderr.write("no test results found\n")
        return 2

    n_ok = n_fail = n_flaky = n_skip = n_xfail = n_xpass = 0
    lines: list[str] = []
    details: list[str] = []

    for name in sorted(by_name):
        runs = by_name[name]
        xfail = next((r.get("xfail") for r in runs if r.get("xfail")), None)
        real = [r for r in runs if r["status"] != SKIP]
        if not real:
            agg = SKIP
        elif all(r["status"] == PASS for r in real):
            agg = "OK"
        elif all(r["status"] == FAIL for r in real):
            agg = "FAIL"
        else:
            agg = "FLAKY"

        label = agg
        if xfail and agg == "FAIL":
            label, n_xfail = "XFAIL", n_xfail + 1
        elif xfail and agg == "OK":
            label, n_xpass = "XPASS", n_xpass + 1
        elif agg == "OK":
            n_ok += 1
        elif agg == "FAIL":
            n_fail += 1
        elif agg == "FLAKY":
            n_flaky += 1
        else:
            n_skip += 1

        secs = max((r.get("seconds", 0.0) for r in runs), default=0.0)
        note = f"  [xfail: {xfail}]" if xfail else ""
        lines.append(f"  {label:<6} {name} ({secs:.1f}s){note}")

        if agg in ("FAIL", "FLAKY"):
            details.append(f"--- reproduce: ./build test -Dfilter='{name}'")
            for r in runs:
                if r["status"] == FAIL:
                    details.append(f"--- {name} run {r.get('run', 0)}: {r.get('detail', '')}")
                    # artifacts are embedded file tails, keyed by filename
                    arts = r.get("artifacts") or {}
                    labels = {
                        "scenario.out": "scenario output",
                        "imway.log": "compositor log",
                        "final-state.txt": "final state dump",
                        "gdb-stacks.txt": "gdb thread stacks (shutdown hang)",
                    }
                    for fn, label in labels.items():
                        if arts.get(fn):
                            details.append(f"    {label}:")
                            details.append(indent(arts[fn]))

    body = "\n".join(lines)
    if details:
        body += "\n\n" + "\n".join(details)
    summary = (f"{n_ok} ok, {n_flaky} flaky, {n_fail} fail, {n_skip} skip"
               + (f", {n_xfail} xfail" if n_xfail else "")
               + (f", {n_xpass} xpass" if n_xpass else ""))
    body += "\n\n" + summary + "\n"
    if n_xpass:
        body += "warning: XPASS — a test marked xfail now passes; drop the marker\n"

    print(body, end="")
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(body)

    if n_fail or (n_flaky and not args.allow_flaky):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
