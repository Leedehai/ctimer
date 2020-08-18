#!/usr/bin/env python3
# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.
# -----
# Run test: ./test.py

import json
import os
import re
import subprocess
import sys

# yapf: disable
TESTS = [
    {
        # This test case verifies ctimer can handle timeout.
        "file": "samples/infinite.py",
        "child_exit": "timeout",
        "child_exit_repr": 1500,
        "child_exit_desc": "child runtime limit (ms)",
        "time": lambda t: t >= 1500,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test case verifies ctimer can handle timeout.
        "file": "samples/infinite.sh",
        "child_exit": "timeout",
        "child_exit_repr": 1500,
        "child_exit_desc": "child runtime limit (ms)",
        "time": lambda t: t >= 1500,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test case verifies ctimer can handle normal execution
        # with exit 0.
        "file": "samples/quick.py",
        "child_exit": "return",
        "child_exit_repr": 0,
        "child_exit_desc": "exit code",
        "time": lambda t: t < 500,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test verifies ctimer can handle a signal exit.
        "file": "samples/sigkill.py",
        "child_exit": "signal",
        "child_exit_repr": 9,  # SIGKILL value.
        "child_exit_desc": re.compile(r".*kill.*", re.IGNORECASE),
        "time": lambda t: t < 500,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test case verifies that sleep time (1.0 sec not on processor)
        # is not included in the time measured.
        "file": "samples/sleep.py",
        "child_exit": "return",
        "child_exit_repr": 0,
        "child_exit_desc": "exit code",
        "time": lambda t: t < 500,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test case verifies that ctimer can handle missing
        # program.
        "file": "samples/missing",
        "child_exit": "quit",
        "child_exit_repr": None,
        "child_exit_desc": "child error before exec",
        "time": lambda t: t < 50,
        "maxrss": lambda v: v >= 1000,
    },
    {
        # This test case verifies that ctimer can handle
        # execution permission error.
        "file": "samples/text.txt",
        "child_exit": "quit",
        "child_exit_repr": None,
        "child_exit_desc": "child error before exec",
        "time": lambda t: t < 50,
        "maxrss": lambda v: v >= 1000,
    },
]
# yapf: enable

STATS_FILENAME = "stats.log"


def err(s):
    sys.stderr.write(s + "\n")


def run_one(test_item):
    with open(os.devnull,
              'w') as devnull:  # Python2 doesn't have subprocess.DEVNULL
        exit_code = subprocess.Popen(' '.join(["./ctimer", test_item["file"]]),
                                     shell=True,
                                     env={"CTIMER_STATS": STATS_FILENAME},
                                     stdout=devnull,
                                     stderr=subprocess.STDOUT).wait()
        if exit_code != 0:
            err("%s: ctimer exits with %d" % (test_item["file"], exit_code))
            return False
    if not os.path.isfile(STATS_FILENAME):
        err("%s: stat file not found" % test_item["file"])
        return False
    with open(STATS_FILENAME, 'r') as res_file:
        res_dict = json.load(res_file)
        os.remove(STATS_FILENAME)

        def _has_primary_field(key, obj_type):
            if key not in res_dict:
                err("%s: result missing field '%s'" % (test_item["file"], key))
                return False
            if not isinstance(res_dict[key], obj_type):
                err("%s: field '%s' should be of type '%s', but is '%s'" %
                    (test_item["file"], key, obj_type, type(res_dict[key])))
                return False
            return True

        def _has_secondary_field(primary, key, obj_type, *, nullable=False):
            if key not in res_dict[primary]:
                err("%s: result missing field '%s' in '%s'" %
                    (test_item["file"], key, primary))
                return False
            if not isinstance(res_dict[primary][key], obj_type):
                if (not nullable) or (res_dict[primary][key] != None):
                    err("%s: result field '%s' in '%s' should be of type '%s'%s"
                        ", but is '%s'" %
                        (test_item["file"], key, primary, obj_type, " or None"
                         if nullable else "", type(res_dict[primary][key])))
                    return False
            return True

        if not (isinstance(res_dict, dict) and _has_primary_field("pid", int)
                and _has_primary_field("maxrss_kb", int)
                and _has_primary_field("exit", dict)
                and _has_secondary_field("exit", "type", str)
                and _has_secondary_field("exit", "repr", int, nullable=True)
                and _has_secondary_field("exit", "desc", str)
                and _has_primary_field("times_ms", dict)
                and _has_secondary_field("times_ms", "total", float)
                and _has_secondary_field("times_ms", "user", float)
                and _has_secondary_field("times_ms", "sys", float)):
            return False
        if len(res_dict) != 4:
            err("%s: result expects 4 fields, %d found" %
                (test_item["file"], len(res_dict)))
            return False
        if not test_item["maxrss"](res_dict["maxrss_kb"]):
            err("%s: max resident set size %d not in expected range" %
                (test_item["file"], test_item["maxrss"]))
            return False
        if len(res_dict["exit"]) != 3:
            err("%s: result['exit'] expects 3 fields, %d found" %
                (test_item["file"], len(res_dict["exit"])))
            return False
        if len(res_dict["times_ms"]) != 3:
            err("%s: result['times_ms'] expects 3 fields, %d found" %
                (test_item["file"], len(res_dict["times_ms"])))
            return False
        if res_dict["exit"]["type"] != test_item["child_exit"]:
            err("%s: exit type expects '%s', but is '%s'" %
                (test_item["file"], test_item["child_exit"],
                 res_dict["exit"]["type"]))
            return False
        if res_dict["exit"]["repr"] != test_item["child_exit_repr"]:
            err("%s: exit repr expects %s, but is %s" %
                (test_item["file"], test_item["child_exit_repr"],
                 res_dict["exit"]["repr"]))
            return False
        if isinstance(test_item["child_exit_desc"], str):
            if res_dict["exit"]["desc"] != test_item["child_exit_desc"]:
                err("%s: exit desc expects '%s', but is '%s'" %
                    (test_item["file"], test_item["child_exit_desc"],
                     res_dict["exit"]["desc"]))
                return False
        else:  # regex
            if not test_item["child_exit_desc"].match(res_dict["exit"]["desc"]):
                err("%s: exit desc expects pattern '%s', but is '%s'" %
                    (test_item["file"], str(test_item["child_exit_desc"]),
                     res_dict["exit"]["desc"]))
                return False
        if test_item["time"](res_dict["times_ms"]["total"]) == False:
            err("%s: total time %d not in expected range" %
                (test_item["file"], test_item["times_ms"]["total"]))
            return False
    return True


def main():
    if not os.path.isfile("ctimer"):
        err("[Error] no executable 'ctimer' found; did you build ctimer?")
        return 1
    error_count = 0
    for test_item in TESTS:
        if run_one(test_item):
            print("\x1b[32m[OK] %s\x1b[0m" % test_item["file"])
        else:
            error_count += 1
            print("\x1b[31m[Error] %s\x1b[0m" % test_item["file"])
    if error_count:
        print("error count: %d out of %d tests" % (error_count, len(TESTS)))
    else:
        print("all is fine")
    return 0 if error_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
