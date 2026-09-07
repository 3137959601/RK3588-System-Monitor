#include "linux_parser.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <unistd.h>

#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

long CpuState(std::vector<std::string> const& states, std::size_t index) {
  if (index >= states.size()) {
    return 0;
  }
  try {
    return std::stol(states[index]);
  } catch (...) {
    return 0;
  }
}

long ReadProcStatCounter(std::string const& wanted_key) {
  std::ifstream stream(LinuxParser::kProcDirectory +
                       LinuxParser::kStatFilename);
  std::string key;
  long value{0};
  while (stream >> key >> value) {
    if (key == wanted_key) {
      return value;
    }
    stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return 0;
}

bool ReadProcessStat(int pid, long& active_jiffies, long& start_jiffies) {
  std::ifstream stream(LinuxParser::kProcDirectory + std::to_string(pid) +
                       LinuxParser::kStatFilename);
  std::string line;
  if (!std::getline(stream, line)) {
    return false;
  }

  // Field 2 (comm) is enclosed in parentheses and may contain spaces or ')'.
  // Parsing after the final ')' keeps all subsequent field numbers stable.
  std::size_t const closing_paren{line.rfind(')')};
  if (closing_paren == std::string::npos || closing_paren + 2 >= line.size()) {
    return false;
  }

  std::istringstream fields(line.substr(closing_paren + 2));
  std::string token;
  long utime{0};
  long stime{0};
  long cutime{0};
  long cstime{0};
  start_jiffies = 0;

  try {
    for (int field = 3; field <= 22; ++field) {
      if (!(fields >> token)) {
        return false;
      }
      switch (field) {
        case 14:
          utime = std::stol(token);
          break;
        case 15:
          stime = std::stol(token);
          break;
        case 16:
          cutime = std::stol(token);
          break;
        case 17:
          cstime = std::stol(token);
          break;
        case 22:
          start_jiffies = std::stol(token);
          break;
        default:
          break;
      }
    }
  } catch (...) {
    return false;
  }

  active_jiffies = utime + stime + cutime + cstime;
  return true;
}

void TrimTrailingWhitespace(std::string& value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
}

}  // namespace

std::string LinuxParser::OperatingSystem() {
  std::ifstream stream(kOSPath);
  std::string line;
  while (std::getline(stream, line)) {
    constexpr char kKey[] = "PRETTY_NAME=";
    if (line.rfind(kKey, 0) != 0) {
      continue;
    }
    std::string value{line.substr(sizeof(kKey) - 1)};
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    return value;
  }
  return "Unknown";
}

std::string LinuxParser::Kernel() {
  std::ifstream stream(kProcDirectory + kVersionFilename);
  std::string os;
  std::string version;
  std::string kernel;
  if (stream >> os >> version >> kernel) {
    return kernel;
  }
  return "Unknown";
}

std::vector<int> LinuxParser::Pids() {
  std::vector<int> pids;
  DIR* directory{opendir(kProcDirectory.c_str())};
  if (directory == nullptr) {
    return pids;
  }

  while (dirent* entry = readdir(directory)) {
    std::string const name(entry->d_name);
    bool const numeric{!name.empty() &&
                       std::all_of(name.begin(), name.end(), [](char ch) {
                         return std::isdigit(static_cast<unsigned char>(ch));
                       })};
    if (!numeric) {
      continue;
    }
    try {
      pids.push_back(std::stoi(name));
    } catch (...) {
      // A malformed or out-of-range entry should not abort a full snapshot.
    }
  }
  closedir(directory);
  std::sort(pids.begin(), pids.end());
  return pids;
}

float LinuxParser::MemoryUtilization() {
  std::ifstream stream(kProcDirectory + kMeminfoFilename);
  std::string line;
  std::string key;
  long value{0};
  long total{0};
  long available{-1};
  long free{0};
  long buffers{0};
  long cached{0};

  while (std::getline(stream, line)) {
    std::istringstream fields(line);
    if (!(fields >> key >> value)) {
      continue;
    }
    if (key == "MemTotal:") {
      total = value;
    } else if (key == "MemAvailable:") {
      available = value;
    } else if (key == "MemFree:") {
      free = value;
    } else if (key == "Buffers:") {
      buffers = value;
    } else if (key == "Cached:") {
      cached = value;
    }
  }

  if (total <= 0) {
    return 0.0F;
  }
  if (available < 0) {
    available = free + buffers + cached;
  }
  float const used{static_cast<float>(total - available) /
                   static_cast<float>(total)};
  return std::clamp(used, 0.0F, 1.0F);
}

