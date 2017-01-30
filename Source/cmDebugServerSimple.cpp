//
// Created by J on 1/29/2017.
//

#include "cmDebugServerSimple.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"
#include <sstream>

cmDebugServerSimple::cmDebugServerSimple(cmConnection* conn)
  : cmDebugServer(conn)
{
}

class cmLineBufferStrategy : public cmConnectionBufferStrategy
{
public:
  std::string BufferMessage(std::string& rawBuffer) override
  {
    auto needle = rawBuffer.find('\n');

    if (needle == std::string::npos) {
      return "";
    }
    std::string line = rawBuffer.substr(0, needle);
    const auto ls = line.size();
    if (ls > 1 && line.at(ls - 1) == '\r') {
      line.erase(ls - 1, 1);
    }
    rawBuffer.erase(rawBuffer.begin(),
                    rawBuffer.begin() + static_cast<long>(needle) + 1);
    return line;
  }
};
cmDebugServerSimple::cmDebugServerSimple()
  : cmDebugServerSimple(new cmStdIoConnection(new cmLineBufferStrategy()))
{
}

void cmDebugServerSimple::ProcessRequest(const std::string& request)
{
  auto debugger = this->Debugger.lock();
  if (!debugger) {
    this->Connection->WriteData("Debugger isn't currently connected.\n");
    return;
  }
  if (request == "c") {
    debugger->Continue();
  } else if (request == "b") {
    debugger->Break();
  } else if (request == "s") {
    debugger->Step();
  } else if (request == "bt") {
    auto bt = debugger->GetBacktrace();
    std::stringstream ss;
    bt.PrintCallStack(ss);
    Connection->WriteData(ss.str());
  } else if (request.find("print ") == 0) {
    auto whatToPrint = request.substr(strlen("print "));
    auto val = debugger->GetMakefile()->GetDefinition(whatToPrint);
    if (val)
      Connection->WriteData("$ " + whatToPrint + " = " + std::string(val) +
                            "\n");
    else
      Connection->WriteData(whatToPrint + " isn't set.\n");
  } else if (request.find("info br") == 0) {
    std::stringstream ss;
    auto& bps = debugger->GetBreakpoints();
    for (unsigned i = 0; i < bps.size(); i++) {
      if (bps[i]) {
        ss << i << " break at " << bps[i].file << ":" << bps[i].line
           << std::endl;
      }
    }
    Connection->WriteData(ss.str());
  } else if (request.find("clear") == 0) {
    auto space = request.find(' ');
    if (space == std::string::npos) {
      auto& bps = debugger->GetBreakpoints();
      for (unsigned i = 0; i < bps.size(); i++) {
        debugger->ClearBreakpoint(i);
      }
      Connection->WriteData("Cleared all breakpoints\n");
    } else {
      auto clearWhat = stoi(request.substr(space));
      debugger->ClearBreakpoint(clearWhat);
      Connection->WriteData("Cleared breakpoint " + std::to_string(clearWhat) +
                            "\n");
    }
  } else if (request.find("br ") == 0) {
    auto bpSpecifier = request.substr(strlen("br "));
    auto colonPlacement = bpSpecifier.find_last_of(':');
    size_t line = (size_t)-1;

    if (colonPlacement != std::string::npos) {
      line = std::stoi(bpSpecifier.substr(colonPlacement + 1));
      bpSpecifier = bpSpecifier.substr(0, colonPlacement);
    } else if (isdigit(*bpSpecifier.c_str())) {
      line = std::stoi(bpSpecifier);
      bpSpecifier = debugger->CurrentLine().FilePath;
    }

    debugger->SetBreakpoint(bpSpecifier, line);
    Connection->WriteData("Break at " + bpSpecifier + ":" +
                          std::to_string(line) + "\n");
  }
  printPrompt();
}
void cmDebugServerSimple::printPrompt()
{
  auto debugger = Debugger.lock();
  if (!debugger)
    return;

  Connection->WriteData("(debugger) > ");
}
void cmDebugServerSimple::OnChangeState()
{
  cmDebugerListener::OnChangeState();

  auto debugger = Debugger.lock();
  if (!debugger)
    return;
  auto currentLine = debugger->CurrentLine();
  switch (debugger->CurrentState()) {
    case cmDebugger::State::Running:
      Connection->WriteData("Running...\n");
      break;
    case cmDebugger::State::Paused:
      Connection->WriteData("Paused at " + currentLine.FilePath + ":" +
                            std::to_string(currentLine.Line) + " (" +
                            currentLine.Name + ")\n");
      printPrompt();
      break;
    case cmDebugger::State::Unknown:
      Connection->WriteData("Unknown at " + currentLine.FilePath + ":" +
                            std::to_string(currentLine.Line) + " (" +
                            currentLine.Name + ")\n");
      printPrompt();
      break;
  }
}

cmDebugServer::cmDebugServer(cmConnection* conn)
  : cmServerBase(conn)
{
}

bool cmDebugServer::OnSignal(int signum)
{
  if (signum == 2) {
    if (auto debugger = Debugger.lock())
      debugger->Break();
    return true;
  }
  return false;
}
