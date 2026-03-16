# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Shared utilities for generate_*_report.py scripts."""

from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

try:
    _TZ = ZoneInfo("America/New_York")
except ZoneInfoNotFoundError:
    _TZ = timezone.utc


def timestamp() -> str:
    """Return current time formatted as 'YYYY-MM-DD HH:MM TZ'."""
    return datetime.now(_TZ).strftime("%Y-%m-%d %H:%M %Z")


def md_escape(text: str) -> str:
    """Escape characters that break Markdown table cells."""
    return text.replace("|", "\\|").replace("\n", " ").replace("\r", "")


def write_report(output_path: Path, content: str, label: str = "Report") -> None:
    """Ensure parent directory exists, write content, and print confirmation."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")
    print(f"{label} written to: {output_path}")
