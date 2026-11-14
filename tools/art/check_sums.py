# Checks that the committed art is what tools/art/SHA256SUMS records: every file listed has that hash,
# and every PNG under assets/ is listed. Needs nothing but Python (ctest art_sums).
#   python3 tools/art/check_sums.py [repo_root]
# After regenerating into a scratch root, the same file tells whether this platform draws the same bytes:
#   python3 tools/art/check_sums.py <scratch_root>
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main(root):
    listed = {}
    with open(os.path.join(HERE, "SHA256SUMS")) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            digest, name = line.rstrip("\n").split("  ", 1)
            listed[name] = digest
    problems = []
    for name, digest in sorted(listed.items()):
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            problems.append("missing: " + name)
            continue
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() != digest:
                problems.append("differs: " + name)
    assets = os.path.join(root, "assets")
    for base, _, files in os.walk(assets):
        for file in files:
            name = os.path.relpath(os.path.join(base, file), root).replace(os.sep, "/")
            if name.endswith(".png") and name not in listed:
                problems.append("not in SHA256SUMS: " + name)
    if problems:
        sys.exit("\n".join(problems))
    print("check_sums: %d file(s) ok" % len(listed))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, "..", "..")))
