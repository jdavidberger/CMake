/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmListFileCache.h"
#include "cmState.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cmDebugger.h"
#include "cmServerConnection.h"
#include "cm_jsoncpp_value.h"
#include "cm_uv.h"

#endif

#include <string>
#include <thread>
#include <vector>

class cmFileMonitor;
class cmConnection;
class cmServerProtocol;
class cmServerRequest;
class cmServerResponse;

class cmServerBase
{
public:
  cmServerBase(cmConnection* connection);
  virtual ~cmServerBase();
  virtual void PopOne();
  virtual void ProcessRequest(const std::string& request) = 0;
  virtual void QueueRequest(const std::string& request);
  virtual void OnNewConnection();
  virtual void OnDisconnect();

  virtual bool StartServeThread();

  virtual bool Serve(std::string* errorMessage);

  virtual void OnEventsStart(uv_loop_t* loop);
  virtual void OnEventsStop(std::string* pString);
  virtual bool OnSignal(int signum);

protected:
  virtual cmConnection* GetConnection();
  std::unique_ptr<cmConnection> Connection;
  std::vector<std::string> Queue;

  std::thread ServeThread;

  uv_loop_t* Loop = nullptr;

  typedef union
  {
    uv_tty_t tty;
    uv_pipe_t pipe;
  } InOutUnion;

  InOutUnion Input;
  InOutUnion Output;
  uv_stream_t* InputStream = nullptr;
  uv_stream_t* OutputStream = nullptr;

  /** Note -- We run the same callback in prepare and check to properly handle
                  deferred call
  */
  uv_check_t cbHandle;
  uv_prepare_t cbPrepareHandle;
};

class cmServer : public cmServerBase
{
public:
  class DebugInfo;

  cmServer(cmConnection* conn, bool supportExperimental);
  ~cmServer();

  virtual bool Serve(std::string* errorMessage) override;

  cmFileMonitor* FileMonitor() const;

private:
  void RegisterProtocol(cmServerProtocol* protocol);

  // Callbacks from cmServerConnection:

  virtual void ProcessRequest(const std::string& request);
  virtual void QueueRequest(const std::string& request) override;
  std::shared_ptr<cmFileMonitor> fileMonitor;

public:
  void OnEventsStart(uv_loop_t* loop) override;

  void OnEventsStop(std::string* pString) override;

public:
  void OnNewConnection() override;

private:
  static void reportProgress(const char* msg, float progress, void* data);
  static void reportMessage(const char* msg, const char* title, bool& cancel,
                            void* data);

  // Handle requests:
  cmServerResponse SetProtocolVersion(const cmServerRequest& request);

  void PrintHello() const;

  // Write responses:
  void WriteProgress(const cmServerRequest& request, int min, int current,
                     int max, const std::string& message) const;
  void WriteMessage(const cmServerRequest& request, const std::string& message,
                    const std::string& title) const;
  void WriteResponse(const cmServerResponse& response,
                     const DebugInfo* debug) const;
  void WriteParseError(const std::string& message) const;
  void WriteSignal(const std::string& name, const Json::Value& obj) const;

  void WriteJsonObject(Json::Value const& jsonValue,
                       const DebugInfo* debug) const;

  static cmServerProtocol* FindMatchingProtocol(
    const std::vector<cmServerProtocol*>& protocols, int major, int minor);

  const bool SupportExperimental;

  cmServerProtocol* Protocol = nullptr;
  std::vector<cmServerProtocol*> SupportedProtocols;

  std::string DataBuffer;
  std::string JsonData;

  uv_loop_t* Loop = nullptr;

  typedef union
  {
    uv_tty_t tty;
    uv_pipe_t pipe;
  } InOutUnion;

  InOutUnion Input;
  InOutUnion Output;
  uv_stream_t* InputStream = nullptr;
  uv_stream_t* OutputStream = nullptr;

  mutable bool Writing = false;

  friend class cmServerProtocol;
  friend class cmServerRequest;
};
