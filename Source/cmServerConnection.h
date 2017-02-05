/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include <string>
#include <vector>

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cmPipeConnection.h"
#include "cm_uv.h"

#endif

class cmServer;
class cmServerBase;
class cmFileMonitor;
class LoopGuard;

class cmServerBufferStrategy : public cmConnectionBufferStrategy
{
public:
  std::string BufferMessage(std::string& rawBuffer) override;

private:
  std::string RequestBuffer;
};

class cmStdIoConnection : public virtual cmConnection
{
public:
  cmStdIoConnection(cmConnectionBufferStrategy* bufferStrategy = 0);

  void SetServer(cmServerBase* s) override;

  bool OnServeStop(std::string* pString) override;

  bool OnServeStart(std::string* pString) override;

private:
  typedef union
  {
    uv_tty_t tty;
    uv_pipe_t pipe;
  } InOutUnion;

  bool usesTty = false;

  InOutUnion Input;
  InOutUnion Output;
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
