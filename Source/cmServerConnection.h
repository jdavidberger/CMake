/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include <string>
#include <vector>

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_uv.h"
#endif
#include <memory>

class cmServer;
class cmServerBase;
class cmFileMonitor;
class LoopGuard;

class cmConnectionBufferStrategy
{
public:
  virtual ~cmConnectionBufferStrategy();
  virtual std::string BufferMessage(std::string& rawBuffer) = 0;
  virtual void clear();
};

class cmServerBufferStrategy : public cmConnectionBufferStrategy
{
public:
  std::string BufferMessage(std::string& rawBuffer) override;

private:
  std::string RequestBuffer;
};

class cmConnection
{
  uv_signal_t* SIGINTHandler = nullptr;
  uv_signal_t* SIGHUPHandler = nullptr;

public:
  virtual ~cmConnection();
  cmConnection(cmConnectionBufferStrategy* bufferStrategy = 0);
  virtual void Connect(uv_stream_t* server);

  virtual bool ProcessEvents(std::string* errorMessage);

  virtual void ReadData(const std::string& data);

  virtual void TriggerShutdown();
  virtual void OnSignal(int signum);

  virtual void WriteData(const std::string& data);

  virtual void ProcessNextRequest();
  virtual void SetServer(cmServerBase* s);

protected:
  virtual bool DoSetup(std::string* errorMessage) = 0;

  virtual void TearDown() = 0;

  std::shared_ptr<uv_loop_t> Loop() const { return mLoop.lock(); }
  std::weak_ptr<uv_loop_t> mLoop;
  std::string RawReadBuffer;

  uv_stream_t* ReadStream = nullptr;
  uv_stream_t* WriteStream = nullptr;
  std::unique_ptr<cmConnectionBufferStrategy> BufferStrategy;
  cmServerBase* Server = 0;
};

class cmStdIoConnection : public virtual cmConnection
{
public:
  cmStdIoConnection(cmConnectionBufferStrategy* bufferStrategy = 0);
  bool DoSetup(std::string* errorMessage) override;

  void TearDown() override;

private:
  typedef union
  {
    uv_tty_t* tty;
    uv_pipe_t* pipe;
  } InOutUnion;

  bool usesTty = false;

  InOutUnion Input;
  InOutUnion Output;
};

class cmTcpIpConnection : public virtual cmConnection
{
public:
  cmTcpIpConnection(int Port);
  bool DoSetup(std::string* errorMessage) override;

  void TearDown() override;

  void Connect(uv_stream_t* server) override;

private:
  int Port;
  uv_tcp_t ServerHandle;
  uv_tcp_t* ClientHandle = 0;
};

class cmPipeConnection : public virtual cmConnection
{
public:
  cmPipeConnection(const std::string& name,
                   cmConnectionBufferStrategy* bufferStrategy = 0);
  bool DoSetup(std::string* errorMessage) override;

  void TearDown() override;

  void Connect(uv_stream_t* server) override;

private:
  const std::string PipeName;
  uv_pipe_t* ServerPipe = nullptr;
  uv_pipe_t* ClientPipe = nullptr;
};

class cmServerStdIoConnection : public cmStdIoConnection
{
public:
  cmServerStdIoConnection();
};

class cmServerPipeConnection : public cmPipeConnection
{
public:
  cmServerPipeConnection(const std::string& name);
};
