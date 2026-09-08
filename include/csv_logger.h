#ifndef CSV_LOGGER_H
#define CSV_LOGGER_H

#include <filesystem>
#include <fstream>
#include <mutex>

#include "metric_snapshot.h"

class CsvLogger {
 public:
  explicit CsvLogger(std::filesystem::path path);
  void Append(MetricSnapshot const& snapshot);
  std::filesystem::path const& Path() const;

 private:
  static std::string Escape(std::string const& value);
  static std::string FormatTimestamp(
      std::chrono::system_clock::time_point timestamp);
  static std::string FormatMetrics(std::vector<NamedMetric> const& metrics);
  static bool NodeAvailable(MetricSnapshot const& snapshot,
                            std::string const& name);

  std::filesystem::path path_;
  std::ofstream stream_;
  std::mutex mutex_;
};

#endif
