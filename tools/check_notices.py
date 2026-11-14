#!/usr/bin/env python3
"""Checks that the notices shown on the About screen carry the words of the
licence files they are taken from, in their sections and in order.

Usage: check_notices.py <Notices.hpp> game:<LICENSE> <THIRD_PARTY_NOTICES.md>
                        [<section>:<licence file>]...

The notices are paragraphs, one per line feed pair, in sections that each
begin with a heading paragraph, in this order and each once:

  game      MULTIBALL ACADEMY: <copyright>. Licensed under the <title>.
  box2d     BOX2D: MIT LICENSE
  sdl       SDL (SIMPLE DIRECTMEDIA LAYER): ZLIB LICENSE
  yuv2rgb   YUV2RGB (PART OF SDL): BSD 3-CLAUSE LICENSE

The game's section is built from LICENSE, whose terms are not shown in
full: its heading carries each "Required Notice:" copyright (less the
address in brackets) and the licence's title (its first "# " heading), and
the only other paragraph is the address of the terms (the first
<https://...> line). Nothing else may be in it.

Each library section, less its heading, must say exactly what the matching
"```text" block of THIRD_PARTY_NOTICES.md says, white space aside: the
blocks are the libraries in the order above. Its "```notice" blocks are
for code not in the released builds and are not shown. A licence file given as
<section>:<path> (the pinned sources' own) must have every paragraph in its
section. A first paragraph that is only the licence's title ("MIT License")
is not part of the notice and is skipped.
"""
import re
import sys

LIBRARIES = [
    ("box2d", "BOX2D: MIT LICENSE"),
    ("sdl", "SDL (SIMPLE DIRECTMEDIA LAYER): ZLIB LICENSE"),
    ("yuv2rgb", "YUV2RGB (PART OF SDL): BSD 3-CLAUSE LICENSE"),
]
GAME_PREFIX = "MULTIBALL ACADEMY: "

problems = []


def problem(text):
    problems.append(text)
    print(text)


def squeeze(text):
    return " ".join(text.split())


def paragraphs_of(text):
    found = [squeeze(p) for p in re.split(r"\n\s*\n", text) if squeeze(p)]
    if found and re.fullmatch(r"[A-Za-z ]+ License", found[0]):
        found = found[1:]
    return found


def notice_paragraphs(header):
    source = open(header, encoding="utf-8").read()
    body = source[source.index("kText"):]
    parts = re.findall(r'"((?:[^"\\\n]|\\.)*)"', body)
    text = "".join(parts)
    text = text.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
    return [squeeze(p) for p in text.split("\n\n")]


def game_notice(path):
    """The game's heading and address, as LICENSE gives them."""
    text = open(path, encoding="utf-8").read()
    title = re.search(r"^# (.+?)\s*$", text, re.M)
    address = re.search(r"^<(https://[^>\s]+)>\s*$", text, re.M)
    if not title or not address:
        problem("no licence title or address in %s" % path)
        return None
    copyrights = []
    for line in text[:title.start()].splitlines():
        if line.startswith("Required Notice:"):
            found = re.fullmatch(r"Required Notice: (Copyright .*?)(?: \(https?://[^)]*\))?\s*",
                                 line)
            notice = found.group(1) if found else ""
            copyrights.append(notice if notice.endswith(".") else notice + ".")
    if not copyrights:
        problem("no Required Notice line above the licence in %s" % path)
        return None
    heading = GAME_PREFIX + " ".join(copyrights) + " Licensed under the %s." % title.group(1)
    return [squeeze(heading), address.group(1)]


def sections_of(paragraphs):
    """Splits the notices at the library headings: {name: paragraphs}."""
    starts = []
    for name, heading in LIBRARIES:
        found = [i for i, p in enumerate(paragraphs) if p == heading]
        if len(found) != 1:
            problem("the heading '%s' is in the notices %d times, not once" % (heading, len(found)))
            return None
        starts.append((found[0], name))
    if [i for i, _ in starts] != sorted(i for i, _ in starts):
        problem("the library sections are not in the order %s"
                % ", ".join(name for name, _ in LIBRARIES))
        return None
    sections = {"game": paragraphs[:starts[0][0]]}
    for k, (start, name) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(paragraphs)
        sections[name] = paragraphs[start + 1:end]
    return sections


def main():
    args = sys.argv[1:]
    if len(args) < 3 or not args[1].startswith("game:"):
        sys.exit(__doc__)
    paragraphs = notice_paragraphs(args[0])
    sections = sections_of(paragraphs)
    if sections is None:
        sys.exit(1)

    expected = game_notice(args[1][len("game:"):])
    if expected is not None and sections["game"] != expected:
        problem("the game's notice is not what LICENSE gives, and only that:\n  is:     %s\n"
                "  wanted: %s" % (sections["game"], expected))
    print("checked the game's notice against %s" % args[1][len("game:"):])

    third_party = args[2]
    blocks = re.findall(r"^```text\n(.*?)^```", open(third_party, encoding="utf-8").read(),
                        re.S | re.M)
    if len(blocks) != len(LIBRARIES):
        problem("%s has %d licence blocks, not %d" % (third_party, len(blocks), len(LIBRARIES)))
    for (name, _), block in zip(LIBRARIES, blocks):
        if squeeze(" ".join(paragraphs_of(block))) != squeeze(" ".join(sections[name])):
            problem("the %s notice differs from its block in %s" % (name, third_party))
    print("checked %d licence blocks of %s" % (len(blocks), third_party))

    names = [name for name, _ in LIBRARIES]
    for arg in args[3:]:
        name, _, path = arg.partition(":")
        if name not in names or not path:
            sys.exit("not <section>:<licence file>, with a section of %s: %s" % (names, arg))
        section = squeeze(" ".join(sections[name]))
        found = paragraphs_of(open(path, encoding="utf-8").read())
        if not found:
            problem("no text in %s" % path)
        for paragraph in found:
            if paragraph not in section:
                problem("not in the %s notice, from %s:\n  %s" % (name, path, paragraph[:200]))
        print("checked %d paragraphs of %s" % (len(found), path))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
