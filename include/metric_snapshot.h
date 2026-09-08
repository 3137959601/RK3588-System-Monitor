#ifndef METRIC_SNAPSHOT_H
#define METRIC_SNAPSHOT_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct NamedMetric {
  std::string name;
  double value{0.0};
  std::string unit;
};

struct NodeStatus {
  std::string name;
  std::string path;
  bool available{false};
};

struct MetricSnapshot {
  std::chrono::system_clock::time_point timestamp{};
  std::uint64_t sequence{0};
  float cpu_utilization{0.0F};
  float memory_utilization{0.0F};
  long uptime_seconds{0};
  std::vector<NamedMetric> temperatures;
  std::vector<NamedMetric> cpu_frequencies;
  std::optional<double> gpu_frequency_mhz;
  std::optional<double> npu_frequency_mhz;
  std::vector<NodeStatus> nodes;
};

#endif
