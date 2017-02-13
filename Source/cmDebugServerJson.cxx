#include "cmDebugServerJson.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"
#include <cmyajl/include/yajl/yajl_parse.h>

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cmConnection.h"
#include "cmTcpIpConnection.h"
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

#include "cmyajl/include/yajl/yajl_parse.h"

static int yajl_callback_start_map(void* ctx)
{
  size_t* jsonObjectDepth = (size_t*)ctx;
  (*jsonObjectDepth)++;
  return 1;
}
static int yajl_callback_end_map(void* ctx)
{
  size_t* jsonObjectDepth = (size_t*)ctx;
  (*jsonObjectDepth)--;
  return 1;
}

class cmJsonBufferStrategy : public cmConnectionBufferStrategy
{
  std::string readBuffer = "";
  yajl_handle parser;
  size_t jsonObjectDepth = 0;
  yajl_callbacks callbacks;

public:
  cmJsonBufferStrategy()
  {
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.yajl_end_map = yajl_callback_end_map;
    callbacks.yajl_start_map = yajl_callback_start_map;
    parser = yajl_alloc(&callbacks, 0, &jsonObjectDepth);

    // yajl (as far as I know) doesn't have a function that
    // resets a parser, and alloc / free per message would
    // be unfortunate. So we just make the entire stream look
    // like a giant json array. Start with an opening brace
    yajl_parse(parser, (const unsigned char*)"[", 1);
  }
  ~cmJsonBufferStrategy() { yajl_free(parser); }

  virtual std::string BufferMessage(std::string& rawBuffer) override
  {
    // This function can only return a single object at a time, so we only call
    // yajl_parse up to the
    // first object. Not every '}' is an object end, but every object end is a
    // '}'
    bool foundPotentialObjEnd = false;
    do {
      auto obj_end = rawBuffer.find('}');

      foundPotentialObjEnd = false;
      if (obj_end == std::string::npos)
        // Just parse the full buffer
        obj_end = rawBuffer.size();
      else {
        // Add one to include the }
        obj_end++;
        foundPotentialObjEnd = true;
      }
      yajl_status status =
        yajl_parse(parser, (const unsigned char*)rawBuffer.c_str(), obj_end);
      if (status != yajl_status_ok) {
        unsigned char error[128] = { 0 };
        auto ymsg = yajl_get_error(parser, 1, error, 128);
        std::string exception_msg = std::to_string(status) + ": " +
          std::string((const char*)ymsg) + (const char*)error;
        yajl_free_error(parser, ymsg);
        throw std::runtime_error(exception_msg);
      }
      size_t burnt = yajl_get_bytes_consumed(parser);
      readBuffer.insert(
        readBuffer.end(), rawBuffer.begin(), rawBuffer.begin() + burnt);
      rawBuffer.erase(rawBuffer.begin(), rawBuffer.begin() + burnt);
    } while (jsonObjectDepth != 0 && foundPotentialObjEnd &&
             !rawBuffer.empty());

    if (jsonObjectDepth == 0 && !readBuffer.empty()) {

      // Since this object is done, put a comma before the next object to
      // convince the parser is seeing an array of objects
      yajl_parse(parser, (const unsigned char*)",", 1);
      std::string rtn;
      rtn.swap(readBuffer);
      return rtn;
    }

    return "";
  }
};

std::unique_ptr<cmConnectionBufferStrategy> CreateJsonConnectionStrategy()
{
  return std::unique_ptr<cmConnectionBufferStrategy>(
    new cmJsonBufferStrategy());
}

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger, cmConnection* conn)
  : cmDebugServer(debugger, conn)
{
}

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger, size_t port)
  : cmDebugServerJson(debugger,
                      new cmTcpIpConnection(port, new cmJsonBufferStrategy()))
{
}

void cmDebugServerJson::ProcessRequest(cmConnection* connection,
                                       const std::string& jsonRequest)
{
  Json::Reader reader;
  Json::Value value;
  if (!reader.parse(jsonRequest, value)) {

    return;
  }
  auto request = value["Command"].asString();

  if (request == "Break") {
    Debugger.Break();
    SendStateUpdate(connection);
  } else if (request.find("ClearBreakpoints") == 0) {
    Debugger.ClearAllBreakpoints();
  } else if (request.find("RemoveBreakpoint") == 0) {
    Debugger.ClearBreakpoint(value["File"].asString(), value["Line"].asInt());
  } else if (request.find("AddBreakpoint") == 0) {
    Debugger.SetBreakpoint(value["File"].asString(), value["Line"].asInt());
  } else {

    auto ctx = Debugger.PauseContext();
    if (!ctx)
      return;

    if (request == "Continue") {
      ctx.Continue();
    } else if (request == "StepIn") {
      ctx.StepIn();
    } else if (request == "StepOut") {
      ctx.StepOut();
    } else if (request == "StepOver") {
      ctx.Step();
    } else if (request.find("Evaluate") == 0) {
      auto requestVal = value["Request"].asString();
      const char* v = 0;
      if (!requestVal.empty() && requestVal[0] == '"' &&
          requestVal.back() == '"')
        v = ctx.GetMakefile()->ExpandVariablesInString(requestVal);
      else
        v = ctx.GetMakefile()->GetDefinition(requestVal);

      value.removeMember("Command");
      if (v)
        value["Response"] = std::string(v);
      else
        value["Response"] = false;
      connection->WriteData(value.toStyledString());
    }
  }
}

void cmDebugServerJson::SendStateUpdate(cmConnection* connection)
{

  std::string state = "";
  Json::Value value;
  value["PID"] = ::getpid();
  switch (Debugger.CurrentState()) {
    case cmDebugger::State::Running:
      value["State"] = "Running";
      break;
    case cmDebugger::State::Paused: {
      value["State"] = "Paused";
      Json::Value back(Json::arrayValue);

      if (auto ctx = Debugger.PauseContext()) {
        auto currentLine = ctx.CurrentLine();
        auto backtrace = ctx.GetBacktrace();
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
      }

    } break;
    case cmDebugger::State::Unknown:
      value["State"] = "Unknown";
      break;
  }

  if (connection && connection->IsOpen())
    connection->WriteData(value.toStyledString());
}

void cmDebugServerJson::OnChangeState()
{
  cmDebuggerListener::OnChangeState();
  for (auto& connection : Connections)
    SendStateUpdate(connection.get());
}

void cmDebugServerJson::OnConnected(cmConnection* connection)
{
  SendStateUpdate(connection);
}
