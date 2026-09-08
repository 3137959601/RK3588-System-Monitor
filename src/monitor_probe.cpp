#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "csv_logger.h"
#include "metric_collector.h"
#include "sampler.h"

namespace {

struct Options {
  int count{5};
  int interval_ms{1000};
  std::optional<std::filesystem::path> csv_path;
};

int ParsePositiveInteger(std::string const& value, std::string const& name,
                         int maximum) {
  std::size_t consumed{0};
  long const parsed{std::stol(value, &consumed)};
  if (consumed != value.size() || parsed <= 0 || parsed > maximum) {
    throw std::invalid_argument(name + "超出有效范围");
  }
  return static_cast<int>(parsed);
}

Options ParseOptions(int argc, char* argv[]) {
  Options options;
  for (int index{1}; index < argc; ++index) {
    std::string const argument{argv[index]};
    if (argument == "--count" && index + 1 < argc) {
      options.count = ParsePositiveInteger(argv[++index], "count", 100000);
    } else if (argument == "--interval" && index + 1 < argc) {
      options.interval_ms =
          ParsePositiveInteger(argv[++index], "interval", 3600000);
    } else if (argument == "--csv" && index + 1 < argc) {
      options.csv_path = std::filesystem::path(argv[++index]);
    } else {
      throw std::invalid_argument(
          "用法：monitor_probe [--count 次数] [--interval 毫秒] [--csv 路径]");
    }
  }
  return options;
}

void PrintSnapshot(MetricSnapshot const& snapshot) {
  std::cout << std::fixed << std::setprecision(1) << "采样#"
            << snapshot.sequence << " CPU="
            << snapshot.cpu_utilization * 100.0F << "% 内存="
            << snapshot.memory_utilization * 100.0F << "% 运行="
            << snapshot.uptime_seconds << "s\n";

  std::cout << "  温度：";
  for (auto const& metric : snapshot.temperatures) {
    std::cout << metric.name << '=' << metric.value << "°C ";
  }
  std::cout << "\n  CPU频率：";
  for (auto const& metric : snapshot.cpu_frequencies) {
    std::cout << metric.name << '=' << metric.value << "MHz ";
  }
  std::cout << "\n  GPU/NPU：GPU=";
  if (snapshot.gpu_frequency_mhz) {
    std::cout << *snapshot.gpu_frequency_mhz << "MHz";
  } else {
    std::cout << "不可用";
  }
  std::cout << " NPU=";
  if (snapshot.npu_frequency_mhz) {
    std::cout << *snapshot.npu_frequency_mhz << "MHz";
  } else {
    std::cout << "不可用";
  }
  std::cout << "\n  节点：";
  for (auto const& node : snapshot.nodes) {
    std::cout << node.name << '=' << (node.available ? "可用" : "缺失") << ' ';
  }
  std::cout << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    Options const options{ParseOptions(argc, argv)};
    std::unique_ptr<CsvLogger> logger;
    if (options.csv_path) {
      logger = std::make_unique<CsvLogger>(*options.csv_path);
    }
    Sampler sampler(std::make_unique<Rk3588MetricCollector>(),
                    std::chrono::milliseconds(options.interval_ms),
                    std::move(logger));
    sampler.Start();

    std::uint64_t last_sequence{0};
    int printed{0};
    while (printed < options.count) {
      if (auto const error = sampler.LastError()) {
        throw std::runtime_error(*error);
      }
      auto const snapshot{sampler.Snapshot()};
      if (snapshot && snapshot->sequence != last_sequence) {
        PrintSnapshot(*snapshot);
        last_sequence = snapshot->sequence;
        ++printed;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    sampler.Stop();
    return EXIT_SUCCESS;
  } catch (std::exception const& error) {
    std::cerr << "monitor_probe：" << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
