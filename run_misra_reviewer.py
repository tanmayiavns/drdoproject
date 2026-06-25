"""
run_misra_review.py
===================
Full MISRA C compliance pipeline:

  Step 1 — Run MyChecker.exe on a C/C++ source file
  Step 2 — Parse all Rule warnings from checker output
  Step 3 — Build a structured prompt from warnings + source code
  Step 4 — Call misra_ai_reviewer.py (LLM) for fix suggestions
  Step 5 — Save JSON + TXT report to misra_reports\ folder

Usage:
    python run_misra_review.py <source_file.cpp>

Examples:
    python run_misra_review.py test.cpp
    python run_misra_review.py myproject\main.c

Environment variables:
    OPENAI_API_KEY      (required for OpenAI)
    MISRA_AI_API_KEY    (alternative to OPENAI_API_KEY)
    MISRA_AI_BASE_URL   (default: https://api.openai.com/v1)
    MISRA_AI_MODEL      (default: gpt-4o-mini)

For local Ollama (no API key needed):
    set MISRA_AI_BASE_URL=http://localhost:11434/v1
    set MISRA_AI_MODEL=llama3
"""

import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

# =============================================================================
# CONFIGURATION — edit these two paths to match your project layout
# =============================================================================
CHECKER_EXE = r".\build\MyChecker.exe"
REVIEWER_PY = r".\misra_ai_reviewer (1).py"
CLANG_FLAGS  = ["--", "-std=c11", "-x", "c"]
OUTPUT_DIR   = "misra_reports"
# =============================================================================


# ---------------------------------------------------------------------------
# Step 1: Run the checker
# ---------------------------------------------------------------------------
def run_checker(source_file: str) -> str:
    """
    Run MyChecker.exe on source_file.
    Returns combined stdout + stderr as a single string.
    """
    cmd = [CHECKER_EXE, source_file] + CLANG_FLAGS
    print(f"\n[1/4] Running checker ...")
    print(f"      Command : {' '.join(cmd)}")

    result = subprocess.run(cmd, capture_output=True, text=True)

    # Clang-tidy based checkers write warnings to stderr
    output = result.stdout + result.stderr
    print(f"      Output  : {len(output.splitlines())} lines, "
          f"{len(output)} characters")
    return output


# ---------------------------------------------------------------------------
# Step 2: Parse warnings
# ---------------------------------------------------------------------------
def parse_warnings(raw_output: str) -> list:
    """
    Parse checker output into a list of warning dicts.

    Handles two common clang-tidy output formats:

    Format A (full path):
        C:/path/file.cpp:12:5: warning: Rule 8: message [checker]

    Format B (short, no path):
        test.cpp:12:5: warning: Rule 8: message
    """
    warnings = []
    seen = set()

    # Primary pattern - captures file:line:col: warning: Rule N: message
    pattern = re.compile(
        r"^(?P<file>[^:\n]+\.(?:cpp|c|h|hpp))"
        r":(?P<line>\d+):(?P<col>\d+):\s*warning:\s*"
        r"(?P<message>Rule\s+\d+[^\[^\n]*)",
        re.IGNORECASE | re.MULTILINE
    )

    for m in pattern.finditer(raw_output):
        key = (m.group("file").strip(),
               int(m.group("line")),
               m.group("message").strip())
        if key not in seen:
            seen.add(key)
            warnings.append({
                "file"   : m.group("file").strip(),
                "line"   : int(m.group("line")),
                "col"    : int(m.group("col")),
                "message": m.group("message").strip(),
                "rule"   : extract_rule_number(m.group("message")),
            })

    # Fallback - any line that mentions "Rule N"
    if not warnings:
        for line in raw_output.splitlines():
            if re.search(r"Rule\s+\d+", line, re.IGNORECASE):
                warnings.append({
                    "file"   : "unknown",
                    "line"   : 0,
                    "col"    : 0,
                    "message": line.strip(),
                    "rule"   : extract_rule_number(line),
                })

    return warnings


def extract_rule_number(text: str) -> int:
    """Extract the integer rule number from a warning message."""
    m = re.search(r"Rule\s+(\d+)", text, re.IGNORECASE)
    return int(m.group(1)) if m else 0


def group_by_rule(warnings: list) -> dict:
    """Return {rule_number: [warning, ...]} sorted by rule number."""
    groups = {}
    for w in warnings:
        key = w["rule"]
        groups.setdefault(key, []).append(w)
    return dict(sorted(groups.items()))


