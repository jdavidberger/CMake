/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef CMAKE_CMREMOTEDEBUGGER_H
#define CMAKE_CMREMOTEDEBUGGER_H

#include "cmListFileCache.h"
#include <memory>
#include <mutex>

class cmake;
class cmDebugger;

class cmDebuggerListener
{
protected:
  cmDebugger& Debugger;

public:
  cmDebuggerListener(cmDebugger& debugger);
  virtual ~cmDebuggerListener() {}
  virtual void OnChangeState() {}
  virtual void OnUserMessage(const std::string& msg) { (void)msg; }
};

class cmBreakpoint
{
public:
  std::string file;
  size_t line;
  cmBreakpoint(const std::string& file = "", size_t line = 0);
  operator bool() const;
  bool matches(const cmListFileContext& ctx) const;

  bool matches(const std::string& fileName, size_t line) const;
};

class cmPauseContext
{
  cmDebugger* Debugger = 0;
  std::unique_lock<std::mutex> Lock;

public:
  cmPauseContext(std::mutex& m, cmDebugger* debugger);
  operator bool() const;
  virtual cmListFileBacktrace GetBacktrace() const;
  virtual cmMakefile* GetMakefile() const;

  virtual void Continue();
  virtual void Step();
  virtual void StepIn();
  virtual void StepOut();
  virtual cmListFileContext CurrentLine() const;
};

class cmDebugger
{
private:
  friend class cmPauseContext;
  virtual cmListFileBacktrace GetBacktrace() const = 0;
  virtual cmMakefile* GetMakefile() const = 0;

  virtual void Continue() = 0;
  virtual void Step() = 0;
  virtual void StepIn() = 0;
  virtual void StepOut() = 0;
  virtual cmListFileContext CurrentLine() const = 0;

public:
  typedef size_t breakpoint_id;
  struct State
  {
    enum t
    {
      Unknown = 0,
      Running = 1,
      Paused = 2
    };
  };
  virtual const std::vector<cmBreakpoint>& GetBreakpoints() const = 0;
  virtual State::t CurrentState() const = 0;

  static cmDebugger* Create(cmake& global);
  virtual ~cmDebugger() {}

  virtual void PreRunHook(const cmListFileContext& context,
                          const cmListFileFunction& line) = 0;
  virtual void ErrorHook(const cmListFileContext& context) = 0;

  virtual breakpoint_id SetBreakpoint(const std::string& fileName,
                                      size_t line) = 0;
  virtual breakpoint_id SetWatchpoint(const std::string& expr) = 0;
  virtual void ClearBreakpoint(breakpoint_id) = 0;
  virtual void ClearBreakpoint(const std::string& fileName, size_t line) = 0;
  virtual void ClearAllBreakpoints() = 0;
  virtual void Break() = 0;

  virtual cmPauseContext PauseContext() = 0;

  virtual void AddListener(cmDebuggerListener* listener) = 0;
  virtual void RemoveListener(cmDebuggerListener* listener) = 0;
};

#endif // CMAKE_CMREMOTEDEBUGGER_H
