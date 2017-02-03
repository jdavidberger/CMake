//
// Created by justin on 2/3/17.
//

#pragma once

#include "cmDebugger.h"
#include "cmServer.h"

class cmDebugServer : public cmServerBase, public cmDebuggerListener
{
public:
  cmDebugServer(cmDebugger& debugger, cmConnection* conn);
  virtual bool OnSignal(int signum) override;
};
