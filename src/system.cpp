#include "system.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "linux_parser.h"

Processor& System::Cpu() { return cpu_; }

std::vector<Process>& System::Processes() {
  processes_.clear();
  for (int pid : LinuxParser::Pids()) {
    Process process(pid);
    // A process can exit between enumerating /proc and reading its files.
    if (!process.Command().empty()) {
      processes_.push_back(std::move(process));
    }
  }
  std::sort(processes_.begin(), processes_.end());
  return processes_;
}

std::string System::Kernel() { return LinuxParser::Kernel(); }

float System::MemoryUtilization() { return LinuxParser::MemoryUtilization(); }

std::string System::OperatingSystem() {
  return LinuxParser::OperatingSystem();
}

int System::RunningProcesses() { return LinuxParser::RunningProcesses(); }

int System::TotalProcesses() { return LinuxParser::TotalProcesses(); }

long int System::UpTime() { return LinuxParser::UpTime(); }
