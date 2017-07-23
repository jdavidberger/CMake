#pragma once

#include "cmAutoHandle.h"
#include "cmConnection.h"

class cmTcpIpConnection : public cmEventBasedConnection
{
public:
  ~cmTcpIpConnection() override;
  cmTcpIpConnection(int Port);
  cmTcpIpConnection(int Port, cmConnectionBufferStrategy* bufferStrategy);

  bool OnServeStart(std::string* pString) override;

  bool OnConnectionShuttingDown() override;

  void SetServer(cmServerBase* s) override;

  void Connect(uv_stream_t* server) override;

private:
  int Port;
  auto_tcp_t ServerHandle;
  auto_tcp_t ClientHandle;
};
