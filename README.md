# ctimer

Often times I find it frustrating that there is no simple, standalone program <u>**written in C/C++**</u> that
can do the simple trick:
- measures [processor time](https://en.wikipedia.org/wiki/CPU_time) of an arbitrary program in a subprocess,
- terminates the subprocess if it exceeds a predefined limit,
- report exit status and time measurements (milliseconds) in JSON format.

So here you have it:)

> Motivation:<br>I need a timer to run tests for my projects. I want to run them in parallel, so measuring the processor time, i.e. time truly spent on execution, instead of the wall time is important.<br>I rolled one myself in Python, which works fine, but I was not satisfied with the need of accomodating both Python2 and Python3 (since not everyone uses Python3), and for Python2 I had to write a non-daemonic process class, together with a number of other aerobatics.

## Prerequisites
- OS: Linux (kernel 2.6+) or macOS 10.12+
- build tool: GNU Make 3.81+
- compiler: Clang (GCC is not tested but should work) with C++14 standard
- run samples: Python2 or Python3

## How to build
```sh
make ctimer
```

## How to use
- For help:
```
$ ./ctimer --help
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
    CTIMER_TIMEOUT   processor time limit (sec), default: 2
```

- Examples:
```sh
# default configs: output = (stdout), timeout = 2
./ctimer out/some_program --foo 42

# timeout and output file set by environment variables
CTIMER_TIMEOUT=5 CTIMER_STATS=res.txt ./ctimer out/some_program --foo 42

# (dev) enable verbose printout
ctimer -v out/some_program --foo 42
```

- Play:
```sh
# try with the samples
./ctimer samples/quick.py
./ctimer samples/quick.py --print
./ctimer samples/infinite.py
./ctimer samples/sigsegv.py

# you can be playful
./ctimer -v ./ctimer -v ./ctimer -h
```

##### License
[MIT License](LICENSE.txt)

###### EOF
