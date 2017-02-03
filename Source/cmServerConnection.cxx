/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmServerConnection.h"

#include "cmServerDictionary.h"

#include "cmFileMonitor.h"
#include "cmServer.h"

#include <assert.h>
#include <string.h>

namespace {

struct write_req_t
{
  uv_write_t req;
  uv_buf_t buf;
};

void on_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  (void)(handle);
  char* rawBuffer = new char[suggested_size];
  *buf = uv_buf_init(rawBuffer, static_cast<unsigned int>(suggested_size));
}

void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  auto conn = reinterpret_cast<cmConnection*>(stream->data);
  if (nread >= 0) {
    conn->ReadData(std::string(buf->base, buf->base + nread));
  } else {
    conn->TriggerShutdown();
  }

  delete[](buf->base);
}

void on_write(uv_write_t* req, int status)
{
  (void)(status);
  auto conn = reinterpret_cast<cmConnection*>(req->data);

  // Free req and buffer
  write_req_t* wr = reinterpret_cast<write_req_t*>(req);
  delete[](wr->buf.base);
  delete wr;

  conn->ProcessNextRequest();
}

void on_new_connection(uv_stream_t* stream, int status)
{
  (void)(status);
  auto conn = reinterpret_cast<cmConnection*>(stream->data);
  conn->Connect(stream);
}

void on_signal(uv_signal_t* signal, int signum)
{
  auto conn = reinterpret_cast<cmConnection*>(signal->data);
  conn->OnSignal(signum);
}

void on_signal_close(uv_handle_t* handle)
{
  delete reinterpret_cast<uv_signal_t*>(handle);
}

void on_pipe_close(uv_handle_t* handle)
{
  delete reinterpret_cast<uv_pipe_t*>(handle);
}

void on_tty_close(uv_handle_t* handle)
{
  delete reinterpret_cast<uv_tty_t*>(handle);
}

} // namespace

cmStdIoConnection::cmStdIoConnection(
  cmConnectionBufferStrategy* bufferStrategy)
  : cmConnection(bufferStrategy)
{
  this->Input.tty = nullptr;
  this->Output.tty = nullptr;
}

bool cmStdIoConnection::DoSetup(std::string* errorMessage)
{
  (void)(errorMessage);

  if (uv_guess_handle(1) == UV_TTY) {
    usesTty = true;
    this->Input.tty = new uv_tty_t;
    uv_tty_init(this->Loop().get(), this->Input.tty, 0, 1);
    uv_tty_set_mode(this->Input.tty, UV_TTY_MODE_NORMAL);
    Input.tty->data = static_cast<cmConnection*>(this);
    this->ReadStream = reinterpret_cast<uv_stream_t*>(this->Input.tty);

    this->Output.tty = new uv_tty_t;
    uv_tty_init(this->Loop().get(), this->Output.tty, 1, 0);
    uv_tty_set_mode(this->Output.tty, UV_TTY_MODE_NORMAL);
    Output.tty->data = static_cast<cmConnection*>(this);
    this->WriteStream = reinterpret_cast<uv_stream_t*>(this->Output.tty);
  } else {
    usesTty = false;
    this->Input.pipe = new uv_pipe_t;
    uv_pipe_init(this->Loop().get(), this->Input.pipe, 0);
    uv_pipe_open(this->Input.pipe, 0);
    Input.pipe->data = static_cast<cmConnection*>(this);
    this->ReadStream = reinterpret_cast<uv_stream_t*>(this->Input.pipe);

    this->Output.pipe = new uv_pipe_t;
    uv_pipe_init(this->Loop().get(), this->Output.pipe, 0);
    uv_pipe_open(this->Output.pipe, 1);
    Output.pipe->data = static_cast<cmConnection*>(this);
    this->WriteStream = reinterpret_cast<uv_stream_t*>(this->Output.pipe);
  }
  Server->OnNewConnection();
  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);

  return true;
}

void cmStdIoConnection::TearDown()
{
  if (usesTty) {
    uv_read_stop(reinterpret_cast<uv_stream_t*>(this->Input.tty));
    uv_close(reinterpret_cast<uv_handle_t*>(this->Input.tty), &on_tty_close);
    uv_close(reinterpret_cast<uv_handle_t*>(this->Output.tty), &on_tty_close);
    this->Input.tty = nullptr;
    this->Output.tty = nullptr;
  } else {
    uv_close(reinterpret_cast<uv_handle_t*>(this->Input.pipe), &on_pipe_close);
    uv_close(reinterpret_cast<uv_handle_t*>(this->Output.pipe),
             &on_pipe_close);
    this->Input.pipe = nullptr;
    this->Input.pipe = nullptr;
  }
  this->ReadStream = nullptr;
  this->WriteStream = nullptr;
}

cmPipeConnection::cmPipeConnection(const std::string& name,
                                   cmConnectionBufferStrategy* bufferStrategy)
  : cmConnection(bufferStrategy)
  , PipeName(name)
{
}

