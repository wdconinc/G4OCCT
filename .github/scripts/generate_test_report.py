# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert CTest JUnit XML output to a Markdown report."""

import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
from pathlib import Path

try:
    _TZ = ZoneInfo("America/New_York")
except ZoneInfoNotFoundError:
    _TZ = timezone.utc

_STATUS_LABELS = {
    "passed":  "✅ PASS",
    "failed":  "❌ FAIL",
    "error":   "❌ ERROR",
    "skipped": "⏭ SKIP",
}


def _parse_junit_xml(xml_path: str) -> list:
    """Parse a JUnit XML file; return list of test-case dicts."""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    if root.tag == "testsuites":
        suites = [s for s in root if s.tag == "testsuite"]
    elif root.tag == "testsuite":
        suites = [root]
    else:
        raise ValueError(f"Unexpected root element: {root.tag!r}")

    cases = []
    for suite in suites:
        for case in suite.findall("testcase"):
            name       = case.get("name", "")
            time_val   = float(case.get("time", "0") or "0")
            status_attr = case.get("status", "run")

            failure_el = case.find("failure")
            error_el   = case.find("error")
            skip_el    = case.find("skipped")

            if skip_el is not None or status_attr in ("notrun", "disabled"):
                status  = "skipped"
                message = skip_el.get("message", "") if skip_el is not None else "Not run"
            elif failure_el is not None:
                status  = "failed"
                message = failure_el.get("message", "") or (failure_el.text or "")
            elif error_el is not None:
                status  = "error"
                message = error_el.get("message", "") or (error_el.text or "")
            else:
                status  = "passed"
                message = ""

            cases.append({
                "name":    name,
                "status":  status,
                "time":    time_val,
                "message": (message or "").strip(),
            })
    return cases


def _md_escape(text: str) -> str:
    """Escape characters that break Markdown table cells."""
    return text.replace("|", "\\|").replace("\n", " ").replace("\r", "")


def _render_report(cases: list) -> str:
    """Render the Markdown string for the test-case list."""
    total   = len(cases)
    passed  = sum(1 for c in cases if c["status"] == "passed")
    failed  = sum(1 for c in cases if c["status"] in ("failed", "error"))
    skipped = sum(1 for c in cases if c["status"] == "skipped")
    timestamp = datetime.now(_TZ).strftime("%Y-%m-%d %H:%M %Z")

    overall_text = (
        f"✅ All {total} tests passed"
        if failed == 0
        else f"❌ {failed} of {total} tests failed"
    )

    lines = [
        "# G4OCCT Test Results",
        "",
        f"Generated: {timestamp}",
        "",
        f"**{overall_text}**",
        "",
        "| Total | Passed | Failed | Skipped |",
        "|------:|-------:|-------:|--------:|",
        f"| {total} | {passed} | {failed} | {skipped} |",
        "",
        "## Test Cases",
        "",
        "| Test Name | Status | Time | Message |",
        "|-----------|--------|-----:|---------|",
    ]

    for c in cases:
        label = _STATUS_LABELS.get(c["status"], c["status"])
        msg   = _md_escape(c["message"][:300]) if c["message"] else ""
        t     = f"{c['time']:.2f}s" if c["time"] else "—"
        lines.append(f"| {_md_escape(c['name'])} | {label} | {t} | {msg} |")

    return "\n".join(lines) + "\n"


def _render_error(message: str) -> str:
    """Render a minimal Markdown error report."""
    timestamp = datetime.now(_TZ).strftime("%Y-%m-%d %H:%M %Z")
    return (
        "# G4OCCT Test Results\n\n"
        f"Generated: {timestamp}\n\n"
        f"❌ Could not generate report: {message}\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <junit.xml> <output.md>", file=sys.stderr)
        sys.exit(1)

    xml_path, md_path = sys.argv[1], sys.argv[2]
    Path(md_path).parent.mkdir(parents=True, exist_ok=True)

    try:
        cases = _parse_junit_xml(xml_path)
        md    = _render_report(cases)
    except (ET.ParseError, FileNotFoundError, OSError, ValueError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        md = _render_error(str(exc))

    Path(md_path).write_text(md, encoding="utf-8")
    print(f"Test report written to: {md_path}")


if __name__ == "__main__":
    main()
