# RK3588 指标采集与后台 Sampler 设计记录

## 1. 本阶段范围与状态

本阶段一次完成第 2 天的核心代码类别：

- `MetricSnapshot`：统一一次采样的数据模型；
- `IMetricCollector`：隔离数据来源的接口；
- `LinuxMetricCollector`：复用通用 `/proc` 指标；
- `Rk3588MetricCollector`：读取RK3588温区、CPU/GPU/NPU频率及Mali/MPP/RGA节点；
- `Sampler`：后台线程、暂停/恢复、动态间隔、线程安全快照和异常收敛；
- `CsvLogger`：把快照持续保存为稳定列结构的CSV；
- 伪sysfs与伪设备节点测试、Sampler时序测试和异常恢复测试。

Ubuntu Debug构建、55项检查、CTest及ASan/UBSan已通过。源码已同步到Windows审阅工作副本，但本批尚未提交，也未部署到RK3588；板端仍保持只测试已提交版本的边界。

## 2. 为什么需要这组分层

如果直接把所有 `/proc`、`/sys` 读取和线程代码写进ncurses界面，会产生三个问题：界面刷新频率绑死硬件采样频率、离开RK3588后无法测试、后续Unix Socket和无界面模式只能重复实现采集逻辑。

当前依赖方向为：

```text
LinuxParser / Processor
          ↓
LinuxMetricCollector ──→ MetricSnapshot
          ↑                     ↓
Rk3588MetricCollector         Sampler ──→ CsvLogger
                                  ↓
                      后续ncurses / monitorctl / UDS
```

关键设计理由：

1. `MetricSnapshot`是值对象。消费者拿到的是某一时刻的完整副本，不会在显示一半时被采样线程改写。
2. `IMetricCollector`只约束`Collect()`。单元测试可以替换为计数或抛异常的假采集器，不依赖真实开发板。
3. 通用Linux和RK3588扩展分层。x86_64只采CPU、内存和运行时间；RK3588再增加sysfs与设备节点，不在通用解析器里散布板卡判断。
4. sysfs和`/dev`根路径可注入。测试使用临时目录模拟内核文件，既能验证单位转换和缺失节点，又不会修改真实系统。
5. `Sampler`持有唯一采集器并只在后台线程调用它；互斥锁只保护状态和快照，执行文件I/O时不占锁，避免阻塞读取者。
6. CSV由独立类负责，避免采集器同时承担“读硬件”和“决定输出格式”两种职责。

## 3. RK3588指标来源与单位

### 温度

```text
/sys/class/thermal/thermal_zone*/type
/sys/class/thermal/thermal_zone*/temp
```

`temp`通常以毫摄氏度表示，因此代码除以1000转成摄氏度。实际板卡已发现：

```text
soc-thermal
bigcore0-thermal
bigcore1-thermal
littlecore-thermal
center-thermal
gpu-thermal
npu-thermal
```

不能按`thermal_zone0`永久等同某一部件，所以名称从同目录`type`读取。

### CPU频率

```text
/sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq
```

值以kHz表示，除以1000得到MHz。板端当前有`policy0`、`policy4`、`policy6`，它们对应频率策略域，而不是简单等同8个逻辑CPU各一个文件。

### GPU与NPU频率

```text
/sys/class/devfreq/*/name
/sys/class/devfreq/*/cur_freq
```

采集器读取`name`并查找`gpu`或`npu`，而不是只依赖目录枚举顺序。`cur_freq`以Hz表示，除以1,000,000得到MHz。实机当前值曾观测为GPU 300 MHz、NPU 950 MHz；这是瞬时值，不是固定性能结论。

### 加速设备可用性

```text
/dev/mali0
/sys/class/devfreq/fdab0000.npu
/dev/mpp_service
/dev/rga
```

“节点存在”只说明驱动向用户态暴露了入口，不等于当前正在运行负载，也不证明SDK、权限和算法调用已经成功。后续性能阶段仍需分别执行真实GPU/NPU/MPP/RGA工作负载。

## 4. Sampler并发模型

`Start()`创建一个工作线程并立即采第一帧，此后按`interval`等待。公开方法使用同一互斥锁保护：

- `Snapshot()`复制最新快照；
- `Pause()`停止发起新采样，允许已经开始的一次采样正常收尾；
- `Resume()`唤醒条件变量；
- `SetInterval()`检查间隔大于0并唤醒工作线程，使新周期及时生效；
- `Stop()`修改状态、唤醒并`join`，保证对象析构时没有悬空线程。

