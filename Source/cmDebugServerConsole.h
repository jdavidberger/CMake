#ifndef CMAKE_CMDEBUGSERVERSIMPLE_H
#define CMAKE_CMDEBUGSERVERSIMPLE_H

#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"
#include <memory>

class cmDebugServer : public cmServerBase, public cmDebugerListener
{
public:
  cmDebugServer(cmDebugger& debugger, cmConnection* conn);
  virtual bool OnSignal(int signum) override;
};

class cmDebugServerConsole : public cmDebugServer
{
public:
  cmDebugServerConsole(cmDebugger& debugger);
  cmDebugServerConsole(cmDebugger& debugger, cmConnection* conn);
  void printPrompt();
  virtual void ProcessRequest(const std::string& request) override;

  void OnChangeState() override;
};

#endif // CMAKE_CMDEBUGSERVERSIMPLE_H
