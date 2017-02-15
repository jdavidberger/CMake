#include "cmDebugServerJson.h"

int testJsonBuffering(int, char** const)
{
  std::vector<std::string> messages = {
    "{ \"test\": 10}", "{ \"test\": { \"test2\": false} }",
    "{ \"test\": [1, 2, 3] }",
    "{ \"a\": { \"1\": {}, \n\n\n \"2\":[] \t\t\t\t}}"
  };

  std::string fullMessage;
  for (auto& msg : messages) {
    fullMessage += msg;
  }

  // The buffering strategy should cope with any fragmentation, including
  // just getting the characters one at a time.
  auto jsonBuffer = CreateJsonConnectionStrategy();
  std::vector<std::string> response;
  std::string rawBuffer = "";
  for (size_t i = 0; i < fullMessage.size(); i++) {
    rawBuffer += fullMessage[i];
    std::string packet = jsonBuffer->BufferMessage(rawBuffer);
    do {
      if (!packet.empty()) {
        response.push_back(packet);
      }
      packet = jsonBuffer->BufferMessage(rawBuffer);
    } while (!packet.empty());
  }

  if (response != messages)
    return 1;

  // We should also be able to deal with getting a bunch at once
  response.clear();
  std::string packet = jsonBuffer->BufferMessage(fullMessage);
  do {
    if (!packet.empty()) {
      response.push_back(packet);
    }
    packet = jsonBuffer->BufferMessage(fullMessage);
  } while (!packet.empty());

  if (response != messages)
    return 1;

  return 0;
}
