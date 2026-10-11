#!/usr/bin/env python3
"""Validate every section file and regenerate index.json.

Layout:
  <subject>/<section>.json   e.g. biology/4.8.json, chemistry/2.1.json
  index.json                 generated - do not edit by hand

Section file:
  {"subject": "biology", "section": "4.8", "title": "Short title",
   "cards": [{"id": "bio-4.8-01", "q": "...", "a": "..."}]}

Run from the repo root:  python3 tools/build_index.py
Exits non-zero (and writes nothing) if any file is invalid.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAX_Q, MAX_A, MAX_TITLE = 120, 200, 40

def section_key(s):
    return [int(p) if p.isdigit() else p for p in re.split(r"[.\-]", s)]

def main():
    errors, subjects, seen_ids = [], {}, set()
    for path in sorted(ROOT.glob("*/*.json")):
        if path.parent.name in ("tools", ".git"):
            continue
        rel = path.relative_to(ROOT).as_posix()
        try:
            d = json.loads(path.read_text(encoding="utf-8"))
        except Exception as e:
            errors.append(f"{rel}: invalid JSON ({e})"); continue
        subj, sec, title, cards = d.get("subject"), d.get("section"), d.get("title", ""), d.get("cards")
        if subj != path.parent.name:
            errors.append(f"{rel}: subject '{subj}' must match folder '{path.parent.name}'")
        if sec != path.stem:
            errors.append(f"{rel}: section '{sec}' must match file name '{path.stem}'")
        if not title or not title.isascii() or len(title) > MAX_TITLE:
            errors.append(f"{rel}: title must be ASCII, 1-{MAX_TITLE} chars")
        if not isinstance(cards, list) or not cards:
            errors.append(f"{rel}: needs a non-empty 'cards' list"); continue
        for i, c in enumerate(cards):
            where = f"{rel} card {i + 1}"
            cid, q, a = c.get("id"), c.get("q"), c.get("a")
            if not cid or cid in seen_ids:
                errors.append(f"{where}: missing or duplicate id '{cid}'")
            seen_ids.add(cid)
            for name, val, mx in (("q", q, MAX_Q), ("a", a, MAX_A)):
                if not isinstance(val, str) or not val.strip():
                    errors.append(f"{where}: '{name}' is empty")
                elif not val.isascii():
                    errors.append(f"{where}: '{name}' has non-ASCII characters")
                elif len(val) > mx:
                    errors.append(f"{where}: '{name}' is {len(val)} chars (max {mx})")
        subjects.setdefault(path.parent.name, []).append(
            {"section": sec, "title": title, "file": rel, "count": len(cards)})

    if errors:
        print("Validation failed:\n  " + "\n  ".join(errors)); sys.exit(1)

    index = {"version": 2, "subjects": [
        {"name": name, "sections": sorted(secs, key=lambda s: section_key(s["section"]))}
        for name, secs in sorted(subjects.items())]}
    (ROOT / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    total = sum(s["count"] for subj in index["subjects"] for s in subj["sections"])
    print(f"index.json: {len(index['subjects'])} subject(s), "
          f"{sum(len(s['sections']) for s in index['subjects'])} section(s), {total} cards")

if __name__ == "__main__":
    main()
