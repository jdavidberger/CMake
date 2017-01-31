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
  std::mutex m;
  std::condition_variable cv;
  bool breakPending = true; // Break on connection
  std::vector<cmBreakpoint> breakpoints;

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
  {
  }
  std::set<cmDebugerListener*> listeners;
  void AddListener(cmDebugerListener* listener) override
  {
    listeners.insert(listener);
  }

  void RemoveListener(cmDebugerListener* listener) override
  {
    listeners.erase(listener);
  }
  virtual const std::vector<cmBreakpoint>& GetBreakpoints() const
  {
    return breakpoints;
  }
  void PauseExecution(std::unique_lock<std::mutex>& lk)
  {
    breakPending = false;
    state = State::Paused;
    for (auto& l : listeners) {
      l->OnChangeState();
    }
    cv.wait(lk);
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
    std::unique_lock<std::mutex> lk(m);
    state = State::Running;
    currentLocation = context;
    if (breakPending) {
      PauseExecution(lk);
    }

    for (auto& bp : breakpoints) {
      if (bp.matches(context)) {
        PauseExecution(lk);
        break;
      }
    }
  }

  void ErrorHook(const cmListFileContext& context) override {}

  breakpoint_id SetBreakpoint(const std::string& fileName,
                              size_t line) override
  {
    breakpoints.emplace_back(fileName, line);
    return breakpoints.size() - 1;
  }

  breakpoint_id SetWatchpoint(const std::string& expr) override { return 0; }

  void ClearBreakpoint(breakpoint_id id) override
  {
    if (breakpoints.size() > id) {
      breakpoints[id].file = "";
    }
  }

  void Continue() override { cv.notify_all(); }

  void Break() override { breakPending = true; }

  void Step(size_t n = 1) override
  {
    for (size_t i = 0; i < n; i++) {
      StepIn();
    }
  }

  void StepIn() override
  {
    breakPending = true;
    Continue();
  }

  void StepOut() override { Step(); }

  std::string Print(const std::string& expr) override { return ""; }

  std::string PrintBacktrace() override { return ""; }

  State::t CurrentState() const override { return this->state; }
};

cmDebugger* cmDebugger::Create(cmake& global)
{
  return new cmRemoteDebugger_impl(global);
}

cmDebugerListener::cmDebugerListener(cmDebugger& debugger)
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
  if (file.empty())
    return false;

  if (line != ctx.Line && line != (size_t)-1)
    return false;

  return ctx.FilePath.find(file) != std::string::npos;
}
