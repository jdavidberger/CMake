/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmServer.h"
#include <iomanip>
#include <sstream>

#include "cmFileMonitor.h"
#include "cmServerConnection.h"
#include "cmServerDictionary.h"
#include "cmServerProtocol.h"
#include "cmSystemTools.h"
#include "cmVersionMacros.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>

class cmServer::DebugInfo
{
public:
  DebugInfo()
    : StartTime(uv_hrtime())
  {
  }

  bool PrintStatistics = false;

  std::string OutputFile;
  uint64_t StartTime;
};

cmServer::cmServer(cmConnection* conn, bool supportExperimental)
  : cmServerBase(conn)
  , SupportExperimental(supportExperimental)
{
  // Register supported protocols:
  this->RegisterProtocol(new cmServerProtocol1_0);
}

cmServer::~cmServer()
{
  if (!this->Protocol) { // Server was never fully started!
    return;
  }

  for (cmServerProtocol* p : this->SupportedProtocols) {
    delete p;
  }
}
void cmServer::ProcessRequest(const std::string& input)
{
  Json::Reader reader;
  Json::Value value;
  if (!reader.parse(input, value)) {
    this->WriteParseError("Failed to parse JSON input.");
    return;
  }

  std::unique_ptr<DebugInfo> debug;
  Json::Value debugValue = value["debug"];
  if (!debugValue.isNull()) {
    debug = std::make_unique<DebugInfo>();
    debug->OutputFile = debugValue["dumpToFile"].asString();
    debug->PrintStatistics = debugValue["showStats"].asBool();
  }

  const cmServerRequest request(this, value[kTYPE_KEY].asString(),
                                value[kCOOKIE_KEY].asString(), value);

  if (request.Type == "") {
    cmServerResponse response(request);
    response.SetError("No type given in request.");
    this->WriteResponse(response, nullptr);
    return;
  }

  cmSystemTools::SetMessageCallback(reportMessage,
                                    const_cast<cmServerRequest*>(&request));
  if (this->Protocol) {
    this->Protocol->CMakeInstance()->SetProgressCallback(
      reportProgress, const_cast<cmServerRequest*>(&request));
    this->WriteResponse(this->Protocol->Process(request), debug.get());
  } else {
    this->WriteResponse(this->SetProtocolVersion(request), debug.get());
  }
}

void cmServer::RegisterProtocol(cmServerProtocol* protocol)
{
  if (protocol->IsExperimental() && !this->SupportExperimental) {
    return;
  }
  auto version = protocol->ProtocolVersion();
  assert(version.first >= 0);
  assert(version.second >= 0);
  auto it = std::find_if(this->SupportedProtocols.begin(),
                         this->SupportedProtocols.end(),
                         [version](cmServerProtocol* p) {
                           return p->ProtocolVersion() == version;
                         });
  if (it == this->SupportedProtocols.end()) {
    this->SupportedProtocols.push_back(protocol);
  }
}

void cmServer::PrintHello() const
{
  Json::Value hello = Json::objectValue;
  hello[kTYPE_KEY] = "hello";

  Json::Value& protocolVersions = hello[kSUPPORTED_PROTOCOL_VERSIONS] =
    Json::arrayValue;

  for (auto const& proto : this->SupportedProtocols) {
    auto version = proto->ProtocolVersion();
    Json::Value tmp = Json::objectValue;
    tmp[kMAJOR_KEY] = version.first;
    tmp[kMINOR_KEY] = version.second;
    if (proto->IsExperimental()) {
      tmp[kIS_EXPERIMENTAL_KEY] = true;
    }
    protocolVersions.append(tmp);
  }

  this->WriteJsonObject(hello, nullptr);
}

void cmServer::QueueRequest(const std::string& request)
{
  cmServerBase::QueueRequest(request);
  this->PopOne();
}

void cmServer::reportProgress(const char* msg, float progress, void* data)
{
  const cmServerRequest* request = static_cast<const cmServerRequest*>(data);
  assert(request);
  if (progress < 0.0 || progress > 1.0) {
    request->ReportMessage(msg, "");
  } else {
    request->ReportProgress(0, static_cast<int>(progress * 1000), 1000, msg);
  }
}

void cmServer::reportMessage(const char* msg, const char* title,
                             bool& /* cancel */, void* data)
{
  const cmServerRequest* request = static_cast<const cmServerRequest*>(data);
  assert(request);
  assert(msg);
  std::string titleString;
  if (title) {
    titleString = title;
  }
  request->ReportMessage(std::string(msg), titleString);
}

