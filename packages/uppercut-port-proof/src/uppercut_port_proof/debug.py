"""A client for the debug socket of a recompiled port built with its debug tools on, and the loop a proof run drives it with.

The server of such a build reads one JSON line, answers one JSON line and closes the connection, so every call opens a new
one (`DebugClient`). The commands (`frame`, `press`, `clear_input`, `screenshot`, `quit`, ...) belong to the port; this
module only knows how to send one and wait for a frame number.
"""

from __future__ import annotations

import json
import os
import socket
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence

from .errors import DebugError
from .presses import Press, pad_word


class DebugClient:
    """One request per connection.

    `host`, `port`: where the server listens. With `wait=True` (the default) the constructor waits up to `timeout` seconds for it
    to start listening, then raises `DebugError`. A call that gets no answer is tried again up to `retries` times, `retry_delay`
    seconds apart, and then raises `DebugError` (also a `ConnectionError`)."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 4370,
        *,
        timeout: float = 30,
        wait: bool = True,
        retries: int = 5,
        retry_delay: float = 0.05,
        call_timeout: float = 30,
        connect_timeout: float = 15,
    ) -> None:
        self.host, self.port = host, port
        self.retries, self.retry_delay = retries, retry_delay
        self.call_timeout, self.connect_timeout = call_timeout, connect_timeout
        self.calls = 0
        if wait:
            self.wait_listening(timeout)

    def wait_listening(self, timeout: float = 30, interval: float = 0.5) -> None:
        deadline = time.monotonic() + timeout
        while True:
            try:
                socket.create_connection((self.host, self.port), timeout=self.connect_timeout).close()
                return
            except OSError as error:
                if time.monotonic() > deadline:
                    raise DebugError(f"nothing listens on {self.host}:{self.port}: {error}") from error
                time.sleep(interval)

    def call(self, cmd: str, **fields: Any) -> Dict[str, Any]:
        self.calls += 1
        message = dict(fields, id=self.calls, cmd=cmd)
        last: Optional[BaseException] = None
        for attempt in range(self.retries):
            if attempt:
                time.sleep(self.retry_delay)
            try:
                with socket.create_connection((self.host, self.port), timeout=self.call_timeout) as sock:
                    stream = sock.makefile("rw", encoding="utf-8", newline="\n")
                    stream.write(json.dumps(message) + "\n")
                    stream.flush()
                    line = stream.readline()
                if line:
                    return json.loads(line)  # type: ignore[no-any-return]
                last = DebugError("the connection closed with no answer")
            except (OSError, ValueError) as error:  # a refused or reset connection, or an answer that is not JSON
                last = error
        raise DebugError(f"the debug server did not answer {cmd}: {last}")

    def frame(self) -> int:
        return int(self.call("frame")["frame"])


def wait_frame(
    get_frame: Callable[[], int],
    target: int,
    *,
    timeout: float = 90,
    interval: float = 0.05,
    alive: Optional[Callable[[], bool]] = None,
) -> int:
    """Poll `get_frame()` until it reaches `target` and return the frame it read.

    `alive`, when given, is asked on every poll: a False answer means the program is gone, and the wait stops with a
    `DebugError` instead of running to the timeout. `TimeoutError` is raised when `timeout` seconds pass first."""
    deadline = time.monotonic() + timeout
    while True:
        current = get_frame()
        if current >= target:
            return current
        if alive is not None and not alive():
            raise DebugError(f"the program exited while waiting for frame {target} (last frame {current})")
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Frame {target} not reached; last frame {current}")
        time.sleep(interval)


def wait_ready(
    ping: Callable[[], object],
    *,
    timeout: float = 30,
    interval: float = 0.2,
    alive: Optional[Callable[[], bool]] = None,
    what: str = "the debug server",
) -> None:
    """Call `ping()` until it stops raising `OSError` or `TimeoutError`, for up to `timeout` seconds.

    `alive`, when given, is asked before every ping: a False answer means the program is gone, and the wait stops with a
    `DebugError` at once. `TimeoutError` is raised when `timeout` seconds pass first."""
    deadline = time.monotonic() + timeout
    while True:
        if alive is not None and not alive():
            raise DebugError(f"the program exited before {what} answered")
        try:
            ping()
            return
        except (OSError, TimeoutError):
            if time.monotonic() >= deadline:
                raise TimeoutError(f"{what} did not start") from None
            time.sleep(interval)


def run_presses(
    client: DebugClient,
    presses: Sequence[Press],
    until: int,
    *,
    shot_every: int = 300,
    out_dir: Optional[Path] = None,
    log: Callable[[str], None] = lambda line: print(line, flush=True),
    poll: float = 0.002,
    quit_at_end: bool = False,
) -> int:
    """Press buttons at frame numbers and save a screenshot every `shot_every` frames, until the game reaches `until`.

    The frame counter is read by polling, so frame numbers are open loop (a poll or two of jitter). A press is sent when the
    counter reaches its frame, held for its `hold` frames and released one frame later. Presses that fall due while an earlier one is still
    held wait for it. Screenshots go to `out_dir/shot_NNNNNN.png` (made if needed); with `shot_every` 0 or no `out_dir` there
    are none. Returns the last frame read. One log line is written per press and per shot."""
    if out_dir is not None:
        Path(out_dir).mkdir(parents=True, exist_ok=True)
    cur = client.frame()
    log("connected at frame %d" % cur)
    index = 0
    release_at: Optional[int] = None
    next_shot = shot_every if shot_every else None
    while cur < until:
        cur = client.frame()
        if release_at is not None and cur >= release_at:
            client.call("clear_input")
            release_at = None
        if index < len(presses) and cur >= presses[index].frame and release_at is None:
            p = presses[index]
            client.call("press", buttons=pad_word(p.buttons), frames=p.hold)
            log("frame %d: press %s for %d (asked for frame %d)" % (cur, "+".join(p.buttons), p.hold, p.frame))
            release_at = cur + p.hold + 1
            index += 1
        if next_shot is not None and out_dir is not None and cur >= next_shot:
            path = os.path.abspath(os.path.join(out_dir, "shot_%06d.png" % next_shot))
            reply = client.call("screenshot", path=path)
            log("frame %d: shot %s %s" % (cur, os.path.basename(path), reply.get("ok")))
            next_shot += shot_every
        time.sleep(poll)
    if quit_at_end:
        try:
            client.call("quit")
        except DebugError:
            pass  # the program closes the connection as it exits
    log("done at frame %d" % cur)
    return cur
