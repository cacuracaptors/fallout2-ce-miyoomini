#ifndef FALLOUT_CRASH_HANDLER_H_
#define FALLOUT_CRASH_HANDLER_H_

namespace fallout {

// Installs signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL) that
// write a backtrace to "crash_log.txt" (in the process' current working
// directory) before letting the crash proceed normally. This lets a crash
// that happens during ordinary play - without a debugger attached - still
// leave behind enough information (raw addresses) to later locate the
// exact source line with `addr2line` against a matching unstripped build,
// without needing gdb/gdbserver connected live at the time of the crash.
void installCrashHandler();

} // namespace fallout

#endif /* FALLOUT_CRASH_HANDLER_H_ */