cmServerResponse cmServer::SetProtocolVersion(const cmServerRequest& request)
{
  if (request.Type != kHANDSHAKE_TYPE) {
    return request.ReportError("Waiting for type \"" + kHANDSHAKE_TYPE +
                               "\".");
  }

  Json::Value requestedProtocolVersion = request.Data[kPROTOCOL_VERSION_KEY];
  if (requestedProtocolVersion.isNull()) {
    return request.ReportError("\"" + kPROTOCOL_VERSION_KEY +
                               "\" is required for \"" + kHANDSHAKE_TYPE +
                               "\".");
  }

  if (!requestedProtocolVersion.isObject()) {
    return request.ReportError("\"" + kPROTOCOL_VERSION_KEY +
                               "\" must be a JSON object.");
  }

  Json::Value majorValue = requestedProtocolVersion[kMAJOR_KEY];
  if (!majorValue.isInt()) {
    return request.ReportError("\"" + kMAJOR_KEY +
                               "\" must be set and an integer.");
  }

  Json::Value minorValue = requestedProtocolVersion[kMINOR_KEY];
  if (!minorValue.isNull() && !minorValue.isInt()) {
    return request.ReportError("\"" + kMINOR_KEY +
                               "\" must be unset or an integer.");
  }

  const int major = majorValue.asInt();
  const int minor = minorValue.isNull() ? -1 : minorValue.asInt();
  if (major < 0) {
    return request.ReportError("\"" + kMAJOR_KEY + "\" must be >= 0.");
  }
  if (!minorValue.isNull() && minor < 0) {
    return request.ReportError("\"" + kMINOR_KEY +
                               "\" must be >= 0 when set.");
  }

  this->Protocol =
    this->FindMatchingProtocol(this->SupportedProtocols, major, minor);
  if (!this->Protocol) {
    return request.ReportError("Protocol version not supported.");
  }

  std::string errorMessage;
  if (!this->Protocol->Activate(this, request, &errorMessage)) {
    this->Protocol = CM_NULLPTR;
    return request.ReportError("Failed to activate protocol version: " +
                               errorMessage);
  }
  return request.Reply(Json::objectValue);
}

bool cmServer::Serve(std::string* errorMessage)
{
  if (this->SupportedProtocols.empty()) {
    *errorMessage =
      "No protocol versions defined. Maybe you need --experimental?";
    return false;
  }
  assert(!this->Protocol);

  return Connection->ProcessEvents(errorMessage);
}

cmFileMonitor* cmServer::FileMonitor() const
{
  return fileMonitor.get();
}

void cmServer::WriteJsonObject(const Json::Value& jsonValue,
                               const DebugInfo* debug) const
{
  Json::FastWriter writer;

  auto beforeJson = uv_hrtime();
  std::string result = writer.write(jsonValue);

  if (debug) {
    Json::Value copy = jsonValue;
    if (debug->PrintStatistics) {
      Json::Value stats = Json::objectValue;
      auto endTime = uv_hrtime();

      stats["jsonSerialization"] = double(endTime - beforeJson) / 1000000.0;
      stats["totalTime"] = double(endTime - debug->StartTime) / 1000000.0;
      stats["size"] = static_cast<int>(result.size());
      if (!debug->OutputFile.empty()) {
        stats["dumpFile"] = debug->OutputFile;
      }

      copy["zzzDebug"] = stats;

      result = writer.write(copy); // Update result to include debug info
    }

    if (!debug->OutputFile.empty()) {
      std::ofstream myfile;
      myfile.open(debug->OutputFile);
      myfile << result;
      myfile.close();
    }
  }

  Connection->WriteData(std::string("\n") + kSTART_MAGIC + std::string("\n") +
                        result + kEND_MAGIC + std::string("\n"));
}

cmServerProtocol* cmServer::FindMatchingProtocol(
  const std::vector<cmServerProtocol*>& protocols, int major, int minor)
{
  cmServerProtocol* bestMatch = nullptr;
  for (auto protocol : protocols) {
    auto version = protocol->ProtocolVersion();
    if (major != version.first) {
      continue;
    }
    if (minor == version.second) {
      return protocol;
    }
    if (!bestMatch || bestMatch->ProtocolVersion().second < version.second) {
      bestMatch = protocol;
    }
  }
  return minor < 0 ? bestMatch : nullptr;
}

void cmServer::WriteProgress(const cmServerRequest& request, int min,
                             int current, int max,
                             const std::string& message) const
{
  assert(min <= current && current <= max);
  assert(message.length() != 0);

  Json::Value obj = Json::objectValue;
  obj[kTYPE_KEY] = kPROGRESS_TYPE;
  obj[kREPLY_TO_KEY] = request.Type;
  obj[kCOOKIE_KEY] = request.Cookie;
  obj[kPROGRESS_MESSAGE_KEY] = message;
  obj[kPROGRESS_MINIMUM_KEY] = min;
  obj[kPROGRESS_MAXIMUM_KEY] = max;
  obj[kPROGRESS_CURRENT_KEY] = current;

  this->WriteJsonObject(obj, nullptr);
}

