//
// Created by justin on 2/4/17.
//

#pragma once

#include "cm_uv.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

class cmServerBase;

/***
 * Given a sequence of bytes with any kind of buffering, instances of this
 * class arrange logical chunks according
 * to whatever the use case is for the connection. Examples include waiting to
 * grab the whole line before sending,
 * to waiting for an entire object like XML or JSON.
 */
class cmConnectionBufferStrategy
{
public:
  virtual ~cmConnectionBufferStrategy();

  /***
   * Called whenever with an active raw buffer. If a logical chunk
   * becomes available, that chunk is returned and that portion is
   * removed from the rawBuffer
   * @param rawBuffer in/out parameter. Receive buffer; the buffer strategy is
   * free to manipulate this buffer
   * anyway it needs to.
   * @return Next chunk from the stream. Returns the empty string if a chunk
   * isn't ready yet. Users of this
   * interface should repeatedly call this function until an empty string is
   * returned since its entirely possible
   * multiple chunks come in a raw buffer.
   */
  virtual std::string BufferMessage(std::string& rawBuffer) = 0;

  /***
   * Resets the internal state of the buffering
   */
  virtual void clear();

  // TODO: There should be a callback / flag set for errors
};

/***
 * Abstraction of a connection; ties in event callbacks from libuv and notifies
 * the server when appropriate
 */
class cmConnection
{
public:
  virtual ~cmConnection();

  /***
   * @param bufferStrategy If no strategy is given, it will process the raw
   * chunks as they come in. The connection
   * owns the pointer given.
   */
  cmConnection(cmConnectionBufferStrategy* bufferStrategy = 0);

  virtual void Connect(uv_stream_t* server);

  virtual void ReadData(const std::string& data);

  virtual void OnSignal(int signum);

  virtual bool OnServeStart(std::string* pString);

  virtual bool OnServerShuttingDown();

  virtual bool IsOpen() const;

  virtual void WriteData(const std::string& data);

  virtual void QueueRequest(const std::string& request);

  virtual void ProcessNextRequest();

  virtual void SetServer(cmServerBase* s);

  virtual void OnDisconnect(int errorCode);
  uv_stream_t* ReadStream = nullptr;
  cmServerBase* Server = 0;
  uv_stream_t* WriteStream = nullptr;

  static void on_close(uv_handle_t* handle);

protected:
  std::deque<std::string> Queue;

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
