"""Smoke-test terminal startup and clean exit through an actual pseudoterminal."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="platypus-terminal-") as workspace:
    pid, master = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(binary, [binary, "--backend", "fake", "--project", workspace])
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    output = b""
    exited = False
    try:
        deadline = time.monotonic() + 8
        sent = False
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], .1)
            if ready:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    chunk = b""
                output += chunk
                if b"\x1b[6n" in chunk:
                    os.write(master, b"\x1b[1;1R")
            if b"Platypus" in output and not sent:
                time.sleep(.2)
                os.write(master, b"\x1b")
                sent = True
            result, status = os.waitpid(pid, os.WNOHANG)
            if result:
                exited = True
                assert os.waitstatus_to_exitcode(status) == 0, output.decode(errors="replace")
                break
        assert exited, "Terminal did not exit after Escape"
        assert b"Platypus" in output
    finally:
        if not exited:
            os.killpg(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(master)
print("FTXUI pseudoterminal startup/exit passed")
