#include "crash_handler.h"

#include <signal.h>
#include <string.h>

#if defined(__linux__)
#include <execinfo.h>
#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>
#define CRASH_HANDLER_HAVE_BACKTRACE 1

// Provided by the GNU linker: start and end of this program's code.
extern "C" char __executable_start;
extern "C" char __etext;
#endif

namespace fallout {

#ifdef CRASH_HANDLER_HAVE_BACKTRACE

static const int kCrashSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL };
static const int kCrashSignalCount = sizeof(kCrashSignals) / sizeof(kCrashSignals[0]);

// Async-signal-safe hex writer (no printf/malloc inside a signal handler).
static void writeHex(int fd, const char* label, unsigned long value)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[16];
    int count = 0;
    do {
        tmp[count++] = digits[value & 0xF];
        value >>= 4;
    } while (value != 0 && count < 16);

    char out[24];
    int n = 0;
    out[n++] = '0';
    out[n++] = 'x';
    while (count > 0) {
        out[n++] = tmp[--count];
    }
    out[n++] = '\n';

    write(fd, label, strlen(label));
    write(fd, out, n);
}

static void writeText(int fd, const char* text)
{
    write(fd, text, strlen(text));
}

static void crashHandlerSignal(int signum, siginfo_t* info, void* context)
{
    int fd = open("crash_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != -1) {
        const char* name = "UNKNOWN";
        switch (signum) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGILL:  name = "SIGILL";  break;
        }

        writeText(fd, "\n--- Crash detected: ");
        writeText(fd, name);
        writeText(fd, " ---\n");

        // On 32-bit ARM, backtrace() usually cannot unwind past the signal
        // frame, so it only shows this handler. The CPU registers saved by
        // the kernel always hold the exact crash site:
        // pc = the faulting instruction, lr = where it was called from.
#if defined(__arm__)
        ucontext_t* uc = (ucontext_t*)context;
        if (uc != NULL) {
            writeHex(fd, "pc: ", (unsigned long)uc->uc_mcontext.arm_pc);
            writeHex(fd, "lr: ", (unsigned long)uc->uc_mcontext.arm_lr);
        }
#endif
        if (info != NULL) {
            writeHex(fd, "fault addr: ", (unsigned long)info->si_addr);
        }

        // When the crash happens inside a system library (libc, SDL...), pc
        // and lr point there. Scan the stack for return addresses into this
        // program's own code: those are the game functions that were running.
#if defined(__arm__)
        if (uc != NULL) {
            const unsigned long lo = (unsigned long)&__executable_start;
            const unsigned long hi = (unsigned long)&__etext;
            const unsigned long* sp = (const unsigned long*)uc->uc_mcontext.arm_sp;
            writeText(fd, "stack (game code addresses, newest first):\n");
            int found = 0;
            for (int i = 0; i < 2048 && found < 32; i++) {
                unsigned long value = sp[i];
                if (value >= lo && value < hi) {
                    writeHex(fd, "  ", value);
                    found++;
                }
            }
        }
#endif

        void* addresses[64];
        int count = backtrace(addresses, 64);
        backtrace_symbols_fd(addresses, count, fd);

        // Where each library was loaded, to turn pc/lr into library offsets.
        int maps = open("/proc/self/maps", O_RDONLY);
        if (maps != -1) {
            writeText(fd, "maps:\n");
            char buffer[1024];
            ssize_t n;
            while ((n = read(maps, buffer, sizeof(buffer))) > 0) {
                write(fd, buffer, n);
            }
            close(maps);
        }

        close(fd);
    }

    // SA_RESETHAND already restored the default action, so re-raising lets
    // the OS terminate the process normally.
    raise(signum);
}

void installCrashHandler()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandlerSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    for (int i = 0; i < kCrashSignalCount; i++) {
        sigaction(kCrashSignals[i], &sa, NULL);
    }
}

#else

void installCrashHandler()
{
}

#endif

} // namespace fallout
