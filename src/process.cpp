#include "process.h"

#include <unistd.h>

#include <algorithm>
#include <string>

#include "linux_parser.h"

Process::Process(int pid)
    : pid_(pid),
      user_(LinuxParser::User(pid)),
      command_(LinuxParser::Command(pid)),
      ram_(LinuxParser::Ram(pid)),
      uptime_(LinuxParser::UpTime(pid)) {
  long const ticks_per_second{sysconf(_SC_CLK_TCK)};
  if (ticks_per_second > 0 && uptime_ > 0) {
    double const cpu_seconds{
        static_cast<double>(LinuxParser::ActiveJiffies(pid)) /
        static_cast<double>(ticks_per_second)};
    cpu_utilization_ =
        static_cast<float>(cpu_seconds / static_cast<double>(uptime_));
  }
  cpu_utilization_ = std::max(0.0F, cpu_utilization_);
}

int Process::Pid() const { return pid_; }

float Process::CpuUtilization() const { return cpu_utilization_; }

std::string Process::Command() const { return command_; }

std::string Process::Ram() const { return ram_; }

std::string Process::User() const { return user_; }

long int Process::UpTime() const { return uptime_; }

bool Process::operator<(Process const& other) const {
  if (cpu_utilization_ == other.cpu_utilization_) {
    return pid_ < other.pid_;
  }
  // std::sort then places the busiest processes at the top of the display.
  return cpu_utilization_ > other.cpu_utilization_;
}
