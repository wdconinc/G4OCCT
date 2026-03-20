# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert example run logs and exit codes to a Markdown report."""

import sys
from pathlib import Path

from report_utils import timestamp, write_report

_EXAMPLES = [
    {
        "id": "B1",
        "name": "B1 — Water Phantom",
        "doc_link": "../example_b1.md",
        "cmd": "exampleB1 run.mac",
    },
    {
        "id": "B4c",
        "name": "B4c — Sampling Calorimeter",
        "doc_link": "../example_b4c.md",
        "cmd": "exampleB4c -m exampleB4.in",
    },
]


def _read_file(path: Path, default: str = "") -> str:
    """Read file contents, returning *default* if missing or unreadable."""
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return default


def _render_report(log_dir: Path) -> str:
    ts = timestamp()

    rows: list[tuple[str, str, str, str, str, int, str]] = []
    for ex in _EXAMPLES:
        exit_str = _read_file(log_dir / f"{ex['id']}.exit", "").strip()
        try:
            exit_code = int(exit_str)
        except ValueError:
            exit_code = -1
        log_text = _read_file(log_dir / f"{ex['id']}.log", "(no output captured)")
        # Geant4's UIbatch does not set a non-zero exit code when a command
        # fails and interrupts the batch session, so also scan the log.
        batch_interrupted = "Batch is interrupted!!" in log_text
        ok = exit_code == 0 and not batch_interrupted
        status = "✅ PASS" if ok else "❌ FAIL"
        rows.append((ex["id"], ex["name"], ex["doc_link"], ex["cmd"], status, exit_code, log_text))

    overall_ok = all(row[4] == "✅ PASS" for row in rows)
    overall_text = (
        "✅ All examples ran successfully."
        if overall_ok
        else "❌ One or more examples failed."
    )

    lines = [
        "# G4OCCT Example Logs",
        "",
        f"Generated: {ts}",
        "",
        f"**{overall_text}**",
        "",
        "| Example | Status |",
        "|---------|--------|",
    ]
    for _, name, doc_link, _, status, _, _ in rows:
        lines.append(f"| [{name}]({doc_link}) | {status} |")
    lines.append("")

    for ex_id, name, doc_link, cmd, status, exit_code, log_text in rows:
        lines += [
            f"## {name} — {status}",
            "",
            f"Command: `{cmd}`  |  Exit code: `{exit_code}`  "
            f"|  [Documentation]({doc_link})",
            "",
            "```",
            log_text.rstrip(),
            "```",
            "",
        ]

    return "\n".join(lines) + "\n"


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <log-dir> <output.md>", file=sys.stderr)
        sys.exit(1)

    log_dir = Path(sys.argv[1])
    md_path = Path(sys.argv[2])

    md = _render_report(log_dir)
    write_report(md_path, md, label="Example log report")


if __name__ == "__main__":
    main()
