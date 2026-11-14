#!/usr/bin/env python3
"""Checks that the notices shown on the About screen carry the words of the
licence files they are taken from.

Usage: check_notices.py <Notices.hpp> [game:]<licence file>...

Every paragraph of every licence file must appear in the header's text,
white space aside. A first line that is only the licence's title
("MIT License") is not part of the notice and is skipped. In a Markdown
file (.md) only the text of its fenced code blocks counts, each block a
licence of its own.

A file given as game:<path> is the game's own LICENSE, whose terms are not
shown in full: the notices must carry its copyright (the "Required Notice:"
line, less the address in brackets), the licence's title (its first
heading) and the address of the terms (the first <https://...> line).
"""
import re
import sys


def squeeze(text):
    return " ".join(text.split())


def notice_text(header):
    source = open(header, encoding="utf-8").read()
    body = source[source.index("kText"):]
    parts = re.findall(r'"((?:[^"\\\n]|\\.)*)"', body)
    text = "".join(parts)
    text = text.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
    return squeeze(text)


def check_game(notices, path):
    text = open(path, encoding="utf-8").read()
    wanted = [
        ("copyright", r"^Required Notice: (Copyright .*?)(?: \(https?://[^)]*\))?\s*$"),
        ("licence title", r"^# (.+?)\s*$"),
        ("licence address", r"^<(https://[^>\s]+)>\s*$"),
    ]
    missing = 0
    for what, pattern in wanted:
        found = re.search(pattern, text, re.M)
        if not found:
            print("no %s in %s" % (what, path))
            missing += 1
        elif squeeze(found.group(1)) not in notices:
            print("not in the notices, the %s from %s:\n  %s" % (what, path, found.group(1)))
            missing += 1
    print("checked the game's notice against %s" % path)
    return missing


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    notices = notice_text(sys.argv[1])
    missing = 0
    for arg in sys.argv[2:]:
        if arg.startswith("game:"):
            missing += check_game(notices, arg[len("game:"):])
            continue
        path = arg
        text = open(path, encoding="utf-8").read()
        texts = [text]
        if path.endswith(".md"):
            texts = re.findall(r"^```[a-z]*\n(.*?)^```", text, re.S | re.M)
        paragraphs = []
        for licence in texts:
            found = re.split(r"\n\s*\n", licence)
            found = [squeeze(p) for p in found if squeeze(p)]
            if found and re.fullmatch(r"[A-Za-z ]+ License", found[0]):
                found = found[1:]
            paragraphs += found
        if not paragraphs:
            print("no text in", path)
            missing += 1
        for paragraph in paragraphs:
            if paragraph not in notices:
                print("not in the notices, from %s:\n  %s" % (path, paragraph[:200]))
                missing += 1
        print("checked %d paragraphs of %s" % (len(paragraphs), path))
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
