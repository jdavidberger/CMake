/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
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
   * Starts a json server using a socket with a json connection strategy on
   * stdin
   *
   * @param debugger The debugger to attach to
   */
  cmDebugServerJson(cmDebugger& debugger);

  /***
   * Starts a json server using a socket with a json connection strategy
   *
   * @param debugger The debugger to attach to
   * @param pipe The named pipe to use
   */
  cmDebugServerJson(cmDebugger& debugger, const std::string& pipe);

  /***
 * Starts a json server using a socket with a json connection strategy
 *
 * @param debugger The debugger to attach to
 * @param pipe The named pipe to use
 */
  cmDebugServerJson(cmDebugger& debugger, int port);

  /***
 * General purpose constructor. It is expected that the buffering strategy
 * only ever gives valid JSON objects.
 * @param debugger The debugger to attach to
 * @param conn Any valid connection
 */
  cmDebugServerJson(cmDebugger& debugger, cmConnection* conn);

  ~cmDebugServerJson() override { Close(); }

  void ProcessRequest(cmConnection* connection,
                      const std::string& request) override;

  void OnConnected(cmConnection* connection) override;

  void OnChangeState() override;

  std::string StatusString() const;
  void SendStatus(cmConnection* connection);
};

/***
 * Creates a buffering strategy which chunks data as JSON objects
 */
std::unique_ptr<cmConnectionBufferStrategy> CreateJsonConnectionStrategy();
