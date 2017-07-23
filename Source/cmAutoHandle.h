/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once
#include "cm_uv.h"

#include <cmConfigure.h>
#include <mutex>

/***
 * RAII class to simplify and insure the safe usage of uv_*_t types.
 *
 * @tparam T actual uv_*_t type represented.
 */
template <typename T>
class auto_handle_base_
{
  CM_DISABLE_COPY(auto_handle_base_);

protected:
  T* handle = CM_NULLPTR;

  /**
   * Allocate memory for the type and optionally set it's 'data' pointer.
   * Protected since
   * this should only be called for an appropriate 'init' call.
   *
   * @param data data pointer to set
   */
  virtual void allocate(void* data);
  void allocate();

public:
  auto_handle_base_();
  virtual ~auto_handle_base_();
  /**
   * Properly close the handle if needed and sets the inner handle to nullptr
   *
   * For completely safe operation, if you override reset you must provide a
   * dtor which calls reset.
   */
  virtual void reset();

  /**
   * Allow less verbose calling of uv_handle_* functions
   * @return reinterpreted handle
   */
  operator uv_handle_t*();
};

template <typename T>
class auto_handle_ : public auto_handle_base_<T>
{
public:
  /***
   * Allow less verbose calling of uv_<T> functions
   * @return reinterpreted handle
   */
  operator T*();
};

/***
 * This specialization is required to avoid duplicate 'operator uv_handle_t*()'
 * declarations
 */
template <>
class auto_handle_<uv_handle_t> : public auto_handle_base_<uv_handle_t>
{
};

class auto_async_t : public auto_handle_<uv_async_t>
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

struct auto_signal_t : public auto_handle_<uv_signal_t>
{
  virtual int init(uv_loop_t& loop, void* data = CM_NULLPTR);
  int start(uv_signal_cb cb, int signum);

  void reset() override;
  void stop();
};

struct auto_pipe_t : public auto_handle_<uv_pipe_t>
{
  operator uv_stream_t*();
  int init(uv_loop_t& loop, int ipc, void* data = CM_NULLPTR);
};

struct auto_tcp_t : public auto_handle_<uv_tcp_t>
{
  operator uv_stream_t*();
  virtual int init(uv_loop_t& loop, void* data = CM_NULLPTR);
};
