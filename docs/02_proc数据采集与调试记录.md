# 通用Linux `/proc` 数据采集与调试记录

## 1. 本阶段结论

本阶段一次完成Udacity上游工程中通用Linux监控相关的全部TODO，包括系统信息、CPU、内存、进程、时间格式化和对象封装。Ubuntu 20.04 x86_64已完成标准工具链构建、41项自动检查、ASan/UBSan检查、ncurses实跑、GDB调用栈和strace系统调用验证。

本阶段仍保持未提交状态，没有更新Windows裸仓库和GitHub，也没有部署到RK3588。RK3588只测试经过用户审阅并正式提交的版本。

## 2. 数据流与分层

```text
/proc、/etc
    ↓ 文件读取和字段解析
LinuxParser
    ↓ 形成单项数据
Process / Processor
    ↓ 形成并排序系统快照
System
    ↓
NCursesDisplay
```

- `LinuxParser`只负责Linux文件格式，不负责界面。
- `Process`保存一次进程快照，避免界面反复读取同一PID时得到互相矛盾的数据。
- `Processor`保存前一次CPU累计值，通过两次采样的差值计算区间利用率。
- `System`每次刷新重新枚举PID、构造快照并按CPU占用排序。
- `NCursesDisplay`只显示结果，不解析 `/proc`。

这样拆分后，采集层可以单独测试；第2天增加RK3588专用采集器时，也不必把板级路径写进ncurses界面。

## 3. 读取的Linux接口

| 文件 | 读取内容 | 关键处理 |
|---|---|---|
| `/etc/os-release` | `PRETTY_NAME` | 去除键和值外层引号 |
| `/proc/version` | 内核版本 | 读取第三个字段 |
| `/proc/uptime` | 系统运行秒数 | 浮点秒向下转换为long |
| `/proc/meminfo` | 总内存、可用内存 | `(MemTotal-MemAvailable)/MemTotal`，旧内核回退到Free+Buffers+Cached |
| `/proc/stat`首行 | CPU各状态累计jiffies | 活动时间与idle+iowait分开；不重复累计guest字段 |
| `/proc/stat` | `processes`、`procs_running` | 前者是开机后的累计创建数，后者是当前运行态数量 |
| `/proc/<pid>/stat` | CPU时间、启动jiffies | 从最后一个`)`后解析，兼容进程名中有空格或右括号 |
| `/proc/<pid>/status` | UID、VmRSS | VmRSS由KiB换算为MiB |
| `/proc/<pid>/cmdline` | 启动参数 | 将NUL分隔转换为空格 |
| `/proc/<pid>/comm` | 内核线程名 | cmdline为空时作为回退 |
| `/etc/passwd` | UID到用户名 | 找不到时保留数字UID |

### CPU利用率为什么必须差分

`/proc/stat`记录的是开机以来的累计时钟滴答，不是当前百分比。两次采样分别得到：

```text
active_delta = active_now - active_previous
idle_delta   = idle_now - idle_previous
CPU使用率     = active_delta / (active_delta + idle_delta)
```

首次调用没有前一帧，因此使用开机以来的平均值；从第二次刷新开始使用采样区间差值。结果最终限制在`[0, 1]`，交给界面换算成百分比。

### 为什么解析 `/proc/<pid>/stat` 不能直接按空格切分

字段2是括号包围的进程名，其中允许出现空格，甚至可能包含右括号。直接按空格计数会让第14、15、16、17和22字段错位。实现先寻找整行最后一个`)`，再从字段3开始计数，因此能稳定读取CPU时间和进程启动时间。

### 为什么读取进程必须允许失败

`/proc`是实时视图。程序枚举出PID后，该进程可能立即退出，对应目录随即消失。这不是应用故障。解析函数对文件打开失败、字段不足和数字转换失败返回空值或零，`System`跳过已经消失且无法取得命令的进程，保证一次短暂竞态不会终止整个监控器。

## 4. 构建结构调整

核心代码被拆成静态库`monitor_core`：

```text
monitor_core
├── format.cpp
├── linux_parser.cpp
├── process.cpp
├── processor.cpp
└── system.cpp

monitor = main.cpp + ncurses_display.cpp + monitor_core + ncurses
monitor_tests = system_monitor_tests.cpp + monitor_core
```

测试程序不链接ncurses，因此可以在无交互终端、CI或SSH中独立验证采集逻辑。

## 5. 实际构建与测试命令

```bash
cd /home/d508/projects/RK3588_System_Monitor
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --clean-first --parallel 8
cd build-debug
ctest --output-on-failure
./monitor_tests
```

结果：GCC 9.4.0构建完成，无编译警告；41项检查全部通过，CTest为`1/1 Passed`。

测试范围包括：

- 时间格式化边界和超过24小时的运行时间；
- OS、内核、uptime、内存、CPU状态、jiffies和进程计数；
- 当前测试进程的PID、UID、用户名、命令、RSS和CPU时间；
- cmdline中的NUL转换；
- 不存在PID的空值/零值处理；
- CPU利用率范围；
- System进程快照和CPU降序排列。

