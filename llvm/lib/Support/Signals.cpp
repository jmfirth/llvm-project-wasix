//===- Signals.cpp - Signal Handling support --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// Unix signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Signals.h"

#include "DebugOptions.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cmath>

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

using namespace llvm;

// Use explicit storage to avoid accessing cl::opt in a signal handler.
static bool DisableSymbolicationFlag = false;
static ManagedStatic<std::string> CrashDiagnosticsDirectory;
namespace {
struct CreateDisableSymbolication {
  static void *call() {
    return new cl::opt<bool, true>(
        "disable-symbolication",
        cl::desc("Disable symbolizing crash backtraces."),
        cl::location(DisableSymbolicationFlag), cl::Hidden);
  }
};
struct CreateCrashDiagnosticsDir {
  static void *call() {
    return new cl::opt<std::string, true>(
        "crash-diagnostics-dir", cl::value_desc("directory"),
        cl::desc("Directory for crash diagnostic files."),
        cl::location(*CrashDiagnosticsDirectory), cl::Hidden);
  }
};
} // namespace
void llvm::initSignalsOptions() {
  static ManagedStatic<cl::opt<bool, true>, CreateDisableSymbolication>
      DisableSymbolication;
  static ManagedStatic<cl::opt<std::string, true>, CreateCrashDiagnosticsDir>
      CrashDiagnosticsDir;
  *DisableSymbolication;
  *CrashDiagnosticsDir;
}

constexpr char DisableSymbolizationEnv[] = "LLVM_DISABLE_SYMBOLIZATION";
constexpr char LLVMSymbolizerPathEnv[] = "LLVM_SYMBOLIZER_PATH";
constexpr char EnableSymbolizerMarkupEnv[] = "LLVM_ENABLE_SYMBOLIZER_MARKUP";

// Callbacks to run in signal handler must be lock-free because a signal handler
// could be running as we add new callbacks. We don't add unbounded numbers of
// callbacks, an array is therefore sufficient.
struct CallbackAndCookie {
  sys::SignalHandlerCallback Callback;
  void *Cookie;
  enum class Status { Empty, Initializing, Initialized, Executing };
  std::atomic<Status> Flag;
};

static constexpr size_t MaxSignalHandlerCallbacks = 8;

// A global array of CallbackAndCookie may not compile with
// -Werror=global-constructors in c++20 and above
static std::array<CallbackAndCookie, MaxSignalHandlerCallbacks> &
CallBacksToRun() {
  static std::array<CallbackAndCookie, MaxSignalHandlerCallbacks> callbacks;
  return callbacks;
}

// Signal-safe.
void sys::RunSignalHandlers() {
  // Let's not interfere with stack trace symbolication and friends.
  auto BypassSandbox = sandbox::scopedDisable();

  for (CallbackAndCookie &RunMe : CallBacksToRun()) {
    auto Expected = CallbackAndCookie::Status::Initialized;
    auto Desired = CallbackAndCookie::Status::Executing;
    if (!RunMe.Flag.compare_exchange_strong(Expected, Desired))
      continue;
    (*RunMe.Callback)(RunMe.Cookie);
    RunMe.Callback = nullptr;
    RunMe.Cookie = nullptr;
    RunMe.Flag.store(CallbackAndCookie::Status::Empty);
  }
}

// Signal-safe.
static void insertSignalHandler(sys::SignalHandlerCallback FnPtr,
                                void *Cookie) {
  for (CallbackAndCookie &SetMe : CallBacksToRun()) {
    auto Expected = CallbackAndCookie::Status::Empty;
    auto Desired = CallbackAndCookie::Status::Initializing;
    if (!SetMe.Flag.compare_exchange_strong(Expected, Desired))
      continue;
    SetMe.Callback = FnPtr;
    SetMe.Cookie = Cookie;
    SetMe.Flag.store(CallbackAndCookie::Status::Initialized);
    return;
  }
  report_fatal_error("too many signal callbacks already registered");
}

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool);