# ---------------------------------------------------------------------------
# Step 3: Build the LLM prompt
# ---------------------------------------------------------------------------
def build_prompt(source_file: str,
                 warnings: list,
                 source_code: str) -> str:
    """
    Build a structured prompt that misra_ai_reviewer.py will receive.
    Groups violations by rule number for cleaner LLM output.
    """
    grouped    = group_by_rule(warnings)
    rule_count = len(grouped)
    total      = len(warnings)

    lines = [
        "You are a MISRA C compliance expert.",
        "",
        "The checker found the following MISRA violations in the C source file below.",
        "For EACH rule violation provide:",
        "  1. Rule number and its MISRA description",
        "  2. The non-compliant line(s) from the source",
        "  3. A corrected replacement snippet",
        "  4. A one-line explanation of the fix",
        "",
        f"Source file      : {source_file}",
        f"Total violations : {total} across {rule_count} rule(s)",
        "",
        "=" * 60,
        "VIOLATIONS BY RULE",
        "=" * 60,
    ]

    for rule_num, ws in grouped.items():
        label = f"Rule {rule_num}" if rule_num else "Unknown"
        lines.append(f"\n{label}  -  {len(ws)} occurrence(s)")
        for w in ws[:10]:          # cap at 10 per rule to keep prompt size sane
            loc = f"line {w['line']}" if w["line"] else "unknown line"
            lines.append(f"  * [{loc}]  {w['message']}")
        if len(ws) > 10:
            lines.append(f"  ... and {len(ws) - 10} more occurrences")

    lines += [
        "",
        "=" * 60,
        "SOURCE CODE",
        "=" * 60,
        source_code,
    ]

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Step 4: Call misra_ai_reviewer.py
# ---------------------------------------------------------------------------
def call_llm_reviewer(prompt: str) -> str:
    """
    Write the prompt to a temp file and call misra_ai_reviewer.py,
    which expects exactly one argument: the path to the prompt file.
    """
    print("\n[3/4] Calling LLM reviewer ...")

    # Check environment variables so the user gets a clear error early
    base_url  = os.environ.get("MISRA_AI_BASE_URL", "https://api.openai.com/v1")
    is_ollama = ("localhost:11434" in base_url or
                 "127.0.0.1:11434" in base_url)

    if not is_ollama:
        api_key = (os.environ.get("MISRA_AI_API_KEY") or
                   os.environ.get("OPENAI_API_KEY"))
        if not api_key:
            print("      ERROR: OPENAI_API_KEY is not set.", file=sys.stderr)
            print("      Set it with:  set OPENAI_API_KEY=sk-...",
                  file=sys.stderr)
            return "LLM review skipped - OPENAI_API_KEY not set."

    model = os.environ.get("MISRA_AI_MODEL", "gpt-4o-mini")
    print(f"      Model   : {model}")
    print(f"      API URL : {base_url}")

    # Write prompt to a temporary file
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".txt", delete=False, encoding="utf-8"
    ) as tmp:
        tmp.write(prompt)
        tmp_path = tmp.name

    try:
        result = subprocess.run(
            [sys.executable, REVIEWER_PY, tmp_path],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.returncode != 0:
            err = result.stderr.strip()
            print(f"      ERROR from reviewer: {err}", file=sys.stderr)
            return f"LLM review failed:\n{err}"

        print(f"      Done    : received {len(result.stdout)} characters from LLM")
        return result.stdout

    except subprocess.TimeoutExpired:
        return "LLM review failed: request timed out after 120 seconds."
    finally:
        os.unlink(tmp_path)


# ---------------------------------------------------------------------------
# Step 5: Save reports
# ---------------------------------------------------------------------------
def save_report(source_file: str,
                warnings: list,
                raw_output: str,
                ai_review: str) -> tuple:
    """
    Save two report files:
      <name>_<timestamp>.json  - machine-readable
      <name>_<timestamp>.txt   - human-readable
    Returns (json_path, txt_path).
    """
    timestamp  = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name  = Path(source_file).stem
    report_dir = Path(OUTPUT_DIR)
    report_dir.mkdir(exist_ok=True)

    grouped = group_by_rule(warnings)

    # JSON report
    json_path   = report_dir / f"{base_name}_{timestamp}.json"
    report_data = {
        "generated_at"   : timestamp,
        "source_file"    : str(source_file),
        "total_warnings" : len(warnings),
        "rules_violated" : sorted(grouped.keys()),
        "warnings"       : warnings,
        "ai_review"      : ai_review,
    }
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(report_data, f, indent=2)

    # TXT report
    txt_path = report_dir / f"{base_name}_{timestamp}.txt"
    sep      = "=" * 60
    dash     = "-" * 60

    with open(txt_path, "w", encoding="utf-8") as f:

        f.write(f"{sep}\n")
        f.write("MISRA C COMPLIANCE REPORT\n")
        f.write("DIA COE IIT Hyderabad - Indigenous Code Compliance Framework\n")
        f.write(f"{sep}\n")
        f.write(f"Source file      : {source_file}\n")
        f.write(f"Generated at     : {timestamp}\n")
        f.write(f"Total violations : {len(warnings)}\n")
        f.write(f"Rules violated   : {len(grouped)}\n")
        f.write("\n")

        f.write(f"{dash}\n")
        f.write("VIOLATION SUMMARY\n")
        f.write(f"{dash}\n")
        f.write(f"{'Rule':<12}{'Count':>8}  {'Description'}\n")
        f.write(f"{'-'*12}{'-'*8}  {'-'*36}\n")
        for rule_num, ws in grouped.items():
            label = f"Rule {rule_num}" if rule_num else "Unknown"
            first = ws[0]["message"]
            desc  = first[:50] + ("..." if len(first) > 50 else "")
            f.write(f"{label:<12}{len(ws):>8}  {desc}\n")
        f.write("\n")

        f.write(f"{dash}\n")
        f.write("RAW CHECKER OUTPUT\n")
        f.write(f"{dash}\n")
        f.write(raw_output)
        f.write("\n\n")

        f.write(f"{dash}\n")
        f.write("AI REVIEW AND FIX SUGGESTIONS\n")
        f.write(f"{dash}\n")
        f.write(ai_review)
        f.write("\n")

    return str(json_path), str(txt_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> int:
    print("=" * 60)
    print(" MISRA C Compliance Pipeline")
    print(" DIA COE IIT Hyderabad")
    print("=" * 60)

    if len(sys.argv) < 2:
        print("Usage:   python run_misra_review.py <source_file.cpp>")
        print("Example: python run_misra_review.py test.cpp")
        return 1

    source_file = sys.argv[1]

    # File existence checks
    if not Path(source_file).exists():
        print(f"ERROR: source file not found: {source_file}")
        return 1

    if not Path(CHECKER_EXE).exists():
        print(f"ERROR: checker not found at : {CHECKER_EXE}")
        print(f"       Edit CHECKER_EXE at the top of this script.")
        return 1

    if not Path(REVIEWER_PY).exists():
        print(f"ERROR: reviewer not found at: {REVIEWER_PY}")
        print(f"       Edit REVIEWER_PY at the top of this script.")
        return 1

    # Step 1: Run checker
    raw_output = run_checker(source_file)

    # Step 2: Parse warnings
    warnings = parse_warnings(raw_output)
    grouped  = group_by_rule(warnings)

    print(f"\n[2/4] Parsing warnings ...")
    print(f"      Total violations : {len(warnings)}")
    print(f"      Rules violated   : {len(grouped)}")
    print()
    for rule_num, ws in grouped.items():
        label = f"Rule {rule_num:<4}" if rule_num else "Unknown "
        bar   = "#" * min(len(ws), 40)
        print(f"      {label}  {len(ws):>4}x  {bar}")

    if not warnings:
        print("\n      No MISRA violations found.")
        print("      Check CHECKER_EXE path if this is unexpected.")

    # Step 3: Build prompt
    source_code = Path(source_file).read_text(encoding="utf-8",
                                               errors="replace")
    prompt = build_prompt(source_file, warnings, source_code)
    print(f"\n      Prompt size : {len(prompt)} characters")

    # Step 4: Call LLM reviewer
    ai_review = call_llm_reviewer(prompt)

    # Step 5: Save reports
    print(f"\n[4/4] Saving reports ...")
    json_path, txt_path = save_report(
        source_file, warnings, raw_output, ai_review
    )
    print(f"      JSON : {json_path}")
    print(f"      TEXT : {txt_path}")

    print()
    print("=" * 60)
    print(" COMPLETE")
    print("=" * 60)
    print(f"  Source file      : {source_file}")
    print(f"  Total violations : {len(warnings)}")
    print(f"  Rules violated   : {len(grouped)}")
    print(f"  Reports saved to : {OUTPUT_DIR}\\")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())