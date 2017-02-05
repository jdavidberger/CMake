//
// Created by justin on 2/4/17.
//

#include "cmConnection.h"
#include "cmServer.h"
#include "cmServerConnection.h"
#include "cm_uv.h"
#include "string.h"
#include <fstream>

struct write_req_t
{
  uv_write_t req;
  uv_buf_t buf;
};

void cmConnection::on_alloc_buffer(uv_handle_t* handle, size_t suggested_size,
                                   uv_buf_t* buf)
{
  (void)(handle);
  char* rawBuffer = new char[suggested_size];
  *buf = uv_buf_init(rawBuffer, static_cast<unsigned int>(suggested_size));
}

void cmConnection::on_read(uv_stream_t* stream, ssize_t nread,
                           const uv_buf_t* buf)
{
  auto conn = reinterpret_cast<cmConnection*>(stream->data);
  if (nread >= 0) {
    conn->ReadData(std::string(buf->base, buf->base + nread));
  } else {
    conn->OnDisconnect(nread);
  }

  delete[](buf->base);
}

void cmConnection::on_close_malloc(uv_handle_t* handle)
{
  free(handle);
}
void cmConnection::on_close(uv_handle_t* handle)
{
}

void cmConnection::on_write(uv_write_t* req, int status)
{
  (void)(status);
  auto conn = reinterpret_cast<cmConnection*>(req->data);

  // Free req and buffer
  write_req_t* wr = reinterpret_cast<write_req_t*>(req);
  delete[](wr->buf.base);
  delete wr;

  conn->ProcessNextRequest();
}

void cmConnection::on_new_connection(uv_stream_t* stream, int status)
{
  (void)(status);
  auto conn = reinterpret_cast<cmConnection*>(stream->data);
  conn->Connect(stream);
}

void cmConnection::on_signal(uv_signal_t* signal, int signum)
{
  auto conn = reinterpret_cast<cmConnection*>(signal->data);
  conn->OnSignal(signum);
}

void cmConnection::OnSignal(int signum)
{
  Server->OnSignal(signum);
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
  uv_write(reinterpret_cast<uv_write_t*>(req),
           static_cast<uv_stream_t*>(this->WriteStream), &req->buf, 1,
           on_write);
}

cmConnection::~cmConnection()
{
  OnServerShuttingDown();
}

void cmConnection::ReadData(const std::string& data)
{
  this->RawReadBuffer += data;
  if (BufferStrategy) {
    std::string packet = BufferStrategy->BufferMessage(this->RawReadBuffer);
    do {
      QueueRequest(packet);
      packet = BufferStrategy->BufferMessage(this->RawReadBuffer);
    } while (!packet.empty());

  } else {
    QueueRequest(this->RawReadBuffer);
    this->RawReadBuffer.clear();
  }
}

void cmConnection::PopOne()
{
  if (this->Queue.empty()) {
    return;
  }

  const std::string input = this->Queue.front();
  this->Queue.erase(this->Queue.begin());
  Server->ProcessRequest(this, input);
}

void cmConnection::ProcessNextRequest()
{
  PopOne();
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
  Server->OnConnected(nullptr);
}

void cmConnection::QueueRequest(const std::string& request)
{
  this->Queue.push_back(request);
  this->Server->NotifyDataQueued();
}

bool cmConnection::OnServeStart(std::string* pString)
{
  return true;
}

bool cmConnection::OnServerShuttingDown()
{
  return true;
}

void cmConnection::OnDisconnect(int onerror)
{
  this->Server->OnDisconnect(this);
}
