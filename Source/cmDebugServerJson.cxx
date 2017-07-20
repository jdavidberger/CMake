/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebugServerJson.h"
#include "cmMakefile.h"

#include "cmsys/SystemInformation.hxx"

static cmsys::SystemInformation info;

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cmConnection.h"
#include "cm_jsoncpp_reader.h"
#endif

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger, cmConnection* conn)
  : cmDebugServer(debugger, conn)
{
}

cmDebugServerJson::cmDebugServerJson(cmDebugger& debugger,
                                     const std::string& name)
  : cmDebugServerJson(debugger,
                      new cmPipeConnection(name, new cmServerBufferStrategy()))
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
  } else if (request.find("ClearBreakpoints") == 0) {
    Debugger.ClearAllBreakpoints();
  } else if (request.find("RemoveBreakpoint") == 0) {
    Debugger.ClearBreakpoint(value["File"].asString(), value["Line"].asInt());
  } else if (request.find("AddBreakpoint") == 0) {
    Debugger.SetBreakpoint(value["File"].asString(), value["Line"].asInt());
  } else if (request.find("AddWatchpoint") == 0) {
    auto type = value["Type"].asString();
    cmWatchpoint::WatchpointType watchpointType =
      cmWatchpoint::WATCHPOINT_WRITE;
    if (type == "Read") {
      watchpointType = cmWatchpoint::WATCHPOINT_READ;
    } else if (type == "All") {
      watchpointType = cmWatchpoint::WATCHPOINT_ALL;
    }

    Debugger.SetWatchpoint(value["Expr"].asString(), watchpointType);
  } else if (request.find("RemoveWatchpoint") == 0) {
    auto expr = value["Expr"].asString();
    auto watchpoints = Debugger.GetWatchpoints();
    std::vector<watchpoint_id> removeList;
    removeList.reserve(watchpoints.size());
    for (auto& watchpoint : watchpoints) {
      if (watchpoint.Variable == expr) {
        removeList.push_back(watchpoint.Id);
      }
    }

    for (auto& w_id : removeList) {
      Debugger.ClearWatchpoint(w_id);
    }
  } else if (request.find("ClearWatchpoints") == 0) {
    Debugger.ClearAllWatchpoints();
  } else {

    auto ctx = Debugger.PauseContext();
    if (!ctx) {
      value["Error"] = "Improper command for running context";
      connection->WriteData(value.toStyledString());
      return;
    }

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
      const char* v = CM_NULLPTR;
      if (!requestVal.empty() && requestVal[0] == '"' &&
          requestVal.back() == '"') {
        v = ctx.GetMakefile()->ExpandVariablesInString(requestVal);
      } else {
        v = ctx.GetMakefile()->GetDefinition(requestVal);
      }

      value.removeMember("Command");
      if (v) {
        value["Response"] = std::string(v);
      } else {
        value["Response"] = false;
      }
      connection->WriteData(value.toStyledString());
    } else {
      value["Error"] = "Improper command for paused context";
      connection->WriteData(value.toStyledString());
      return;
    }
  }
}

void cmDebugServerJson::SendStateUpdate(cmConnection* connection)
{
  std::string state = "";
  Json::Value value;
  value["PID"] = info.GetProcessId();

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
      return;
  }

  if (connection && connection->IsOpen()) {
    connection->WriteData(value.toStyledString());
  }
}

void cmDebugServerJson::OnChangeState()
{
  cmDebuggerListener::OnChangeState();
  std::lock_guard<std::recursive_mutex> l(ConnectionsMutex);
  for (auto& connection : Connections) {
    SendStateUpdate(connection.get());
  }
}

void cmDebugServerJson::OnConnected(cmConnection* connection)
{
  SendStateUpdate(connection);
}
