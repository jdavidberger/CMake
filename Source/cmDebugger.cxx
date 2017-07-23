/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmDebugger.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmake.h"
#include <condition_variable>
#include <set>

/**
 * Actual debugger implementation.
 */
class cmDebugger_impl : public cmDebugger
{
  cmake& CMakeInstance;
  State::t state = State::Unknown;

  /***
   * The main thread atomics used to control execution. We use a recursive
   * mutex here so that we don't have to worry
   * about juggling the mutex when we execute listener's callbacks. This allows
   * users to get a pause context even
   * within a callback.
   *
   * The long and short of it is that the mutex is almost always locked, and
   * only goes into cv.wait when some break
   * condition is hit.
   */
  std::recursive_mutex m;
  std::condition_variable_any cv;
  std::unique_lock<std::recursive_mutex> Lock;

  /**
   * This flag sets up the next instruction to go into the pause state.
   */
  bool breakPending = true; // Break on connection

  /***
   * We run the breakpoints off of a separate mutex so that they can be set and
   * cleared while running
   */
  std::mutex breakpointMutex;
  std::vector<cmBreakpoint> breakpoints;

  /***
   * When breakDepth isn't -1, we check the current stack size on execution and
   * when the stack size is
   * equal to that breakDepth we set breakPending. This makes step in / step
   * out functionality divorced
   * from understanding anything about the actual commands.
   */
  int32_t breakDepth = -1;

  std::set<cmDebuggerListener*> listeners;
  cmListFileContext currentLocation;

public:
  ~cmDebugger_impl() override
  {
    for (auto& s : listeners) {
      delete s;
    }
    listeners.clear();
  }
  cmDebugger_impl(cmake& cmakeInstance)
    : CMakeInstance(cmakeInstance)
    , Lock(m)
  {
  }

  void AddListener(cmDebuggerListener* listener) override
  {
    listeners.insert(listener);
  }

  void RemoveListener(cmDebuggerListener* listener) override
  {
    listeners.erase(listener);
  }
  const std::vector<cmBreakpoint>& GetBreakpoints() const override
  {
    return breakpoints;
  }

  void PauseExecution()
  {
    breakPending = false;
    breakDepth = -1;
    state = State::Paused;
    for (auto& l : listeners) {
      l->OnChangeState();
    }

    cv.wait(Lock);
    state = State::Running;
    for (auto& l : listeners) {
      l->OnChangeState();
    }
  }

  cmListFileContext CurrentLine() const override { return currentLocation; }
  cmListFileBacktrace GetBacktrace() const override
  {
    if (this->CMakeInstance.GetGlobalGenerator()) {
      return this->CMakeInstance.GetGlobalGenerator()
            ->GetCurrentMakefile()
            ->GetBacktrace();
    }

    cmListFileBacktrace empty;
    return empty;
  }
  cmMakefile* GetMakefile() const override
  {
    if (this->CMakeInstance.GetGlobalGenerator()) {
      return this->CMakeInstance.GetGlobalGenerator()->GetCurrentMakefile();
    }
    return CM_NULLPTR;
  }
  void PreRunHook(const cmListFileContext& context,
                  const cmListFileFunction& line) override
  {
    (void)line;

    state = State::Running;
    currentLocation = context;

    // Step in / Step out logic. We have a target
    // stack depth, and when we hit it, pause.
    if (breakDepth != -1) {
      auto currentDepth = GetBacktrace().Depth();
      if (currentDepth == (size_t)breakDepth) {
          breakPending = true;
      }
    }

    // Breakpoint detection
    {
      std::lock_guard<std::mutex> l(breakpointMutex);
      for (auto& bp : breakpoints) {
        if (bp.matches(context)) {
          breakPending = true;
          break;
        }
      }
    }

    // If a break is pending, act on it.
    // Note that we
    if (breakPending) {
      PauseExecution();
    }
  }

  void ErrorHook(const cmListFileContext& context) override
  {
    (void)context;
    PauseExecution();
  }

  breakpoint_id SetBreakpoint(const std::string& fileName,
                              size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.emplace_back(fileName, line);
    return breakpoints.size() - 1;
  }

  breakpoint_id SetWatchpoint(const std::string& expr) override
  {
    (void)expr;
    return 0;
  }

  void ClearBreakpoint(breakpoint_id id) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.erase(breakpoints.begin() + id);
  }

  void Continue() override { cv.notify_all(); }

  void Break() override { breakPending = true; }

  void Step() override
  {
    breakDepth = (int32_t)GetBacktrace().Depth();
    Continue();
  }

  void StepIn() override
  {
    breakPending = true;
    Continue();
  }

  void StepOut() override
  {
    breakDepth = (int32_t)(GetBacktrace().Depth()) - 1;
    Continue();
  }

  void ClearBreakpoint(const std::string& fileName, size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    auto pred = [&](const cmBreakpoint br) {
      return br.matches(fileName, line);
    };
    breakpoints.erase(
      std::remove_if(breakpoints.begin(), breakpoints.end(), pred),
      breakpoints.end());
  }

  void ClearAllBreakpoints() override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.clear();
  }

  State::t CurrentState() const override { return this->state; }

  // Inherited via cmDebugger
  cmPauseContext PauseContext() override { return cmPauseContext(m, this); }
};

cmDebugger* cmDebugger::Create(cmake& global)
{
  return new cmDebugger_impl(global);
}

cmDebuggerListener::cmDebuggerListener(cmDebugger& debugger)
  : Debugger(debugger)
{
}

cmBreakpoint::cmBreakpoint(const std::string& _file, size_t _line)
  : File(_file)
  , Line(_line)
{
}
cmBreakpoint::operator bool() const
{
  return !File.empty();
}
inline bool cmBreakpoint::matches(const cmListFileContext& ctx) const
{
  return matches(ctx.FilePath, ctx.Line);
}

bool cmBreakpoint::matches(const std::string& testFile, size_t testLine) const
{
  if (File.empty()) {
    return false;
  }

  if (Line != testLine && Line != (size_t)-1) {
    return false;
  }

  return testFile.find(File) != std::string::npos;
}

cmPauseContext::cmPauseContext(std::recursive_mutex& m, cmDebugger* debugger)
  : Debugger(debugger)
  , Lock(m, std::try_to_lock)
{
}

cmPauseContext::operator bool() const
{
  return Lock.owns_lock();
}

cmListFileBacktrace cmPauseContext::GetBacktrace() const
{
  if (!*this) {
      throw std::runtime_error(
              "Attempt to access backtrace with invalid context");
  }
  return Debugger->GetBacktrace();
}

cmMakefile* cmPauseContext::GetMakefile() const
{
  if (!*this) {
      throw std::runtime_error(
              "Attempt to access makefile with invalid context");
  }
  return Debugger->GetMakefile();
}

void cmPauseContext::Continue()
{
  if (!*this) {
      throw std::runtime_error("Attempt to continue with invalid context");
  }
  Debugger->Continue();
}

void cmPauseContext::Step()
{
  if (!*this) {
      throw std::runtime_error("Attempt to step with invalid context");
  }
  Debugger->Step();
}

void cmPauseContext::StepIn()
{
  if (!*this) {
      throw std::runtime_error("Attempt to step inwith invalid context");
  }
  Debugger->StepIn();
}

void cmPauseContext::StepOut()
{
  if (!*this) {
      throw std::runtime_error("Attempt to stepout with invalid context");
  }
  Debugger->StepOut();
}

cmListFileContext cmPauseContext::CurrentLine() const
{
  if (!*this) {
      throw std::runtime_error(
              "Attempt to access current line with invalid context");
  }
  return Debugger->CurrentLine();
}
