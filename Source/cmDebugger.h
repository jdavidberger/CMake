/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef CMAKE_CMREMOTEDEBUGGER_H
#define CMAKE_CMREMOTEDEBUGGER_H

#include "cmListFileCache.h"
#include <memory>

class cmGlobalGenerator;
class cmDebugger;

class cmDebugerListener
{
protected:
  std::weak_ptr<cmDebugger> Debugger;

public:
  virtual void SetDebugger(const std::shared_ptr<cmDebugger>& debugger);
  cmDebugerListener(const std::weak_ptr<cmDebugger>& debugger);
  cmDebugerListener();
  virtual ~cmDebugerListener() {}

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
};

class cmDebugger
{
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
  virtual cmListFileContext CurrentLine() const = 0;
  virtual cmListFileBacktrace GetBacktrace() const = 0;
  virtual cmMakefile* GetMakefile() const = 0;

  static std::unique_ptr<cmDebugger> Create(cmGlobalGenerator& global);
  virtual ~cmDebugger() {}

  virtual void PreRunHook(const cmListFileContext& context,
                          const cmListFileFunction& line) = 0;
  virtual void ErrorHook(const cmListFileContext& context) = 0;

  virtual breakpoint_id SetBreakpoint(const std::string& fileName,
                                      size_t line) = 0;
  virtual breakpoint_id SetWatchpoint(const std::string& expr) = 0;
  virtual void ClearBreakpoint(breakpoint_id) = 0;

  virtual void Continue() = 0;
  virtual void Break() = 0;
  virtual void Step(size_t n = 1) = 0;
  virtual void StepIn() = 0;
  virtual void StepOut() = 0;

  virtual std::string Print(const std::string& expr) = 0;
  virtual std::string PrintBacktrace() = 0;

  virtual void AddListener(cmDebugerListener& listener) = 0;
  virtual void RemoveListener(cmDebugerListener& listener) = 0;
};

#endif // CMAKE_CMREMOTEDEBUGGER_H
