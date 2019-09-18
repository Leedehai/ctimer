#!/usr/bin/env python

import sys

if "-p" in sys.argv or "--print" in sys.argv: # yeah I know argpase
    sys.stdout.write("hello, world (from stdout)\n")
    sys.stderr.write("hello, ctimer (from stderr)\n")

# busy loop
acc = 0
while True:
    acc += 1
    if acc >= 1e6:
        break
