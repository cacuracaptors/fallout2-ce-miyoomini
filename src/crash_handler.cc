#include "crash_handler.h"

#include <signal.h>
#include <string.h>

#if defined(__linux__)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#define CRASH_HANDLER_HAVE_BACKTRACE 1
#endif

namespace fallout {

#ifdef CRASH_HANDLER_HAVE_BACKTRACE

static const int kCrashSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL };
static const int kCrashSignalCount = sizeof(kCrashSignals) / sizeof(kCrashSignals[0]);

// NOTE: This handler intentionally sticks to a small set of functions that
// are safe (or safe enough in practice on this platform) to call from
// inside a signal handler - no malloc, no iostreams, no backtrace_symbols()
// (which mallocs internally; backtrace_symbols_fd() does not).
static void crashHandlerSignal(int signum)
{
    // Restore the default handler for this signal first. If writing the
    // crash log itself faults, the second occurrence terminates normally
    // instead of recursing into this handler forever.
    signal(signum, SIG_DFL);

    int fd = open("crash_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != -1) {
        const char* name = "UNKNOWN";
        switch (signum) {
        case SIGSEGV:
            name = "SIGSEGV";
            break;
        case SIGABRT:
            name = "SIGABRT";
            break;
        case SIGFPE:
            name = "SIGFPE";
            break;
        case SIGBUS:
            name = "SIGBUS";
            break;
        case SIGILL:
            name = "SIGILL";
            break;
        }

        static const char header[] = "\n--- Crash detected: ";
        write(fd, header, sizeof(header) - 1);
        write(fd, name, strlen(name));
        static const char footer[] = " ---\n";
        write(fd, footer, sizeof(footer) - 1);

        void* addresses[64];
        int count = backtrace(addresses, 64);
        backtrace_symbols_fd(addresses, count, fd);

        close(fd);
    }

    // Re-raise so the OS handles termination normally (core dump if
    // enabled, correct exit status, etc.) rather than us swallowing the
    // signal silently.
    raise(signum);
}

void installCrashHandler()
{
    for (int i = 0; i < kCrashSignalCount; i++) {
        signal(kCrashSignals[i], crashHandlerSignal);
    }
}

#else

void installCrashHandler()
{
    // Not implemented on this platform - no-op.
}

#endif

} // namespace fallout
