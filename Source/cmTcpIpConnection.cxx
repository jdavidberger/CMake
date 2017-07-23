//
// Created by justin on 2/4/17.
//

#include "cmTcpIpConnection.h"
#include "cmServer.h"

void cmTcpIpConnection::Connect(uv_stream_t* server)
{
  if ((uv_handle_t*)this->ClientHandle) {
    // Ignore it; we already have a connection
    return;
  }

  this->ClientHandle.init(*this->Server->GetLoop(),
                          static_cast<cmConnection*>(this));
  if (uv_accept(server, this->ClientHandle) != 0) {
    uv_close(this->ClientHandle, nullptr);
    return;
  }
  this->ReadStream = this->ClientHandle;
  this->WriteStream = this->ClientHandle;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
  this->Server->OnConnected(this);
}

cmTcpIpConnection::cmTcpIpConnection(int port)
  : Port(port)
{
}

cmTcpIpConnection::cmTcpIpConnection(
  int port, cmConnectionBufferStrategy* bufferStrategy)
  : cmEventBasedConnection(bufferStrategy)
  , Port(port)
{
}

cmTcpIpConnection::~cmTcpIpConnection()
{
}

bool cmTcpIpConnection::OnServeStart(std::string* errorMessage)
{
  this->ServerHandle.init(*this->Server->GetLoop(),
                          static_cast<cmConnection*>(this));

  struct sockaddr_in recv_addr;
  uv_ip4_addr("0.0.0.0", Port, &recv_addr);

  int r;
  if ((r = uv_tcp_bind(this->ServerHandle, (const sockaddr*)&recv_addr, 0)) !=
      0) {
    *errorMessage = std::string("Internal Error trying to bind to port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }

  if ((r = uv_listen(this->ServerHandle, 1, on_new_connection)) != 0) {
    *errorMessage = std::string("Internal Error listening on port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }

  return true;
}

bool cmTcpIpConnection::OnConnectionShuttingDown()
{
  this->ClientHandle.reset();
  this->ServerHandle.reset();

  this->WriteStream = nullptr;
  this->ReadStream = nullptr;
  return true;
}

void cmTcpIpConnection::SetServer(cmServerBase* s)
{
  cmConnection::SetServer(s);
}
