/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#pragma once

#include "cmConnection.h"

#include "cm_uv.h"

#include "cmAutoHandle.h"
#include <string>

class cmPipeConnection : public cmEventBasedConnection
{
public:
  cmPipeConnection(const std::string& name,
                   cmConnectionBufferStrategy* bufferStrategy = nullptr);

  bool OnServeStart(std::string* pString) override;

  bool OnConnectionShuttingDown() override;

  void Connect(uv_stream_t* server) override;

private:
  const std::string PipeName;
  auto_pipe_t ServerPipe;
  auto_pipe_t ClientPipe;
};
