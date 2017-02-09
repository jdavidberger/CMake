#pragma once

#include "cmDebugServer.h"
#include "cmDebugServerConsole.h"
#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"

class cmDebugServerJson : public cmDebugServer
{
public:
  cmDebugServerJson(cmDebugger& debugger, size_t port);
  cmDebugServerJson(cmDebugger& debugger, cmConnection* conn);

  virtual void ProcessRequest(cmConnection* connection,
                              const std::string& request) override;
  void SendStateUpdate(cmConnection* connection);
  void OnChangeState() override;

  void OnConnected(cmConnection* connection) override;
};

std::unique_ptr<cmConnectionBufferStrategy> CreateJsonConnectionStrategy();
