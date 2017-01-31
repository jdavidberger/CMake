//
// Created by J on 1/28/2017.
//

#include "cmDebugger.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include <condition_variable>
#include <set>

class cmRemoteDebugger_impl : public cmDebugger
{
  cmGlobalGenerator& global;
  State::t state = State::Unknown;
  std::mutex m;
  std::condition_variable cv;
  bool breakPending = true; // Break on connection
  std::vector<cmBreakpoint> breakpoints;

public:
  cmRemoteDebugger_impl(cmGlobalGenerator& global)
    : global(global)
  {
  }
  std::set<cmDebugerListener*> listeners;
  void AddListener(cmDebugerListener& listener) override
  {
    listeners.insert(&listener);
  }

  void RemoveListener(cmDebugerListener& listener) override
  {
    listeners.erase(&listener);
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
    return global.GetCurrentMakefile()->GetBacktrace();
  }
  virtual cmMakefile* GetMakefile() const
  {
    return global.GetCurrentMakefile();
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

std::unique_ptr<cmDebugger> cmDebugger::Create(cmGlobalGenerator& global)
{
  return std::unique_ptr<cmDebugger>(new cmRemoteDebugger_impl(global));
}

cmDebugerListener::cmDebugerListener(const std::weak_ptr<cmDebugger>& debugger)
  : Debugger(debugger)
{
  if (auto dbg = debugger.lock())
    dbg->AddListener(*this);
}

cmDebugerListener::cmDebugerListener()
{
}

void cmDebugerListener::SetDebugger(
  const std::shared_ptr<cmDebugger>& debugger)
{
  this->Debugger = debugger;
  debugger->AddListener(*this);
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
