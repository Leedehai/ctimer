/**
 * Copyright: see README and LICENSE under the project root directory.
 * Author: @Leedehai
 *
 * File: ctimer.cc
 * ---------------------------
 * The implementation of ctimer (see README.md for more info).
 * Most could be in C, but I like C++'s grammar sugars like r-strings.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* C++ std */
#include <numeric>
#include <string>
/* POSIX system */
#include <unistd.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>

static bool g_verbose = false;
/* NOTE stderr is unbuffered, while stdout is buffered */
#define VERBOSE(format, ...)         \
    if (g_verbose) {                 \
        fprintf(stderr, "[verbose] ");        \
        fprintf(stderr, format, __VA_ARGS__); \
        fprintf(stderr, "\n");                \
    }
#define ERROR_STR(str)  fprintf(stderr, "[Error] %s\n", str);
#define ERROR_FMT(format, ...)            \
    fprintf(stderr, "[Error] ");          \
    fprintf(stderr, format, __VA_ARGS__); \
    fprintf(stderr, "\n");
#define CHECKED_SYSCALL(expr, description) \
    if ((expr) == -1) {                    \
        ERROR_FMT("syscall %s: %s", description, strerror(errno)); \
        return 1;                          \
    }

static const char *kStatsFilenameEnvVar = "CTIMER_STATS";
static const char *kTimeoutEnvVar = "CTIMER_TIMEOUT";
static const unsigned int kDefaultTimeoutMillisec = 2000;
static const char *helpMessage = R"(usage: ctimer [-h] [-v] program [args ...]

ctimer: measure a program's processor time

positional arguments:
    program          path to the inspected program
    args             commandline arguments

optional arguments:
    -h, --help       print this help message and exit
    -v, --verbose    (dev) print verbosely

optional environment vairables:
    %-15s  file to write stats, default: (stdout)
    %-15s  processor time limit (ms), default: %d
)";

/**** helpers ****/

/** helper: print help */
static void printHelp() {
    fprintf(stdout, helpMessage,
            kStatsFilenameEnvVar, kTimeoutEnvVar, kDefaultTimeoutMillisec);
}

/** helper: check whether a null-terminated string is all digits */
static bool isShortDigit(const char *s, int maxCount) {
    if (maxCount <= 0) { return false; }
    short int count = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9' && count < maxCount) {
            ++s;
            ++count;
        } else {
            return false;
        }
    }
    return true;
}

/** helper: compare flag string */
static bool matchFlag(const char *in, const char *short_flag, const char *long_flag) {
    if (!in) { return false; }
    if (strcmp(short_flag, in) == 0 || strcmp(long_flag, in) == 0) {
        return true;
    }
    return false;
}

/**** main works ****/

struct WorkParams {
    /** argument count in command, including the program name */
    int argc;
    /** limit of runtime on processor */
    int timeout_millisec;
    /** the inspected command: the program name, followed by
     * whatever args it has, then NULL (i.e. command[argc] == NULL) */
    char **command;
    /** the file to write stats; write to stdout if NULL */
    char *stats_filename;
};

/** main works */
int work(const WorkParams &params) {
    pid_t child_pid = -1;
    CHECKED_SYSCALL(child_pid = fork(), "fork");

    if (child_pid == 0) { /* child process */
        rlimit rlimit_obj = { 2, 2 };
        setrlimit(RLIMIT_CPU, &rlimit_obj);
        CHECKED_SYSCALL(execvp(params.command[0], params.command), "exec");
    } else { /* parent process */
        VERBOSE("child forked; pid %d", child_pid);
        int child_status;
        CHECKED_SYSCALL(waitpid(child_pid, &child_status, 0), "waitpid");
        if (WIFEXITED(child_status)) {
            int exit_status = WEXITSTATUS(child_status);
            VERBOSE("child %d exited with %d", child_pid, exit_status);
        } else if (WIFSIGNALED(child_status)) {
            int sig = WTERMSIG(child_status);
            const char *sig_name = strsignal(sig);
            VERBOSE("child terminated by signal %d (%s)", sig, sig_name);
        } else {
            VERBOSE("child exited abnormally without signal, pid = %d", child_pid);
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        ERROR_STR("arguments expected, use '-h' for help");
        return 1;
    }

    WorkParams params;

    /** file to write stats; stdout if NULL */
    params.stats_filename = getenv(kStatsFilenameEnvVar);

    /** time limit for the program */
    params.timeout_millisec = kDefaultTimeoutMillisec;
    char *timeout_env = getenv(kTimeoutEnvVar);
    if (timeout_env && isShortDigit(timeout_env, 5) && timeout_env[0] != '0') {
        params.timeout_millisec = atoi(timeout_env);
    }

    int command_start = -1;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (matchFlag(argv[i], "-h", "--help")) {
                printHelp();
                return 0;
            } else if (matchFlag(argv[i], "-v", "--verbose")) {
                g_verbose = true;
            } else {
                ERROR_FMT("option '%s' not recognized, use '-h' for help", argv[1]);
                return 1;
            }
        } else {
            command_start = i;
            break;
        }
    }
    if (command_start < 0) {
        ERROR_STR("program name expected, use '-h' for help");
        return 1;
    }

    params.argc = argc - command_start;
    params.command = argv + command_start;

    VERBOSE("stats output: %s", params.stats_filename ? params.stats_filename : "(stdout)");
    VERBOSE("timeout:      %d", params.timeout_millisec);
    VERBOSE("command:      %s", std::accumulate(params.command + 1, params.command + params.argc,
        std::string(params.command[0]), [](const std::string &acc, const char *part){
            return acc + " " + part;
        }).c_str());

    return work(params);
}
