#!/usr/bin/env python

import os, signal

os.kill(os.getpid(), signal.SIGKILL)
# should not raise SIGSEGV or SIGABRT, otherwise macOS's pre-installed
# Python will complain and macOS will create a dialog asking if a crash
# report needs to be sent to Apple.