long LinuxParser::UpTime() {
  std::ifstream stream(kProcDirectory + kUptimeFilename);
  double seconds{0.0};
  stream >> seconds;
  return seconds > 0.0 ? static_cast<long>(seconds) : 0;
}

std::vector<std::string> LinuxParser::CpuUtilization() {
  std::ifstream stream(kProcDirectory + kStatFilename);
  std::string line;
  if (!std::getline(stream, line)) {
    return {};
  }
  std::istringstream fields(line);
  std::string label;
  fields >> label;
  if (label != "cpu") {
    return {};
  }

  std::vector<std::string> states;
  std::string value;
  while (fields >> value) {
    states.push_back(value);
  }
  return states;
}

long LinuxParser::ActiveJiffies() {
  std::vector<std::string> const states{CpuUtilization()};
  return CpuState(states, kUser_) + CpuState(states, kNice_) +
         CpuState(states, kSystem_) + CpuState(states, kIRQ_) +
         CpuState(states, kSoftIRQ_) + CpuState(states, kSteal_);
}

long LinuxParser::IdleJiffies() {
  std::vector<std::string> const states{CpuUtilization()};
  return CpuState(states, kIdle_) + CpuState(states, kIOwait_);
}

long LinuxParser::Jiffies() {
  std::vector<std::string> const states{CpuUtilization()};
  long const active{CpuState(states, kUser_) + CpuState(states, kNice_) +
                    CpuState(states, kSystem_) + CpuState(states, kIRQ_) +
                    CpuState(states, kSoftIRQ_) + CpuState(states, kSteal_)};
  long const idle{CpuState(states, kIdle_) + CpuState(states, kIOwait_)};
  return active + idle;
}

long LinuxParser::ActiveJiffies(int pid) {
  long active{0};
  long start{0};
  return ReadProcessStat(pid, active, start) ? active : 0;
}

int LinuxParser::TotalProcesses() { return ReadProcStatCounter("processes"); }

int LinuxParser::RunningProcesses() {
  return ReadProcStatCounter("procs_running");
}

std::string LinuxParser::Command(int pid) {
  std::ifstream stream(kProcDirectory + std::to_string(pid) +
                           kCmdlineFilename,
                       std::ios::binary);
  std::string command((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  std::replace(command.begin(), command.end(), '\0', ' ');
  TrimTrailingWhitespace(command);
  if (!command.empty()) {
    return command;
  }

  std::ifstream comm_stream(kProcDirectory + std::to_string(pid) +
                            kCommFilename);
  std::getline(comm_stream, command);
  TrimTrailingWhitespace(command);
  return command;
}

std::string LinuxParser::Ram(int pid) {
  std::ifstream stream(kProcDirectory + std::to_string(pid) +
                       kStatusFilename);
  std::string key;
  long kilobytes{0};
  std::string unit;
  while (stream >> key) {
    if (key == "VmRSS:") {
      stream >> kilobytes >> unit;
      std::ostringstream formatted;
      formatted << std::fixed << std::setprecision(1)
                << static_cast<double>(kilobytes) / 1024.0;
      return formatted.str();
    }
    stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return "0.0";
}

std::string LinuxParser::Uid(int pid) {
  std::ifstream stream(kProcDirectory + std::to_string(pid) +
                       kStatusFilename);
  std::string key;
  std::string uid;
  while (stream >> key) {
    if (key == "Uid:") {
      stream >> uid;
      return uid;
    }
    stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return {};
}

std::string LinuxParser::User(int pid) {
  std::string const wanted_uid{Uid(pid)};
  if (wanted_uid.empty()) {
    return {};
  }

  std::ifstream stream(kPasswordPath);
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream fields(line);
    std::string name;
    std::string password;
    std::string uid;
    std::getline(fields, name, ':');
    std::getline(fields, password, ':');
    std::getline(fields, uid, ':');
    if (uid == wanted_uid) {
      return name;
    }
  }
  return wanted_uid;
}

long LinuxParser::UpTime(int pid) {
  long active{0};
  long start_jiffies{0};
  if (!ReadProcessStat(pid, active, start_jiffies)) {
    return 0;
  }
  long const ticks_per_second{sysconf(_SC_CLK_TCK)};
  if (ticks_per_second <= 0) {
    return 0;
  }
  long const age{UpTime() - start_jiffies / ticks_per_second};
  return std::max(0L, age);
}
