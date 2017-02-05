//
// Created by justin on 2/4/17.
//

#include "cmTcpIpConnection.h"
#include "cmServer.h"

void cmTcpIpConnection::Connect(uv_stream_t* server)
{
  if (this->ClientHandle) {
    // Ignore it; we already hae a connection
    return;
  }

  this->ClientHandle = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(this->Server->GetLoop(), this->ClientHandle);
  this->ClientHandle->data = static_cast<cmConnection*>(this);
  auto client = reinterpret_cast<uv_stream_t*>(this->ClientHandle);
  if (uv_accept(server, client) != 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
    return;
  }
  this->ReadStream = client;
  this->WriteStream = client;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
  this->Server->OnConnected(this);
}

cmTcpIpConnection::cmTcpIpConnection(int Port)
  : Port(Port)
{
}

cmTcpIpConnection::cmTcpIpConnection(
  int port, cmConnectionBufferStrategy* bufferStrategy)
  : cmConnection(bufferStrategy)
  , Port(port)
{
}

cmTcpIpConnection::~cmTcpIpConnection()
{
}

bool cmTcpIpConnection::OnServeStart(std::string* errorMessage)
{

  this->ServerHandle = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(this->Server->GetLoop(), this->ServerHandle);
  this->ServerHandle->data = static_cast<cmConnection*>(this);

  struct sockaddr_in recv_addr;
  uv_ip4_addr("0.0.0.0", Port, &recv_addr);

  int r;
  if ((r = uv_tcp_bind(this->ServerHandle, (const sockaddr*)&recv_addr, 0)) !=
      0) {
    *errorMessage = std::string("Internal Error trying to bind to port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }
  auto serverStream = reinterpret_cast<uv_stream_t*>(this->ServerHandle);
  if ((r = uv_listen(serverStream, 1, on_new_connection)) != 0) {
    *errorMessage = std::string("Internal Error listening on port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }

  return true;
}

bool cmTcpIpConnection::OnServerShuttingDown()
{
  if (this->ClientHandle) {
    uv_close(reinterpret_cast<uv_handle_t*>(this->ClientHandle), &on_close);
    this->WriteStream->data = nullptr;
  }
  uv_close(reinterpret_cast<uv_handle_t*>(&this->ServerHandle), &on_close);

  this->ClientHandle = nullptr;
  this->WriteStream = nullptr;
  this->ReadStream = nullptr;
  return true;
}

void cmTcpIpConnection::SetServer(cmServerBase* s)
{
  cmConnection::SetServer(s);
}