/// Format a pointer value as hexadecimal. Zero pad it out so its always the
/// same width.
static FormattedNumber format_ptr(void *PC) {
  // Each byte is two hex digits plus 2 for the 0x prefix.
  unsigned PtrWidth = 2 + 2 * sizeof(void *);
  return format_hex((uint64_t)PC, PtrWidth);
}

/// Reads a file \p Filename written by llvm-symbolizer containing function
/// names and source locations for the addresses in \p AddressList and returns
/// the strings in a vector of pairs, where the first pair element is the index
/// of the corresponding entry in AddressList and the second is the symbolized
/// frame, in a format based on the sanitizer stack trace printer, with the
/// exception that it does not write out frame numbers (i.e. "#2 " for the
/// third address), as it is not assumed that \p AddressList corresponds to a
/// single stack trace.
/// There may be multiple returned entries for a single \p AddressList entry if
/// that frame address corresponds to one or more inlined frames; in this case,
/// all frames for an address will appear contiguously and in-order.
std::optional<SmallVector<std::pair<unsigned, std::string>, 0>>
collectAddressSymbols(void **AddressList, unsigned AddressCount,
                      const char *MainExecutableName,
                      const std::string &LLVMSymbolizerPath) {
  BumpPtrAllocator Allocator;
  StringSaver StrPool(Allocator);
  SmallVector<const char *, 0> Modules(AddressCount, nullptr);
  SmallVector<intptr_t, 0> Offsets(AddressCount, 0);
  if (!findModulesAndOffsets(AddressList, AddressCount, Modules.data(),
                             Offsets.data(), MainExecutableName, StrPool))
    return {};
  int InputFD;
  SmallString<32> InputFile, OutputFile;
  sys::fs::createTemporaryFile("symbolizer-input", "", InputFD, InputFile);
  sys::fs::createTemporaryFile("symbolizer-output", "", OutputFile);
  FileRemover InputRemover(InputFile.c_str());
  FileRemover OutputRemover(OutputFile.c_str());

  {
    raw_fd_ostream Input(InputFD, true);
    for (unsigned AddrIdx = 0; AddrIdx < AddressCount; AddrIdx++) {
      if (Modules[AddrIdx])
        Input << Modules[AddrIdx] << " " << (void *)Offsets[AddrIdx] << "\n";
    }
  }

  std::optional<StringRef> Redirects[] = {InputFile.str(), OutputFile.str(),
                                          StringRef("")};
  StringRef Args[] = {"llvm-symbolizer", "--functions=linkage", "--inlining",
#ifdef _WIN32
                      // Pass --relative-address on Windows so that we don't
                      // have to add ImageBase from PE file.
                      // FIXME: Make this the default for llvm-symbolizer.
                      "--relative-address",
#endif
                      "--demangle"};
  int RunResult =
      sys::ExecuteAndWait(LLVMSymbolizerPath, Args, std::nullopt, Redirects);
  if (RunResult != 0)
    return {};

  SmallVector<std::pair<unsigned, std::string>, 0> Result;
  auto OutputBuf = MemoryBuffer::getFile(OutputFile.c_str());
  if (!OutputBuf)
    return {};
  StringRef Output = OutputBuf.get()->getBuffer();
  SmallVector<StringRef, 32> Lines;
  Output.split(Lines, "\n");
  auto *CurLine = Lines.begin();
  // Lines contains the output from llvm-symbolizer, which should contain for
  // each address with a module in order of appearance, one or more lines
  // containing the function name and line associated with that address,
  // followed by an empty line.
  // For each address, adds an output entry for every real or inlined frame at
  // that address. For addresses without known modules, we have a single entry
  // containing just the formatted address; for all other output entries, we
  // output the function entry if it is known, and either the line number if it
  // is known or the module+address offset otherwise.
  for (unsigned AddrIdx = 0; AddrIdx < AddressCount; AddrIdx++) {
    if (!Modules[AddrIdx]) {
      auto &SymbolizedFrame = Result.emplace_back(std::make_pair(AddrIdx, ""));
      raw_string_ostream OS(SymbolizedFrame.second);
      OS << format_ptr(AddressList[AddrIdx]);
      continue;
    }
    // Read pairs of lines (function name and file/line info) until we
    // encounter empty line.
    for (;;) {
      if (CurLine == Lines.end())
        return {};
      StringRef FunctionName = *CurLine++;
      if (FunctionName.empty())
        break;
      auto &SymbolizedFrame = Result.emplace_back(std::make_pair(AddrIdx, ""));
      raw_string_ostream OS(SymbolizedFrame.second);
      OS << format_ptr(AddressList[AddrIdx]) << ' ';
      if (!FunctionName.starts_with("??"))
        OS << FunctionName << ' ';
      if (CurLine == Lines.end())
        return {};
      StringRef FileLineInfo = *CurLine++;
      if (!FileLineInfo.starts_with("??")) {
        OS << FileLineInfo;
      } else {
        OS << "(" << Modules[AddrIdx] << '+' << format_hex(Offsets[AddrIdx], 0)
           << ")";
      }
    }
  }
  return Result;
}

