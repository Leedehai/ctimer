#!/usr/bin/env python

import sys, time

sys.stderr.write("""* NOTE * * * * * * * * * * * * * * * *
* Though this program takes > 1 s (1000 ms) to complete,
* its processor time should be much smaller than that.
* END NOTE * * * * * * * * * * * * * *
""")
# switch context voluntarily
time.sleep(1.0)
