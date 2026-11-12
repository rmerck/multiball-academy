#!/usr/bin/env python3
"""Checks that the game's own code was compiled with its hardening flags.

Usage: python3 -I tools/check_hardening.py <build directory>

For a Linux build that is neither Debug nor sanitized (CI runs it on the
Release build). Looking at the linked game for __stack_chk_fail or a
fortified call proves nothing about this project's code: SDL is linked in
statically and brings those symbols with it. So this looks at what the
project's own files were compiled with:

1. In <build directory>/compile_commands.json, every translation unit under
   src/, tools/ and tests/ must end up with the stack protector, FORTIFY
   level 3 with optimisation on, control-flow protection, the standard
   library assertions and position-independent code. A later flag that undoes
   an earlier one counts, as it does for the compiler.
2. Two object files known to need the protections must ask for them: the
   object of src/core/Format.cpp (vsnprintf into a caller's buffer) must
   reference a fortified __*_chk function, and it and the object of
   src/core/Sha256.cpp (arrays on the stack) must reference
   __stack_chk_fail. Flags that are named but have no effect fail here.

Exits 0 if all of that holds, 1 with one line per problem if not, and 2 if
it could not check.
"""
import json
import os
import platform
import re
import shlex
import subprocess
import sys

PROJECT_DIRECTORIES = ("src", "tools", "tests")
# Source file -> what its object must reference.
FORTIFIED_CALL = re.compile(r"^__[a-z0-9]+_chk$")
PROBES = {
    "src/core/Format.cpp": ("fortified call", "stack check"),
    "src/core/Sha256.cpp": ("stack check",),
}


def give_up(message):
    print("check_hardening: " + message, file=sys.stderr)
    sys.exit(2)


def arguments_of(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def defines_of(arguments):
    """The macros defined on the command line, after every -D and -U."""
    defined = {}
    pending = None
    for argument in arguments:
        if pending is not None:
            kind, text = pending, argument
            pending = None
        elif argument in ("-D", "-U"):
            pending = argument
            continue
        elif argument.startswith(("-D", "-U")):
            kind, text = argument[:2], argument[2:]
        else:
            continue
        name, _, value = text.partition("=")
        if kind == "-U":
            defined.pop(name, None)
        else:
            defined[name] = value if "=" in text else "1"
    return defined


def last_matching(arguments, pattern):
    found = None
    for argument in arguments:
        if re.fullmatch(pattern, argument):
            found = argument
    return found


def flag_problems(arguments):
    problems = []
    defined = defines_of(arguments)

    stack = last_matching(arguments, r"-f(no-)?stack-protector(-[a-z]+)?")
    if stack not in ("-fstack-protector-strong", "-fstack-protector-all"):
        problems.append("no -fstack-protector-strong (in effect: %s)" % stack)

    if defined.get("_FORTIFY_SOURCE") != "3":
        problems.append("_FORTIFY_SOURCE is %s, not 3" % defined.get("_FORTIFY_SOURCE", "undefined"))
    optimisation = last_matching(arguments, r"-O.*")
    if optimisation in (None, "-O0"):
        problems.append("not optimised (%s), so _FORTIFY_SOURCE does nothing" % optimisation)

    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64", "i386", "i686"):
        flow = last_matching(arguments, r"-fcf-protection(=[a-z]+)?")
        if flow not in ("-fcf-protection=full", "-fcf-protection"):
            problems.append("no -fcf-protection=full (in effect: %s)" % flow)
    elif machine in ("aarch64", "arm64"):
        flow = last_matching(arguments, r"-mbranch-protection=.*")
        if flow != "-mbranch-protection=standard":
            problems.append("no -mbranch-protection=standard (in effect: %s)" % flow)

    if "_GLIBCXX_ASSERTIONS" not in defined:
        problems.append("_GLIBCXX_ASSERTIONS is not defined")
    libcxx = defined.get("_LIBCPP_HARDENING_MODE", "undefined")
    if libcxx not in ("_LIBCPP_HARDENING_MODE_FAST", "_LIBCPP_HARDENING_MODE_EXTENSIVE",
                      "_LIBCPP_HARDENING_MODE_DEBUG"):
        problems.append("_LIBCPP_HARDENING_MODE is %s" % libcxx)

    position = last_matching(arguments, r"-f(no-)?(pic|PIC|pie|PIE)")
    if position not in ("-fPIE", "-fPIC", "-fpie", "-fpic"):
        problems.append("not position independent (in effect: %s)" % position)
    return problems


def undefined_symbols(object_path):
    try:
        output = subprocess.run(["nm", "-u", object_path], check=True, capture_output=True,
                                text=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        give_up("cannot read symbols of %s: %s" % (object_path, error))
    return {line.split()[-1] for line in output.splitlines() if line.split()}


def symbol_problems(need, symbols):
    problems = []
    if "stack check" in need and "__stack_chk_fail" not in symbols:
        problems.append("object has no reference to __stack_chk_fail")
    if "fortified call" in need and not any(
            FORTIFIED_CALL.match(s) and s != "__stack_chk_fail" for s in symbols):
        problems.append("object has no reference to a fortified __*_chk function")
    return problems


def main(argv):
    if len(argv) != 2:
        give_up("usage: check_hardening.py <build directory>")
    build = os.path.realpath(argv[1])
    root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    database = os.path.join(build, "compile_commands.json")
    try:
        with open(database, encoding="utf-8") as handle:
            entries = json.load(handle)
    except (OSError, ValueError) as error:
        give_up("cannot read %s: %s" % (database, error))

    failures = []
    checked = 0
    probed = set()
    for entry in entries:
        source = os.path.realpath(os.path.join(entry["directory"], entry["file"]))
        relative = os.path.relpath(source, root).replace(os.sep, "/")
        parts = relative.split("/")
        if parts[0] not in PROJECT_DIRECTORIES or "_deps" in parts:
            continue  # A dependency, or generated: not this project's code.
        checked += 1
        arguments = arguments_of(entry)
        problems = flag_problems(arguments)
        if relative in PROBES:
            probed.add(relative)
            if "output" not in entry:
                give_up("no object file named for %s (CMake 3.20 or newer writes it)" % relative)
            object_path = os.path.join(entry["directory"], entry["output"])
            problems += symbol_problems(PROBES[relative], undefined_symbols(object_path))
        failures += ["%s: %s" % (relative, problem) for problem in problems]

    if checked == 0:
        give_up("no project file in %s" % database)
    for missing in sorted(set(PROBES) - probed):
        give_up("%s is not in %s" % (missing, database))
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        print("check_hardening: %d problem(s) in %d project files" % (len(failures), checked),
              file=sys.stderr)
        return 1
    print("check_hardening: %d project files carry the hardening flags; %d objects checked"
          % (checked, len(PROBES)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
