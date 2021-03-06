/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebugServer.h"
#include "cmDebugServerConsole.h"
#include "cmMakefile.h"

static void __broadcast(uv_async_t* arg)
{
  auto server = reinterpret_cast<cmDebugServer*>(arg->data);
  server->ProcessBroadcastQueue();
}

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

void cmDebugServer::StartShutDown()
{
  BroadcastQueueSignal.reset();
  cmServerBase::StartShutDown();
}

void cmDebugServer::AsyncBroadcast(const std::string& msg)
{
  if (msg.empty()) {
    return;
  }

  std::lock_guard<std::mutex> l(BroadcastQueueMutex);
  BroadcastQueue.push_back(msg);
  this->BroadcastQueueSignal.send();
}

void cmDebugServer::Broadcast(const std::string& msg)
{
  uv_rwlock_rdlock(&ConnectionsMutex);
  for (auto& connection : Connections) {
    if (connection->IsOpen()) {
      connection->WriteData(msg);
    }
  }
  uv_rwlock_rdunlock(&ConnectionsMutex);
}

void cmDebugServer::ProcessBroadcastQueue()
{
  std::lock_guard<std::mutex> l(BroadcastQueueMutex);
  for (auto& msg : BroadcastQueue) {
    this->Broadcast(msg);
  }
  this->BroadcastQueue.clear();
}

cmDebugServer::~cmDebugServer()
{
  Close();
}

void cmDebugServer::OnServeStart()
{
  cmServerBase::OnServeStart();
  this->BroadcastQueueSignal.init(Loop, __broadcast, this);
}
