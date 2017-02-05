//
// Created by justin on 2/4/17.
//

#pragma once

#include "cm_uv.h"
#include <memory>
#include <string>
#include <vector>

class cmServerBase;

namespace uv_utils {
}

class cmConnectionBufferStrategy
{
public:
  virtual ~cmConnectionBufferStrategy();

  virtual std::string BufferMessage(std::string& rawBuffer) = 0;

  virtual void clear();
};

class cmConnection
{
public:
  virtual ~cmConnection();

  cmConnection(cmConnectionBufferStrategy* bufferStrategy = 0);

  virtual void Connect(uv_stream_t* server);

  virtual void ReadData(const std::string& data);

  virtual void OnSignal(int signum);

  virtual bool OnServeStart(std::string* pString);

  virtual bool OnServerShuttingDown();

  virtual bool IsOpen() const;

  virtual void WriteData(const std::string& data);

  virtual void QueueRequest(const std::string& request);

  virtual void PopOne();

  virtual void ProcessNextRequest();

  virtual void SetServer(cmServerBase* s);

  virtual void OnDisconnect(int errorCode);
  uv_stream_t* ReadStream = nullptr;
  cmServerBase* Server = 0;
  uv_stream_t* WriteStream = nullptr;

  static void on_close(uv_handle_t* handle);

protected:
  std::vector<std::string> Queue;

  std::string RawReadBuffer;

  std::unique_ptr<cmConnectionBufferStrategy> BufferStrategy;

  static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

  static void on_write(uv_write_t* req, int status);

  static void on_new_connection(uv_stream_t* stream, int status);

  static void on_signal(uv_signal_t* signal, int signum);

  static void on_close_malloc(uv_handle_t* handle);

  static void on_alloc_buffer(uv_handle_t* handle, size_t suggested_size,
                              uv_buf_t* buf);
};
