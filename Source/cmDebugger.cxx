/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmDebugger.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmake.h"
#include <atomic>
#include <cmVariableWatch.h>
#include <condition_variable>
#include <map>
#include <set>

/**
 * Actual debugger implementation.
 */
class cmDebugger_impl : public cmDebugger
{
  cmake& CMakeInstance;
  std::atomic<State::t> state = { State::Unknown };

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
  typedef cmPauseContext::master_mutex_t master_mutex_t;
  master_mutex_t m;
  std::condition_variable_any cv;
  std::unique_lock<master_mutex_t> Lock;

  /**
   * This flag sets up the next instruction to go into the pause state.
   */
  std::atomic<bool> breakPending = { true }; // Break on connection

  /**
   * This flag is used to avoid spurious wakeups resuming the debugger
   */
  std::atomic<bool> continuePending = { false };

  /***
   * We run the breakpoints off of a separate mutex so that they can be set and
   * cleared while running
   */
  mutable std::mutex breakpointMutex;
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

  bool BreakOnError = true;

  struct Watchpoint : public cmWatchpoint
  {
    typedef std::shared_ptr<Watchpoint> ptr;
    cmDebugger_impl* debugger = CM_NULLPTR;
    void* user_data = CM_NULLPTR;
    Watchpoint(cmDebugger_impl* _debugger, watchpoint_id _Id,
               WatchpointType _Type, const std::string& _Variable)
      : cmWatchpoint(_Id, _Type, _Variable)
      , debugger(_debugger)
    {
    }
  };
  std::map<watchpoint_id, std::weak_ptr<Watchpoint> > activeWatchpoints;
  size_t nextBreakId = 1;

public:
  ~cmDebugger_impl() override
  {
    ClearListeners();
    ClearAllWatchpoints();
    ClearAllBreakpoints();
  }
  cmDebugger_impl(cmake& cmakeInstance)
    : CMakeInstance(cmakeInstance)
    , Lock(m)
  {
  }
  void ClearListeners() override
  {
    for (auto& s : listeners) {
      delete s;
    }
    listeners.clear();
  }
  void AddListener(cmDebuggerListener* listener) override
  {
    listeners.insert(listener);
  }

  void RemoveListener(cmDebuggerListener* listener) override
  {
    listeners.erase(listener);
  }

  void SetState(State::t newState) { state = newState; }

  void PauseExecution()
  {
    breakPending = false;
    breakDepth = -1;
    SetState(State::Paused);
    for (auto& l : listeners) {
      l->OnChangeState();
    }

    continuePending = false;
    cv.wait(Lock, [this] { return (bool)continuePending; });
    SetState(State::Running);
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

    SetState(State::Running);
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
      std::lock_guard<std::mutex> lock(breakpointMutex);
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
  void SetBreakOnError(bool flag) override { BreakOnError = flag; }
  void ErrorHook(const cmListFileContext& context) override
  {
    (void)context;
    if (BreakOnError) {
      PauseExecution();
    }
  }

  void OnWatchCallback(const std::string& variable, int access_type,
                       const char* newValue)
  {
    // It's possible that this is triggered by the user setting / reading the
    // variable via the debugger;
    // in which case we can't pause and shouldn't notify listeners.
    if (state != State::Paused) {
      for (auto& l : listeners) {
        l->OnWatchpoint(variable, access_type, newValue);
      }
      PauseExecution();
    }
  }

  static void WatchMethodCB(const std::string& variable, int access_type,
                            void* client_data, const char* newValue,
                            const cmMakefile* mf)
  {
    (void)mf;
    auto watchpointPtr = (Watchpoint::ptr*)client_data;
    if (watchpointPtr == CM_NULLPTR) {
      return;
    }

    auto watchpoint = watchpointPtr->get();
    if (watchpoint == CM_NULLPTR) {
      return;
    }

    auto debugger = watchpoint->debugger;
    if (debugger == CM_NULLPTR) {
      return;
    }

    bool isRead =
      (access_type == cmVariableWatch::UNKNOWN_VARIABLE_READ_ACCESS) |
      (access_type == cmVariableWatch::VARIABLE_READ_ACCESS);
    bool isWrite = access_type == cmVariableWatch::VARIABLE_MODIFIED_ACCESS;
    bool isDefined =
      access_type == cmVariableWatch::UNKNOWN_VARIABLE_DEFINED_ACCESS;
    bool isUnset = access_type == cmVariableWatch::VARIABLE_REMOVED_ACCESS;

    bool matchesFilter =
      (isRead && watchpoint->Type & cmWatchpoint::WATCHPOINT_READ) |
      (isWrite && watchpoint->Type & cmWatchpoint::WATCHPOINT_WRITE) |
      (isDefined && watchpoint->Type & cmWatchpoint::WATCHPOINT_DEFINE) |
      (isUnset && watchpoint->Type & cmWatchpoint::WATCHPOINT_UNDEFINED);

    if (matchesFilter) {
      debugger->OnWatchCallback(variable, access_type, newValue);
    }
  }
  static void WatchMethodDelete(void* client_data)
  {
    delete (Watchpoint::ptr*)client_data;
  }

  std::vector<cmWatchpoint> GetWatchpoints() const override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    std::vector<cmWatchpoint> rtn;
    rtn.reserve(activeWatchpoints.size());
    for (auto& watchWeak : activeWatchpoints) {
      if (auto watch = watchWeak.second.lock()) {
        rtn.push_back(*watch);
      }
    }
    return rtn;
  }

