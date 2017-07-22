/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
file Copyright.txt or https://cmake.org/licensing for details.  */
#define NOMINMAX
#include "cmDebugServerConsole.h"
#include "cmConnection.h"
#include "cmMakefile.h"
#include "cmServerConnection.h"
#include "cmVariableWatch.h"
#include <fstream>

static std::string getFileLines(const std::string& filename, long lineStart,
                                size_t lineCount)
{
  std::ifstream file(filename);
  file.seekg(std::ios::beg);
  for (int i = 0; i < lineStart - 1; ++i) {
    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  std::stringstream rtn;
  std::string line;
  for (auto i = lineStart; i < (long)(lineStart + lineCount); i++) {
    if (std::getline(file, line)) {
        rtn << i << "\t|" << line << std::endl;
    }
  }

  return rtn.str();
}

cmDebugServerConsole::cmDebugServerConsole(cmDebugger& debugger,
                                           cmConnection* conn,
                                           bool printPrompt)
  : cmDebugServer(debugger, conn)
  , PrintPrompt(printPrompt)
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

void cmDebugServerConsole::ProcessRequest(cmConnection* connection,
                                          const std::string& request)
{
  if (request == "b") {
    Debugger.Break();
  } else if (request == "q") {
    exit(0);
  } else if (request.find("watch ") == 0) {
    auto whatToWatch = request.substr(strlen("watch "));
    Debugger.SetWatchpoint(whatToWatch);
    connection->WriteData("Set watchpoint on write '" + whatToWatch + "'\n");
  } else if (request.find("rwatch ") == 0) {
    auto whatToWatch = request.substr(strlen("rwatch "));
    Debugger.SetWatchpoint(whatToWatch, cmWatchpoint::WATCHPOINT_READ);
    connection->WriteData("Set watchpoint on read '" + whatToWatch + "'\n");
  } else if (request.find("awatch ") == 0) {
    auto whatToWatch = request.substr(strlen("awatch "));
    Debugger.SetWatchpoint(whatToWatch, cmWatchpoint::WATCHPOINT_ALL);
    connection->WriteData("Set watchpoint on read/write '" + whatToWatch +
                          "'\n");
  } else if (request.find("info br") == 0) {
    std::stringstream ss;
    auto bps = Debugger.GetBreakpoints();
    for (unsigned i = 0; i < bps.size(); i++) {
      ss << bps[i].Id << " \tbreakpoint \t" << bps[i].File << ":"
         << bps[i].Line << std::endl;
    }
    auto wps = Debugger.GetWatchpoints();
    for (unsigned i = 0; i < wps.size(); i++) {
      ss << wps[i].Id << " \twatchpoint \t" << wps[i].Variable << " \t("
         << cmWatchpoint::GetTypeAsString(wps[i].Type) << ")" << std::endl;
    }
    connection->WriteData(ss.str());
  } else if (request.find("clear") == 0) {
    auto space = request.find(' ');
    if (space == std::string::npos) {
      Debugger.ClearAllBreakpoints();
      Debugger.ClearAllWatchpoints();
      connection->WriteData("Cleared all breakpoints and watchpoints\n");
    } else {
      auto clearWhat = stoi(request.substr(space));

      if (Debugger.ClearBreakpoint(clearWhat)) {
        connection->WriteData("Cleared breakpoint " +
                              std::to_string(clearWhat) + "\n");
      } else if (Debugger.ClearWatchpoint(clearWhat)) {
        connection->WriteData("Cleared watchpoint " +
                              std::to_string(clearWhat) + "\n");
      } else {
        connection->WriteData(
          "Could not find breakpoint or watchpoint with ID of " +
          std::to_string(clearWhat) + "\n");
      }
    }
  } else if (request.find("br") == 0) {
    auto space = request.find(' ');

    // The state doesn't matter if we set an absolute path, so handle that
    // here.
    if (space != std::string::npos) {
      auto bpSpecifier = request.substr(space + 1);
      auto colonPlacement = bpSpecifier.find_last_of(':');
      size_t line = (size_t)-1;

      if (colonPlacement != std::string::npos) {
        line = std::stoi(bpSpecifier.substr(colonPlacement + 1));
        bpSpecifier = bpSpecifier.substr(0, colonPlacement);

        Debugger.SetBreakpoint(bpSpecifier, line);
        connection->WriteData("Break at " + bpSpecifier + ":" +
                              std::to_string(line) + "\n");
      }
    }
  }

  auto ctx = Debugger.PauseContext();
  if (!ctx) {
    return;
  }

  if (request.find("fin") == 0) {
    ctx.StepOut();
  } else if (request == "c") {
    ctx.Continue();
  } else if (request == "n") {
    ctx.Step();
  } else if (request == "s") {
    ctx.StepIn();
  } else if (request.find("l") == 0) {
    auto ctxLines = 10;
    auto fileLines = getFileLines(ctx.CurrentLine().FilePath,
                                  ctx.CurrentLine().Line, ctxLines);
    connection->WriteData(fileLines + "\n");
  } else if (request.find("open") == 0) {
    auto shellCmd =
      request.size() == 4 ? "" : (request.substr(strlen("open ")) + " ");
    system((shellCmd + ctx.CurrentLine().FilePath).c_str());
  } else if (request == "bt") {
    auto currentLine = ctx.CurrentLine();
    connection->WriteData("Paused at " + currentLine.FilePath + ":" +
                          std::to_string(currentLine.Line) + " (" +
                          currentLine.Name + ")\n");

    auto bt = ctx.GetBacktrace();
    std::stringstream ss;
    bt.PrintCallStack(ss);
    connection->WriteData(ss.str());
  } else if (request.find("print ") == 0) {
    auto whatToPrint = request.substr(strlen("print "));
    auto val = ctx.GetMakefile()->GetDefinition(whatToPrint);
    if (val) {
      connection->WriteData("$ " + whatToPrint + " = " + std::string(val) +
                            "\n");
    }
    else {
      connection->WriteData(whatToPrint + " isn't set.\n");
    }
  } else if (request.find("br") == 0) {
    auto space = request.find(' ');
    if (space != std::string::npos) {
      auto bpSpecifier = request.substr(space + 1);
      auto colonPlacement = bpSpecifier.find_last_of(':');
      size_t line = (size_t)-1;

      if (colonPlacement == std::string::npos &&
          isdigit(*bpSpecifier.c_str())) {
        line = std::stoi(bpSpecifier);
        bpSpecifier = ctx.CurrentLine().FilePath;
        Debugger.SetBreakpoint(bpSpecifier, line);
        connection->WriteData("Break at " + bpSpecifier + ":" +
                              std::to_string(line) + "\n");
      }
    }
  }

  printPrompt(connection);
}

void cmDebugServerConsole::printPrompt(cmConnection* connection)
{
  if (PrintPrompt) {
    connection->WriteData("(debugger) > ");
  }
}
void cmDebugServerConsole::OnChangeState()
{
  cmDebuggerListener::OnChangeState();
  for (auto& Connection : Connections) {
    switch (Debugger.CurrentState()) {
      case cmDebugger::State::Running:
        Connection->WriteData("Running...\n");
        break;
      case cmDebugger::State::Paused: {
        auto ctx = Debugger.PauseContext();
        if (ctx) {
          auto currentLine = ctx.CurrentLine();
          Connection->WriteData("Paused at " + currentLine.FilePath + ":" +
                                std::to_string(currentLine.Line) + " (" +
                                currentLine.Name + ")\n");
        } else {
          Connection->WriteData("Paused at indeterminate state\n");
        }
        printPrompt(Connection.get());
      } break;
      case cmDebugger::State::Unknown:
        Connection->WriteData("Unknown state\n");
        printPrompt(Connection.get());
        break;
    }
  }
}

void cmDebugServerConsole::OnBreakpoint(breakpoint_id breakpoint)
{
  std::stringstream ss;
  ss << "# Breakpoint " << breakpoint << " hit" << std::endl;

  for (auto& Connection : Connections) {
    Connection->WriteData(ss.str());
  }
}

void cmDebugServerConsole::OnWatchpoint(const std::string& variable,
                                        int access,
                                        const std::string& newValue)
{
  std::stringstream ss;
  ss << "Watchpoint '" << variable << "' hit -- '" << newValue << "' ("
     << cmVariableWatch::GetAccessAsString(access) << ")" << std::endl;

  for (auto& Connection : Connections) {
    Connection->WriteData(ss.str());
  }
}