ErrorOr<std::string> getLLVMSymbolizerPath(StringRef Argv0 = {}) {
  ErrorOr<std::string> LLVMSymbolizerPathOrErr = std::error_code();
  if (const char *Path = getenv(LLVMSymbolizerPathEnv)) {
    LLVMSymbolizerPathOrErr = sys::findProgramByName(Path);
  } else if (!Argv0.empty()) {
    StringRef Parent = llvm::sys::path::parent_path(Argv0);
    if (!Parent.empty())
      LLVMSymbolizerPathOrErr =
          sys::findProgramByName("llvm-symbolizer", Parent);
  }
  if (!LLVMSymbolizerPathOrErr)
    LLVMSymbolizerPathOrErr = sys::findProgramByName("llvm-symbolizer");
  return LLVMSymbolizerPathOrErr;
}

/// Helper that launches llvm-symbolizer and symbolizes a backtrace.
LLVM_ATTRIBUTE_USED
static bool printSymbolizedStackTrace(StringRef Argv0, void **StackTrace,
                                      int Depth, llvm::raw_ostream &OS) {
  if (DisableSymbolicationFlag || getenv(DisableSymbolizationEnv))
    return false;

  // Don't recursively invoke the llvm-symbolizer binary.
  if (Argv0.contains("llvm-symbolizer"))
    return false;

  // FIXME: Subtract necessary number from StackTrace entries to turn return
  // addresses into actual instruction addresses.
  // Use llvm-symbolizer tool to symbolize the stack traces. First look for it
  // alongside our binary, then in $PATH.
  ErrorOr<std::string> LLVMSymbolizerPathOrErr = getLLVMSymbolizerPath(Argv0);
  if (!LLVMSymbolizerPathOrErr)
    return false;
  const std::string &LLVMSymbolizerPath = *LLVMSymbolizerPathOrErr;

  // If we don't know argv0 or the address of main() at this point, try
  // to guess it anyway (it's possible on some platforms).
  std::string MainExecutableName =
      sys::fs::exists(Argv0) ? std::string(Argv0)
                             : sys::fs::getMainExecutable(nullptr, nullptr);

  auto SymbolizedAddressesOpt = collectAddressSymbols(
      StackTrace, Depth, MainExecutableName.c_str(), LLVMSymbolizerPath);
  if (!SymbolizedAddressesOpt)
    return false;
  for (unsigned FrameNo = 0; FrameNo < SymbolizedAddressesOpt->size();
       ++FrameNo) {
    OS << right_justify(formatv("#{0}", FrameNo).str(), std::log10(Depth) + 2)
       << ' ' << (*SymbolizedAddressesOpt)[FrameNo].second << '\n';
  }
  return true;
}

