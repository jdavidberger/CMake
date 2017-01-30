#ifndef CMAKE_CMDEBUGSERVERSIMPLE_H
#define CMAKE_CMDEBUGSERVERSIMPLE_H

#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"
#include <memory>

class cmDebugServer : public cmServerBase, public cmDebugerListener
{
public:
  cmDebugServer(cmConnection* conn);
  virtual bool OnSignal(int signum) override;
};

class cmDebugServerSimple : public cmDebugServer
{
public:
  cmDebugServerSimple();

  cmDebugServerSimple(cmConnection* conn);
  void printPrompt();
  virtual void ProcessRequest(const std::string& request) override;

  void OnChangeState() override;
};

#endif // CMAKE_CMDEBUGSERVERSIMPLE_H
