#ifndef PROCESS_H
#define PROCESS_H

#include <string>
/*
Basic class for Process representation
It contains relevant attributes as shown below
*/
class Process {
 public:
  explicit Process(int pid);

  int Pid() const;
  std::string User() const;
  std::string Command() const;
  float CpuUtilization() const;
  std::string Ram() const;
  long int UpTime() const;
  bool operator<(Process const& other) const;

 private:
  int pid_{0};
  std::string user_{};
  std::string command_{};
  float cpu_utilization_{0.0F};
  std::string ram_{};
  long int uptime_{0};
};

#endif
