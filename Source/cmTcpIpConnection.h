#pragma once

#include "cmConnection.h"

class cmTcpIpConnection : public cmConnection
{
public:
  ~cmTcpIpConnection();
  cmTcpIpConnection(int Port);
  cmTcpIpConnection(int Port, cmConnectionBufferStrategy* bufferStrategy);

  bool OnServeStart(std::string* pString) override;

  bool OnServerShuttingDown() override;

  void SetServer(cmServerBase* s) override;

  void Connect(uv_stream_t* server) override;

private:
  int Port;
  uv_tcp_t* ServerHandle = 0;
  uv_tcp_t* ClientHandle = 0;
};
