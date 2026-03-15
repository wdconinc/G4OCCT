# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert CTest JUnit XML output to a self-contained HTML report."""

import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from html import escape
from pathlib import Path

_CSS = """
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: #f0f2f5; color: #333; padding: 24px;
    }
    .container { max-width: 1100px; margin: 0 auto; }
    h1 { font-size: 1.6rem; color: #1a2a3a; margin-bottom: 4px; }
    .meta { font-size: 0.85rem; color: #666; margin-bottom: 20px; }
    .summary { display: flex; flex-wrap: wrap; gap: 12px; margin-bottom: 20px; }
    .stat {
      padding: 14px 24px; border-radius: 8px; min-width: 100px; text-align: center;
    }
    .stat-label { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.05em; }
    .stat-value { font-size: 2rem; font-weight: 700; margin-top: 4px; }
    .stat-total   { background: #e9ecef; color: #495057; }
    .stat-passed  { background: #d1fae5; color: #065f46; }
    .stat-failed  { background: #fee2e2; color: #991b1b; }
    .stat-skipped { background: #fef3c7; color: #92400e; }
    .overall-pass {
      background: #d1fae5; padding: 10px 16px; border-radius: 6px;
      font-weight: 600; color: #065f46; margin-bottom: 20px;
    }
    .overall-fail {
      background: #fee2e2; padding: 10px 16px; border-radius: 6px;
      font-weight: 600; color: #991b1b; margin-bottom: 20px;
    }
    table {
      width: 100%; border-collapse: collapse; background: #fff;
      border-radius: 8px; overflow: hidden;
      box-shadow: 0 1px 4px rgba(0,0,0,0.1);
    }
    th {
      background: #1a2a3a; color: #fff; padding: 12px 16px;
      text-align: left; font-size: 0.85rem; letter-spacing: 0.03em;
    }
    td { padding: 10px 16px; border-bottom: 1px solid #f0f2f5; font-size: 0.9rem; }
    tr:last-child td { border-bottom: none; }
    tr:hover td { background: #f8f9fa; }
    .status-passed  { color: #065f46; font-weight: 600; }
    .status-failed  { color: #991b1b; font-weight: 600; }
    .status-error   { color: #991b1b; font-weight: 600; }
    .status-skipped { color: #92400e; }
    .detail { color: #555; font-size: 0.82rem; }
    .time   { color: #888; font-size: 0.85rem; white-space: nowrap; }
    .error-box {
      background: #fee2e2; border-left: 4px solid #dc2626;
      padding: 16px; border-radius: 4px; color: #991b1b;
    }
"""

_STATUS_LABELS = {
    "passed":  "✓ PASS",
    "failed":  "✗ FAIL",
    "error":   "✗ ERROR",
    "skipped": "⊘ SKIP",
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
    """Render the HTML string for the test-case list."""
    total   = len(cases)
    passed  = sum(1 for c in cases if c["status"] == "passed")
    failed  = sum(1 for c in cases if c["status"] in ("failed", "error"))
    skipped = sum(1 for c in cases if c["status"] == "skipped")
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    overall_class = "overall-pass" if failed == 0 else "overall-fail"
    overall_text  = (
        f"✓ All {total} tests passed"
        if failed == 0
        else f"✗ {failed} of {total} tests failed"
    )

    rows = []
    for c in cases:
        css   = f"status-{c['status']}"
        label = _STATUS_LABELS.get(c["status"], c["status"])
        msg   = escape(c["message"][:300]) if c["message"] else ""
        t     = f"{c['time']:.2f}s" if c["time"] else "—"
        rows.append(
            f"      <tr>\n"
            f"        <td>{escape(c['name'])}</td>\n"
            f"        <td class=\"{css}\">{label}</td>\n"
            f"        <td class=\"time\">{t}</td>\n"
            f"        <td class=\"detail\">{msg}</td>\n"
            f"      </tr>"
        )

    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>G4OCCT \u2014 Test Results</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <h1>G4OCCT Test Results</h1>\n"
        f"    <p class=\"meta\">Generated: {timestamp}</p>\n"
        "    <div class=\"summary\">\n"
        f"      <div class=\"stat stat-total\"><div class=\"stat-label\">Total</div>"
        f"<div class=\"stat-value\">{total}</div></div>\n"
        f"      <div class=\"stat stat-passed\"><div class=\"stat-label\">Passed</div>"
        f"<div class=\"stat-value\">{passed}</div></div>\n"
        f"      <div class=\"stat stat-failed\"><div class=\"stat-label\">Failed</div>"
        f"<div class=\"stat-value\">{failed}</div></div>\n"
        f"      <div class=\"stat stat-skipped\"><div class=\"stat-label\">Skipped</div>"
        f"<div class=\"stat-value\">{skipped}</div></div>\n"
        "    </div>\n"
        f"    <div class=\"{overall_class}\">{overall_text}</div>\n"
        "    <table>\n"
        "      <thead>\n"
        "        <tr><th>Test Name</th><th>Status</th><th>Time</th><th>Message</th></tr>\n"
        "      </thead>\n"
        "      <tbody>\n"
        + "\n".join(rows) + "\n"
        "      </tbody>\n"
        "    </table>\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n"
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>G4OCCT \u2014 Test Results</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <h1>G4OCCT Test Results</h1>\n"
        f"    <p class=\"meta\">Generated: {timestamp}</p>\n"
        f"    <div class=\"error-box\">Could not generate report: {escape(message)}</div>\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <junit.xml> <output.html>", file=sys.stderr)
        sys.exit(1)

    xml_path, html_path = sys.argv[1], sys.argv[2]
    Path(html_path).parent.mkdir(parents=True, exist_ok=True)

    try:
        cases = _parse_junit_xml(xml_path)
        html  = _render_report(cases)
    except (ET.ParseError, FileNotFoundError, OSError, ValueError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        html = _render_error(str(exc))

    Path(html_path).write_text(html, encoding="utf-8")
    print(f"Test report written to: {html_path}")


if __name__ == "__main__":
    main()
