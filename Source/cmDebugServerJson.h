#pragma once

#include "cmDebugServer.h"
#include "cmDebugServerConsole.h"
#include "cmDebugger.h"
#include "cmServer.h"
#include "cmServerConnection.h"

/***
 * Debug server which communicates via JSON objects.
 */
class cmDebugServerJson : public cmDebugServer
{
public:
  /***
   * Starts a json server using a socket with a json connection strategy
   *
   * @param debugger The debugger to attach to
   * @param port The port to use
   */
  cmDebugServerJson(cmDebugger& debugger, size_t port);

  /***
   * General purpose constructor. It is expected that the buffering strategy
   * only ever gives valid JSON objects.
   * @param debugger The debugger to attach to
   * @param conn Any valid connection
   */
  cmDebugServerJson(cmDebugger& debugger, cmConnection* conn);

  virtual void ProcessRequest(cmConnection* connection,
                              const std::string& request) override;
  void SendStateUpdate(cmConnection* connection);
  void OnChangeState() override;

  void OnConnected(cmConnection* connection) override;
};

/***
 * Creates a buffering strategy which chunks data as JSON objects
 */
std::unique_ptr<cmConnectionBufferStrategy> CreateJsonConnectionStrategy();
