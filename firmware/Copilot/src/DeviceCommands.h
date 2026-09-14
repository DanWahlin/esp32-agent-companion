#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace copilot {
enum class DeviceCommand {
  None, Invalid, Capture, Heap, Info, UploadOpenClaw,
  Idle, Surprise, Working, Complete, Attention
};
constexpr unsigned kDeviceProtocol = 2;

inline const char* commandName(DeviceCommand command) {
  switch (command) {
    case DeviceCommand::Idle: return "idle";
    case DeviceCommand::Surprise: return "surprise";
    case DeviceCommand::Working: return "working";
    case DeviceCommand::Complete: return "complete";
    case DeviceCommand::Attention: return "attention";
    default: return "invalid";
  }
}

class DeviceCommands {
 public:
  DeviceCommand feed(char byte, uint64_t milliseconds) {
    if (!receiving_) {
      if (byte == '!') {
        receiving_ = true;
        length_ = 0;
        invalid_ = false;
        previous_ = milliseconds;
        return DeviceCommand::None;
      }
      if (byte == 's') return DeviceCommand::Capture;
      if (byte == 'h') return DeviceCommand::Heap;
      if (byte == 'i') return DeviceCommand::Info;
      if (byte == 'u') return DeviceCommand::UploadOpenClaw;
      return byte == '\r' || byte == '\n' ? DeviceCommand::None : DeviceCommand::Invalid;
    }
    previous_ = milliseconds;
    if (byte == '\n') {
      receiving_ = false;
      if (invalid_) return DeviceCommand::Invalid;
      text_[length_] = '\0';
      for (DeviceCommand command : {DeviceCommand::Idle, DeviceCommand::Surprise, DeviceCommand::Working,
                                     DeviceCommand::Complete, DeviceCommand::Attention}) {
        if (std::strcmp(text_, commandName(command)) == 0) return command;
      }
      return DeviceCommand::Invalid;
    }
    if (byte == '\r') return DeviceCommand::None;
    if (invalid_ || length_ == sizeof(text_) - 1 || byte < 'a' || byte > 'z') invalid_ = true;
    else text_[length_++] = byte;
    return DeviceCommand::None;
  }

  DeviceCommand expire(uint64_t milliseconds) {
    if (!receiving_ || milliseconds - previous_ < 1000) return DeviceCommand::None;
    receiving_ = false;
    return DeviceCommand::Invalid;
  }

 private:
  char text_[16] = {};
  unsigned length_ = 0;
  uint64_t previous_ = 0;
  bool receiving_ = false, invalid_ = false;
};
}
