#!/usr/bin/env python3
"""Check that OSC addresses in src/ and API.md match.

Collects quoted "/..." string literals from src/ (line comments stripped) and
backticked `/...` addresses from API.md, then reports any address found in
only one of them. Exits 1 on a mismatch.

Usage (from the repository root): python3 tools/check_api_addresses.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# literals that are only pieces of addresses assembled at runtime
# (e.g. "/act/" + action + "/param", bench toggles' address + "/ack")
FRAGMENTS = {"/act/", "/ack/", "/param", "/ack"}
# the addresses those pieces produce
ACTIONS = ("click", "press", "release")
ASSEMBLED = (
    {f"/act/{a}/param" for a in ACTIONS}
    | {f"/ack/{a}/param" for a in ACTIONS}
)

ADDRESS = r"/[a-z0-9_]+(?:/[a-z0-9_*]+)*/?"


def code_addresses():
    found = set()
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".hpp"):
            continue
        text = re.sub(r"//[^\n]*", "", path.read_text())
        for match in re.finditer(rf'"({ADDRESS})"', text):
            found.add(match.group(1))
    return (found - FRAGMENTS) | ASSEMBLED


def doc_addresses():
    text = (ROOT / "API.md").read_text()
    found = {m.group(1) for m in re.finditer(rf"`({ADDRESS})`", text)}
    return found - FRAGMENTS


def main():
    code, doc = code_addresses(), doc_addresses()
    # API.md documents some address families with a wildcard
    wildcards = {d for d in doc if "*" in d}
    doc -= wildcards
    for wildcard in wildcards:
        pattern = re.compile(
            re.escape(wildcard).replace(r"\*", "[a-z0-9_]+") + "$"
        )
        code = {c for c in code if not pattern.match(c)}

    only_code, only_doc = sorted(code - doc), sorted(doc - code)
    for address in only_code:
        print(f"in src/ but not API.md: {address}")
    for address in only_doc:
        print(f"in API.md but not src/: {address}")
    if only_code or only_doc:
        return 1
    print(f"OK: {len(code | doc)} addresses match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
