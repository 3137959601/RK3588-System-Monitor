#include "sampler.h"

#include <stdexcept>
#include <string>
#include <utility>

Sampler::Sampler(std::unique_ptr<IMetricCollector> collector,
                 std::chrono::milliseconds interval,
                 std::unique_ptr<CsvLogger> logger)
    : collector_(std::move(collector)),
      logger_(std::move(logger)),
      interval_(interval) {
  if (!collector_) {
    throw std::invalid_argument("Sampler需要有效的采集器");
  }
  if (interval_.count() <= 0) {
    throw std::invalid_argument("采样间隔必须大于0毫秒");
  }
}

Sampler::~Sampler() { Stop(); }

void Sampler::Start() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (running_) {
    return;
  }
  if (worker_.joinable()) {
    lock.unlock();
    worker_.join();
    lock.lock();
  }
  running_ = true;
  paused_ = false;
  last_error_.reset();
  worker_ = std::thread(&Sampler::Run, this);
}

void Sampler::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    paused_ = false;
  }
  condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void Sampler::Pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    paused_ = true;
  }
}

void Sampler::Resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = false;
  }
  condition_.notify_all();
}

void Sampler::SetInterval(std::chrono::milliseconds interval) {
  if (interval.count() <= 0) {
    throw std::invalid_argument("采样间隔必须大于0毫秒");
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    interval_ = interval;
  }
  condition_.notify_all();
}

std::optional<MetricSnapshot> Sampler::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool Sampler::Running() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

bool Sampler::Paused() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return paused_;
}

std::optional<std::string> Sampler::LastError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

void Sampler::Run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (running_) {
    condition_.wait(lock, [this] { return !running_ || !paused_; });
    if (!running_) {
      break;
    }

    lock.unlock();
    MetricSnapshot next;
    try {
      next = collector_->Collect();
      if (logger_) {
        logger_->Append(next);
      }
    } catch (std::exception const& error) {
      lock.lock();
      last_error_ = error.what();
      running_ = false;
      return;
    } catch (...) {
      lock.lock();
      last_error_ = "采样线程发生未知异常";
      running_ = false;
      return;
    }
    lock.lock();
    snapshot_ = std::move(next);

    auto const interval{interval_};
    condition_.wait_for(lock, interval, [this, interval] {
      return !running_ || paused_ || interval_ != interval;
    });
  }
}
