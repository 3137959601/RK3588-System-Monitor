#include "format.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

std::string Format::ElapsedTime(long seconds) {
  seconds = std::max(0L, seconds);
  long const hours{seconds / 3600};
  long const minutes{(seconds % 3600) / 60};
  long const remaining_seconds{seconds % 60};

  std::ostringstream formatted;
  formatted << std::setfill('0') << std::setw(2) << hours << ':'
            << std::setw(2) << minutes << ':' << std::setw(2)
            << remaining_seconds;
  return formatted.str();
}
