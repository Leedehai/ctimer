#!/usr/bin/env python
# SIGINT typically delivered by user.

import os
import sys
import signal

sys.stderr.write("""* NOTE * * * * * * * * * * * * * * * *
* Python interpreter's default handler will
* \x1b[1mcatch SIGINT, print stack trace, and exit with 1\x1b[0m.
* END NOTE * * * * * * * * * * * * * *
""")

os.kill(os.getpid(), signal.SIGINT)
