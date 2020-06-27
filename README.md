# ctimer

[![Build Status](https://travis-ci.com/Leedehai/ctimer.svg?branch=master)](https://travis-ci.com/Leedehai/ctimer)

Often times I find it frustrating that there is no simple, standalone program <u>**written in C/C++**</u> that
can do the simple tricks:
- measure [processor time](https://en.wikipedia.org/wiki/CPU_time) of an arbitrary program, including any descendant processes that program may spawn,
- terminate the program if it exceeds a predefined limit,
- report exit status and time measurements (msec) in JSON format.

So here you have it:)

> Motivation:<br>I needed a timer to [run tests](https://github.com/Leedehai/score). I want to run them in parallel, so it is crucial to measure the processor time, i.e. time truly spent on execution, instead of the wall time.<br>I rolled one myself in Python, which works fine, but I was not satisfied with the speed, and more importantly the need of accomodating both Python2 and Python3 (since not everyone uses Python3), and for Python2 I had to write a non-daemonic process class, together with a number of other aerobatics.

## Prerequisites
- OS: Linux (kernel 2.6+) or macOS 10.12+ (no Windows, because POSIX is needed)
- build tool: GNU Make 3.81+
- compiler: Clang (GCC is not tested but should work) with C++14 standard
- run tests and samples: Python3.7+

## How to build
```sh
make ctimer
```

## How to test
```sh
./test.py # or: make check
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
    CTIMER_STATS      file to write stats in JSON, default: (stdout)
    CTIMER_TIMEOUT    processor time limit (ms), default: 1500
    CTIMER_DELIMITER  delimiter encompassing the stats string
```

> To pose no time limit (effectively), use `CTIMER_TIMEOUT=0`.

- Examples:
```sh
# default configs: output = (stdout), timeout = 1500 ms
./ctimer out/some_program --foo 42

# timeout and output file set by environment variables
CTIMER_TIMEOUT=5000 CTIMER_STATS=res.txt ./ctimer out/some_program --foo 42

# (dev) enable verbose printout
./ctimer -v out/some_program --foo 42
```

- Play:
```sh
# try with the samples
./ctimer samples/infinite.py      # timeout
./ctimer samples/quick.py         # return 0
./ctimer samples/quick.py --print # return 0
./ctimer samples/sleep.py         # return 0
./ctimer samples/sigint.py        # return 0: Python catches SIGINT
./ctimer samples/sigkill.py       # killed by signal SIGKILL
./ctimer samples/foo              # quit: file not found
./ctimer samples/text.txt         # quit: lack execution privilege

# you can be playful
./ctimer -v ./ctimer -v ./ctimer -h
```

## Result
The output is in JSON format, and is a dictionary containing 3 keys:
- `pid`: process ID of the inspected program,
- `exit`: exit status of the inspected program, which is a dictionary:
    - `type`: exit type
    - `repr`: the numeric representation of the exit info, can be `null`
    - `desc`: description of `repr`
- `times_ms`: time measurements, which is a dictionary:
    - `total`: processor time (milliseconds),
    - `user`: processor time spent in user space
    - `sys`: processor time spent in kernel space

```sh
$ ./ctimer date
Tue Sep 17 21:52:52 PDT 2019
{
    "pid" : 35871,
    "exit" : {
        "type" : "return",
        "repr" : 0,
        "desc" : "exit code"
    },
    "times_ms" : {
        "total" : 2.090,
        "user"  : 0.753,
        "sys"   : 1.337
    }
}
```
```sh
$ CTIMER_STATS=res.txt ./ctimer samples/infinite.py && cat res.txt
{
    "pid" : 249775,
    "exit" : {
        "type" : "timeout",
        "repr" : 1500,
        "desc" : "child runtime limit (ms)"
    },
    "times_ms" : {
        "total" : 1501.476,
        "user"  : 1493.490,
        "sys"   : 7.986
    }
}
```

## Some design decisions

**Why use environment variables to pass in custom output filename and timeout value?**

I do not want to handle ambiguous cases of missing argument, and setting these is a rare need.

For example, `./ctimer --xyz foo` could be interpreted as "missing argument for option '--xyz'" or as "missing program name"; `./ctimer --xyz foo bar` could be interpreted as "missing argument for option '--xyz' while executing program 'foo' with argument 'bar'" or as "use argument `--xyz foo` and execute program 'bar'".

This ambiguity could be resolved by introducing double-dash `--` to denote the start of program name, but this is less elegant: `./ctimer -- foo` is ugly. Moreover, I dislike how [GDB](https://www.gnu.org/software/gdb/) handles this ambiguity.<br>Also, name collision should be unlikely because these variable names have prefix `CTIMER_`.

**What's the point of CTIMER_DELIMITER?**

If the inspected program itself has stdout outputs, it may pose a problem for users to extract the stats report from the stdout. In this case, the user can use `CTIMER_DELIMITER` to specify a pattern to denote the beginning and the end of the stats report.

Original:
```sh
$ ./ctimer inspected_program
abc def {} {}
{ some printout }
{
    # ... stats report
}
```

With `CTIMER_DELIMITER`:
```sh
$ CTIMER_DELIMITER=::::::::: ./ctimer inspected_program
abc def {} {}
{ some printout }
:::::::::{
    # ... stats report
}:::::::::
```

Admittedly, to tackle the said problem, a user could also use `CTIMER_STATS` to direct the stats report to a file, but disk IO is slow in a high concurrency use case unless the OS has an in-memory filesystem. Opening a pipe is fine, too, but it adds extra work to consumers.

##### License
[MIT License](LICENSE.txt)

###### EOF
