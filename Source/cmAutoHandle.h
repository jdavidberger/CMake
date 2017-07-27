/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once
#include "cm_uv.h"

#include <cmConfigure.h>
#include <mutex>

template <typename T>
class auto_handle_base_
{
  CM_DISABLE_COPY(auto_handle_base_);

protected:
  T* handle = CM_NULLPTR;
  virtual void allocate(void* data = CM_NULLPTR);

public:
  auto_handle_base_();
  virtual ~auto_handle_base_();
  virtual void reset();
  operator uv_handle_t*();
  operator T*();
  T* operator->();
};

template <>
class auto_handle_base_<uv_handle_t>
{
};

class auto_async_t : public auto_handle_base_<uv_async_t>
{
  /***
   * Wile uv_async_send is itself thread-safe, there are
   * no strong guarantees that close hasn't already been
   * called on the handle; and that it might be deleted
   * as the send call goes through. This mutex guards
   * against that.
   */
  std::mutex handleMutex;

public:
  int init(uv_loop_t& loop, uv_async_cb async_cb, void* data = CM_NULLPTR);
  ~auto_async_t() override;

  void send();

  void reset() override;
};

struct auto_signal_t : public auto_handle_base_<uv_signal_t>
{
  virtual int init(uv_loop_t& loop, void* data = CM_NULLPTR);
  int start(uv_signal_cb cb, int signum);

  void reset() override;
  void stop();
};

struct auto_pipe_t : public auto_handle_base_<uv_pipe_t>
{
  operator uv_stream_t*();
  int init(uv_loop_t& loop, int ipc, void* data = CM_NULLPTR);
};
