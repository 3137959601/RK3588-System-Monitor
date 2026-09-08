#include "csv_logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string OptionalNumber(std::optional<double> const& value) {
  if (!value) {
    return {};
  }
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << *value;
  return stream.str();
}

}  // namespace

CsvLogger::CsvLogger(std::filesystem::path path) : path_(std::move(path)) {
  std::error_code error;
  bool const write_header =
      !std::filesystem::exists(path_, error) ||
      std::filesystem::file_size(path_, error) == 0;
  stream_.open(path_, std::ios::app);
  if (!stream_) {
    throw std::runtime_error("无法打开CSV日志：" + path_.string());
  }
  if (write_header) {
    stream_ << "timestamp_utc,sequence,cpu_percent,memory_percent,"
               "uptime_seconds,temperatures_c,cpu_frequencies_mhz,"
               "gpu_frequency_mhz,npu_frequency_mhz,mali_available,"
               "npu_available,mpp_available,rga_available\n";
    stream_.flush();
  }
}

void CsvLogger::Append(MetricSnapshot const& snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_ << Escape(FormatTimestamp(snapshot.timestamp)) << ','
          << snapshot.sequence << ',' << std::fixed << std::setprecision(3)
          << snapshot.cpu_utilization * 100.0F << ','
          << snapshot.memory_utilization * 100.0F << ','
          << snapshot.uptime_seconds << ','
          << Escape(FormatMetrics(snapshot.temperatures)) << ','
          << Escape(FormatMetrics(snapshot.cpu_frequencies)) << ','
          << OptionalNumber(snapshot.gpu_frequency_mhz) << ','
          << OptionalNumber(snapshot.npu_frequency_mhz) << ','
          << NodeAvailable(snapshot, "mali") << ','
          << NodeAvailable(snapshot, "npu") << ','
          << NodeAvailable(snapshot, "mpp") << ','
          << NodeAvailable(snapshot, "rga") << '\n';
  stream_.flush();
  if (!stream_) {
    throw std::runtime_error("写入CSV日志失败：" + path_.string());
  }
}

std::filesystem::path const& CsvLogger::Path() const { return path_; }

std::string CsvLogger::Escape(std::string const& value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (char character : value) {
    if (character == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

std::string CsvLogger::FormatTimestamp(
    std::chrono::system_clock::time_point timestamp) {
  std::time_t const time{std::chrono::system_clock::to_time_t(timestamp)};
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

std::string CsvLogger::FormatMetrics(
    std::vector<NamedMetric> const& metrics) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < metrics.size(); ++index) {
    if (index > 0) {
      stream << ';';
    }
    stream << metrics[index].name << '=' << std::fixed << std::setprecision(3)
           << metrics[index].value;
  }
  return stream.str();
}

bool CsvLogger::NodeAvailable(MetricSnapshot const& snapshot,
                              std::string const& name) {
  for (auto const& node : snapshot.nodes) {
    if (node.name == name) {
      return node.available;
    }
  }
  return false;
}