bool cmPipeConnection::DoSetup(std::string* errorMessage)
{
  this->ServerPipe = new uv_pipe_t;
  uv_pipe_init(this->Loop().get(), this->ServerPipe, 0);
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

  return true;
}

void cmPipeConnection::TearDown()
{
  if (this->ClientPipe) {
    uv_close(reinterpret_cast<uv_handle_t*>(this->ClientPipe), &on_pipe_close);
    this->WriteStream->data = nullptr;
  }
  uv_close(reinterpret_cast<uv_handle_t*>(this->ServerPipe), &on_pipe_close);

  this->ClientPipe = nullptr;
  this->ServerPipe = nullptr;
  this->WriteStream = nullptr;
  this->ReadStream = nullptr;
}

void cmPipeConnection::Connect(uv_stream_t* server)
{
  if (this->ClientPipe) {
    // Accept and close all pipes but the first:
    uv_pipe_t* rejectPipe = new uv_pipe_t;

    uv_pipe_init(this->Loop().get(), rejectPipe, 0);
    auto rejecter = reinterpret_cast<uv_stream_t*>(rejectPipe);
    uv_accept(server, rejecter);
    uv_close(reinterpret_cast<uv_handle_t*>(rejecter), &on_pipe_close);
    return;
  }

  this->ClientPipe = new uv_pipe_t;
  uv_pipe_init(this->Loop().get(), this->ClientPipe, 0);
  this->ClientPipe->data = static_cast<cmConnection*>(this);
  auto client = reinterpret_cast<uv_stream_t*>(this->ClientPipe);
  if (uv_accept(server, client) != 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
    return;
  }
  this->ReadStream = client;
  this->WriteStream = client;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
}

bool cmConnection::ProcessEvents(std::string* errorMessage)
{
  assert(Server);
  auto keepLoopAlive = Loop();
  if (!keepLoopAlive) {
    keepLoopAlive =
      std::shared_ptr<uv_loop_t>(uv_default_loop(), &uv_loop_close);
    mLoop = keepLoopAlive;
  }
  errorMessage->clear();

  this->RawReadBuffer.clear();
  if (BufferStrategy)
    BufferStrategy->clear();

  if (!keepLoopAlive) {
    *errorMessage = "Internal Error: Failed to create event loop.";
    return false;
  }

  this->SIGINTHandler = new uv_signal_t;
  uv_signal_init(keepLoopAlive.get(), this->SIGINTHandler);
  this->SIGINTHandler->data = static_cast<void*>(this);
  uv_signal_start(this->SIGINTHandler, &on_signal, SIGINT);

  this->SIGHUPHandler = new uv_signal_t;
  uv_signal_init(keepLoopAlive.get(), this->SIGHUPHandler);
  this->SIGHUPHandler->data = static_cast<void*>(this);
  uv_signal_start(this->SIGHUPHandler, &on_signal, SIGHUP);

  if (!DoSetup(errorMessage)) {
    return false;
  }
  Server->OnEventsStart(keepLoopAlive.get());
  if (uv_run(keepLoopAlive.get(), UV_RUN_DEFAULT) != 0) {
    *errorMessage = "Internal Error: Event loop stopped in unclean state.";
    Server->OnEventsStop(errorMessage);
    return false;
  }
  Server->OnEventsStop(errorMessage);

  // These need to be cleaned up by now:
  assert(!this->ReadStream);
  assert(!this->WriteStream);

  this->RawReadBuffer.clear();
  if (BufferStrategy)
    BufferStrategy->clear();

  return true;
}

void cmConnection::TriggerShutdown()
{
  if (auto l = this->Loop())
    uv_stop(l.get());
  uv_signal_stop(this->SIGINTHandler);
  uv_signal_stop(this->SIGHUPHandler);

  uv_close(reinterpret_cast<uv_handle_t*>(this->SIGINTHandler),
           &on_signal_close); // delete handle
  uv_close(reinterpret_cast<uv_handle_t*>(this->SIGHUPHandler),
           &on_signal_close); // delete handle

  this->SIGINTHandler = nullptr;
  this->SIGHUPHandler = nullptr;
  if (Server)
    Server->OnDisconnect();
  this->TearDown();
}

void cmConnection::OnSignal(int signum)
{
  if (!Server->OnSignal(signum)) {
    TriggerShutdown();
  }
}

bool cmConnection::IsOpen() const
{
  return this->WriteStream != 0;
}

void cmConnection::WriteData(const std::string& data)
{
  assert(this->WriteStream);

  auto ds = data.size();

  write_req_t* req = new write_req_t;
  req->req.data = this;
  req->buf = uv_buf_init(new char[ds], static_cast<unsigned int>(ds));
  memcpy(req->buf.base, data.c_str(), ds);
  printf("Out: %s\n", data.c_str());

  uv_write(reinterpret_cast<uv_write_t*>(req),
           static_cast<uv_stream_t*>(this->WriteStream),
           &req->buf,
           1,
           on_write);
}

