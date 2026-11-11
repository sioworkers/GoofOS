#!/usr/bin/env python3
"""boots goofos in headless qemu"""

import os
import re
import socket
import subprocess
import sys
import tempfile
import time

TIMEOUT = 30
KEY_DELAY = 0.05

# (what to type, text that must show up, what the last line must be after)
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
    ("halt", "bye!", None),
]

# chars that don't map to a qemu key name by themselves
SPECIAL_KEYS: dict[str, str] = {
    " ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot",
    ",": "comma", "/": "slash", ";": "semicolon", "'": "apostrophe",
    "+": "shift-equal", "!": "shift-1", "?": "shift-slash", ">": "shift-dot",
    "<": "shift-comma", ":": "shift-semicolon", '"': "shift-apostrophe",
}


class Monitor:
    """talks to the qemu monitor over a unix socket."""

    sock: socket.socket

    def __init__(self, path: str) -> None:
        self.sock = socket.socket(socket.AF_UNIX)
        deadline = time.time() + TIMEOUT
        while True:
            try:
                self.sock.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.1)
        _ = self.read_until_prompt()  # skip the banner

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
        """read the 80x25 vga text buffer as a list of lines."""
        out = self.command("xp /2000hx 0xb8000")
        cells: list[int] = []
        for line in out.splitlines():
            # lines look like "000b8000: 0x0f48 0x0f65 ..."
            if re.match(r"^[0-9a-f]+:", line):
                cells += [int(word, 16) for word in line.split(":", 1)[1].split()]
        chars = "".join(chr(c & 0xFF) if 32 <= (c & 0xFF) < 127 else " " for c in cells[:2000])
        return [chars[i:i + 80].rstrip() for i in range(0, 2000, 80)]


def last_line(lines: list[str]) -> str:
    filled = [line for line in lines if line]
    return filled[-1] if filled else ""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: tools/boottest.py build/goofos.img", file=sys.stderr)
        return 2

    tmp = tempfile.mkdtemp()
    sock_path = os.path.join(tmp, "monitor.sock")
    qemu = subprocess.Popen([
        "qemu-system-i386", "-display", "none", "-no-reboot",
        "-drive", f"format=raw,file={sys.argv[1]}",
        "-monitor", f"unix:{sock_path},server,nowait",
    ])
    lines: list[str] = []
    try:
        monitor = Monitor(sock_path)
        for keys, expect, prompt in STEPS:
            if keys is not None:
                monitor.type_text(keys + "\n")
            deadline = time.time() + TIMEOUT
            while True:
                lines = monitor.screen()
                text_ok = expect is None or any(expect in line for line in lines)
                prompt_ok = prompt is None or last_line(lines) == prompt
                if text_ok and prompt_ok:
                    break
                if time.time() > deadline:
                    raise RuntimeError(
                        f"after typing {keys!r}: wanted {expect!r} and prompt {prompt!r}")
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
        try:
            os.remove(sock_path)
            os.rmdir(tmp)
        except OSError:
            pass
    print("all good")
    return 0


if __name__ == "__main__":
    sys.exit(main())
