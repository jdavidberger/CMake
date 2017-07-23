/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmPipeConnection.h"

#include "cmConfigure.h"
#include "cmServer.h"

cmPipeConnection::cmPipeConnection(const std::string& name,
                                   cmConnectionBufferStrategy* bufferStrategy)
  : cmEventBasedConnection(bufferStrategy)
  , PipeName(name)
{
}

void cmPipeConnection::Connect(uv_stream_t* server)
{
  if ((uv_pipe_t*)this->ClientPipe) {
    // Accept and close all pipes but the first:
    auto_pipe_t rejectPipe;

    rejectPipe.init(*this->Server->GetLoop(), 0);
    uv_accept(server, rejectPipe);

    return;
  }

  this->ClientPipe.init(*this->Server->GetLoop(), 0,
                        static_cast<cmEventBasedConnection*>(this));

  if (uv_accept(server, this->ClientPipe) != 0) {
    this->ClientPipe.reset();
    return;
  }
  this->ReadStream = this->ClientPipe;
  this->WriteStream = this->ClientPipe;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
  Server->OnConnected(this);
}

bool cmPipeConnection::OnServeStart(std::string* errorMessage)
{
  this->ServerPipe.init(*this->Server->GetLoop(), 0,
                        static_cast<cmEventBasedConnection*>(this));

  int r;
  if ((r = uv_pipe_bind(this->ServerPipe, this->PipeName.c_str())) != 0) {
    *errorMessage = std::string("Internal Error with ") + this->PipeName +
      ": " + uv_err_name(r);
    return false;
  }

  if ((r = uv_listen(this->ServerPipe, 1, on_new_connection)) != 0) {
    *errorMessage = std::string("Internal Error listening on ") +
      this->PipeName + ": " + uv_err_name(r);
    return false;
  }

  return cmConnection::OnServeStart(errorMessage);
}

bool cmPipeConnection::OnConnectionShuttingDown()
{
  this->ClientPipe.reset();
  this->ServerPipe.reset();

  return cmConnection::OnConnectionShuttingDown();
}