采集和CSV文件I/O发生在解锁区间。这是为了不让较慢的sysfs或磁盘写入阻塞UI读取快照。代价是`Pause()`不是取消正在进行的系统调用，而是“当前采样完成后暂停”；这一语义可预测，也比强行中止线程安全。

后台异常不能越过线程入口，否则C++会调用`std::terminate`终止整个程序。`Run()`因此捕获标准异常和未知异常，保存到`LastError()`并停止采样。还处理了“失败线程已经退出但`std::thread`仍为joinable”的情况：再次`Start()`前先回收旧线程，避免给joinable线程重新赋值触发终止。

## 5. CSV格式

固定列为：

```text
timestamp_utc,sequence,cpu_percent,memory_percent,uptime_seconds,
temperatures_c,cpu_frequencies_mhz,gpu_frequency_mhz,npu_frequency_mhz,
mali_available,npu_available,mpp_available,rga_available
```

温区和CPU策略数量可能因设备树、内核或在线CPU变化，所以这两组采用单元格内的`name=value;name=value`形式；核心列保持稳定，便于Excel/Python读取。文件为空时才写表头，追加每条记录后立即`flush`，降低异常退出时丢失整段缓存的风险。字段统一进行CSV双引号转义。

## 6. 修改文件

```text
CMakeLists.txt
include/metric_snapshot.h
include/metric_collector.h
include/csv_logger.h
include/sampler.h
src/metric_collector.cpp
src/csv_logger.cpp
src/sampler.cpp
tests/system_monitor_tests.cpp
docs/04_RK3588指标采集与后台Sampler设计记录.md
```

CMake新增`Threads::Threads`。配置输出中：

```text
Performing Test CMAKE_HAVE_LIBC_PTHREAD - Failed
Looking for pthread_create in pthread - found
Found Threads: TRUE
```

第一行不是最终构建失败，而是CMake依次探测线程库链接方式；随后找到`libpthread`且`Threads_FOUND=TRUE`，目标成功链接。

## 7. 实际构建与验证命令

Ubuntu主仓库：

```bash
cd /home/d508/projects/RK3588_System_Monitor
cmake -S . -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-debug --parallel 8
cd build-debug
ctest --output-on-failure
./monitor_tests
```

Sanitizer构建：

```bash
cd /home/d508/projects/RK3588_System_Monitor
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan --parallel 8
ASAN_OPTIONS=detect_leaks=1 \
UBSAN_OPTIONS=print_stacktrace=1 \
./build-asan/monitor_tests
git diff --check
```

结果：Debug和Sanitizer版本均构建成功；55项检查全部通过；CTest 1/1通过；没有AddressSanitizer、UndefinedBehaviorSanitizer或diff空白错误。

新增测试覆盖：

- 毫摄氏度、kHz、Hz的单位转换；
- Mali/NPU/MPP/RGA节点识别；
- CSV表头、板级数据和状态输出；
- 后台周期快照；
- 暂停、恢复和动态采样间隔；
- 停止与线程回收；
- 采集器抛异常时保存错误而非终止进程；
- 失败线程再次启动前的安全回收。

## 8. 本轮失败、原因与解决方法

### 新文件第一次同步到了Ubuntu仓库根目录

现象：PowerShell用于把`include/...`转换成Linux路径的`-replace '\','/'`在实际命令中转义错误，报告“正则表达式模式 `\` 无效”；目标目录拼接没有执行，8个新增文件被SCP到仓库根目录。

影响：这些文件在Ubuntu中均为本轮新建的未跟踪副本，没有覆盖任何既有源码。`CMakeLists.txt`按正确位置更新。

解决：先检查8个明确文件都位于仓库根目录，再使用非递归`rm -- <明确文件列表>`删除；之后不再动态拼接路径，而是按`include/`、`src/`、`tests/`三组使用明确SCP目标。复查`git status`后文件全部位于正确目录。

经验：跨Shell处理路径时，反斜杠既可能是PowerShell/正则语义，也可能是Windows路径分隔符。少量关键文件应优先用明确目标目录；若必须转换，先输出转换结果，再执行写操作。

## 9. 下一门禁

当前批次保持未提交，先审阅接口边界、并发语义和测试。批准后执行：

1. Ubuntu提交并生成里程碑bundle；
2. Windows裸仓库和GitHub同步；
3. RK3588检出同一提交并原生构建；
4. 真机核对7个温区、3个CPU策略、GPU/NPU频率、设备节点和CSV连续日志；
5. 再进入Unix Domain Socket、`monitorctl`、`--headless`、信号安全退出大类。
