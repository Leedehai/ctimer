#!/usr/bin/env python
# SIGSEGV typically raised by program itself

import os, signal

os.kill(os.getpid(), signal.SIGSEGV)
