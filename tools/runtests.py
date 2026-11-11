#!/usr/bin/env python3
"""runs every tests/**/*.gof and checks it against the comments inside.

a test says what it expects with comments, anywhere on a line:
    #> text     the program prints this line (in order, one per marker)
    #! message  goofc refuses to compile it with this error

runnable tests get their own kernel image under build/tests/ and boot in
headless qemu. output is read from the serial port once the cpu halts.
"""

import difflib
import glob
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

from boottest import Monitor

TESTS_DIR = "tests"
BUILD_DIR = "build/tests"
GOOFC = "build/goofc"
TIMEOUT = 20
KERNEL_HELLO = "Hello from Kernel!\n"

OUTPUT_MARKER = re.compile(r"#>(?: (.*))?$")
ERROR_MARKER = re.compile(r"#! (.*)$")


class Result:
    """what happened to one test."""

    name: str
    ok: bool
    details: str

    def __init__(self, name: str, ok: bool, details: str = "") -> None:
        self.name = name
        self.ok = ok
        self.details = details


def read_markers(path: str) -> tuple[list[str], str | None]:
    """collect the expected output lines and the expected compile error."""
    output: list[str] = []
    error: str | None = None
    with open(path) as source:
        for line in source:
            line = line.rstrip("\n")
            output_match = OUTPUT_MARKER.search(line)
            error_match = ERROR_MARKER.search(line)
            if output_match:
                output.append(output_match.group(1) or "")
            elif error_match:
                error = error_match.group(1)
    return output, error


def diff(expected: list[str], actual: list[str]) -> str:
    lines = difflib.unified_diff(expected, actual, "expected", "actual", lineterm="")
    return "\n".join(lines)


def compile_test(name: str, path: str, error: str) -> Result:
    """goofc has to fail with exactly the expected message."""
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [GOOFC, path, "-o", os.path.join(tmp, "out.gex")],
            capture_output=True, text=True, check=False)
    wanted = "goofc: " + error
    got = result.stderr.strip()
    if result.returncode == 1 and got == wanted:
        return Result(name, True)
    return Result(name, False, f"wanted: {wanted}\n   got: {got or '(compiled fine)'}")


def boot(image: str, log_path: str) -> str:
    """boot an image until the cpu halts, return what went to serial."""
    with tempfile.TemporaryDirectory() as tmp:
        sock_path = os.path.join(tmp, "monitor.sock")
        qemu = subprocess.Popen([
            "qemu-system-i386", "-display", "none", "-no-reboot",
            "-drive", f"format=raw,file={image}",
            "-serial", f"file:{log_path}",
            "-monitor", f"unix:{sock_path},server,nowait",
        ])
        try:
            monitor = Monitor(sock_path)
            deadline = time.time() + TIMEOUT
            while True:
                # the kernel only halts when the program is done (or panics),
                # but the bios might halt too, so wait for our hello first.
                log = read_file(log_path)
                if KERNEL_HELLO in log and "HLT=1" in monitor.command("info registers"):
                    return read_file(log_path)
                if time.time() > deadline:
                    raise TimeoutError(f"no halt after {TIMEOUT}s, serial so far:\n{log}")
                time.sleep(0.1)
        finally:
            qemu.kill()
            _ = qemu.wait()


def read_file(path: str) -> str:
    try:
        with open(path, errors="replace") as file:
            return file.read()
    except FileNotFoundError:
        return ""


def run_test(name: str, path: str, expected: list[str]) -> Result:
    """build an image with this program baked in, boot it, compare output."""
    build = os.path.join(BUILD_DIR, name)
    made = subprocess.run(
        ["make", "-s", f"BUILD={build}", f"PROGRAM={path}", f"GOOFC={GOOFC}",
         f"{build}/goofos.img"],
        capture_output=True, text=True, check=False)
    if made.returncode != 0:
        return Result(name, False, "build failed:\n" + made.stdout + made.stderr)
    try:
        log = boot(f"{build}/goofos.img", os.path.join(build, "serial.log"))
    except Exception as error:
        return Result(name, False, str(error))
    output = log.split(KERNEL_HELLO, 1)[1]
    actual = output.rstrip("\n").split("\n") if output.strip("\n") else []
    while expected and expected[-1] == "":
        expected = expected[:-1]
    if actual == expected:
        return Result(name, True)
    return Result(name, False, diff(expected, actual))


def run_one(path: str) -> Result:
    name = os.path.relpath(path, TESTS_DIR)[:-len(".gof")]
    expected, error = read_markers(path)
    if error is not None:
        return compile_test(name, path, error)
    if not expected:
        return Result(name, False, "test has no #> or #! markers")
    return run_test(name, path, expected)


def main() -> int:
    paths = sorted(glob.glob(os.path.join(TESTS_DIR, "**", "*.gof"), recursive=True))
    if len(sys.argv) > 1:
        # only run tests whose name contains one of the arguments
        paths = [p for p in paths if any(arg in p for arg in sys.argv[1:])]
    if not paths:
        print("no tests found", file=sys.stderr)
        return 1
    if not os.path.exists(GOOFC):
        print(f"{GOOFC} is missing, run make first", file=sys.stderr)
        return 1

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        results = list(pool.map(run_one, paths))

    failed = 0
    for result in results:
        print(f"{'ok  ' if result.ok else 'FAIL'} {result.name}")
        if not result.ok:
            failed += 1
            for line in result.details.splitlines():
                print("     " + line)
    print(f"{len(results) - failed}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
