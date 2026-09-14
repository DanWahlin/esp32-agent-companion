#include "../firmware/Copilot/src/DeviceCommands.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace copilot;

static DeviceCommand send(DeviceCommands& parser, const std::string& text) {
  DeviceCommand result = DeviceCommand::None;
  for (char byte : text) result = parser.feed(byte, 0);
  return result;
}

int main() {
  DeviceCommands parser;
  for (DeviceCommand command : {DeviceCommand::Idle, DeviceCommand::Surprise, DeviceCommand::Working,
                                DeviceCommand::Complete, DeviceCommand::Attention}) {
    assert(send(parser, std::string("!") + commandName(command) + "\r\n") == command);
  }
  assert(send(parser, "s") == DeviceCommand::Capture);
  assert(send(parser, "h") == DeviceCommand::Heap);
  assert(send(parser, "i") == DeviceCommand::Info);
  assert(send(parser, "u") == DeviceCommand::UploadOpenClaw);
  assert(send(parser, "\n") == DeviceCommand::None);
  assert(send(parser, "x") == DeviceCommand::Invalid);
  assert(send(parser, "!not-a-mode\n") == DeviceCommand::Invalid);
  assert(send(parser, "!" + std::string(100, 's') + "\n") == DeviceCommand::Invalid);
  assert(send(parser, "!working\n") == DeviceCommand::Working);
  assert(send(parser, "!att") == DeviceCommand::None);
  assert(parser.expire(999) == DeviceCommand::None);
  assert(parser.expire(1000) == DeviceCommand::Invalid);
  assert(send(parser, "!attention\n") == DeviceCommand::Attention);
  std::cout << "PASS: bounded mode packets, legacy diagnostics, CRLF, overflow and timeout recovery\n";
}
