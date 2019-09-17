/**
 * Copyright: see README and LICENSE under the project root directory.
 * Author: @Leedehai
 *
 * File: ctimer.cc
 * ---------------------------
 * The implementation of ctimer (see README.md for more info).
 * Most could be in C, but I like C++'s grammar sugars like r-strings.
 */

#include <stdio.h>
#include <string.h>
/* C++ std */
#include <numeric>
#include <string>
/* POSIX system */
#include <unistd.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <sys/times.h>
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
#define CHECKED_SYSCALL(expr, description, error_action) \
    if ((expr) == -1) {                                  \
        ERROR_FMT("syscall %s: %s", description, strerror(errno)); \
        error_action;                                    \
    }
#define PARENT_ERR return 1;
#define CHILD_ERR  abort();

enum ChildExit_t { kNormalExit, kSignalExit, kOtherExit, kTimeout };

static const char *kStatsFilenameEnvVar = "CTIMER_STATS";
static const char *kTimeoutEnvVar = "CTIMER_TIMEOUT";
static const unsigned int kDefaultTimeoutSec = 2;

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
    %-15s  processor time limit (sec), default: %d
)";

static const char *kReportJSONFormat = R"({
    "pid" : %d,
    "exit" : {
        "type" : "%s",
        "code" : %d,
        "desc" : "%s"
    },
    "time_ms" : {
        "total" : %.3f,
        "user"  : %.3f,
        "sys"   : %.3f
    }
})";

/**** helpers ****/

/** helper: print help */
static void printHelp() {
    fprintf(stdout, helpMessage,
            kStatsFilenameEnvVar, kTimeoutEnvVar, kDefaultTimeoutSec);
}

/** helper: interpret exit type */
static const char *exitTypeString(ChildExit_t exit_type) {
    switch (exit_type) {
    case kNormalExit: return "normal";
    case kSignalExit: return "signaled";
    case kOtherExit:  return "unknown";
    case kTimeout:    return "timeout";
    default:          return "?";
    }
}

/** helper: return description of |exit_numeric_repr| */
static const char *exitReprString(ChildExit_t exit_type, int exit_numeric_repr) {
    switch (exit_type) {
    case kNormalExit: return "exit code";
    case kSignalExit: return strsignal(exit_numeric_repr);
    case kOtherExit:  return "unknown";
    case kTimeout:    return strsignal(exit_numeric_repr);
    default:          return "?";
    }
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
    unsigned timeout_sec;
    /** the inspected command: the program name, followed by
     * whatever args it has, then NULL (i.e. command[argc] == NULL) */
    char **command;
    /** the file to write stats; write to stdout if NULL */
    char *stats_filename;
};

/** print stats; return 0 on success, 1 otherwise */
int reportTimes(ChildExit_t exit_type,
                 const WorkParams &params,
                 pid_t pid,
                 int exit_numeric_repr,
                 const tms &tms_obj) {
    /* milleseconds - to prevent overflow, devide first instead of multiply first */
    double child_user_msec = -1, child_sys_msec = -1;
    if (exit_type != kTimeout) {
        child_user_msec  = 1000 * (1.0 * tms_obj.tms_cutime / CLOCKS_PER_SEC);
        child_sys_msec   = 1000 * (1.0 * tms_obj.tms_cstime / CLOCKS_PER_SEC);
    }

    char buffer[512];
    memset(buffer, 0, sizeof(buffer));
    int snprintf_ret = snprintf(buffer, sizeof(buffer), kReportJSONFormat,
        pid,
        exitTypeString(exit_type), exit_numeric_repr,
        exitReprString(exit_type, exit_numeric_repr),
        exit_type != kTimeout ? child_user_msec + child_sys_msec : 1000 * params.timeout_sec,
        child_user_msec, child_sys_msec);
    if (snprintf_ret == -1) { return 1; }

    int fprintf_ret;
    if (!params.stats_filename) {
        fprintf_ret = fprintf(stderr, "%s\n", buffer);
    } else {
        FILE *stats_file = fopen(params.stats_filename, "w");
        if (!stats_file) {
            ERROR_FMT("error at openning file %s", params.stats_filename);
            return 1;
        }
        fprintf_ret = fprintf(stats_file, "%s\n", buffer);
    }
    return fprintf_ret != -1 ? 0 : 1;
}

/** main works; return 0 on success, 1 otherwise */
int work(const WorkParams &params) {
    pid_t child_pid = -1;
    CHECKED_SYSCALL(child_pid = fork(), "fork", PARENT_ERR);

    if (child_pid == 0) {
        /* child process */
        rlimit rlimit_obj = { params.timeout_sec, params.timeout_sec };
        CHECKED_SYSCALL(setrlimit(RLIMIT_CPU, &rlimit_obj), "setrlimit in child, abort", CHILD_ERR);
        CHECKED_SYSCALL(execvp(params.command[0], params.command), "exec in child, abort", CHILD_ERR);
    } else {
        /* parent process */
        VERBOSE("child forked; pid %d", child_pid);
        int child_status = 0;
        tms tms_obj;
        memset(&tms_obj, 0, sizeof(tms_obj));
        /* user time, system time of parent and child */
        CHECKED_SYSCALL(waitpid(child_pid, &child_status, 0), "waitpid", PARENT_ERR);
        if (WIFEXITED(child_status)) {
            int exit_status = WEXITSTATUS(child_status);
            CHECKED_SYSCALL(times(&tms_obj), "times", PARENT_ERR);
            reportTimes(kNormalExit, params, child_pid, exit_status, tms_obj);
            VERBOSE("child %d exited with %d", child_pid, exit_status);
        } else if (WIFSIGNALED(child_status)) {
            int sig = WTERMSIG(child_status);
            CHECKED_SYSCALL(times(&tms_obj), "times", PARENT_ERR);
            if (sig == SIGXCPU || sig == SIGKILL /* Linux */) {
                VERBOSE("timeout, %d sec", params.timeout_sec);
                return reportTimes(kTimeout, params, child_pid, sig, tms_obj);
            } else {
                VERBOSE("child terminated by signal %d (%s)", sig, strsignal(sig));
                return reportTimes(kSignalExit, params, child_pid, sig, tms_obj);
            }
        } else {
            VERBOSE("child exited abnormally without signal, pid = %d", child_pid);
            return reportTimes(kOtherExit, params, child_pid, -1, tms_obj);
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
    params.timeout_sec = kDefaultTimeoutSec;
    char *timeout_env = getenv(kTimeoutEnvVar);
    if (timeout_env && isShortDigit(timeout_env, 5) && timeout_env[0] != '0') {
        params.timeout_sec = atoi(timeout_env);
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
                ERROR_FMT("option '%s' not recognized, use '-h' for help", argv[i]);
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
    VERBOSE("timeout:      %d", params.timeout_sec);
    VERBOSE("command:      %s", std::accumulate(params.command + 1, params.command + params.argc,
        std::string(params.command[0]), [](const std::string &acc, const char *part){
            return acc + " " + part;
        }).c_str());

    return work(params);
}
