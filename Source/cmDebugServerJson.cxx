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
  printf("In: %s\n", jsonRequest.c_str());
  auto request = value["Command"].asString();

  if (request == "Continue") {
    Debugger.Continue();
  } else if (request == "Break") {
    Debugger.Break();
    SendStateUpdate();
  } else if (request == "StepIn") {
    Debugger.StepIn();
  } else if (request == "StepOut") {
    Debugger.StepOut();
  } else if (request == "StepOver") {
    Debugger.Step();
  } else if (request.find("Evaluate") == 0) {
    auto requestVal = value["Request"].asString();
    const char* v = 0;
    if (!requestVal.empty() && requestVal[0] == '"' &&
        requestVal.back() == '"')
      v = Debugger.GetMakefile()->ExpandVariablesInString(requestVal);
    else
      v = Debugger.GetMakefile()->GetDefinition(requestVal);

    value.removeMember("Command");
    if (v)
      value["Response"] = std::string(v);
    else
      value["Response"] = false;
    Connection->WriteData(value.toStyledString());
  } else if (request.find("ClearBreakpoints") == 0) {
    Debugger.ClearAllBreakpoints();
  } else if (request.find("RemoveBreakpoint") == 0) {
    Debugger.ClearBreakpoint(value["File"].asString(), value["Line"].asInt());
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
      int id = 0;
      while (!backtrace.Top().FilePath.empty()) {
        auto line = Json::Value::Int(backtrace.Top().Line);
        if (line != 0) {
          Json::Value frame(Json::objectValue);
          frame["ID"] = id++;
          frame["File"] = backtrace.Top().FilePath;
          frame["Line"] = line;
          frame["Name"] = backtrace.Top().Name;

          switch (backtrace.GetBottom().GetType()) {
            case cmStateEnums::BaseType:
              frame["Type"] = "BaseType";
              break;
            case cmStateEnums::BuildsystemDirectoryType:
              frame["Type"] = "BuildsystemDirectoryType";
              break;
            case cmStateEnums::FunctionCallType:
              frame["Type"] = "FunctionCallType";
              break;
            case cmStateEnums::MacroCallType:
              frame["Type"] = "MacroCallType";
              break;
            case cmStateEnums::IncludeFileType:
              frame["Type"] = "IncludeFileType";
              break;
            case cmStateEnums::InlineListFileType:
              frame["Type"] = "InlineListFileType";
              break;
            case cmStateEnums::PolicyScopeType:
              frame["Type"] = "PolicyScopeType";
              break;
            case cmStateEnums::VariableScopeType:
              frame["Type"] = "VariableScopeType";
              break;
          }

          back.append(frame);
        }
        backtrace = backtrace.Pop();
      }
      value["Backtrace"] = back;
    } break;
    case cmDebugger::State::Unknown:
      value["State"] = "Unknown";
      break;
  }

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
