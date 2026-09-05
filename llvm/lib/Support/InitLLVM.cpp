//===-- InitLLVM.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/InitLLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/AutoConvert.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signals.h"

#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#else
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#endif

static void RaiseLimits() {
#ifdef _AIX
  // AIX has restrictive memory soft-limits out-of-box, so raise them if needed.
  auto RaiseLimit = [](int resource) {
    struct rlimit r;
    getrlimit(resource, &r);

    // Increase the soft limit to the hard limit, if necessary and
    // possible.
    if (r.rlim_cur != RLIM_INFINITY && r.rlim_cur != r.rlim_max) {
      r.rlim_cur = r.rlim_max;
      setrlimit(resource, &r);
    }
  };

  // Address space size.
  RaiseLimit(RLIMIT_AS);
  // Heap size.
  RaiseLimit(RLIMIT_DATA);
  // Stack size.
  RaiseLimit(RLIMIT_STACK);
#ifdef RLIMIT_RSS
  // Resident set size.
  RaiseLimit(RLIMIT_RSS);
#endif
#endif
}

void CleanupStdHandles(void *Cookie) {
  llvm::raw_ostream *Outs = &llvm::outs(), *Errs = &llvm::errs();
  Outs->flush();
  Errs->flush();
  llvm::restoreStdHandleAutoConversion(STDIN_FILENO);
  llvm::restoreStdHandleAutoConversion(STDOUT_FILENO);
  llvm::restoreStdHandleAutoConversion(STDERR_FILENO);
}

using namespace llvm;
using namespace llvm::sys;

InitLLVM::InitLLVM(int &Argc, const char **&Argv,
                   bool InstallPipeSignalExitHandler) {
#ifndef NDEBUG
  static std::atomic<bool> Initialized{false};
  assert(!Initialized && "InitLLVM was already initialized!");
  Initialized = true;
#endif

  // Bring stdin/stdout/stderr into a known state.
  sys::AddSignalHandler(CleanupStdHandles, nullptr);

  // Firebox (firebox#E44): this gate used to cover five statements, and only
  // the first three of them have anything to do with signals. The two below it
  // were swept up by a signal-shaped gate, and one of them is load-bearing.
  //
  // What stays gated, and why. All three belong to the crash/backtrace half of
  // llvm::sys signal handling, which is honestly empty on this target
  // (firebox#967): a wasm trap terminates the store rather than being delivered
  // to a guest handler, and the wasm32 configure leaves HAVE_BACKTRACE and
  // HAVE__UNWIND_BACKTRACE undefined, so a fully ported PrintStackTrace would
  // print nothing anyway. Concretely, PrettyStackTrace reaches stderr only from
  // the CrashHandler that EnablePrettyStackTrace registers via
  // sys::AddSignalHandler, which is a no-op in Signals.cpp's __wasi__ block, so
  // StackPrinter.emplace and PrintStackTraceOnErrorSignal produce no observable
  // output here whether they run or not.
  //
  // SetOneShotPipeSignalFunction is the one that is deferred rather than
  // impossible: Firebox does deliver SIGPIPE, but the registration half of
  // Signals.cpp is not ported (its setter stores the handler nowhere), so
  // calling it would be a no-op that reads as if the facility worked. That gate
  // comes off with the lifecycle port, firebox#YWS -- not here.
#if !defined(__wasi__)
  if (InstallPipeSignalExitHandler)
    // The pipe signal handler must be installed before any other handlers are
    // registered. This is because the Unix \ref RegisterHandlers function does
    // not perform a sigaction() for SIGPIPE unless a one-shot handler is
    // present, to allow long-lived processes (like lldb) to fully opt-out of
    // llvm's SIGPIPE handling and ignore the signal safely.
    sys::SetOneShotPipeSignalFunction(sys::DefaultOneShotPipeSignalHandler);
  // Initialize the stack printer after installing the one-shot pipe signal
  // handler, so we can perform a sigaction() for SIGPIPE on Unix if requested.
  StackPrinter.emplace(Argc, Argv);
  sys::PrintStackTraceOnErrorSignal(Argv[0]);
#endif

  // Not signal machinery, and no platform reason to skip either one.
  //
  // install_out_of_memory_new_handler is pure C++: with LLVM_ENABLE_EXCEPTIONS
  // off -- which is how every wasm32 build of this tree is configured -- it is
  // a std::set_new_handler call whose handler writes "LLVM ERROR: out of
  // memory" to fd 2 and aborts. Skipping it was a false success in invariant-0
  // terms: an in-guest tool that hits the wasm memory ceiling died on the
  // libc++ bad_alloc path with no diagnostic at all, where the same tool on
  // Linux prints the error. std::set_new_handler needs nothing the host cannot
  // provide, so "wasi" was never a bound here.
  //
  // RaiseLimits' entire body is #ifdef _AIX, so on this target it already
  // compiles to nothing; gating it added no behaviour and only guaranteed that
  // a future non-AIX branch would be silently skipped on wasi.
  install_out_of_memory_new_handler();
  RaiseLimits();

#ifdef __MVS__

  // We use UTF-8 as the internal character encoding. On z/OS, all external
  // output is encoded in EBCDIC. In order to be able to read all
  // error messages, we turn conversion to EBCDIC on for stderr fd.
  std::string Banner = std::string(Argv[0]) + ": ";
  ExitOnError ExitOnErr(Banner);

  // If turning on conversion for stderr fails then the error message
  // may be garbled. There is no solution to this problem.
  ExitOnErr(errorCodeToError(llvm::enableAutoConversion(STDERR_FILENO)));
  ExitOnErr(errorCodeToError(llvm::enableAutoConversion(STDOUT_FILENO)));
#endif

#ifdef _WIN32
  // We use UTF-8 as the internal character encoding. On Windows,
  // arguments passed to main() may not be encoded in UTF-8. In order
  // to reliably detect encoding of command line arguments, we use an
  // Windows API to obtain arguments, convert them to UTF-8, and then
  // write them back to the Argv vector.
  //
  // There's probably other way to do the same thing (e.g. using
  // wmain() instead of main()), but this way seems less intrusive
  // than that.
  std::string Banner = std::string(Argv[0]) + ": ";
  ExitOnError ExitOnErr(Banner);

  ExitOnErr(errorCodeToError(windows::GetCommandLineArguments(Args, Alloc)));

  // GetCommandLineArguments doesn't terminate the vector with a
  // nullptr.  Do it to make it compatible with the real argv.
  Args.push_back(nullptr);

  Argc = Args.size() - 1;
  Argv = Args.data();
#endif
}

InitLLVM::~InitLLVM() {
  CleanupStdHandles(nullptr);
  llvm_shutdown();
}
