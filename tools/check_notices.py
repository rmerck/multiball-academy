#!/usr/bin/env python3
"""Checks that the notices shown on the About screen carry the words of the
licence files they are taken from.

Usage: check_notices.py <Notices.hpp> <licence file>...

Every paragraph of every licence file must appear in the header's text,
white space aside. A first line that is only the licence's title
("MIT License") is not part of the notice and is skipped.
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


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    notices = notice_text(sys.argv[1])
    missing = 0
    for path in sys.argv[2:]:
        paragraphs = re.split(r"\n\s*\n", open(path, encoding="utf-8").read())
        paragraphs = [squeeze(p) for p in paragraphs if squeeze(p)]
        if paragraphs and re.fullmatch(r"[A-Za-z ]+ License", paragraphs[0]):
            paragraphs = paragraphs[1:]
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