void cmServer::WriteMessage(const cmServerRequest& request,
                            const std::string& message,
                            const std::string& title) const
{
  if (message.empty()) {
    return;
  }

  Json::Value obj = Json::objectValue;
  obj[kTYPE_KEY] = kMESSAGE_TYPE;
  obj[kREPLY_TO_KEY] = request.Type;
  obj[kCOOKIE_KEY] = request.Cookie;
  obj[kMESSAGE_KEY] = message;
  if (!title.empty()) {
    obj[kTITLE_KEY] = title;
  }

  WriteJsonObject(obj, nullptr);
}

void cmServer::WriteParseError(const std::string& message) const
{
  Json::Value obj = Json::objectValue;
  obj[kTYPE_KEY] = kERROR_TYPE;
  obj[kERROR_MESSAGE_KEY] = message;
  obj[kREPLY_TO_KEY] = "";
  obj[kCOOKIE_KEY] = "";

  this->WriteJsonObject(obj, nullptr);
}

void cmServer::WriteSignal(const std::string& name,
                           const Json::Value& data) const
{
  assert(data.isObject());
  Json::Value obj = data;
  obj[kTYPE_KEY] = kSIGNAL_TYPE;
  obj[kREPLY_TO_KEY] = "";
  obj[kCOOKIE_KEY] = "";
  obj[kNAME_KEY] = name;

  WriteJsonObject(obj, nullptr);
}

void cmServer::WriteResponse(const cmServerResponse& response,
                             const DebugInfo* debug) const
{
  assert(response.IsComplete());

  Json::Value obj = response.Data();
  obj[kCOOKIE_KEY] = response.Cookie;
  obj[kTYPE_KEY] = response.IsError() ? kERROR_TYPE : kREPLY_TYPE;
  obj[kREPLY_TO_KEY] = response.Type;
  if (response.IsError()) {
    obj[kERROR_MESSAGE_KEY] = response.ErrorMessage();
  }

  this->WriteJsonObject(obj, debug);
}

void cmServer::OnNewConnection()
{
  PrintHello();
}

void cmServer::OnEventsStart(uv_loop_t* loop)
{
  cmServerBase::OnEventsStart(loop);
  fileMonitor = std::make_shared<cmFileMonitor>(loop);
}

void cmServer::OnEventsStop(std::string* pString)
{
  cmServerBase::OnEventsStop(pString);
  if (fileMonitor) {
    fileMonitor->StopMonitoring();
    fileMonitor.reset();
  }
}

void cmServerBase::QueueRequest(const std::string& request)
{
  this->Queue.push_back(request);
}

bool cmServerBase::StartServeThread()
{
  ServeThread = std::thread([&] {
    std::string error;
    Serve(&error);
  });
  return true;
}

cmServerBase::~cmServerBase()
{
  uv_close(reinterpret_cast<uv_handle_t*>(&cbHandle), nullptr);
  uv_close(reinterpret_cast<uv_handle_t*>(&cbPrepareHandle), nullptr);
  Connection->TriggerShutdown();
  ServeThread.join();
}

bool cmServerBase::Serve(std::string* errorMessage)
{
  return GetConnection()->ProcessEvents(errorMessage);
}

void cmServerBase::OnNewConnection()
{
}

void cmServerBase::OnDisconnect()
{
}
template <typename T>
static void onLoop(T* handle)
{
  auto serverBase = reinterpret_cast<cmServerBase*>(handle->data);
  serverBase->PopOne();
}
void cmServerBase::OnEventsStart(uv_loop_t* loop)
{
  uv_check_init(loop, &cbHandle);
  cbHandle.data = this;
  uv_check_start(&cbHandle, &onLoop);

  uv_prepare_init(loop, &cbPrepareHandle);
  cbPrepareHandle.data = this;
  uv_prepare_start(&cbPrepareHandle, &onLoop);
}

void cmServerBase::OnEventsStop(std::string* pString)
{
  uv_check_stop(&cbHandle);
}

bool cmServerBase::OnSignal(int signum)
{
  return false;
}

cmConnection* cmServerBase::GetConnection()
{
  return Connection.get();
}

cmServerBase::cmServerBase(cmConnection* connection)
  : Connection(connection)
{
  Connection->SetServer(this);
}

void cmServerBase::PopOne()
{
  if (this->Queue.empty()) {
    return;
  }

  const std::string input = this->Queue.front();
  this->Queue.erase(this->Queue.begin());
  ProcessRequest(input);
}
