#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "format.h"
#include "linux_parser.h"
#include "process.h"
#include "processor.h"
#include "system.h"

namespace {

int failures{0};

void Check(bool condition, std::string const& message) {
  if (condition) {
    std::cout << "[通过] " << message << '\n';
    return;
  }
  std::cerr << "[失败] " << message << '\n';
  ++failures;
}

bool InUnitRange(float value) { return value >= 0.0F && value <= 1.0F; }

}  // namespace

int main() {
  Check(Format::ElapsedTime(-1) == "00:00:00", "负时间按零处理");
  Check(Format::ElapsedTime(0) == "00:00:00", "零秒格式化");
  Check(Format::ElapsedTime(59) == "00:00:59", "秒格式化");
  Check(Format::ElapsedTime(60) == "00:01:00", "分钟进位");
  Check(Format::ElapsedTime(3661) == "01:01:01", "时分秒格式化");
  Check(Format::ElapsedTime(90061) == "25:01:01", "运行时间可超过24小时");

  Check(LinuxParser::OperatingSystem() != "Unknown", "读取操作系统名称");
  Check(LinuxParser::Kernel() != "Unknown", "读取内核版本");
  Check(LinuxParser::UpTime() > 0, "读取系统运行时间");
  Check(InUnitRange(LinuxParser::MemoryUtilization()), "内存利用率在[0,1]");

  std::vector<std::string> const cpu_states{LinuxParser::CpuUtilization()};
  Check(cpu_states.size() >= 8, "读取CPU累计状态字段");
  Check(LinuxParser::Jiffies() > 0, "系统jiffies为正数");
  Check(LinuxParser::ActiveJiffies() >= 0, "活动jiffies非负");
  Check(LinuxParser::IdleJiffies() >= 0, "空闲jiffies非负");
  Check(LinuxParser::TotalProcesses() > 0, "累计进程数为正数");
  Check(LinuxParser::RunningProcesses() >= 0, "运行进程数非负");

  int const self_pid{static_cast<int>(getpid())};
  std::vector<int> const pids{LinuxParser::Pids()};
  Check(std::find(pids.begin(), pids.end(), self_pid) != pids.end(),
        "PID枚举包含测试进程自身");
  Check(!LinuxParser::Command(self_pid).empty(), "读取自身命令行");
  Check(!LinuxParser::Uid(self_pid).empty(), "读取自身UID");
  Check(!LinuxParser::User(self_pid).empty(), "将自身UID映射为用户名");
  Check(!LinuxParser::Ram(self_pid).empty(), "读取自身常驻内存");
  Check(LinuxParser::UpTime(self_pid) >= 0, "读取自身进程运行时间");
  Check(LinuxParser::ActiveJiffies(self_pid) >= 0, "读取自身活动jiffies");
  Check(LinuxParser::Command(self_pid).find('\0') == std::string::npos,
        "命令行中的NUL分隔符已转换");

  int constexpr missing_pid{2147483647};
  Check(LinuxParser::Command(missing_pid).empty(), "消失进程命令返回空值");
  Check(LinuxParser::Uid(missing_pid).empty(), "消失进程UID返回空值");
  Check(LinuxParser::User(missing_pid).empty(), "消失进程用户返回空值");
  Check(LinuxParser::Ram(missing_pid) == "0.0", "消失进程内存返回零");
  Check(LinuxParser::UpTime(missing_pid) == 0, "消失进程运行时间返回零");
  Check(LinuxParser::ActiveJiffies(missing_pid) == 0,
        "消失进程活动jiffies返回零");

  Process self(self_pid);
  Check(self.Pid() == self_pid, "Process保存PID快照");
  Check(!self.Command().empty(), "Process保存命令快照");
  Check(self.CpuUtilization() >= 0.0F, "Process CPU利用率非负");

  Processor processor;
  Check(InUnitRange(processor.Utilization()), "首次CPU利用率在[0,1]");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Check(InUnitRange(processor.Utilization()), "差分CPU利用率在[0,1]");

  System system;
  Check(system.OperatingSystem() != "Unknown", "System转发操作系统信息");
  Check(system.Kernel() != "Unknown", "System转发内核信息");
  Check(InUnitRange(system.MemoryUtilization()), "System转发内存利用率");
  Check(system.UpTime() > 0, "System转发运行时间");
  std::vector<Process>& processes{system.Processes()};
  Check(!processes.empty(), "System生成进程快照");
  Check(std::is_sorted(processes.begin(), processes.end()),
        "进程按CPU占用从高到低排序");

  if (failures == 0) {
    std::cout << "全部测试通过" << '\n';
    return 0;
  }
  std::cerr << failures << "项测试失败" << '\n';
  return 1;
}
