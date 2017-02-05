//
// Created by justin on 2/4/17.
//

#pragma once

#include "cmConnection.h"
#include "cm_uv.h"
#include <memory>
#include <string>
#include <vector>

class cmPipeConnection : public virtual cmConnection
{
public:
  cmPipeConnection(const std::string& name,
                   cmConnectionBufferStrategy* bufferStrategy = 0);
  bool DoSetup(cmServerBase* s, std::string* errorMessage);
  void TearDown();

  void Connect(uv_stream_t* server) override;

private:
  const std::string PipeName;
  uv_pipe_t* ServerPipe = nullptr;
  uv_pipe_t* ClientPipe = nullptr;
};
