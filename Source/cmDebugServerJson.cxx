#include "cmDebugServerJson.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

#include <sstream>

class cmJsonBufferStrategy : public cmConnectionBufferStrategy
{
  size_t bracesDepth = 0;
  std::string readBuffer = "";

  virtual std::string BufferMessage(std::string& rawBuffer) override
  {
    for (size_t i = 0; i < rawBuffer.size(); i++) {
      auto c = rawBuffer[i];
      if (c == '{')
        bracesDepth++;
      if (c == '}')
        bracesDepth--;
      readBuffer += c;
      if (bracesDepth == 0) {
        std::string rtn;
        rtn.swap(readBuffer);
        rawBuffer.erase(0, i + 1);
        return rtn;
      }
    }

    rawBuffer.clear();
    return std::string();
  }
};

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger, cmConnection* conn)
  : cmDebugServer(debugger, conn)
{
}

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger, size_t port)
  : cmDebugServerJson(debugger,
                      new cmTcpIpConnection(port, new cmJsonBufferStrategy()))
{
}

void cmDebugServerJson::ProcessRequest(const std::string& jsonRequest)
{
  Json::Reader reader;
  Json::Value value;
  if (!reader.parse(jsonRequest, value)) {
    return;
  }

  auto request = value["Command"].asString();

  if (request == "Continue") {
    Debugger.Continue();
  } else if (request == "Break") {
    Debugger.Break();
  } else if (request == "Step") {
    Debugger.Step();
  } else if (request.find("Evaluate") == 0) {
    const char* v = Debugger.GetMakefile()->ExpandVariablesInString(
      value["Request"].asString());
    value.removeMember("Command");
    if (v)
      value["Response"] = std::string(v);
    else
      value["Response"] = false;
    Connection->WriteData(value.toStyledString());
  } else if (request.find("RequestBreakpointInfo") == 0) {

  } else if (request.find("ClearBreakpoints") == 0) {

  } else if (request.find("AddBreakpoint") == 0) {
    Debugger.SetBreakpoint(value["File"].asString(), value["Line"].asInt());
  }
}

void cmDebugServerJson::SendStateUpdate()
{
  auto currentLine = Debugger.CurrentLine();

  std::string state = "";
  Json::Value value;
  switch (Debugger.CurrentState()) {
    case cmDebugger::State::Running:
      value["State"] = "Running";
      break;
    case cmDebugger::State::Paused: {
      value["State"] = "Paused";
      Json::Value back(Json::arrayValue);

      auto backtrace = Debugger.GetBacktrace();
      while (!backtrace.Top().FilePath.empty()) {
        Json::Value frame(Json::objectValue);
        frame["File"] = backtrace.Top().FilePath;
        frame["Line"] = backtrace.Top().Line;
        frame["Name"] = backtrace.Top().Name;
        back.append(frame);
        backtrace = backtrace.Pop();
      }
      value["Backtrace"] = back;
    } break;
    case cmDebugger::State::Unknown:
      value["State"] = "Unknown";
      break;
  }

  value["File"] = currentLine.FilePath;
  value["Line"] = currentLine.Line;
  if (Connection && Connection->IsOpen())
    Connection->WriteData(value.toStyledString());
}

void cmDebugServerJson::OnChangeState()
{
  cmDebuggerListener::OnChangeState();
  SendStateUpdate();
}

void cmDebugServerJson::OnNewConnection()
{
  SendStateUpdate();
}
