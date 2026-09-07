#include "processor.h"

#include <algorithm>

#include "linux_parser.h"

float Processor::Utilization() {
  long const active{LinuxParser::ActiveJiffies()};
  long const idle{LinuxParser::IdleJiffies()};

  if (!initialized_) {
    previous_active_ = active;
    previous_idle_ = idle;
    initialized_ = true;
    long const total{active + idle};
    return total > 0
               ? std::clamp(static_cast<float>(active) /
                                static_cast<float>(total),
                            0.0F, 1.0F)
               : 0.0F;
  }

  long const active_delta{active - previous_active_};
  long const idle_delta{idle - previous_idle_};
  previous_active_ = active;
  previous_idle_ = idle;

  long const total_delta{active_delta + idle_delta};
  if (active_delta < 0 || idle_delta < 0 || total_delta <= 0) {
    return 0.0F;
  }
  return std::clamp(static_cast<float>(active_delta) /
                        static_cast<float>(total_delta),
                    0.0F, 1.0F);
}
