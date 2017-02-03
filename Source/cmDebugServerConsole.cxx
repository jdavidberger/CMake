//
// Created by J on 1/29/2017.
//

#include "cmDebugServerConsole.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"
#include <sstream>

cmDebugServerConsole::cmDebugServerConsole(cmDebugger& debugger,
                                           cmConnection* conn)
  : cmDebugServer(debugger, conn)
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
cmDebugServerConsole::cmDebugServerConsole(cmDebugger& debugger)
  : cmDebugServerConsole(debugger,
                         new cmStdIoConnection(new cmLineBufferStrategy()))
{
}

void cmDebugServerConsole::ProcessRequest(const std::string& request)
{
  if (request == "c") {
    Debugger.Continue();
  } else if (request == "b") {
    Debugger.Break();
  } else if (request == "q") {
    exit(0);
  } else if (request == "s") {
    Debugger.Step();
  } else if (request == "bt") {
    auto bt = Debugger.GetBacktrace();
    std::stringstream ss;
    bt.PrintCallStack(ss);
    Connection->WriteData(ss.str());
  } else if (request.find("print ") == 0) {
    auto whatToPrint = request.substr(strlen("print "));
    auto val = Debugger.GetMakefile()->GetDefinition(whatToPrint);
    if (val)
      Connection->WriteData("$ " + whatToPrint + " = " + std::string(val) +
                            "\n");
    else
      Connection->WriteData(whatToPrint + " isn't set.\n");
  } else if (request.find("info br") == 0) {
    std::stringstream ss;
    auto& bps = Debugger.GetBreakpoints();
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
      auto& bps = Debugger.GetBreakpoints();
      for (unsigned i = 0; i < bps.size(); i++) {
        Debugger.ClearBreakpoint(i);
      }
      Connection->WriteData("Cleared all breakpoints\n");
    } else {
      auto clearWhat = stoi(request.substr(space));
      Debugger.ClearBreakpoint(clearWhat);
      Connection->WriteData("Cleared breakpoint " + std::to_string(clearWhat) +
                            "\n");
    }
  } else if (request.find("br") == 0) {
    auto space = request.find(' ');
    if (space != std::string::npos) {
      auto bpSpecifier = request.substr(space + 1);
      auto colonPlacement = bpSpecifier.find_last_of(':');
      size_t line = (size_t)-1;

      if (colonPlacement != std::string::npos) {
        line = std::stoi(bpSpecifier.substr(colonPlacement + 1));
        bpSpecifier = bpSpecifier.substr(0, colonPlacement);
      } else if (isdigit(*bpSpecifier.c_str())) {
        line = std::stoi(bpSpecifier);
        bpSpecifier = Debugger.CurrentLine().FilePath;
      }

      Debugger.SetBreakpoint(bpSpecifier, line);
      Connection->WriteData("Break at " + bpSpecifier + ":" +
                            std::to_string(line) + "\n");
    }
  }
  printPrompt();
}
void cmDebugServerConsole::printPrompt()
{
  Connection->WriteData("(debugger) > ");
}
void cmDebugServerConsole::OnChangeState()
{
  cmDebuggerListener::OnChangeState();

  auto currentLine = Debugger.CurrentLine();
  switch (Debugger.CurrentState()) {
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
