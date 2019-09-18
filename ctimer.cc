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
#include <signal.h>
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
#define CHECKED_SYSCALL(expr, description, error_action) \
    if ((expr) == -1) {                                  \
        ERROR_FMT("syscall %s: %s", description, strerror(errno)); \
        error_action;                                    \
    }
#define PARENT_ERR return 1;
#define CHILD_ERR  raise(SIGQUIT);

enum ChildExit_t { kNormal, kSignal, kQuit, kTimeout, kUnknown };

static const char *kStatsFilenameEnvVar = "CTIMER_STATS";
static const char *kTimeoutEnvVar       = "CTIMER_TIMEOUT";
static const unsigned int kDefaultTimeoutMillisec = 1500;

static const char *kHelpMessage = R"(usage: ctimer [-h] [-v] program [args ...]

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

static const char *kReportJSONFormat = R"({
    "pid" : %d,
    "exit" : {
        "type" : "%s",
        "repr" : %s,
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
    fprintf(stdout, kHelpMessage,
            kStatsFilenameEnvVar, kTimeoutEnvVar, kDefaultTimeoutMillisec);
}

/** helper: interpret exit type */
static const char *exitTypeString(ChildExit_t exit_type) {
    switch (exit_type) {
    case kNormal:  return "normal";
    case kSignal:  return "signal";
    case kQuit:    return "quit";
    case kTimeout: return "timeout";
    case kUnknown: return "unknown";
    default:       return "?";
    }
}

/** helper: return description of |exit_numeric_repr| */
static const char *exitReprString(ChildExit_t exit_type, int exit_numeric_repr) {
    switch (exit_type) {
    case kNormal:  return "exit code";
    case kSignal:  return strsignal(exit_numeric_repr);
    case kQuit:    return "child error before exec";
    case kTimeout: return "child runtime limit (ms)";
    case kUnknown: return "unknown";
    default:       return "?";
    }
}

/** helper: check whether a null-terminated string is all digits */
static bool isShortDigitStr(const char *s, int maxCount) {
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
    unsigned timeout_msec;
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
                 const rusage &rusage_obj) {
    auto tv2msec = [](const timeval &tv) {
        return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
    };
    double child_user_msec = tv2msec(rusage_obj.ru_utime);
    double child_sys_msec  = tv2msec(rusage_obj.ru_stime);

    char exit_str_repr[16] = { 0 };
    if (exit_numeric_repr >= 0) {
        snprintf(exit_str_repr, sizeof(exit_str_repr), "%d", exit_numeric_repr);
    } else {
        snprintf(exit_str_repr, sizeof(exit_str_repr), "null");
    }

    char buffer[512] = { 0 };
    int snprintf_ret = snprintf(buffer, sizeof(buffer), kReportJSONFormat,
        pid,
        exitTypeString(exit_type), exit_str_repr,
        exitReprString(exit_type, exit_numeric_repr),
        child_user_msec + child_sys_msec,
        child_user_msec, child_sys_msec);
    if (snprintf_ret == -1) { return 1; }

    int fprintf_ret;
    if (!params.stats_filename) {
        fprintf_ret = fprintf(stdout, "%s\n", buffer);
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
    int t_sec = params.timeout_msec / 1000, t_usec = 1000 * (params.timeout_msec % 1000);
    itimerval interval = { /* to */{ 0, 0 }, /* from */{ t_sec, t_usec } };

    pid_t child_pid = -1;
    CHECKED_SYSCALL(child_pid = fork(), "fork", PARENT_ERR);

    if (child_pid == 0) {
        /* child process */
        /* ITIMER_PROF: decrements when the process executes OR
         * when the system is executing on behalf of the process. */
        CHECKED_SYSCALL(setitimer(ITIMER_PROF, &interval, 0), "setitimer in child", CHILD_ERR);
        CHECKED_SYSCALL(execvp(params.command[0], params.command), "exec in child", CHILD_ERR);
    } else {
        /* parent process */
        VERBOSE("child forked; pid %d", child_pid);
        int child_status = 0;
        CHECKED_SYSCALL(waitpid(child_pid, &child_status, 0), "waitpid", PARENT_ERR);

        rusage rusage_obj;
        memset(&rusage_obj, 0, sizeof(rusage_obj));
        CHECKED_SYSCALL(getrusage(RUSAGE_CHILDREN, &rusage_obj), "getrusage", PARENT_ERR);

        if (WIFEXITED(child_status)) {
            int exit_status = WEXITSTATUS(child_status);
            VERBOSE("child %d exited with %d", child_pid, exit_status);
            return reportTimes(kNormal, params, child_pid, exit_status, rusage_obj);
        } else if (WIFSIGNALED(child_status)) {
            int sig = WTERMSIG(child_status);
            if (sig == SIGPROF) {
                VERBOSE("child %d timeout, %d msec", child_pid, params.timeout_msec);
                return reportTimes(kTimeout, params, child_pid, params.timeout_msec, rusage_obj);
            } else if (sig == SIGQUIT) {
                VERBOSE("child %d quit", child_pid);
                return reportTimes(kQuit, params, child_pid, -1, rusage_obj);
            } else {
                VERBOSE("child %d terminated by signal %d (%s)", child_pid, sig, strsignal(sig));
                return reportTimes(kSignal, params, child_pid, sig, rusage_obj);
            }
        } else {
            VERBOSE("child exited abnormally without signal, pid = %d", child_pid);
            return reportTimes(kUnknown, params, child_pid, -1, rusage_obj);
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
    params.timeout_msec = kDefaultTimeoutMillisec;
    if (char *timeout_env = getenv(kTimeoutEnvVar)) {
        if (timeout_env[0] != '0' && isShortDigitStr(timeout_env, 5)) {
            params.timeout_msec = atoi(timeout_env);
        } else {
            ERROR_FMT("%s value '%s' is led by '0', not pure digits, or too long",
                kTimeoutEnvVar, timeout_env);
            return 1;
        }
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
    VERBOSE("timeout (ms): %d", params.timeout_msec);
    VERBOSE("command:      %s", std::accumulate(params.command + 1, params.command + params.argc,
        std::string(params.command[0]), [](const std::string &acc, const char *part){
            return acc + " " + part;
        }).c_str());

    return work(params);
}
