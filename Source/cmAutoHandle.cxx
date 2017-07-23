/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include <assert.h>

#include "cmAutoHandle.h"
#include "cm_uv.h"

template <typename T>
auto_handle_base_<T>::~auto_handle_base_()
{
  reset();
}

template <typename T>
void auto_handle_base_<T>::allocate(void* data)
{
  reset();
  handle = new T();
  handle->data = data;
}

template <typename T>
static void close_delete(uv_handle_t* h)
{
  delete reinterpret_cast<T*>(h);
}

template <typename T>
void auto_handle_base_<T>::reset()
{
  if (handle) {
    assert(!uv_is_closing(*this));
    if (!uv_is_closing(*this)) {
      uv_close(*this, &close_delete<T>);
    }
    handle = CM_NULLPTR;
  }
}

template <typename T>
auto_handle_base_<T>::operator uv_handle_t*()
{
  return reinterpret_cast<uv_handle_t*>(handle);
}

template <typename T>
auto_handle_<T>::operator T*()
{
  return this->handle;
}

template <typename T>
auto_handle_base_<T>::auto_handle_base_()
{
}

template <typename T>
void auto_handle_base_<T>::allocate()
{
  allocate(CM_NULLPTR);
}

void auto_async_t::send()
{
  std::lock_guard<std::mutex> lock(handleMutex);
  if (this->handle) {
    uv_async_send(*this);
  }
}

void auto_async_t::reset()
{
  std::lock_guard<std::mutex> lock(handleMutex);
  auto_handle_base_<uv_async_t>::reset();
}

int auto_async_t::init(uv_loop_t& loop, uv_async_cb async_cb, void* data)
{
  allocate(data);
  return uv_async_init(&loop, handle, async_cb);
}

auto_async_t::~auto_async_t()
{
  reset();
}

int auto_signal_t::init(uv_loop_t& loop, void* data)
{
  allocate(data);
  return uv_signal_init(&loop, handle);
}

int auto_signal_t::start(uv_signal_cb cb, int signum)
{
  assert(handle);
  return uv_signal_start(*this, cb, signum);
}

void auto_signal_t::reset()
{
  stop();
  auto_handle_::reset();
}

void auto_signal_t::stop()
{
  if (handle) {
    uv_signal_stop(*this);
  }
}

auto_tcp_t::operator uv_stream_t*()
{
  return reinterpret_cast<uv_stream_t*>(handle);
}

int auto_tcp_t::init(uv_loop_t& loop, void* data)
{
  allocate(data);
  return uv_tcp_init(&loop, handle);
}

int auto_pipe_t::init(uv_loop_t& loop, int ipc, void* data)
{
  allocate(data);
  return uv_pipe_init(&loop, *this, ipc);
}

auto_pipe_t::operator uv_stream_t*()
{
  return reinterpret_cast<uv_stream_t*>(handle);
}

#define GENERATE_EXPLICIT_INITIALIZATIONS(type, NAME)                         \
  template class auto_handle_base_<uv_##NAME##_t>;                            \
  template class auto_handle_<uv_##NAME##_t>;
UV_HANDLE_TYPE_MAP(GENERATE_EXPLICIT_INITIALIZATIONS);
#undef GENERATE_EXPLICIT_INITIALIZATIONS
