#!/usr/bin/env python3

import contextlib
import importlib.machinery
import importlib.util
import io
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
LOADER = importlib.machinery.SourceFileLoader("imway_build_runner", str(ROOT / "build"))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
LOADER.exec_module(runner)


class BuildSystemTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.out = self.root / "out"

    def tearDown(self):
        self.temp.cleanup()

    def context(self):
        return runner.BuildContext(self.root, self.out)

    def test_default_build_root_and_executor_layout(self):
        with mock.patch.dict("os.environ", {}, clear=True):
            self.assertEqual(runner.parse_args([]).build_dir, ".build")
        executor = runner.Executor(self.context(), 1, False, False)
        self.assertEqual(executor.cas, self.out / "cas")
        self.assertEqual(executor.uids, self.out / "uid")
        self.assertEqual(executor.tmp, self.out / "tmp")
        self.assertEqual(executor.grb, self.out / "grb")

    def test_infers_target_name_from_module_global(self):
        (self.root / "build.py").write_text(
            "thing = library(srcs=['thing.c'])\ninstall(thing)\n",
        )
        (self.root / "thing.c").write_text("int thing;\n")
        context = self.context()
        context.load(self.root / "build.py")
        context.build_graph()
        self.assertEqual(context.target_names["thing"].root.outputs, ["$(B)/libthing.a"])

    def test_scans_transitive_project_and_generated_includes(self):
        (self.root / "main.cpp").write_text(
            '#include <api/public.h>\n#include <generated.h>\nint main() {}\n',
        )
        (self.root / "inc/api").mkdir(parents=True)
        (self.root / "inc/api/public.h").write_text('#include "detail.h"\n')
        (self.root / "inc/api/detail.h").write_text("// detail\n")

        context = self.context()
        generated = context.command(
            name="generated", outputs=["gen/generated.h"],
            cmd=[[sys.executable, "-c", "pass"]], includes=["$(B)/gen"],
        )
        app = context.program(
            name="app", srcs=["main.cpp"], deps=[generated], includes=["inc"],
        )
        context.build_graph()
        compile_node = app.nodes[0]
        self.assertEqual(
            compile_node.source_inputs,
            {
                "$(S)/main.cpp",
                "$(S)/inc/api/public.h",
                "$(S)/inc/api/detail.h",
            },
        )
        self.assertIn(generated.root, compile_node.deps)

    def test_header_content_changes_compile_uid(self):
        (self.root / "main.c").write_text('#include "value.h"\nint main(void) { return V; }\n')
        header = self.root / "value.h"
        header.write_text("#define V 1\n")

        first = self.context()
        app1 = first.program(name="app", srcs=["main.c"])
        first.install(app1)
        first.build_graph()
        first.calculate_uids([app1.root])
        uid1 = app1.nodes[0].uid

        header.write_text("#define V 2\n")
        second = self.context()
        app2 = second.program(name="app", srcs=["main.c"])
        second.install(app2)
        second.build_graph()
        second.calculate_uids([app2.root])
        self.assertNotEqual(uid1, app2.nodes[0].uid)

    def test_cyclic_headers_have_finite_closure(self):
        (self.root / "main.c").write_text('#include "a.h"\n')
        (self.root / "a.h").write_text('#include "b.h"\n')
        (self.root / "b.h").write_text('#include "a.h"\n')
        context = self.context()
        app = context.program(name="app", srcs=["main.c"])
        context.build_graph()
        self.assertEqual(
            app.nodes[0].source_inputs,
            {"$(S)/main.c", "$(S)/a.h", "$(S)/b.h"},
        )

    def test_executor_reuses_uid_manifest_and_cas(self):
        source = self.root / "input.txt"
        count = self.root / "count.txt"
        source.write_text("payload\n")
        script = (
            "from pathlib import Path; import sys; "
            "src, out, count = map(Path, sys.argv[1:]); "
            "count.write_text(str(int(count.read_text()) + 1) if count.exists() else '1'); "
            "out.write_text(src.read_text())"
        )

        def graph():
            context = self.context()
            target = context.command(
                name="copy", inputs=["input.txt"], outputs=["result.txt"],
                cmd=[[sys.executable, "-c", script, "$(S)/input.txt", "$(B)/result.txt", "$(S)/count.txt"]],
            )
            context.install(target)
            context.build_graph()
            context.calculate_uids([target.root])
            return context, target

        first, target1 = graph()
        runner.Executor(first, 2, False, False).run([target1.root])
        self.assertEqual((self.out / "result.txt").read_text(), "payload\n")
        self.assertEqual(count.read_text(), "1")

        second, target2 = graph()
        runner.Executor(second, 2, False, False).run([target2.root])
        self.assertEqual(count.read_text(), "1")

    def test_failed_node_does_not_publish_manifest(self):
        context = self.context()
        target = context.command(
            name="fail", outputs=["missing"],
            cmd=[[sys.executable, "-c", "raise SystemExit(7)"]],
        )
        context.build_graph()
        context.calculate_uids([target.root])
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(runner.BuildError):
                runner.Executor(context, 1, False, False).run([target.root])
        self.assertFalse(runner.Executor(context, 1, False, False)._manifest_path(target.root.uid).exists())

    def test_parser_ignores_commented_directives(self):
        source = self.root / "source.c"
        source.write_text(
            '/* #include "hidden.h" */\n// #include "also-hidden.h"\n#include "seen.h"\n',
        )
        (self.root / "seen.h").write_text("\n")
        context = self.context()
        scanner = runner.IncludeScanner(context)
        self.assertEqual(scanner._parse("$(S)/source.c"), [(True, "seen.h")])

    def test_sigint_kills_worker_process_group_without_traceback(self):
        project = self.root / "project"
        project.mkdir()
        shutil.copy2(ROOT / "build", project / "build")
        (project / "build.py").write_text(
            "job = command(name='job', outputs=['done'], cmd=[\n"
            "    'python3', '-c',\n"
            "    \"import os, pathlib, sys, time; pathlib.Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(60)\",\n"
            "    '$(S)/worker.pid',\n"
            "])\ninstall(job)\n",
        )
        process = subprocess.Popen(
            [str(project / "build"), "-B", "out"], cwd=project,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        pid_file = project / "worker.pid"

        try:
            deadline = time.monotonic() + 5
            while not pid_file.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            if not pid_file.exists():
                if process.poll() is None:
                    process.terminate()
                _stdout, stderr = process.communicate(timeout=5)
                self.fail(f"worker command did not start (rc={process.returncode}): {stderr}")
            worker_pid = int(pid_file.read_text())
            process.send_signal(signal.SIGINT)
            _stdout, stderr = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 128 + signal.SIGINT)
            self.assertNotIn("Traceback", stderr)

            deadline = time.monotonic() + 2
            while Path(f"/proc/{worker_pid}").exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertFalse(Path(f"/proc/{worker_pid}").exists(), "worker survived SIGINT")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


if __name__ == "__main__":
    unittest.main()
