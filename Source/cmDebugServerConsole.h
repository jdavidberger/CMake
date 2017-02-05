#ifndef CMAKE_CMDEBUGSERVERSIMPLE_H
#define CMAKE_CMDEBUGSERVERSIMPLE_H

#include "cmDebugServer.h"
#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"

class cmDebugServerConsole : public cmDebugServer
{
public:
  cmDebugServerConsole(cmDebugger& debugger);
  cmDebugServerConsole(cmDebugger& debugger, cmConnection* conn);
  void printPrompt(cmConnection* connection);
  virtual void ProcessRequest(cmConnection* connection,
                              const std::string& request) override;

  void OnChangeState() override;
};

#endif // CMAKE_CMDEBUGSERVERSIMPLE_H
