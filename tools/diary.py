#!/usr/bin/env python3
"""Append one timestamped SESSION block to the slave's diary (/home/richard/shots/AgentLog.txt).

WHY THIS EXISTS
    The runner only logged its ACTIONS (`ACTION: capture kitchen ...`), so the diary recorded
    what the machine did and nothing about what was decided. Richard asked for the sessions to
    be in the log -- on 2026-10-26 AgentLog.txt held a single line ("runner online") and none of
    the conversation that produced it.

    A diary that cannot answer "why is it like this" is not a diary, it is a telemetry stream.
    So decisions, corrections and findings get written here too, as `SESSION:` blocks, in the
    same format and on the same UTC clock as the runner's own entries.

USAGE
    python3 tools/diary.py "one-liner"
    python3 tools/diary.py <<'EOF'
    SESSION: what was decided
    ...
    EOF

    The `===Timestamp: ...===` header is added here, in UTC, so every entry agrees with
    tools/runner-daemon.py and with the Master's clock. Format consistent by construction is
    the point: twenty hand-typed headers drift.
"""

import sys
from datetime import datetime, timezone

LOG = "/home/richard/shots/AgentLog.txt"


def main(argv):
    body = " ".join(argv[1:]) if len(argv) > 1 else sys.stdin.read()
    if not body.strip():
        print("diary: nothing to write", file=sys.stderr)
        return 2
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    with open(LOG, "a") as f:
        f.write(f"===Timestamp: {ts}===\n{body.rstrip()}\n\n")
    print(f"diary: appended {len(body):,} chars at {ts} UTC -> {LOG}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
