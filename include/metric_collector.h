#ifndef METRIC_COLLECTOR_H
#define METRIC_COLLECTOR_H

#include <cstdint>
#include <filesystem>

#include "metric_snapshot.h"
#include "processor.h"

class IMetricCollector {
 public:
  virtual ~IMetricCollector() = default;
  virtual MetricSnapshot Collect() = 0;
};

class LinuxMetricCollector : public IMetricCollector {
 public:
  MetricSnapshot Collect() override;

 protected:
  MetricSnapshot CollectLinuxMetrics();

 private:
  Processor cpu_{};
  std::uint64_t sequence_{0};
};

class Rk3588MetricCollector : public LinuxMetricCollector {
 public:
  explicit Rk3588MetricCollector(
      std::filesystem::path sys_root = "/sys",
      std::filesystem::path dev_root = "/dev");
  MetricSnapshot Collect() override;

 private:
  std::filesystem::path sys_root_;
  std::filesystem::path dev_root_;
};

#endif
