"""A made-up port's adapter for pih_e2e.py, and a template for a real one (README.md, "An adapter").

The "port" here is simulated in Python, so the runner can be tested without a game: its scripted run is 2000 ticks,
player 1 holds 0x40 for 30 of every 100 ticks, player 2 holds 0x10 for 20 of every 70. A real adapter starts the
port's own program instead of computing the reads (see the comments on each method) and parses what it printed.

  python <path>/pih_e2e.py --adapter examples/e2e_adapter.py --profiles profiles [--opt broken=reads]

--opt broken=reads   makes the simulated port misread a controller (a DualSense in slot 1), so the runner has to fail.

pih_e2e.py's folder is on sys.path when the runner starts, so Run is importable.
"""
import hashlib
import os

from pih_e2e import Run, profile_files

TICKS = 2000


def _script_reads():
    reads = {}
    for t in range(TICKS):
        reads[(t, 0)] = 0x40 if t % 100 < 30 else 0
        reads[(t, 1)] = 0x10 if t % 70 < 20 else 0
    return reads


def _state(reads):
    """The port's state on every tick, in whatever form it has, as bytes. Here: a hash chain of what it read."""
    h = hashlib.sha256()
    out = b""
    for t in range(TICKS):
        h.update(b"%d:%d:%d;" % (t, reads[(t, 0)], reads[(t, 1)]))
        out += h.digest()[:4]
    return out


class Adapter:
    def __init__(self, opts):
        self.opts = opts
        self.profile_dir = opts.get("profiles", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "profiles"))
        self.family = dict(profile_files(self.profile_dir))

    def baseline(self, work):
        """The scripted run played straight into the port (no controllers). A real adapter starts the port in its
        script mode and reads back the state log and the inputs it read on each tick."""
        reads = _script_reads()
        return Run(True, _state(reads), reads)

    def with_controllers(self, work, tag, profiles, plugs):
        """The same script, played on virtual controllers of `profiles` in slots 0 and 1; `plugs` is
        [(tick, slot, profile or None)] (None: unplug) applied during the run. A real adapter starts the port with
        the harness's virtual controllers attached (pih_attach) and presses them with pih_hold each tick."""
        reads = _script_reads()
        plugged = {0: True, 1: True}
        events = sorted(plugs)
        for t in range(TICKS):
            while events and events[0][0] <= t:
                _, slot, prof = events.pop(0)
                plugged[slot] = prof is not None
            for slot in (0, 1):
                if not plugged[slot]:
                    reads[(t, slot)] = 0
        if self.opts.get("broken") == "reads" and len(profiles) > 1 and profiles[1] == "dualsense":
            reads[(500, 1)] ^= 0x01
        return Run(True, _state(reads), reads)

    def menu_cases(self):
        """[(case, "backs" | "stays")]: which buttons leave a menu."""
        return [("confirm", "backs"), ("noise", "stays")]

    def menu(self, work, tag, profile, case):
        """Whether the run backed out of the menu with that button on that controller (None: never got there)."""
        return case == "confirm"

    def has_prompts(self):
        return True

    def distinct_families(self):
        return ["playstation", "xbox", "nintendo"]

    def picture(self, work, tag, profile=None, family=None):
        """The screen as bytes with that controller plugged in, or with that family forced."""
        return b"prompts for " + (family or self.family[profile]).encode()
