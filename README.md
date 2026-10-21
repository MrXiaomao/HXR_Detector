# HXR 分时能谱软件

数字伽马能谱仪上位机（当前版本 V3.4.0）。软件通过 TCP 接收最多 16 路探测器数据，实时显示 1024 道能谱与计数率，并把测量结果写入 HDF5 与原始 dat 文件。另有两个离线模块，用于计数率统计和由 dat 重建能谱 H5。

## 主要功能

- **在线采集**：本地 TCP 服务接收探测器上报的能谱、波形、粒子与参数数据，实时绘制能谱曲线和计数率曲线。
- **测量控制**：全通道或选定通道开始、停止测量；支持手动测量、继续测量、软件触发、外触发和定时测量。
- **供电控制**：登录华为交换机，按探测器对应的 POE 端口开关电源。
- **探测器参数**：读写增益、触发阈值、死时间、高压，以及粒子模式下的 α/γ 甄别阈值。
- **波形与粒子**：采集、显示并保存波形数据和粒子数据。
- **能量刻度**：按探测器做线性或二次拟合，并保存刻度结果。
- **固件升级**：对在线探测器做 OTA 升级。
- **数据落盘**：测量能谱写入 HDF5，同时保留原始能谱 dat，便于事后核对。
- **离线分析**：
  - 计数率统计：`HXR.exe -m countRateStatistics`
  - 能谱 H5 重建：`HXR.exe -m spectrumH5Rebuild`（按开测时刻把 dat 与测量参数重建为 H5）
- **运行维护**：Log4Qt 日志、操作记录、崩溃时写入 `logs/crash_*.log` 与 `logs/crash_*.dmp`，以及界面卡死看门狗。支持明暗主题。

## 目录约定

第三方库与编译输出都在本工程的上一级目录：

```
HXR-Detector/
├── HXR_Detector/    本工程，入口为 HXR.pro
├── 3rdParty/        第三方库
└── build_HXR/       编译输出
```

## 编译环境

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows |
| 构建系统 | qmake，工程文件 `HXR.pro` |
| 语言标准 | C++17 |
| Qt | Qt 5.12 及以上的 Qt 5（推荐 Qt 5.15）。工程使用了 `QTextCodec` 与 Qt WinExtras，不能用 Qt 6 直接编译 |
| 编译器 | MinGW（g++）或 MSVC（VS2017 及以上，需支持 C++17） |
| 架构 | x64 或 x86，常用为 64 位 |

Qt 模块：Core、Gui、Widgets、Network、Concurrent、PrintSupport、WinExtras。后三个由 Log4Qt、QCustomPlot、QGoodWindow 的 `.pri` 引入。

在 Qt Creator 中打开 `HXR.pro`，选择对应 Kit 后构建即可。

- Debug 目标名为 `HXRd`，Release 目标名为 `HXR`。
- MinGW 输出目录：`../build_HXR/x64` 或 `../build_HXR/x86`。
- MSVC 输出目录在上述路径下再加一层 `msvc`，Release 同时生成 PDB，便于用 Visual Studio 打开 `logs/crash_*.dmp`。
- MSVC 链接后会把 `hdf5.dll`、`zlib1.dll` 复制到 exe 同目录。

## 第三方库

库文件放在 `../3rdParty`，由 `HXR.pro` 直接引用，不通过 vcpkg 或系统安装。

| 库 | 版本 | 用途 |
| --- | --- | --- |
| [Log4Qt](https://github.com/mebigfatguy/log4qt) | 随 `3rdParty/log4qt` | 日志（控制台与按日滚动文件） |
| [QCustomPlot](https://www.qcustomplot.com/) | 2.1.1 | 能谱、计数率等曲线 |
| [QGoodWindow](https://github.com/antonypro/QGoodWindow) | 随 `3rdParty/QGoodWindow` | 窗口外观与明暗主题 |
| [HDF5](https://www.hdfgroup.org/solutions/hdf5/) C++ | 1.14.4-3 | 能谱与测量参数的 H5 读写；运行时还依赖同目录的 `zlib1.dll` |
| [ALGLIB](https://www.alglib.net/)（`alglib-cpp`） | 随 `3rdParty/alglib-cpp` | 能量刻度的线性、二次拟合 |
| [Eigen](https://eigen.tuxfamily.org/) | 5.0.0 | 头文件库，数值计算 |
| [libhv](https://github.com/ithewei/libhv) | 1.3.3 | 高性能 TCP 服务。MinGW 链接 `libhv.dll.a`；MSVC 需要 `3rdParty/libhv/lib/hv.lib`，缺少该文件时自动改用工程内的 `TcpAgentServer` |

Windows 系统库：`ws2_32`（套接字）、`dbghelp`（崩溃转储）。

`QTelnet` 位于 `3rdParty/QTelnet`，仅在定义 `USE_QTELNET=1` 时参与编译。默认用 `QTcpSocket` 连接华为交换机，一般不需要打开该选项。
