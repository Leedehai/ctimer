#!/usr/bin/env python3
# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.
# -----
# Run test: ./test.py

import os, sys
import subprocess
import json

TESTS = [
    {
        "file": "samples/infinite.py",
        "child_exit": "timeout",
        "time": lambda t: t > 1000
    },
    {
        "file": "samples/quick.py",
        "child_exit": "return",
        "time": lambda t: t < 500
    },
    {
        "file": "samples/sigint.py",
        "child_exit": "return",
        "time": lambda t: t < 500
    },
    {
        "file": "samples/sigkill.py",
        "child_exit": "signal",
        "time": lambda t: t < 500
    },
    {
        "file": "samples/sleep.py",
        "child_exit": "return",
        "time": lambda t: t < 500
    },
    {
        "file": "samples/foo",
        "child_exit": "quit",
        "time": lambda t: t < 50
    },
    {
        "file": "samples/text.txt",
        "child_exit": "quit",
        "time": lambda t: t < 50
    },
]

STATS_FILENAME = "stats.log"


def run_one(test_item):
    with open(os.devnull,
              'w') as devnull:  # Python2 doesn't have subprocess.DEVNULL
        exit_code = subprocess.Popen(
            ' '.join(["./ctimer", test_item["file"]]),
            shell=True,
            env={
                "CTIMER_STATS": STATS_FILENAME
            },
            stdout=devnull,
            stderr=subprocess.STDOUT).wait()
        if exit_code != 0:
            print("%s: ctimer exits with %d" % (test_item["file"], exit_code))
            return False
    if not os.path.isfile(STATS_FILENAME):
        print("%s: stat file not found" % test_item["file"])
        return False
    with open(STATS_FILENAME, 'r') as res_file:
        res_dict = json.load(res_file)
        os.remove(STATS_FILENAME)

        def _has_primary_field(key):
            if key not in res_dict:
                print("%s: result missing field '%s'" %
                      (test_item["file"], key))
                return False
            return True

        def _has_secondary_field(primary, key, obj_type, nullable=False):
            if key not in res_dict[primary]:
                print("%s: result missing field '%s' in '%s'" %
                      (test_item["file"], key, primary))
                return False
            if obj_type != None and type(res_dict[primary][key]) != obj_type:
                if (not nullable) or (res_dict[primary][key] != None):
                    print(
                        "%s: result field '%s' in '%s' expects to be of type '%s'%s, but is '%s'"
                        % (test_item["file"], key, primary, obj_type,
                           "or NoneType" if nullable else "",
                           type(res_dict[primary][key])))
                    return False
            return True

        if not (_has_primary_field("pid") and
                _has_primary_field("exit") and _has_secondary_field(
                    "exit", "type", obj_type=None
                )  # obj_type: Python2: 'unicode', Python3: 'str' - nasty!
                and _has_secondary_field("exit", "repr", int, True) and
                _has_secondary_field("exit", "desc", obj_type=None)  # see above
                and _has_primary_field("times_ms") and
                _has_secondary_field("times_ms", "total", float) and
                _has_secondary_field("times_ms", "user", float) and
                _has_secondary_field("times_ms", "sys", float)):
            return False
        if len(res_dict) != 3:
            print("%s: result expects 3 fields, %d found" %
                  (test_item["file"], len(res_dict)))
            return False
        if len(res_dict["exit"]) != 3:
            print("%s: result['exit'] expects 3 fields, %d found" %
                  (test_item["file"], len(res_dict["exit"])))
            return False
        if len(res_dict) != 3:
            print("%s: result['times_ms'] expects 3 fields, %d found" %
                  (test_item["file"], len(res_dict["times_ms"])))
            return False
        if res_dict["exit"]["type"] != test_item["child_exit"]:
            print("%s: exit type expects '%s', but is '%s'" %
                  (test_item["file"], test_item["child_exit"],
                   res_dict["exit"]["type"]))
            return False
        if test_item["time"](res_dict["times_ms"]["total"]) == False:
            print("%s: total time %d not in expected range" %
                  (test_item["file"], test_item["times_ms"]["total"]))
            return False
    return True


def main():
    if not os.path.isfile("ctimer"):
        print("[Error] no executable 'ctimer' found; did you build ctimer?")
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
