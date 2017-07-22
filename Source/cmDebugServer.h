/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmDebugger.h"
#include "cmServer.h"
#include <condition_variable>
#include <iostream>

#include "cmAutoHandle.h"

class cmDebugServer : public cmServerBase, public cmDebuggerListener
{
public:
  cmDebugServer(cmDebugger& debugger, cmConnection* conn);

  virtual ~cmDebugServer();

  virtual bool OnSignal(int signum) override;
  void StartShutDown() override;

  virtual void Broadcast(const std::string& msg);
  void AsyncBroadcast(const std::string& msg);
  void ProcessBroadcastQueue();

private:
  auto_async_t BroadcastQueueSignal;

  std::condition_variable BroadcastCV;
  std::vector<std::string> BroadcastQueue;
  std::mutex BroadcastQueueMutex;
};