cmConnection::~cmConnection()
{
}

void cmConnection::ReadData(const std::string& data)
{
  this->RawReadBuffer += data;
  if (BufferStrategy) {
    std::string packet = BufferStrategy->BufferMessage(this->RawReadBuffer);
    do {
      Server->QueueRequest(packet);
      packet = BufferStrategy->BufferMessage(this->RawReadBuffer);
    } while (!packet.empty());

  } else {
    Server->QueueRequest(this->RawReadBuffer);
    this->RawReadBuffer.clear();
  }
}

void cmConnection::ProcessNextRequest()
{
  if (Server)
    Server->PopOne();
}

void cmConnection::SetServer(cmServerBase* s)
{
  Server = s;
}

cmConnection::cmConnection(cmConnectionBufferStrategy* bufferStrategy)
  : BufferStrategy(bufferStrategy)
{
}

void cmConnection::Connect(uv_stream_t* server)
{
  Server->OnNewConnection();
}

cmServerPipeConnection::cmServerPipeConnection(const std::string& name)
  : cmPipeConnection(name, new cmServerBufferStrategy)
{
}

void cmTcpIpConnection::Connect(uv_stream_t* server)
{
  if (this->ClientHandle) {
    // Ignore it; we already hae a connection
    return;
  }

  this->ClientHandle = new uv_tcp_t;
  uv_tcp_init(this->Loop().get(), this->ClientHandle);
  this->ClientHandle->data = static_cast<cmConnection*>(this);
  auto client = reinterpret_cast<uv_stream_t*>(this->ClientHandle);
  if (uv_accept(server, client) != 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
    return;
  }
  this->ReadStream = client;
  this->WriteStream = client;

  uv_read_start(this->ReadStream, on_alloc_buffer, on_read);
  this->Server->OnNewConnection();
}

void cmTcpIpConnection::TearDown()
{
  if (this->ClientHandle) {
    uv_close(reinterpret_cast<uv_handle_t*>(this->ClientHandle),
             &on_pipe_close);
    this->WriteStream->data = nullptr;
  }
  uv_close(reinterpret_cast<uv_handle_t*>(&this->ServerHandle),
           &on_pipe_close);

  this->ClientHandle = nullptr;
  this->WriteStream = nullptr;
  this->ReadStream = nullptr;
}

bool cmTcpIpConnection::DoSetup(std::string* errorMessage)
{
  uv_tcp_init(this->Loop().get(), &this->ServerHandle);
  this->ServerHandle.data = static_cast<cmConnection*>(this);

  struct sockaddr_in recv_addr;
  uv_ip4_addr("0.0.0.0", Port, &recv_addr);

  int r;
  if ((r = uv_tcp_bind(&this->ServerHandle, (const sockaddr*)&recv_addr, 0)) !=
      0) {
    *errorMessage = std::string("Internal Error trying to bind to port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }
  auto serverStream = reinterpret_cast<uv_stream_t*>(&this->ServerHandle);
  if ((r = uv_listen(serverStream, 1, on_new_connection)) != 0) {
    *errorMessage = std::string("Internal Error listening on port ") +
      std::to_string(Port) + ": " + uv_err_name(r);
    return false;
  }

  return true;
}

cmTcpIpConnection::cmTcpIpConnection(int Port)
  : Port(Port)
{
}

cmTcpIpConnection::cmTcpIpConnection(
  int Port,
  cmConnectionBufferStrategy* bufferStrategy)
  : cmConnection(bufferStrategy)
  , Port(Port)
{
}

cmServerStdIoConnection::cmServerStdIoConnection()
  : cmStdIoConnection(new cmServerBufferStrategy)
{
}

cmConnectionBufferStrategy::~cmConnectionBufferStrategy()
{
}

void cmConnectionBufferStrategy::clear()
{
}

std::string cmServerBufferStrategy::BufferMessage(std::string& RawReadBuffer)
{
  for (;;) {
    auto needle = RawReadBuffer.find('\n');

    if (needle == std::string::npos) {
      return "";
    }
    std::string line = RawReadBuffer.substr(0, needle);
    const auto ls = line.size();
    if (ls > 1 && line.at(ls - 1) == '\r') {
      line.erase(ls - 1, 1);
    }
    RawReadBuffer.erase(RawReadBuffer.begin(),
                        RawReadBuffer.begin() + static_cast<long>(needle) + 1);
    if (line == kSTART_MAGIC) {
      RequestBuffer.clear();
      continue;
    }
    if (line == kEND_MAGIC) {
      std::string rtn;
      rtn.swap(this->RequestBuffer);
      return rtn;
    } else {
      this->RequestBuffer += line;
      this->RequestBuffer += "\n";
    }
  }
}
