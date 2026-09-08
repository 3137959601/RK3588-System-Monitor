# RK3588 原生构建与 VS Code 代码跳转记录

## 1. 本轮结论

- Windows 本地打开 Linux 工程时，`Ctrl+单击` 跳转不完整的主要原因不是源码损坏，而是 Windows 没有 Linux 编译器、`unistd.h`、`dirent.h`、ncurses 等目标头文件，也没有该目标生成的 `compile_commands.json`。编辑器缺少真实编译上下文时，只能猜测宏、头文件路径与编译选项。
- 正确开发入口是 Ubuntu 主仓库；RK3588 只检出已经提交的版本并进行真机测试。两端都用各自原生编译器生成编译数据库，VS Code 才能按实际目标进行语义跳转。
- 提交 `563a557ca31ca51b7e047605ce27eda87a243acb` 已通过 bundle 精确部署到 RK3588。板端使用 GCC 10.2.1 原生构建成功，可执行文件确认为 AArch64，41 项测试全部通过，ncurses 界面读取真实 `/proc` 数据并持续刷新。
- 本轮只新增这份记录并同步到 Ubuntu，尚未创建新提交，也未推送 GitHub。

## 2. 为什么 Windows 本地 `Ctrl+单击` 会失败

C/C++ 的跳转不是简单全文搜索。语言服务需要知道每个源文件实际使用的编译器、`-I` 头文件目录、宏定义、C++ 标准和目标架构。CMake 的 `compile_commands.json` 正是这些信息的逐源文件清单。

本项目调用 Linux 专属接口并链接 ncurses。Windows 工作副本适合查看 Git 差异，但 Windows 本地 C++ 扩展无法从系统中找到 Linux 头文件和 AArch64/x86_64 Linux 编译环境。因此：

1. 跳转项目内函数通常仍可工作；
2. 跳转 Linux 系统头或依赖库会失败或落到错误声明；
3. 只有二进制 `.so`/`.a`、没有对应源码时，本来就只能跳到公开头文件声明，不能跳入库实现。

Ubuntu 与 RK3588 均配置：

```json
{
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build-debug/compile_commands.json",
  "cmake.buildDirectory": "${workspaceFolder}/build-debug",
  "cmake.configureOnOpen": false
}
```

该文件和构建目录都被 `.gitignore` 忽略，因为编译器绝对路径与构建产物属于每台机器的本地状态，不应污染共享源码历史。

## 3. Ubuntu 代码跳转配置

Windows SSH 别名配置的关键内容：

```sshconfig
Host ubuntu20-vm
  HostName 192.168.1.117
  HostKeyAlias 192.168.1.111
  Port 22
  User d508
  IdentityFile C:/Users/31379/.ssh/codex_ubuntu20_ed25519
  IdentitiesOnly yes
```

Ubuntu 生成 Debug 构建和编译数据库：

```bash
cd /home/d508/projects/RK3588_System_Monitor
cmake -S . -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-debug --parallel 8
```

VS Code 操作：

1. 按 `F1`，执行 `Remote-SSH: Connect to Host...`，选择 `ubuntu20-vm`；
2. 打开 `/home/d508/projects/RK3588_System_Monitor`，不要打开 Windows 同名目录；
3. 首次配置后执行 `Developer: Reload Window`；
4. 若旧索引仍存在，执行 `C/C++: Reset IntelliSense Database`；
5. 等待右下角索引完成，在函数调用上按 `F12` 或 `Ctrl+单击`。

例如在测试代码中的 `LinuxParser::MemoryUtilization()` 上跳转，应进入 `src/linux_parser.cpp` 的实现。

## 4. RK3588 工具链安装

板端环境：

```text
Debian GNU/Linux 11 (bullseye)
Linux 5.10.160-rt77-gfbf758505158
AArch64，8 核，约 7.7 GiB 内存
根文件系统剩余约 5.6 GiB
```

首次执行：

```bash
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential ninja-build libncurses-dev
```

`apt-get update` 失败，报错为 USTC 的 `bullseye-security/InRelease` 已过有效期约 12 小时。开发板日期正确，因此真实原因是镜像站提供的安全仓库元数据过期，而不是板卡时钟错误。

没有使用 `Acquire::Check-Valid-Until=false` 绕过仓库安全检查，也没有大规模升级系统。查询现有缓存后确认本轮三个包均可由有效的 Debian main 仓库取得，于是只执行：

```bash
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential ninja-build libncurses-dev
```

安装成功，新增约 51.1 MB。关键版本：

```text
g++ 10.2.1
cmake 3.18.4
make 4.3
ninja 1.10.1
ncurses 6.2.20201114
```

没有执行 `apt autoremove`，避免删除厂商系统可能仍需使用的自动安装包。后续若必须更新安全仓库，应优先更换到仍维护 Debian 11 元数据的可信镜像，并单独验证，不应关闭有效期检查。

## 5. 将同一提交部署到 RK3588

Windows 制品：

```text
D:\hjx_workspace\Self_Learning\embedded_projects\RK3588_System_Monitor_563a557.bundle
SHA-256: 7B2E6ECD6F72D951DC7452D9F828B3C31F3C0C43F7983582B352CC4D4E5697FF
```

传输与校验：

```powershell
scp D:\hjx_workspace\Self_Learning\embedded_projects\RK3588_System_Monitor_563a557.bundle `
  tl3588:/tmp/RK3588_System_Monitor_563a557.bundle
