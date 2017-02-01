#pragma once

#include "cmDebugServerConsole.h"
#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"
#include <memory>

class cmDebugServerJson : public cmDebugServer
{
public:
  cmDebugServerJson(cmDebugger& debugger, size_t port);
  cmDebugServerJson(cmDebugger& debugger, cmConnection* conn);

  virtual void ProcessRequest(const std::string& request) override;
  void SendStateUpdate();
  void OnChangeState() override;
  virtual void OnNewConnection();
};
