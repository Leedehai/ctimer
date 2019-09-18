# ctimer

Often times I find it frustrating that there is no simple, standalone program <u>**written in C/C++**</u> that
can do the simple trick:
- measure [processor time](https://en.wikipedia.org/wiki/CPU_time) of an arbitrary program, including any descendant processes that program may spawn,
- terminate the program if it exceeds a predefined limit,
- report exit status and time measurements (msec) in JSON format.

So here you have it:)

> Motivation:<br>I need a timer to run tests for my projects. I want to run them in parallel, so it is crucial to measure the processor time, i.e. time truly spent on execution, instead of the wall time.<br>I rolled one myself in Python, which works fine, but I was not satisfied with the need of accomodating both Python2 and Python3 (since not everyone uses Python3), and for Python2 I had to write a non-daemonic process class, together with a number of other aerobatics.

## Prerequisites
- OS: Linux (kernel 2.6+) or macOS 10.12+ (no Windows, because POSIX is needed)
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
    CTIMER_TIMEOUT   processor time limit (ms), default: 1500
```

- Examples:
```sh
# default configs: output = (stdout), timeout = 1500 ms
./ctimer out/some_program --foo 42

# timeout and output file set by environment variables
CTIMER_TIMEOUT=5000 CTIMER_STATS=res.txt ./ctimer out/some_program --foo 42

# (dev) enable verbose printout
ctimer -v out/some_program --foo 42
```

- Play:
```sh
# try with the samples
./ctimer samples/infinite.py      # timeout
./ctimer samples/quick.py         # normal exit
./ctimer samples/quick.py --print # normal exit
./ctimer samples/sleep.py         # normal exit
./ctimer samples/sigint.py        # normal exit: Python catches SIGINT
./ctimer samples/sigkill.py       # killed by signal SIGKILL
./ctimer samples/foo              # quit: file not found
./ctimer samples/text.txt         # quit: permission doesn't allow execution

# you can be playful
./ctimer -v ./ctimer -v ./ctimer -h
```

## Result
```sh
$ ./ctimer date
Tue Sep 17 21:52:52 PDT 2019
{
    "pid" : 35871,
    "exit" : {
        "type" : "normal",
        "code" : 0,
        "desc" : "exit code"
    },
    "time_ms" : {
        "total" : 2.090,
        "user"  : 0.753,
        "sys"   : 1.337
    }
}
```
```sh
$ CTIMER_STATS=res.txt ./ctimer samples/infinite.py && cat res.txt
{
    "pid" : 35981,
    "exit" : {
        "type" : "timeout signal",
        "code" : 27,
        "desc" : "Profiling timer expired: 27"
    },
    "time_ms" : {
        "total" : 2102.789,
        "user"  : 2079.663,
        "sys"   : 23.126
    }
}
```

##### License
[MIT License](LICENSE.txt)

###### EOF
