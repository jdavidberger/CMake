/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebugServer.h"
#include "cmDebugServerConsole.h"
#include "cmMakefile.h"

cmDebugServer::cmDebugServer(cmDebugger& debugger, cmConnection* conn)
  : cmServerBase(conn)
  , cmDebuggerListener(debugger)
{
}

bool cmDebugServer::OnSignal(int signum)
{
  if (signum == 2) {
    cmDebuggerListener::Debugger.Break();
    return true;
  }
  return false;
}
