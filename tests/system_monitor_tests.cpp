#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "csv_logger.h"
#include "format.h"
#include "linux_parser.h"
#include "metric_collector.h"
#include "process.h"
#include "processor.h"
#include "sampler.h"
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

void WriteFile(std::filesystem::path const& path, std::string const& value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << value;
}

bool HasNode(MetricSnapshot const& snapshot, std::string const& name,
             bool available) {
  return std::any_of(snapshot.nodes.begin(), snapshot.nodes.end(),
                     [&name, available](NodeStatus const& node) {
                       return node.name == name && node.available == available;
                     });
}

class CountingCollector : public IMetricCollector {
 public:
  MetricSnapshot Collect() override {
    MetricSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.sequence = ++sequence_;
    snapshot.cpu_utilization = 0.25F;
    snapshot.memory_utilization = 0.5F;
    snapshot.uptime_seconds = 10;
    return snapshot;
  }

 private:
  std::uint64_t sequence_{0};
};

class ThrowingCollector : public IMetricCollector {
 public:
  MetricSnapshot Collect() override { throw std::runtime_error("模拟采集失败"); }
};

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

  std::filesystem::path const fixture{
      std::filesystem::temp_directory_path() /
      ("rk3588-monitor-test-" + std::to_string(self_pid))};
  std::filesystem::remove_all(fixture);
  WriteFile(fixture / "sys/class/thermal/thermal_zone0/type", "soc-thermal\n");
  WriteFile(fixture / "sys/class/thermal/thermal_zone0/temp", "42500\n");
  WriteFile(fixture / "sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq",
            "1296000\n");
  WriteFile(fixture / "sys/class/devfreq/fb000000.gpu/name",
            "fb000000.gpu\n");
  WriteFile(fixture / "sys/class/devfreq/fb000000.gpu/cur_freq",
            "300000000\n");
  WriteFile(fixture / "sys/class/devfreq/fdab0000.npu/name",
            "fdab0000.npu\n");
  WriteFile(fixture / "sys/class/devfreq/fdab0000.npu/cur_freq",
            "950000000\n");
  WriteFile(fixture / "dev/mali0", "");
  WriteFile(fixture / "dev/mpp_service", "");
  WriteFile(fixture / "dev/rga", "");

  Rk3588MetricCollector rk_collector(fixture / "sys", fixture / "dev");
  MetricSnapshot const rk_snapshot{rk_collector.Collect()};
  Check(rk_snapshot.sequence == 1, "RK3588采集快照序号递增");
  Check(rk_snapshot.temperatures.size() == 1 &&
            rk_snapshot.temperatures.front().value == 42.5,
        "RK3588温区毫摄氏度转换为摄氏度");
  Check(rk_snapshot.cpu_frequencies.size() == 1 &&
            rk_snapshot.cpu_frequencies.front().value == 1296.0,
        "CPU频率kHz转换为MHz");
  Check(rk_snapshot.gpu_frequency_mhz == 300.0,
        "GPU devfreq Hz转换为MHz");
  Check(rk_snapshot.npu_frequency_mhz == 950.0,
        "NPU devfreq Hz转换为MHz");
  Check(HasNode(rk_snapshot, "mali", true) &&
            HasNode(rk_snapshot, "npu", true) &&
            HasNode(rk_snapshot, "mpp", true) &&
            HasNode(rk_snapshot, "rga", true),
        "识别Mali/NPU/MPP/RGA可用节点");

  std::filesystem::path const csv_path{fixture / "metrics.csv"};
  {
    CsvLogger logger(csv_path);
    logger.Append(rk_snapshot);
  }
  std::ifstream csv_stream(csv_path);
  std::string csv_content((std::istreambuf_iterator<char>(csv_stream)),
                          std::istreambuf_iterator<char>());
  Check(csv_content.find("timestamp_utc,sequence,cpu_percent") == 0,
        "CSV首次写入包含固定表头");
  Check(csv_content.find("soc-thermal=42.500") != std::string::npos &&
            csv_content.find(",1,1,1,1") != std::string::npos,
        "CSV写入板级指标与节点状态");

  Sampler sampler(std::make_unique<CountingCollector>(),
                  std::chrono::milliseconds(20));
  sampler.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(75));
  auto first_sample{sampler.Snapshot()};
  Check(first_sample && first_sample->sequence >= 2,
        "后台Sampler周期更新线程安全快照");
  sampler.Pause();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  auto paused_sample{sampler.Snapshot()};
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto paused_sample_later{sampler.Snapshot()};
  Check(sampler.Paused() && paused_sample && paused_sample_later &&
            paused_sample_later->sequence == paused_sample->sequence,
        "Sampler暂停后快照保持不变");
  sampler.SetInterval(std::chrono::milliseconds(10));
  sampler.Resume();
  std::this_thread::sleep_for(std::chrono::milliseconds(35));
  auto resumed_sample{sampler.Snapshot()};
  Check(resumed_sample && paused_sample_later &&
            resumed_sample->sequence > paused_sample->sequence,
        "Sampler恢复并应用新的采样间隔");
  sampler.Stop();
  Check(!sampler.Running(), "Sampler安全停止并回收后台线程");

  Sampler failing_sampler(std::make_unique<ThrowingCollector>(),
                          std::chrono::milliseconds(10));
  failing_sampler.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  Check(!failing_sampler.Running() && failing_sampler.LastError() &&
            *failing_sampler.LastError() == "模拟采集失败",
        "Sampler保存后台异常且不会终止整个进程");
  failing_sampler.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  Check(!failing_sampler.Running() && failing_sampler.LastError(),
        "Sampler可回收失败线程并重新启动");
  failing_sampler.Stop();

  std::filesystem::remove_all(fixture);

  if (failures == 0) {
    std::cout << "全部测试通过" << '\n';
    return 0;
  }
  std::cerr << failures << "项测试失败" << '\n';
  return 1;
}