#if LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN
void sys::symbolizeAddresses(AddressSet &Addresses,
                             SymbolizedAddressMap &SymbolizedAddresses) {
  assert(!DisableSymbolicationFlag && !getenv(DisableSymbolizationEnv) &&
         "Debugify origin stacktraces require symbolization to be enabled.");

  // Convert Set of Addresses to ordered list.
  SmallVector<void *, 0> AddressList(Addresses.begin(), Addresses.end());
  if (AddressList.empty())
    return;
  llvm::sort(AddressList);

  // Use llvm-symbolizer tool to symbolize the stack traces. First look for it
  // alongside our binary, then in $PATH.
  ErrorOr<std::string> LLVMSymbolizerPathOrErr = getLLVMSymbolizerPath();
  if (!LLVMSymbolizerPathOrErr)
    report_fatal_error("Debugify origin stacktraces require llvm-symbolizer");
  const std::string &LLVMSymbolizerPath = *LLVMSymbolizerPathOrErr;

  // Try to guess the main executable name, since we don't have argv0 available
  // here.
  std::string MainExecutableName = sys::fs::getMainExecutable(nullptr, nullptr);

  auto SymbolizedAddressesOpt =
      collectAddressSymbols(AddressList.begin(), AddressList.size(),
                            MainExecutableName.c_str(), LLVMSymbolizerPath);
  if (!SymbolizedAddressesOpt)
    return;
  for (auto SymbolizedFrame : *SymbolizedAddressesOpt) {
    SmallVector<std::string, 0> &SymbolizedAddrs =
        SymbolizedAddresses[AddressList[SymbolizedFrame.first]];
    SymbolizedAddrs.push_back(SymbolizedFrame.second);
  }
  return;
}
#endif

static bool printMarkupContext(raw_ostream &OS, const char *MainExecutableName);

LLVM_ATTRIBUTE_USED
static bool printMarkupStackTrace(StringRef Argv0, void **StackTrace, int Depth,
                                  raw_ostream &OS) {
  const char *Env = getenv(EnableSymbolizerMarkupEnv);
  if (!Env || !*Env)
    return false;

  std::string MainExecutableName =
      sys::fs::exists(Argv0) ? std::string(Argv0)
                             : sys::fs::getMainExecutable(nullptr, nullptr);
  if (!printMarkupContext(OS, MainExecutableName.c_str()))
    return false;
  for (int I = 0; I < Depth; I++)
    OS << format("{{{bt:%d:%#016x}}}\n", I, StackTrace[I]);
  return true;
}

// Include the platform-specific parts of this class.
#if defined(__wasi__)
// Firebox (firebox#967): THIS BLOCK IS A PORT GAP, NOT A PLATFORM LIMIT.
//
// The premise it shipped with — "WASI does not have signals" — is stale for
// Firebox, which has real Linux signal semantics (sigaction/kill/raise, real
// delivery, per-thread masks). It is still true for the *crash* half of this
// interface, and the two halves must not be conflated:
//
//   * CRASH / BACKTRACE half — honestly empty on any wasm target. A wasm trap
//     (OOB access, unreachable, call_indirect type error, stack exhaustion)
//     terminates the store; it is not deliverable to a guest handler, so there
//     is no fault to catch. And even a full Unix/Signals.inc port would print
//     nothing here: the wasm32 configure leaves both HAVE_BACKTRACE and
//     HAVE__UNWIND_BACKTRACE undefined (MEASURED, firebox#967), and LLVM is
//     built -fno-exceptions against a sysroot with no unwinder. PrintStackTrace
//     and PrintStackTraceOnErrorSignal are therefore faithful no-ops, not debt.
//
//   * REGISTRATION / CLEANUP half — NOT faithful, and knowingly so. Firebox
//     delivers SIGINT/SIGTERM/SIGHUP/SIGQUIT/SIGPIPE/SIGUSR1 for real, so
//     RemoveFileOnSignal returning "no error" while registering nothing is a
//     false success in invariant-0 terms: an interrupted `clang -c` leaves the
//     partial .o that Linux would have unlinked. Porting the lifecycle half
//     (an atomic handler table + the FilesToRemove list + sigaction installs
//     for those six signals, i.e. roughly Unix/Signals.inc minus the backtrace
//     machinery) is tracked separately; it cannot be validated without a full
//     in-guest LLVM rebuild and does not belong in the same commit as this one.
//
// WHY THESE SIX DEFINITIONS ARE HERE AT ALL. The block defined seven of the
// THIRTEEN platform entry points Unix/Signals.inc defines and Signals.h
// declares LLVM_ABI. The six below were declared and never defined on this
// target. That is invisible to a normal LLVM build only because --gc-sections
// drops whichever referrer nothing calls; an export-rooted link (the PIC thin
// link's --export-dynamic, firebox#MQQ) roots LLVM's whole public API and the
// hole becomes an undefined symbol. firebox#967 hit exactly one of them —
// SetInfoSignalFunction, via EnablePrettyStackTraceOnSigInfoForThisThread,
// which is live because the wasm32 configure DOES set ENABLE_BACKTRACES=1.
// Adding only that one would leave five identical landmines, so all six land
// together (invariant 1: fix the class, not the symptom).
//
// Live referrers, MEASURED 2026-09-02 on this tree:
//   unregisterHandlers              CrashRecoveryContext.cpp:495 (libLLVMSupport
//                                   itself), llvm-exegesis
//   PrintStackTrace                 libclang CIndex.cpp:10165
//   SetInterruptFunction            bugpoint
//   SetInfoSignalFunction           PrettyStackTrace.cpp:308  <- firebox#967
//   SetOneShotPipeSignalFunction    InitLLVM.cpp:93 (__wasi__-guarded today)
//   DefaultOneShotPipeSignalHandler InitLLVM.cpp:93 (likewise)
//
// Retires when the lifecycle port above lands (these become real) or when
// upstream LLVM carries a WASI signal port. Do NOT "simplify" this to
// `#include "Unix/Signals.inc"`: that pulls in dlfcn/link.h/backtrace and the
// llvm-symbolizer fork-exec path, none of which has a wasm meaning.
#include "llvm/Support/ExitCodes.h" // EX_IOERR, for the pipe handler below
#include <cstdlib>

