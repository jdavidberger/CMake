//
// Created by justin on 2/4/17.
//

#include "cmPipeConnection.h"
#include "cmServer.h"
#include "cmServerConnection.h"

cmPipeConnection::cmPipeConnection(const std::string& name,
                                   cmConnectionBufferStrategy* bufferStrategy)
  : cmConnection(bufferStrategy)
  , PipeName(name)
{
}

void cmPipeConnection::Connect(uv_stream_t* server)
{
  if (this->ClientPipe) {
    // Accept and close all pipes but the first:
    uv_pipe_t* rejectPipe = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));

    uv_pipe_init(this->Server->GetLoop(), rejectPipe, 0);
    auto rejecter = reinterpret_cast<uv_stream_t*>(rejectPipe);
    uv_accept(server, rejecter);
    uv_close(reinterpret_cast<uv_handle_t*>(rejecter), &on_close);
    return;
  }

  this->ClientPipe = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
  uv_pipe_init(this->Server->GetLoop(), this->ClientPipe, 0);
  this->ClientPipe->data = static_cast<cmConnection*>(this);
  auto client = reinterpret_cast<uv_stream_t*>(this->ClientPipe);
  if (uv_accept(server, client) != 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
    return;
  }
  this->ReadStream = client;
  this->WriteStream = client;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
  Server->OnConnected(this);
}

bool cmPipeConnection::OnServeStart(std::string* errorMessage)
{
  this->ServerPipe = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
  uv_pipe_init(this->Server->GetLoop(), this->ServerPipe, 0);
  this->ServerPipe->data = static_cast<cmConnection*>(this);

  int r;
  if ((r = uv_pipe_bind(this->ServerPipe, this->PipeName.c_str())) != 0) {
    *errorMessage = std::string("Internal Error with ") + this->PipeName +
      ": " + uv_err_name(r);
    return false;
  }
  auto serverStream = reinterpret_cast<uv_stream_t*>(this->ServerPipe);
  if ((r = uv_listen(serverStream, 1, on_new_connection)) != 0) {
    *errorMessage = std::string("Internal Error listening on ") +
      this->PipeName + ": " + uv_err_name(r);
    return false;
  }

  return cmConnection::OnServeStart(errorMessage);
}

bool cmPipeConnection::OnServerShuttingDown()
{
  if (this->ClientPipe) {
    uv_close(reinterpret_cast<uv_handle_t*>(this->ClientPipe), &on_close);
    this->WriteStream->data = nullptr;
  }
  uv_close(reinterpret_cast<uv_handle_t*>(this->ServerPipe), &on_close);

  this->ClientPipe = nullptr;
  this->ServerPipe = nullptr;
  this->WriteStream = nullptr;
  this->ReadStream = nullptr;

  return cmConnection::OnServerShuttingDown();
}
