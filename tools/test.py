#!/usr/bin/env python3
"""run language tests or boot the image and type through the shell"""

import difflib
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

TESTS_DIR = "tests"
BUILD_DIR = "build/tests"
GOOFC = "build/goofc"
CONNECT_TIMEOUT = 30
BOOT_TIMEOUT = 30
TEST_TIMEOUT = 20
KEY_DELAY = 0.05
KERNEL_HELLO = "Hello from Kernel!\n"

OUTPUT_MARKER = re.compile(r"#>(?: (.*))?$")
ERROR_MARKER = re.compile(r"#! (.*)$")

STEPS: list[tuple[str | None, str | None, str | None]] = [
    (None, "type help for commands", ">"),
    ("help", "halt   stop the cpu", ">"),
    ("add", None, "a:"),
    ("12", None, "b:"),
    ("-5", "12 + -5 = 7", ">"),
    ("greet", None, "name:"),
    ("sioworker", "hi sioworker!", ">"),
    ("fact", None, "n:"),
    ("10", "10! = 3628800", ">"),
    ("nope", "unknown command 'nope', try help", ">"),
    ("write", None, "File Name:"),
    ("notes", "Text (Type EOF to write):", "Text (Type EOF to write):"),
    ("Hello GoofOS", None, "Hello GoofOS"),
    ("EOF", "saved notes", ">"),
    ("cls", None, ">"),
    ("read", None, "File Name:"),
    ("notes", "Hello GoofOS", ">"),
    ("files", "13 bytes", ">"),
    ("uptime", "up ", ">"),
    ("sleep", None, "ms:"),
    ("300", None, ">"),
    ("halt", "bye!", None),
]

SPECIAL_KEYS = {
    " ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot",
    ",": "comma", "/": "slash", ";": "semicolon", "'": "apostrophe",
    "+": "shift-equal", "!": "shift-1", "?": "shift-slash", ">": "shift-dot",
    "<": "shift-comma", ":": "shift-semicolon", '"': "shift-apostrophe",
}


class Result:
    """what happened to one test."""

    name: str
    ok: bool
    details: str

    def __init__(self, name: str, ok: bool, details: str = "") -> None:
        self.name = name
        self.ok = ok
        self.details = details


class Monitor:
    """talk to the qemu monitor over a unix socket."""

    sock: socket.socket

    def __init__(self, path: str) -> None:
        self.sock = socket.socket(socket.AF_UNIX)
        deadline = time.time() + CONNECT_TIMEOUT
        while True:
            try:
                self.sock.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.1)
        _ = self.read_until_prompt()

    def read_until_prompt(self) -> str:
        """read everything up to the next (qemu) prompt."""
        data = b""
        while not data.rstrip().endswith(b"(qemu)"):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("qemu monitor closed")
            data += chunk
        return data.decode(errors="ignore")

    def command(self, text: str) -> str:
        self.sock.sendall(text.encode() + b"\n")
        return self.read_until_prompt()

    def type_text(self, text: str) -> None:
        for char in text:
            if char in SPECIAL_KEYS:
                key = SPECIAL_KEYS[char]
            elif char.isupper():
                key = "shift-" + char.lower()
            else:
                key = char
            _ = self.command("sendkey " + key)
            time.sleep(KEY_DELAY)

    def screen(self) -> list[str]:
        """read the 80x25 vga text buffer."""
        output = self.command("xp /2000hx 0xb8000")
        cells: list[int] = []
        for line in output.splitlines():
            if re.match(r"^[0-9a-f]+:", line):
                cells.extend(int(word, 16) for word in line.split(":", 1)[1].split())
        chars = "".join(
            chr(cell & 0xFF) if 32 <= (cell & 0xFF) < 127 else " "
            for cell in cells[:2000]
        )
        return [chars[i:i + 80].rstrip() for i in range(0, 2000, 80)]


def last_line(lines: list[str]) -> str:
    filled = [line for line in lines if line]
    return filled[-1] if filled else ""