void llvm::sys::AddSignalHandler(sys::SignalHandlerCallback FnPtr,
                                 void *Cookie) {}
void llvm::sys::RunInterruptHandlers() {}
void sys::CleanupOnSignal(uintptr_t Context) {}
bool llvm::sys::RemoveFileOnSignal(StringRef Filename, std::string *ErrMsg) {
  // Returns "no error" and registers nothing. See the REGISTRATION half above:
  // this is the one stub in this block that is a false success rather than an
  // honest absence, and it is left as-is deliberately — the return value is
  // ignored by every in-tree caller (LTO.cpp, DTLTO.cpp, CompilerInstance.cpp,
  // cc1as_main.cpp), so flipping it to `true` would only add a diagnostic path
  // nobody reads while still not removing the file. The fix is the port.
  return false;
}
void llvm::sys::DontRemoveFileOnSignal(StringRef Filename) {}
void llvm::sys::DisableSystemDialogsOnCrash() {}
void llvm::sys::PrintStackTraceOnErrorSignal(StringRef Argv0,
                                             bool DisableCrashReporting) {
  // No fault signal can reach a wasm guest handler; nothing to install.
}
void llvm::sys::PrintStackTrace(raw_ostream &OS, int Depth) {
  // No backtrace provider on this target (HAVE_BACKTRACE and
  // HAVE__UNWIND_BACKTRACE are both undefined for wasm32), so Unix/Signals.inc
  // would emit nothing here either. Empty is the faithful body, not a stub.
}
void sys::unregisterHandlers() {}
void llvm::sys::SetInterruptFunction(void (*IF)()) {}
void llvm::sys::SetInfoSignalFunction(void (*Handler)()) {}
void llvm::sys::SetOneShotPipeSignalFunction(void (*Handler)()) {}
void llvm::sys::DefaultOneShotPipeSignalHandler() {
  // Unix exits EX_IOERR so drivers can distinguish a closed-pipe death from a
  // real failure. Keep that behaviour: it is a plain exit(), it needs no signal
  // machinery, and a caller that reaches this function has already decided the
  // pipe is gone.
  exit(EX_IOERR);
}
#elif defined(LLVM_ON_UNIX)
#include "Unix/Signals.inc"
#elif defined(_WIN32)
#include "Windows/Signals.inc"
#endif
