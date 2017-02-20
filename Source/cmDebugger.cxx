/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebugger.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmake.h"
#include <cmVariableWatch.h>
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

  struct Watchpoint
  {
    typedef std::shared_ptr<Watchpoint> ptr;
    cmDebugger_impl* debugger = 0;
    WatchpointType watchpointType;
    Watchpoint(cmDebugger_impl* debugger, WatchpointType watchpointType)
      : debugger(debugger)
      , watchpointType(watchpointType)
    {
    }
  };
  std::vector<std::weak_ptr<Watchpoint> > activeWatchpoints;

public:
  ~cmDebugger_impl()
  {
    for (auto& s : listeners) {
      delete s;
    }
    for (auto& weakWatch : activeWatchpoints) {
      if (auto watch = weakWatch.lock())
        watch->debugger = 0;
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

    // Step in / Step out logic. We have a target
    // stack depth, and when we hit it, pause.
    if (breakDepth != -1) {
      auto currentDepth = GetBacktrace().Depth();
      if (currentDepth == breakDepth)
        breakPending = true;
    }

    // Breakpoint detection
    {
      std::lock_guard<std::mutex> l(breakpointMutex);
      for (size_t bid = 0; bid < breakpoints.size(); bid++) {
        auto& bp = breakpoints[bid];
        if (bp.matches(context)) {
          breakPending = true;
          for (auto& l : listeners) {
            l->OnBreakpoint(bid);
          }
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
    PauseExecution();
  }

  breakpoint_id SetBreakpoint(const std::string& fileName,
                              size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.emplace_back(fileName, line);
    return breakpoints.size() - 1;
  }

  void OnWatchCallback(const std::string& variable,
                       int access_type,
                       const char* newValue)
  {
    // It's possible that this is triggered by the user setting / reading the
    // variable via the debugger;
    // in which case we can't pause and shouldn't notify listeners.
    if (Lock.owns_lock()) {
      for (auto& l : listeners) {
        l->OnWatchpoint(variable, access_type, newValue);
      }
      PauseExecution();
    }
  }

  static void WatchMethodCB(const std::string& variable,
                            int access_type,
                            void* client_data,
                            const char* newValue,
                            const cmMakefile* mf)
  {
    auto watchpointPtr = (Watchpoint::ptr*)client_data;
    if (watchpointPtr == 0)
      return;

    auto watchpoint = watchpointPtr->get();
    if (watchpoint == 0)
      return;

    auto debugger = watchpoint->debugger;
    if (debugger == 0)
      return;

    bool matchesFilter = false;
    bool isRead =
      access_type == cmVariableWatch::UNKNOWN_VARIABLE_READ_ACCESS |
      access_type == cmVariableWatch::VARIABLE_READ_ACCESS;
    bool isWrite = access_type == cmVariableWatch::VARIABLE_MODIFIED_ACCESS;
    bool isDefined =
      access_type == cmVariableWatch::UNKNOWN_VARIABLE_DEFINED_ACCESS;
    bool isUnset = access_type == cmVariableWatch::VARIABLE_REMOVED_ACCESS;

    matchesFilter |=
      isRead && watchpoint->watchpointType & cmDebugger::WATCHPOINT_READ;
    matchesFilter |=
      isWrite && watchpoint->watchpointType & cmDebugger::WATCHPOINT_WRITE;
    matchesFilter |=
      isDefined && watchpoint->watchpointType & cmDebugger::WATCHPOINT_DEFINE;
    matchesFilter |=
      isUnset && watchpoint->watchpointType & cmDebugger::WATCHPOINT_UNDEFINED;

    if (matchesFilter) {
      debugger->OnWatchCallback(variable, access_type, newValue);
    }
  }
  static void WatchMethodDelete(void* client_data)
  {
    delete (Watchpoint::ptr*)client_data;
  }
  breakpoint_id SetWatchpoint(const std::string& expr,
                              WatchpointType watchpoint) override
  {
    // The pointer to a smart pointer mechanism isn't pretty, but it should be
    // safe.The idea is that we store away the control block into
    // activeWatchpoints as a weak ptr; and so if the VariableWatch is
    // destroyed first, the weak ptr just goes stale. If the debugger is
    // destroyed first, the weak_ptrs must still be valid, so in the
    // debugger_impl dtor we clear the debugger on all still-valid watchpoints,
    // and the watchpoint callback is a noop if the debugger field isn't set.

    // NOTE: This means we are safe for either order, but is still isn't thread
    // safe -- you can hit a race condition if you delete the cmVariableWatch
    // and the cmDebugger simultaneously in different threads.
    auto watch = new Watchpoint::ptr(new Watchpoint(this, watchpoint));
    activeWatchpoints.push_back(*watch);

    this->CMakeInstance.GetVariableWatch()->AddWatch(
      expr, WatchMethodCB, watch, WatchMethodDelete);
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
  virtual cmPauseContext PauseContext() override
  {
    return cmPauseContext(m, this);
  }
};

cmDebugger* cmDebugger::Create(cmake& global)
{
  return new cmDebugger_impl(global);
}

cmDebuggerListener::cmDebuggerListener(cmDebugger& debugger)
  : Debugger(debugger)
{
}

cmBreakpoint::cmBreakpoint(const std::string& file, size_t line)
  : File(file)
  , Line(line)
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
  if (File.empty())
    return false;

  if (Line != testLine && Line != (size_t)-1)
    return false;

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
