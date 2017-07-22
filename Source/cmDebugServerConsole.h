/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef CMAKE_CMDEBUGSERVERSIMPLE_H
#define CMAKE_CMDEBUGSERVERSIMPLE_H

#include "cmDebugServer.h"
#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"

class cmDebugServerConsole : public cmDebugServer
{
  bool PrintPrompt = true;

public:
  cmDebugServerConsole(cmDebugger& debugger);
  cmDebugServerConsole(cmDebugger& debugger, cmConnection* conn,
                       bool PrintPrompt = true);
  void printPrompt(cmConnection* connection);
  void ProcessRequest(cmConnection* connection,
                              const std::string& request) override;

  void OnChangeState() override;
  void OnBreakpoint(breakpoint_id breakpoint) override;

  void OnWatchpoint(const std::string& variable, int access,
                    const std::string& newValue) override;
};

#endif // CMAKE_CMDEBUGSERVERSIMPLE_H
