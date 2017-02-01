#include "cmDebugServerJson.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"
#include <sstream>

class cmJsonBufferStrategy : public cmConnectionBufferStrategy
{
  size_t bracesDepth = 0;
  std::string readBuffer = "";

  virtual std::string BufferMessage(std::string& rawBuffer) override
  {
    for (auto c : rawBuffer) {
      if (c == '{')
        bracesDepth++;
      if (c == '}')
        bracesDepth--;
      readBuffer += c;
      if (bracesDepth == 0) {
        std::string rtn;
        rtn.swap(readBuffer);
        return rtn;
      }
    }

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

void cmDebugServerJson::ProcessRequest(const std::string& request)
{
}

void cmDebugServerJson::SendStateUpdate()
{
  auto currentLine = Debugger.CurrentLine();

  std::string state = "";
  switch (Debugger.CurrentState()) {
    case cmDebugger::State::Running:
      state = "Running";
      break;
    case cmDebugger::State::Paused:
      state = "Paused";
      break;
    case cmDebugger::State::Unknown:
      state = "Unknown";
      break;
  }
  if (Connection && Connection->IsOpen())
    Connection->WriteData(
      "{State: '" + state + "', File: '" + currentLine.FilePath +
      "', Line: " + std::to_string(currentLine.Line) + "}");
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
