#include "metric_collector.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "linux_parser.h"

namespace {

std::optional<double> ReadNumber(std::filesystem::path const& path) {
  std::ifstream stream(path);
  double value{0.0};
  if (!(stream >> value)) {
    return std::nullopt;
  }
  return value;
}

std::string ReadText(std::filesystem::path const& path) {
  std::ifstream stream(path);
  std::string value;
  std::getline(stream, value);
  return value;
}

std::vector<std::filesystem::path> SortedDirectories(
    std::filesystem::path const& root, std::string const& prefix) {
  std::vector<std::filesystem::path> result;
  std::error_code error;
  for (std::filesystem::directory_iterator it(root, error), end;
       !error && it != end; it.increment(error)) {
    std::string const name{it->path().filename().string()};
    if (name.rfind(prefix, 0) == 0) {
      result.push_back(it->path());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::optional<double> FindDevfreqMhz(std::filesystem::path const& root,
                                     std::string const& token) {
  std::error_code error;
  for (std::filesystem::directory_iterator it(root, error), end;
       !error && it != end; it.increment(error)) {
    std::string name{ReadText(it->path() / "name")};
    if (name.empty()) {
      name = it->path().filename().string();
    }
    if (name.find(token) == std::string::npos) {
      continue;
    }
    auto const frequency{ReadNumber(it->path() / "cur_freq")};
    if (frequency) {
      return *frequency / 1000000.0;
    }
  }
  return std::nullopt;
}

bool Exists(std::filesystem::path const& path) {
  std::error_code error;
  return std::filesystem::exists(path, error);
}

}  // namespace

MetricSnapshot LinuxMetricCollector::CollectLinuxMetrics() {
  MetricSnapshot snapshot;
  snapshot.timestamp = std::chrono::system_clock::now();
  snapshot.sequence = ++sequence_;
  snapshot.cpu_utilization = cpu_.Utilization();
  snapshot.memory_utilization = LinuxParser::MemoryUtilization();
  snapshot.uptime_seconds = LinuxParser::UpTime();
  return snapshot;
}

MetricSnapshot LinuxMetricCollector::Collect() { return CollectLinuxMetrics(); }

Rk3588MetricCollector::Rk3588MetricCollector(std::filesystem::path sys_root,
                                             std::filesystem::path dev_root)
    : sys_root_(std::move(sys_root)), dev_root_(std::move(dev_root)) {}

MetricSnapshot Rk3588MetricCollector::Collect() {
  MetricSnapshot snapshot{CollectLinuxMetrics()};

  auto const thermal_root{sys_root_ / "class/thermal"};
  for (auto const& zone : SortedDirectories(thermal_root, "thermal_zone")) {
    auto const millidegrees{ReadNumber(zone / "temp")};
    if (!millidegrees) {
      continue;
    }
    std::string name{ReadText(zone / "type")};
    if (name.empty()) {
      name = zone.filename().string();
    }
    snapshot.temperatures.push_back({name, *millidegrees / 1000.0, "C"});
  }

  auto const cpufreq_root{sys_root_ / "devices/system/cpu/cpufreq"};
  for (auto const& policy : SortedDirectories(cpufreq_root, "policy")) {
    auto const kilohertz{ReadNumber(policy / "scaling_cur_freq")};
    if (kilohertz) {
      snapshot.cpu_frequencies.push_back(
          {policy.filename().string(), *kilohertz / 1000.0, "MHz"});
    }
  }

  auto const devfreq_root{sys_root_ / "class/devfreq"};
  snapshot.gpu_frequency_mhz = FindDevfreqMhz(devfreq_root, "gpu");
  snapshot.npu_frequency_mhz = FindDevfreqMhz(devfreq_root, "npu");

  snapshot.nodes = {
      {"mali", (dev_root_ / "mali0").string(), Exists(dev_root_ / "mali0")},
      {"npu", (devfreq_root / "fdab0000.npu").string(),
       snapshot.npu_frequency_mhz.has_value()},
      {"mpp", (dev_root_ / "mpp_service").string(),
       Exists(dev_root_ / "mpp_service")},
      {"rga", (dev_root_ / "rga").string(), Exists(dev_root_ / "rga")},
  };
  return snapshot;
}