```

```bash
sha256sum /tmp/RK3588_System_Monitor_563a557.bundle
mkdir -p /root/projects
cd /root/projects
git init bundle-check
cd bundle-check
git bundle verify /tmp/RK3588_System_Monitor_563a557.bundle
```

校验显示 bundle 包含完整历史、`main` 与 `upstream-baseline`。临时校验仓库执行普通 `rmdir` 时失败，因为 `git init` 已创建非空 `.git` 目录。先用 `readlink -f` 确认绝对路径严格等于 `/root/projects/bundle-check`，然后只删除该临时目录：

```bash
p=$(readlink -f /root/projects/bundle-check)
test "$p" = /root/projects/bundle-check
rm -rf -- "$p"
```

正式克隆与远端说明：

```bash
git clone --branch main \
  /tmp/RK3588_System_Monitor_563a557.bundle \
  /root/projects/RK3588_System_Monitor
cd /root/projects/RK3588_System_Monitor
git remote rename origin bundle-source
git remote add upstream https://github.com/udacity/CppND-System-Monitor.git
git remote add origin git@github.com:3137959601/RK3588-System-Monitor.git
git rev-parse HEAD
git rev-list -n 1 upstream-baseline
```

核验结果：

```text
HEAD:       563a557ca31ca51b7e047605ce27eda87a243acb
标签目标:   284b7154b4a8d447c494b1f41d0df2d68b00b909
工作树:     clean
```

板端保留 `origin` 仅用于说明来源；RK3588 没有配置 GitHub 私钥，也不负责向远端推送。

## 6. RK3588 原生构建与测试

```bash
cd /root/projects/RK3588_System_Monitor
cmake -S . -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-debug --parallel 8
cd build-debug
ctest --output-on-failure
file monitor monitor_tests
ldd monitor
```

结果：

- 构建完成到 100%；
- CTest 1/1 通过，直接运行时 41 项检查全部通过；
- `monitor` 和 `monitor_tests` 均为 `ELF 64-bit LSB pie executable, ARM aarch64`，带调试信息且未 strip；
- ncurses、tinfo、libstdc++、libgcc、libc、libm 等依赖均解析到板端 AArch64 系统库。

受控运行终端界面：

```bash
cd /root/projects/RK3588_System_Monitor
script -q -e \
  -c 'TERM=xterm timeout 5s ./build-debug/monitor' \
  /tmp/rk3588-monitor.typescript
```

`timeout` 返回 124 是达到预设 5 秒后主动结束，不是程序崩溃。捕获结果显示：

- OS：Debian GNU/Linux 11；
- Kernel：5.10.160-rt77-gfbf758505158；
- CPU、内存、运行时间和进程列表持续刷新；
- 程序能看到自身进程、SSH、Xorg 与 XFCE 等真实板端进程。

## 7. GDB 与 strace 证据

GDB 命令：

```bash
cd /root/projects/RK3588_System_Monitor
gdb -q -batch \
  -ex 'break LinuxParser::MemoryUtilization()' \
  -ex run \
  -ex 'bt 3' \
  --args ./build-debug/monitor_tests
```

断点命中 `src/linux_parser.cpp:164`，调用栈为：

```text
#0 LinuxParser::MemoryUtilization()
#1 main() at tests/system_monitor_tests.cpp:44
```

这证明测试确实进入了板端构建的实际解析函数。GDB 回答“程序执行到哪段源码、调用关系是什么”。

strace 命令：

```bash
strace -f -e trace=openat,read \
  -o /tmp/rk3588-monitor.strace \
  ./build-debug/monitor_tests
grep -E 'openat.*(/proc/|/etc/os-release)' \
  /tmp/rk3588-monitor.strace | sed -n '1,60p'
```

证据包括成功打开 `/etc/os-release`、`/proc/version`、`/proc/uptime`、`/proc/meminfo`、`/proc/stat` 和多个 `/proc/<pid>/...` 文件。测试还故意访问不存在的 PID `2147483647` 并收到 `ENOENT`，验证进程退出/不存在时的容错路径。strace 回答“程序实际向内核发出了哪些系统调用、文件访问是否成功”。

## 8. RK3588 的 VS Code 入口

Windows命令：

```powershell
code --new-window --remote ssh-remote+tl3588 `
  /root/projects/RK3588_System_Monitor
```

已确认当前窗口标题含：

```text
RK3588_System_Monitor [SSH: tl3588]
```

板端已存在 ARM64 版本的 `ms-vscode.cpptools`、CMake Tools 和 C++ Dev Tools，且 `build-debug/compile_commands.json` 有效。首次打开后执行一次：

1. `Developer: Reload Window`；
2. `C/C++: Reset IntelliSense Database`；
3. 等待索引完成，再测试 `F12` 或 `Ctrl+单击`。

若 VS Code 状态页同时显示旧的 `Connection ... Canceled`，应看它对应哪个窗口。当前窗口已经列出 `Remote: SSH: tl3588`、Linux arm64 和远程 server 进程，说明新连接成功；旧取消记录不等于当前连接失败。

## 9. 下一阶段

当前完成的是第 1 天闭环：基线、通用 `/proc` 采集、Ubuntu 验证、同一提交在 RK3588 原生构建和运行。下一大类开发进入 RK3588 指标采集架构：

- `MetricSnapshot`；
- `IMetricCollector`；
- `LinuxMetricCollector`；
- `Rk3588MetricCollector`；
- `Sampler`；
- 温区、CPU 频率、GPU/NPU/MPP/RGA 节点、后台采样、线程安全快照和 CSV 日志。

这些修改应只在 Ubuntu 主仓库完成，测试和同步到 Windows 审阅副本后保持未提交，待人工审阅，再由用户决定是否提交并部署下一提交到 RK3588。
