//
// Created by J on 1/28/2017.
//

#include "cmDebugger.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmake.h"
#include <condition_variable>
#include <set>

class cmRemoteDebugger_impl : public cmDebugger
{
  cmake& CMakeInstance;
  State::t state = State::Unknown;
  std::recursive_mutex m;
  std::condition_variable_any cv;
  bool breakPending = true; // Break on connection

  std::mutex breakpointMutex;
  std::vector<cmBreakpoint> breakpoints;
  int32_t breakDepth = -1;
  std::unique_lock<std::recursive_mutex> Lock;

public:
  ~cmRemoteDebugger_impl()
  {
    for (auto& s : listeners) {
      delete s;
    }
    listeners.clear();
  }
  cmRemoteDebugger_impl(cmake& cmakeInstance)
    : CMakeInstance(cmakeInstance)
    , Lock(m)
  {
  }
  std::set<cmDebuggerListener*> listeners;
  void AddListener(cmDebuggerListener* listener) override
  {
    listeners.insert(listener);
  }

  void RemoveListener(cmDebuggerListener* listener) override
  {
    listeners.erase(listener);
  }
  virtual const std::vector<cmBreakpoint>& GetBreakpoints() const
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

  cmListFileContext currentLocation;
  virtual cmListFileContext CurrentLine() const override
  {
    return currentLocation;
  }
  virtual cmListFileBacktrace GetBacktrace() const override
  {
    if (this->CMakeInstance.GetGlobalGenerator())
      return this->CMakeInstance.GetGlobalGenerator()
        ->GetCurrentMakefile()
        ->GetBacktrace();

    cmListFileBacktrace empty;
    return empty;
  }
  virtual cmMakefile* GetMakefile() const
  {
    if (this->CMakeInstance.GetGlobalGenerator())
      return this->CMakeInstance.GetGlobalGenerator()->GetCurrentMakefile();
    return 0;
  }
  void PreRunHook(const cmListFileContext& context,
                  const cmListFileFunction& line) override
  {
    state = State::Running;
    currentLocation = context;

    if (breakDepth != -1) {
      auto currentDepth = GetBacktrace().Depth();
      if (currentDepth == breakDepth)
        breakPending = true;
    }

    {
      std::lock_guard<std::mutex> l(breakpointMutex);
      for (auto& bp : breakpoints) {
        if (bp.matches(context)) {
          breakPending = true;
          break;
        }
      }
    }

    if (breakPending) {
      PauseExecution();
    }
  }

  void ErrorHook(const cmListFileContext& context) override
  {
    PauseExecution();
  }

  breakpoint_id SetBreakpoint(const std::string& fileName,
                              size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.emplace_back(fileName, line);
    return breakpoints.size() - 1;
  }

  breakpoint_id SetWatchpoint(const std::string& expr) override { return 0; }

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
  virtual cmPauseContext PauseContext() override
  {
    return cmPauseContext(m, this);
  }
};

cmDebugger* cmDebugger::Create(cmake& global)
{
  return new cmRemoteDebugger_impl(global);
}

cmDebuggerListener::cmDebuggerListener(cmDebugger& debugger)
  : Debugger(debugger)
{
}

cmBreakpoint::cmBreakpoint(const std::string& file, size_t line)
  : file(file)
  , line(line)
{
}
cmBreakpoint::operator bool() const
{
  return !file.empty();
}
inline bool cmBreakpoint::matches(const cmListFileContext& ctx) const
{
  return matches(ctx.FilePath, ctx.Line);
}

bool cmBreakpoint::matches(const std::string& testFile, size_t testLine) const
{
  if (file.empty())
    return false;

  if (line != testLine && line != (size_t)-1)
    return false;

  return testFile.find(file) != std::string::npos;
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
  if (!*this)
    throw std::runtime_error(
      "Attempt to access backtrace with invalid context");
  return Debugger->GetBacktrace();
}

cmMakefile* cmPauseContext::GetMakefile() const
{
  if (!*this)
    throw std::runtime_error(
      "Attempt to access makefile with invalid context");
  return Debugger->GetMakefile();
}

void cmPauseContext::Continue()
{
  if (!*this)
    throw std::runtime_error("Attempt to continue with invalid context");
  Debugger->Continue();
}

void cmPauseContext::Step()
{
  if (!*this)
    throw std::runtime_error("Attempt to step with invalid context");
  Debugger->Step();
}

void cmPauseContext::StepIn()
{
  if (!*this)
    throw std::runtime_error("Attempt to step inwith invalid context");
  Debugger->StepIn();
}

void cmPauseContext::StepOut()
{
  if (!*this)
    throw std::runtime_error("Attempt to stepout with invalid context");
  Debugger->StepOut();
}

cmListFileContext cmPauseContext::CurrentLine() const
{
  if (!*this)
    throw std::runtime_error(
      "Attempt to access current line with invalid context");
  return Debugger->CurrentLine();
}
