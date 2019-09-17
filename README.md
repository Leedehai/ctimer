# ctimer

Often times I find it frustrating that there is no simple, standalone program <u>**written in C/C++**</u> that
can do the simple trick:
- measures [processor time](https://en.wikipedia.org/wiki/CPU_time) of an arbitrary program running in a subprocess,
- terminates the subprocess if it exceeds a predefined limit.

So here you have it:)

## Prerequisites
- OS: Linux (kernel 2.6+) or macOS 10.12+
- build tool: GNU Make 3.81+
- compiler: Clang (GCC is not tested but should work) with C++14 support
- run tests: Python2 or Python3

## How to build
```sh
make ctimer
```

## How to use
- For help:
```
$ ctimer --help
usage: ctimer [-h] [-v] program [args ...]

ctimer: measure a program's processor time

positional arguments:
    program          path to the inspected program
    args             commandline arguments

optional arguments:
    -h, --help       print this help message and exit
    -v, --verbose    (dev) print verbosely

optional environment vairables:
    CTIMER_STATS     file to write stats, default: (stdout)
    CTIMER_TIMEOUT   processor time limit (ms), default: 2000
```

- Examples:
```sh
# default configs: output = (stdout), timeout = 2000
ctimer out/some_program --foo 42

# timeout and output file set by environment variables
CTIMER_TIMEOUT=100 CTIMER_STATS=res.txt ctimer out/some_program --foo 42

# (dev) enable verbose printout
ctimer -v out/some_program --foo 42

# you can be playful
./ctimer -v ./ctimer -v ./ctimer -h
```

##### License
[MIT License](LICENSE.txt)

###### EOF
