/**
 * Copyright (c) 2020 Leedehai. All rights reserved.
 * Use of this source code is governed under the MIT LICENSE.txt file.
 * -----
 * The implementation of ctimer (see README.md for more info).
 * Most could be in C, but I like C++'s grammar sugars like r-strings.
 */

#include <stdio.h>
#include <string.h>

#include <cstdlib>
#include <numeric>
#include <string>

#include <signal.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static bool g_verbose = false;
// NOTE stderr is unbuffered, while stdout is buffered.
#define VERBOSE(format, ...)              \
  if (g_verbose) {                        \
    fprintf(stderr, "[verbose] ");        \
    fprintf(stderr, format, __VA_ARGS__); \
    fprintf(stderr, "\n");                \
  }
#define ERROR_STR(str) fprintf(stderr, "[Error] %s\n", str);
#define ERROR_FMT(format, ...)          \
  fprintf(stderr, "[Error] ");          \
  fprintf(stderr, format, __VA_ARGS__); \
  fprintf(stderr, "\n");
#define CHECKED_SYSCALL(expr, description, error_action)       \
  if ((expr) == -1) {                                          \
    ERROR_FMT("syscall %s: %s", description, strerror(errno)); \
    error_action;                                              \
  }
#define PARENT_ERR return 1;
#define CHILD_ERR raise(SIGQUIT);

enum ChildExit { kReturn, kSignal, kQuit, kTimeout, kUnknown };

static const char kStatsFilenameEnvVar[] = "CTIMER_STATS";
static const char kTimeoutEnvVar[] = "CTIMER_TIMEOUT";
static const char kDelimiterEnvVar[] = "CTIMER_DELIMITER";
// NOTE Do not make kEffectiveInfiniteTime a wider integer type, as Linux's
// itimerval struct only guarantees 32-bit or narrower integers.
static const unsigned int kEffectiveInfiniteTime =
    0x7FFFFFFF;  // In msec, over 24 days.
static const unsigned int kDefaultTimeoutMillisec = 1500;

static const char kHelpMessage[] = R"(usage: ctimer [-h] [-v] program [args ...]

ctimer: measure a program's processor time

positional arguments:
    program          path to the inspected program
    args             commandline arguments

optional arguments:
    -h, --help       print this help message and exit
    -v, --verbose    (dev) print verbosely

optional environment vairables:
    %-16s  file to write stats in JSON, default: (stdout)
    %-16s  processor time limit (ms), default: %d
    %-16s  delimiter encompassing the stats string
)";

static const char kReportJSONFormat[] = R"(%s{
  "pid" : %d,
  "maxrss_kb" : %ld,
  "exit" : {
    "type" : "%s",
    "repr" : %s,
    "desc" : "%s"
  },
  "times_ms" : {
    "total" : %.3f,
    "user"  : %.3f,
    "sys"   : %.3f
  }
}%s)";

/// Helper: print help.
static void PrintHelp() {
  fprintf(stdout, kHelpMessage, kStatsFilenameEnvVar, kTimeoutEnvVar,
          kDefaultTimeoutMillisec, kDelimiterEnvVar);
}

/// Helper: interpret exit type.
static const char* GetExitTypeString(ChildExit exit_type) {
  switch (exit_type) {
    case kReturn:
      return "return";
    case kSignal:
      return "signal";
    case kQuit:
      return "quit";
    case kTimeout:
      return "timeout";
    case kUnknown:
      return "unknown";
    default:
      return "?";
  }
}

/// Helper: return description of `exit_numeric_repr`.
static const char* GetExitReprString(ChildExit exit_type,
                                     int exit_numeric_repr) {
  switch (exit_type) {
    case kReturn:
      return "exit code";
    case kSignal:
      return strsignal(exit_numeric_repr);
    case kQuit:
      return "child error before exec";
    case kTimeout:
      return "child runtime limit (ms)";
    case kUnknown:
      return "unknown";
    default:
      return "?";
  }
}