### ASan与UBSan

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --parallel 8
cd build-sanitize
ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure
```

结果：测试通过，没有AddressSanitizer或UndefinedBehaviorSanitizer报告。

### ncurses受控实跑

```bash
cd /home/d508/projects/RK3588_System_Monitor
script -q -e -c 'TERM=xterm timeout 4s ./build-debug/monitor' \
  /tmp/rkmonitor-proc.typescript
```

退出码124表示`timeout`按计划在4秒后结束程序，不是崩溃。日志中没有Assertion、Segmentation fault或core dumped。一次现场快照显示：Ubuntu 20.04.6、内核5.15.0-139、CPU约10.9%后更新为3.5%、内存约45.3%、累计进程45701、运行进程1，并显示真实PID、用户、RSS和命令。

## 6. GDB调用栈

```bash
gdb -q -batch \
  -ex 'set env TERM xterm' \
  -ex 'break LinuxParser::MemoryUtilization()' \
  -ex run -ex bt \
  --args ./build-debug/monitor
```

关键调用栈：

```text
#0 LinuxParser::MemoryUtilization()
#1 System::MemoryUtilization()
#2 NCursesDisplay::DisplaySystem()
#3 NCursesDisplay::Display()
#4 main()
```

这证明界面中的内存数据经过解析层和System层取得，也展示了用函数断点验证分层调用关系的方法。

## 7. strace系统调用证据

```bash
strace -f -e trace=openat,read \
  -o /tmp/monitor_tests.strace \
  ./build-debug/monitor_tests
```

日志共5920行，可看到`openat`打开：

```text
/etc/os-release
/etc/passwd
/proc/<pid>/cmdline
/proc/<pid>/comm
/proc/<pid>/status
/proc/<pid>/stat
```

GDB回答“代码如何调用”，strace回答“进程实际向内核请求了哪些文件和系统调用”，两者证据层级不同。

## 8. 失败、原因与解决方法

### CTest报告“No tests were found”

- 现象：使用`ctest --test-dir build-debug --output-on-failure`后，CTest错误地在源码目录查找测试。
- 原因：Ubuntu 20.04自带CTest 3.16，不支持后续版本提供的`--test-dir`调用方式；生成的`build-debug/CTestTestfile.cmake`中实际已经存在`monitor_tests`。
- 解决：使用兼容命令`cd build-debug && ctest --output-on-failure`，测试正常发现并通过。
- 错误调用在源码根目录生成了仅含`LastTest.log`和`CTestCostData.txt`的`Testing/`临时目录。核对绝对路径和两个文件后删除该生成目录；它不包含源码且不可恢复，但可由CTest重新生成。

### `-Wpedantic`发现namespace结尾多余分号

- 现象：`include/ncurses_display.h`在`}`后仍有`;`，GCC报告`extra ';'`。
- 原因：namespace定义结尾不需要分号，原文件把类/结构体语法误用于namespace。
- 解决：把`};`改为`}`，全量clean build后无警告。

### 健壮性补丁首次报告`corrupt patch at line 49`

- 原因：测试hunk实际新增11行，但hunk头误写成新增12行，导致统一diff行数不一致。
- 解决：把`@@ -53,6 +53,18 @@`修正为`@@ -53,6 +53,17 @@`；始终先执行`git apply --check`，所以失败补丁没有修改Ubuntu源码。

### 一次补丁工具拒绝同文件同时Delete与Add

- 原因：本地补丁工具不允许在同一补丁中对同一路径同时声明删除和新增。
- 解决：拆成两次操作，先删除旧文件，再用独立补丁新增完整实现。该失败发生在本地补丁装配阶段，没有影响Ubuntu工作仓库。

### 第一次跨端SHA-256清单未识别中文路径

- 现象：`git diff --name-only`把中文文档输出成带引号的八进制转义文本，`Get-FileHash`和`sha256sum`将其当作实际文件名，报告文件不存在。第二次远端脚本还因PowerShell提前处理`$f`而得到错误路径。
- 原因：Git默认启用`core.quotepath`，且PowerShell双引号会先展开变量。
- 解决：只对本次命令增加`git -c core.quotepath=false`，并用单引号保护传给远端shell的脚本。最终18个未提交文件的SHA-256在Ubuntu和Windows逐项一致；没有修改仓库级Git配置。

## 9. 当前边界

- 单进程CPU百分比使用“进程累计CPU秒/进程存活秒”的生命周期平均值；第2天引入Sampler后可扩展为区间采样。
- `/proc/stat`中的`processes`是开机后的累计创建次数，不是当前PID目录数量；界面沿用上游字段语义。
- 目前只完成通用Linux数据。RK3588温区、CPU频率、GPU、NPU、MPP和RGA节点尚未进入本阶段。
- 目前没有提交、没有推送、没有部署开发板。

## 10. 下一阶段

下一大类将实现采集接口和采样架构：`MetricSnapshot`、`IMetricCollector`、`LinuxMetricCollector`、`Rk3588MetricCollector`、`Sampler`、线程安全快照与CSV日志。开始前先由用户审阅本阶段完整差异。
