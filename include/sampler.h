#ifndef SAMPLER_H
#define SAMPLER_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "csv_logger.h"
#include "metric_collector.h"

class Sampler {
 public:
  explicit Sampler(std::unique_ptr<IMetricCollector> collector,
                   std::chrono::milliseconds interval,
                   std::unique_ptr<CsvLogger> logger = nullptr);
  ~Sampler();

  Sampler(Sampler const&) = delete;
  Sampler& operator=(Sampler const&) = delete;

  void Start();
  void Stop();
  void Pause();
  void Resume();
  void SetInterval(std::chrono::milliseconds interval);
  std::optional<MetricSnapshot> Snapshot() const;
  bool Running() const;
  bool Paused() const;
  std::optional<std::string> LastError() const;

 private:
  void Run();

  std::unique_ptr<IMetricCollector> collector_;
  std::unique_ptr<CsvLogger> logger_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread worker_;
  std::chrono::milliseconds interval_;
  std::optional<MetricSnapshot> snapshot_;
  std::optional<std::string> last_error_;
  bool running_{false};
  bool paused_{false};
};

#endif