  watchpoint_id SetWatchpoint(
    const std::string& expr,
    cmWatchpoint::WatchpointType watchpointType) override
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
    // and the cmDebugger simultaneously in different threads. Since these
    // objects are both dtored in the main thread, this shouldn't be an issue.
    std::lock_guard<std::mutex> l(breakpointMutex);
    watchpoint_id nextId = nextBreakId++;
    auto watch =
      new Watchpoint::ptr(new Watchpoint(this, nextId, watchpointType, expr));

    // Looks odd for sure but we need to store away user_data we hand off to
    // AddWatch so we can remove the watch later on
    (*watch)->user_data = watch;
    activeWatchpoints[nextId] = *watch;

    this->CMakeInstance.GetVariableWatch()->AddWatch(expr, WatchMethodCB,
                                                     watch, WatchMethodDelete);
    return nextId;
  }
  bool ClearWatchpoint(watchpoint_id id) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    auto watchpointIt = activeWatchpoints.find(id);
    auto originalSize = activeWatchpoints.size();
    if (watchpointIt != activeWatchpoints.end()) {
      if (auto watchpoint = watchpointIt->second.lock()) {
        this->CMakeInstance.GetVariableWatch()->RemoveWatch(
          watchpoint->Variable, WatchMethodCB, watchpoint->user_data);
        activeWatchpoints.erase(id);
      }
    }

    return originalSize != activeWatchpoints.size();
  }

  void ClearAllWatchpoints() override
  {
    auto watchpoints = GetWatchpoints();
    for (auto& watch : watchpoints) {
      ClearWatchpoint(watch.Id);
    }
  }

  std::vector<cmBreakpoint> GetBreakpoints() const override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    return breakpoints;
  }

  breakpoint_id SetBreakpoint(const std::string& fileName,
                              size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    auto nextId = nextBreakId++;
    breakpoints.emplace_back(nextId, fileName, line);
    return nextId;
  }

  bool ClearBreakpoint(breakpoint_id id) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    auto predicate = [id](const cmBreakpoint& breakpoint) {
      return breakpoint.Id == id;
    };
    auto originalSize = breakpoints.size();
    breakpoints.erase(
      std::remove_if(breakpoints.begin(), breakpoints.end(), predicate),
      breakpoints.end());
    return originalSize != breakpoints.size();
  }

  size_t ClearBreakpoint(const std::string& fileName, size_t line) override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    auto pred = [&](const cmBreakpoint br) {
      return br.matches(fileName, line);
    };
    auto originalSize = breakpoints.size();
    breakpoints.erase(
      std::remove_if(breakpoints.begin(), breakpoints.end(), pred),
      breakpoints.end());
    return originalSize - breakpoints.size();
  }

  void ClearAllBreakpoints() override
  {
    std::lock_guard<std::mutex> l(breakpointMutex);
    breakpoints.clear();
  }

  void Continue() override
  {
    continuePending = true;
    cv.notify_all();
  }

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

cmBreakpoint::cmBreakpoint(breakpoint_id id, const std::string& file,
                           size_t line)
  : Id(id)
  , File(file)
  , Line(line)
{
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

cmPauseContext::cmPauseContext(master_mutex_t& m, cmDebugger* debugger)
  : Debugger(debugger)
  , Lock(m, std::try_to_lock)
{
  // If we didn't aquire the lock, but the current state is paused, we are
  // likely
  // in a very simple race condition; right before the main thread is about to
  // wait.
  // This is fine in general but annoying for unit testing. A smallish timeout
  // is used here for consistency. The only conditions that will fail are:
  // a) Incredibly slow machine; or starved threads
  // b) Multiple cmPauseContexts in multiple threads were created
  // c) Execution was resumed as this oject was created
  if (!Lock.owns_lock() &&
      Debugger->CurrentState() == cmDebugger::State::Paused) {
    Lock.try_lock_for(std::chrono::milliseconds(100));
  }
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

cmWatchpoint::cmWatchpoint(watchpoint_id _Id,
                           cmWatchpoint::WatchpointType _Type,
                           const std::string& _Variable)
  : Id(_Id)
  , Type(_Type)
  , Variable(_Variable)
{
}

std::string cmWatchpoint::GetTypeAsString(cmWatchpoint::WatchpointType type)
{
  switch (type) {
    case WATCHPOINT_NONE:
      return "NONE";
    case WATCHPOINT_ALL:
      return "ALL";
    case WATCHPOINT_MODIFY:
      return "MODIFY";
    case WATCHPOINT_DEFINE:
      return "DEFINE";
    case WATCHPOINT_READ:
      return "READ";
    case WATCHPOINT_UNDEFINED:
      return "UNDEFINED";
    case WATCHPOINT_WRITE:
      return "WRITE";
    default:
      break;
  }

  std::string rtn;
  for (auto field : { WATCHPOINT_WRITE, WATCHPOINT_UNDEFINED, WATCHPOINT_READ,
                      WATCHPOINT_DEFINE, WATCHPOINT_MODIFY, WATCHPOINT_ALL }) {
    if (field & type) {
      rtn += GetTypeAsString(field) + ", ";
    }
  }

  // If it doesn't match any of the bits in the field or none, what exactly was
  // passed in?
  assert(!rtn.empty());

  // Remove last comma and space
  if (!rtn.empty()) {
    rtn.pop_back();
    rtn.pop_back();
  }

  return rtn;
}