def boot_check(image: str) -> int:
    """boot an image and run the scripted shell interaction."""
    with tempfile.TemporaryDirectory() as tmp:
        # the shell writes files, so boot a copy and leave the real image alone
        copy = os.path.join(tmp, "goofos.img")
        _ = shutil.copyfile(image, copy)
        image = copy
        sock_path = os.path.join(tmp, "monitor.sock")
        qemu = subprocess.Popen([
            "qemu-system-i386", "-display", "none", "-no-reboot",
            "-drive", f"format=raw,file={image}",
            "-monitor", f"unix:{sock_path},server,nowait",
        ])
        lines: list[str] = []
        try:
            monitor = Monitor(sock_path)
            for keys, expected, prompt in STEPS:
                if keys is not None:
                    monitor.type_text(keys + "\n")
                deadline = time.time() + BOOT_TIMEOUT
                while True:
                    lines = monitor.screen()
                    text_ok = expected is None or any(expected in line for line in lines)
                    prompt_ok = prompt is None or last_line(lines) == prompt
                    if text_ok and prompt_ok:
                        break
                    if time.time() > deadline:
                        raise TimeoutError(
                            f"after typing {keys!r}: wanted {expected!r} and prompt {prompt!r}"
                        )
                    time.sleep(0.2)
                print(f"ok   {keys or '(boot)'}")
        except Exception as error:
            print(f"FAIL {error}\n--- screen ---", file=sys.stderr)
            for line in lines:
                print("|" + line, file=sys.stderr)
            return 1
        finally:
            qemu.kill()
            _ = qemu.wait()
    print("all good")
    return 0


def read_markers(path: str) -> tuple[list[str], str | None]:
    """collect expected output and compile errors from source comments."""
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
    """goofc must fail with the expected error."""
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [GOOFC, path, "-o", os.path.join(tmp, "out.gex")],
            capture_output=True, text=True, check=False,
        )
    wanted = "goofc: " + error
    got = result.stderr.strip()
    if result.returncode == 1 and got == wanted:
        return Result(name, True)
    return Result(name, False, f"wanted: {wanted}\n   got: {got or '(compiled fine)'}")


def read_file(path: str) -> str:
    try:
        with open(path, errors="replace") as file:
            return file.read()
    except FileNotFoundError:
        return ""


def finished(registers: str) -> bool:
    """true once the cpu is halted for good: in hlt with interrupts off.

    the kernel also halts while it waits for a timer tick or a key, but with
    interrupts on, and a program only ever stops with them off (cli).
    """
    flags = re.search(r"EFL=([0-9a-f]+)", registers)
    if flags is None or "HLT=1" not in registers:
        return False
    return int(flags.group(1), 16) & 0x200 == 0


def boot_program(image: str, log_path: str) -> str:
    """boot a test image until it halts and return its serial output."""
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
            deadline = time.time() + TEST_TIMEOUT
            while True:
                log = read_file(log_path)
                if KERNEL_HELLO in log and finished(monitor.command("info registers")):
                    return log
                if time.time() > deadline:
                    raise TimeoutError(f"no halt after {TEST_TIMEOUT}s, serial so far:\n{log}")
                time.sleep(0.1)
        finally:
            qemu.kill()
            _ = qemu.wait()


def run_test(name: str, path: str, expected: list[str]) -> Result:
    """build an image with this source and compare its serial output."""
    build = os.path.join(BUILD_DIR, name)
    made = subprocess.run(
        ["make", "-s", f"BUILD={build}", f"PROGRAM={path}", f"GOOFC={GOOFC}",
         f"{build}/goofos.img"],
        capture_output=True, text=True, check=False,
    )
    if made.returncode != 0:
        return Result(name, False, "build failed:\n" + made.stdout + made.stderr)
    try:
        log = boot_program(f"{build}/goofos.img", os.path.join(build, "serial.log"))
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


def run_tests() -> int:
    paths = sorted(glob.glob(os.path.join(TESTS_DIR, "**", "*.gof"), recursive=True))
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


def main() -> int:
    if len(sys.argv) != 1:
        print("tools/test.py takes no arguments", file=sys.stderr)
        return 2

    image = "build/goofos.img"
    if os.path.exists(image):
        boot_result = boot_check(image)
    else:
        print(f"{image} is missing, run make first", file=sys.stderr)
        boot_result = 1

    suite_result = run_tests()
    return 1 if boot_result or suite_result else 0


if __name__ == "__main__":
    sys.exit(main())
