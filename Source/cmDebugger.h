/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef CMAKE_CMREMOTEDEBUGGER_H
#define CMAKE_CMREMOTEDEBUGGER_H

#include "cmListFileCache.h"
#include <memory>
#include <mutex>

class cmake;
class cmDebugger;
typedef size_t breakpoint_id;
typedef size_t watchpoint_id;

/***
 * Interface for receiving events from the debugger.
 */
class cmDebuggerListener
{
protected:
  cmDebugger& Debugger;

public:
  cmDebuggerListener(cmDebugger& debugger);
  virtual ~cmDebuggerListener() {}

  /***
   * Triggers whenever the state changes. The listener is left to query for
   * what that state is and/or attempt to
   * get a pause context.
   */
  virtual void OnChangeState() {}

  virtual void OnBreakpoint(breakpoint_id breakpoint) { (void)breakpoint; }

  virtual void OnWatchpoint(const std::string& variable, int access,
                            const std::string& newValue)
  {
    (void)variable;
    (void)access;
    (void)newValue;
  }
};

class cmWatchpoint
{
public:
  enum WatchpointType
  {
    WATCHPOINT_NONE = 0,
    WATCHPOINT_DEFINE = 1,
    WATCHPOINT_WRITE = 2,
    WATCHPOINT_READ = 4,
    WATCHPOINT_UNDEFINED = 8,
    WATCHPOINT_MODIFY =
      WATCHPOINT_UNDEFINED | WATCHPOINT_WRITE | WATCHPOINT_DEFINE,
    WATCHPOINT_ALL = WATCHPOINT_UNDEFINED | WATCHPOINT_READ |
      WATCHPOINT_WRITE |
      WATCHPOINT_DEFINE
  };
  static std::string GetTypeAsString(WatchpointType type);
  watchpoint_id Id;
  WatchpointType Type;
  std::string Variable;

  cmWatchpoint(watchpoint_id Id, WatchpointType Type,
               const std::string& Variable);
};

class cmBreakpoint
{
public:
  breakpoint_id Id;
  std::string File;
  size_t Line;
  cmBreakpoint(breakpoint_id ID, const std::string& file = "",
               size_t line = 0);

  bool matches(const cmListFileContext& ctx) const;
  bool matches(const std::string& fileName, size_t line) const;
};

/***
 * This object secures and protects against thread-safety concerns. All
 * operations in this class can only safely be
 * called when the state of the debugger is paused.
 *
 * It is up to the user to verify any given object is valid via the overload
 * bool operator. If it is not locked,
 * any operation will through a run-time exception.
 */
class cmPauseContext
{
public:
  typedef std::recursive_timed_mutex master_mutex_t;
  cmPauseContext(master_mutex_t& m, cmDebugger* debugger);
  /**
   * @return Whether or not this object has aquired the required lock to do
   * anything.
   */
  operator bool() const;

  cmListFileBacktrace GetBacktrace() const;
  cmMakefile* GetMakefile() const;
  void Continue();
  void Step();
  void StepIn();
  void StepOut();
  cmListFileContext CurrentLine() const;

private:
  cmDebugger* Debugger = CM_NULLPTR;
  std::unique_lock<master_mutex_t> Lock;
};

/***
 * Debugger engine. All public functions on this interface are meant to be
 * thread safe.
 */
class cmDebugger
{
private:
  friend class cmPauseContext;

  // The following private functions are meant to be called only from
  // cmPauseContext.
  virtual cmListFileBacktrace GetBacktrace() const = 0;
  virtual cmMakefile* GetMakefile() const = 0;

  virtual void Continue() = 0;
  virtual void Step() = 0;
  virtual void StepIn() = 0;
  virtual void StepOut() = 0;
  virtual cmListFileContext CurrentLine() const = 0;

public:
  /***
   * Factor constructor for a debugger instance.
   * @param global Global used to query the running instance
   * @return A pointer to a debugger engine.
   */
  static cmDebugger* Create(cmake& global);

  struct State
  {
    enum t
    {
      Unknown = 0,
      Running = 1,
      Paused = 2
    };
  };
  virtual std::vector<cmBreakpoint> GetBreakpoints() const = 0;
  virtual std::vector<cmWatchpoint> GetWatchpoints() const = 0;
  virtual State::t CurrentState() const = 0;

  virtual ~cmDebugger() {}

  /***
   * This hook is entered before a given line; and is where the thread may be
   * safely paused and queried.
   */
  virtual void PreRunHook(const cmListFileContext& context,
                          const cmListFileFunction& line) = 0;

  /**
   * This hook is entered before a fatal error is officially reported, and may
   * pause the thread for interspection
   */
  virtual void ErrorHook(const cmListFileContext& context) = 0;

  virtual breakpoint_id SetBreakpoint(const std::string& fileName,
                                      size_t line) = 0;
  virtual bool ClearBreakpoint(breakpoint_id id) = 0;
  virtual size_t ClearBreakpoint(const std::string& fileName, size_t line) = 0;
  virtual void ClearAllBreakpoints() = 0;

  virtual breakpoint_id SetWatchpoint(const std::string& expr,
                                      cmWatchpoint::WatchpointType watchpoint =
                                        cmWatchpoint::WATCHPOINT_MODIFY) = 0;
  virtual bool ClearWatchpoint(watchpoint_id id) = 0;
  virtual void ClearAllWatchpoints() = 0;

  virtual void Break() = 0;
  virtual void SetBreakOnError(bool flag) = 0;
  /***
   * Requests a pause context
   * @return The returned pause context MUST be checked for validity before
   * using it. The lock is scoped to
   * the objects lifetime; so while this object is active cmake will be paused.
   */
  virtual cmPauseContext PauseContext() = 0;

  /***
   * @param listener New listener to the debugger. When given a listener, the
   * debugger then owns that pointer
   * and will delete it at dtor.
   */
  virtual void AddListener(cmDebuggerListener* listener) = 0;

  /***
   * @param listener Delist a given listener. Also gives up ownership of the
   * pointer; the caller is now responsible
   * for freeing it.
   */
  virtual void RemoveListener(cmDebuggerListener* listener) = 0;
  virtual void ClearListeners() = 0;
};

#endif // CMAKE_CMREMOTEDEBUGGER_H
