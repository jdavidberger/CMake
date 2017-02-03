//
// Created by justin on 2/3/17.
//

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
