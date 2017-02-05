/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmListFileCache.h"
#include "cmState.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cmConnection.h"
#include "cmDebugger.h"
#include "cmServerConnection.h"
#include "cm_jsoncpp_value.h"
#include "cm_uv.h"

#endif

#include <thread>

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

  virtual void AddNewConnection(cmConnection* ownedConnection);
  virtual void ProcessOne();
  virtual void ProcessRequest(cmConnection* connection,
                              const std::string& request) = 0;
  virtual void OnConnected(cmConnection* connection);
  virtual void OnDisconnect();

  virtual bool StartServeThread();

  virtual bool Serve(std::string* errorMessage);

  virtual void OnServeStart();
  virtual void OnServeStop(std::string* pString);
  virtual bool OnSignal(int signum);
  uv_loop_t* GetLoop();

protected:
  std::vector<std::unique_ptr<cmConnection> > Connections;

  std::thread ServeThread;

  uv_loop_t Loop;
  uv_async_t WakeupLoop;
  uv_signal_t SIGINTHandler;
  uv_signal_t SIGHUPHandler;

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

  virtual void ProcessRequest(cmConnection* connection,
                              const std::string& request) override;
  std::shared_ptr<cmFileMonitor> fileMonitor;

public:
  void OnServeStart() override;

  void OnServeStop(std::string* pString) override;

public:
  void OnConnected(cmConnection* connection) override;

private:
  static void reportProgress(const char* msg, float progress, void* data);
  static void reportMessage(const char* msg, const char* title, bool& cancel,
                            void* data);

  // Handle requests:
  cmServerResponse SetProtocolVersion(const cmServerRequest& request);

  void PrintHello(cmConnection* connection) const;

  // Write responses:
  void WriteProgress(const cmServerRequest& request, int min, int current,
                     int max, const std::string& message) const;
  void WriteMessage(const cmServerRequest& request, const std::string& message,
                    const std::string& title) const;
  void WriteResponse(cmConnection* connection,
                     const cmServerResponse& response,
                     const DebugInfo* debug) const;
  void WriteParseError(cmConnection* connection,
                       const std::string& message) const;
  void WriteSignal(cmConnection* connection, const std::string& name,
                   const Json::Value& obj) const;

  void WriteJsonObject(cmConnection* connection, Json::Value const& jsonValue,
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
