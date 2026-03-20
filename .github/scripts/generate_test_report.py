# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Convert CTest JUnit XML output to a Markdown report."""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from report_utils import md_escape, timestamp, write_report

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


def _render_report(cases: list) -> str:
    """Render the Markdown string for the test-case list."""
    total   = len(cases)
    passed  = sum(1 for c in cases if c["status"] == "passed")
    failed  = sum(1 for c in cases if c["status"] in ("failed", "error"))
    skipped = sum(1 for c in cases if c["status"] == "skipped")
    ts = timestamp()

    overall_text = (
        f"✅ All {total} tests passed"
        if failed == 0
        else f"❌ {failed} of {total} tests failed"
    )

    lines = [
        "# G4OCCT Test Results",
        "",
        f"Generated: {ts}",
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
        msg   = md_escape(c["message"][:300]) if c["message"] else ""
        t     = f"{c['time']:.2f}s" if c["time"] else "—"
        lines.append(f"| {md_escape(c['name'])} | {label} | {t} | {msg} |")

    return "\n".join(lines) + "\n"


def _render_error(message: str) -> str:
    """Render a minimal Markdown error report."""
    return (
        "# G4OCCT Test Results\n\n"
        f"Generated: {timestamp()}\n\n"
        f"❌ Could not generate report: {message}\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <junit.xml> <output.md>", file=sys.stderr)
        sys.exit(1)

    xml_path, md_path = sys.argv[1], sys.argv[2]

    try:
        cases = _parse_junit_xml(xml_path)
        md    = _render_report(cases)
    except (ET.ParseError, FileNotFoundError, OSError, ValueError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        md = _render_error(str(exc))

    write_report(Path(md_path), md, label="Test report")


if __name__ == "__main__":
    main()