/// Helper: check whether a null-terminated string is all digits.
static bool IsShortDigitStr(const char* s, int maxCount) {
  if (maxCount <= 0) {
    return false;
  }
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

/// Helper: compare flag string.
static bool MatchFlag(const char* in,
                      const char* short_flag,
                      const char* long_flag) {
  if (!in) {
    return false;
  }
  if (strcmp(short_flag, in) == 0 || strcmp(long_flag, in) == 0) {
    return true;
  }
  return false;
}

/**** Main works ****/

struct WorkParams {
  /// Argument count in command, including the program name.
  int argc;
  /// Limit of runtime on processor.
  unsigned timeout_msec;
  /// The inspected command: the program name, followed by
  /// whatever args it has, then NULL(i.e.command[argc] == NULL).
  char** command;
  /// The file to write stats; write to stdout if NULL.
  char* stats_filename;
  /// Delimiter that encompasses the stats string.
  char* delimiter;
};

/// Print stats; return 0 on success, 1 otherwise.
int ReportStats(ChildExit exit_type,
                const WorkParams& params,
                pid_t pid,
                int exit_numeric_repr,
                const rusage& rusage_obj) {
  auto tv2msec = [](const timeval& tv) {
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
  };
  double child_user_msec = tv2msec(rusage_obj.ru_utime);
  double child_sys_msec = tv2msec(rusage_obj.ru_stime);

  char exit_str_repr[16] = {0};
  if (exit_numeric_repr >= 0) {
    snprintf(exit_str_repr, sizeof(exit_str_repr), "%d", exit_numeric_repr);
  } else {
    snprintf(exit_str_repr, sizeof(exit_str_repr), "null");
  }

  // NOTE Only ru_utime, ru_stime are guaranteed by POSIX; other fields are
  // implementation-dependent: https://pubs.opengroup.org/onlinepubs/9699919799/
  // That said, ru_maxrss is present on Linux, macOS. See "man getrusage".
  long maxrss = rusage_obj.ru_maxrss; // KB on Linux and iOS, B on macOS.
  if (std::getenv("RUSAGE_SIZE_BYTES")) {
    maxrss /= 1024;
  }


  char buffer[512] = {0};
  int snprintf_ret =
      snprintf(buffer, sizeof(buffer), kReportJSONFormat,
               params.delimiter ? params.delimiter : "", pid,
               maxrss,
               GetExitTypeString(exit_type), exit_str_repr,
               GetExitReprString(exit_type, exit_numeric_repr),
               child_user_msec + child_sys_msec, child_user_msec,
               child_sys_msec, params.delimiter ? params.delimiter : "");
  if (snprintf_ret == -1) {
    return 1;
  }

  int fprintf_ret;
  if (!params.stats_filename) {
    fprintf_ret = fprintf(stdout, "%s\n", buffer);
  } else {
    FILE* stats_file = fopen(params.stats_filename, "w");
    if (!stats_file) {
      ERROR_FMT("error at openning file %s", params.stats_filename);
      return 1;
    }
    fprintf_ret = fprintf(stats_file, "%s\n", buffer);
  }
  return fprintf_ret != -1 ? 0 : 1;
}

/// Main works; return 0 on success, 1 otherwise.
int Work(const WorkParams& params) {
  int t_sec = params.timeout_msec / 1000,
      t_usec = 1000 * (params.timeout_msec % 1000);
  itimerval interval = {/* to */ {0, 0}, /* from */ {t_sec, t_usec}};

  pid_t child_pid = -1;
  CHECKED_SYSCALL(child_pid = fork(), "fork", PARENT_ERR);

  if (child_pid == 0) {
    // Child process.
    // ITIMER_PROF: decrements when the process executes OR
    // when the system is executing on behalf of the process.
    // POSIX call execvp() takes the environment variables from the
    // parent process.
    CHECKED_SYSCALL(setitimer(ITIMER_PROF, &interval, 0), "setitimer in child",
                    CHILD_ERR);
    CHECKED_SYSCALL(execvp(params.command[0], params.command), "exec in child",
                    CHILD_ERR);
  } else {
    // Parent process.
    VERBOSE("child forked; pid %d", child_pid);
    int child_status = 0;
    CHECKED_SYSCALL(waitpid(child_pid, &child_status, 0), "waitpid",
                    PARENT_ERR);

    rusage rusage_obj;
    memset(&rusage_obj, 0, sizeof(rusage_obj));
    CHECKED_SYSCALL(getrusage(RUSAGE_CHILDREN, &rusage_obj), "getrusage",
                    PARENT_ERR);

    if (WIFEXITED(child_status)) {
      int exit_status = WEXITSTATUS(child_status);
      VERBOSE("child %d exited with %d", child_pid, exit_status);
      return ReportStats(kReturn, params, child_pid, exit_status, rusage_obj);
    } else if (WIFSIGNALED(child_status)) {
      int sig = WTERMSIG(child_status);
      if (sig == SIGPROF) {
        VERBOSE("child %d timeout, %d msec", child_pid, params.timeout_msec);
        return ReportStats(kTimeout, params, child_pid, params.timeout_msec,
                           rusage_obj);
      } else if (sig == SIGQUIT) {
        VERBOSE("child %d quit", child_pid);
        return ReportStats(kQuit, params, child_pid, -1, rusage_obj);
      } else {
        VERBOSE("child %d terminated by signal %d (%s)", child_pid, sig,
                strsignal(sig));
        return ReportStats(kSignal, params, child_pid, sig, rusage_obj);
      }
    } else {
      VERBOSE("child exited abnormally without signal, pid = %d", child_pid);
      return ReportStats(kUnknown, params, child_pid, -1, rusage_obj);
    }
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    ERROR_STR("arguments expected, use '-h' for help");
    return 1;
  }

  WorkParams params;

  // File to write stats; stdout if NULL.
  params.stats_filename = getenv(kStatsFilenameEnvVar);

  // Time limit for the program.
  params.timeout_msec = kDefaultTimeoutMillisec;
  if (char* timeout_env = getenv(kTimeoutEnvVar)) {
    if (timeout_env[0] == '0' && timeout_env[1] == '\0') {
      params.timeout_msec = kEffectiveInfiniteTime;
    } else if (timeout_env[0] != '0' && IsShortDigitStr(timeout_env, 5)) {
      params.timeout_msec = atoi(timeout_env);
    } else {
      ERROR_FMT("%s value '%s' is led by '0', not pure digits, or too long",
                kTimeoutEnvVar, timeout_env);
      return 1;
    }
  }

  // Delimiter that encompasses the stats report.
  params.delimiter = getenv(kDelimiterEnvVar);
  if (params.delimiter && strlen(params.delimiter) >= 20) {
    ERROR_FMT("delimiter string is too long (>=20): %s", params.delimiter);
    return 1;
  }

  int command_start = -1;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (MatchFlag(argv[i], "-h", "--help")) {
        PrintHelp();
        return 0;
      } else if (MatchFlag(argv[i], "-v", "--verbose")) {
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

  VERBOSE("stats output: %s",
          params.stats_filename ? params.stats_filename : "(stdout)");
  VERBOSE("timeout (ms): %d%s", params.timeout_msec,
          params.timeout_msec == kEffectiveInfiniteTime ? " (infinite)" : "");
  VERBOSE("command:      %s",
          std::accumulate(params.command + 1, params.command + params.argc,
                          std::string(params.command[0]),
                          [](const std::string& acc, const char* part) {
                            return acc + " " + part;
                          })
              .c_str());

  return Work(params);
}
