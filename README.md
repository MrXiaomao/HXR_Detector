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

## H5 文件数据说明

测量过程会写出三类 HDF5 文件。能谱文件里的 `Spectrum` 是二维 `uint32` 表，一行是一次刷新得到的完整能谱，不是从开测起一直累加的谱。

### 能谱文件

全通道开测：

```
{保存路径}/{发次号}/{yyyy-MM-dd_HHmmss}.H5
```

测量过程中某一路掉线后重新上线，会为该路另建文件（同秒重名时在扩展名前再加 `_2`、`_3`）：

```
{保存路径}/{发次号}/{yyyy-MM-dd_HHmmss}_{通道号}.H5
```

文件结构：

```
/
├── Config/
│   ├── Configuration    16 行探测器参数，复合类型，字段见下文
│   ├── StartTime        可变长字符串，开测时刻，格式 yyyy-MM-dd_HHmmss
│   ├── ShotTime         可变长字符串，首帧能谱到达时刻，格式 yyyy-MM-dd hh:mm:ss.zzz；开测时若还没有则稍后补写
│   └── ChannelId        uint8，仅单通道文件有，表示该文件对应的通道号 1~16
└── Detector#1 .. Detector#16/
    └── Spectrum         二维 uint32，形状 (行数, 1029)，行数随测量增长
```

`Detector#n/Spectrum` 在该路第一次写入时才创建。没有上报数据的通道不会出现对应分组。

`Spectrum` 每行 1029 列，与内存中的 `H5Spectrum` 按 `uint32` 顺序铺开一致。数据集属性按列号说明含义，其中 `5-1028` 描述能谱道址：

| 列 | 属性键 | 内容 |
| --- | --- | --- |
| 0 | `0` | 能谱序号 |
| 1 | `1` | UTC 整数秒，从 1900-01-01 00:00:00 UTC 起算 |
| 2 | `2` | UTC 小数秒，单位是 1/2^32 秒 |
| 3 | `3` | 本帧测量时间间隔，单位 ms |
| 4 | `4` | 本帧死时间。100 MHz 时钟下单位为 10 ns，实际死时间 = 该值 × 10 ns |
| 5 ~ 1028 | `5-1028` | 1024 道能谱计数。列号 = 5 + 道址，道址 0~1023。列 5 是第 1 道，列 1028 是第 1024 道 |

能谱计数是这一刷新周期内各道的计数。探测器线上按 16 bit 上报，两个 512 道子包拼成 1024 道后，以 `uint32` 写入。界面上看到的累积能谱是软件把这些行再加起来的结果，文件里不保存累积谱。

前半包（能谱编号 1）写入道址 0~511，后半包（能谱编号 2）写入道址 512~1023。两个子包都到齐才写一行。

### 测量参数文件

每次开始测量时另存一份当时的探测器参数，供离线重建使用：

```
{保存路径}/{发次号}/{yyyyMMdd_HHmmss}_measureParam.h5
```

```
/
└── Config/
    ├── MeasureParameters    16 行探测器参数，复合类型与 Configuration 相同，并带字段属性
    └── SaveTime             可变长字符串，保存时刻，格式 yyyy-MM-dd HH:mm:ss
```

### 本地配置文件

程序启动时读写 `Config/Settings.H5`，只保存当前探测器参数，不含能谱：

```
/
└── Config/
    └── Configuration        16 行探测器参数，带字段属性
```

### Configuration / MeasureParameters 字段

两处使用同一套复合类型，固定 16 行，行序对应通道 1~16。字段属性写在数据集上，名称与下表一致。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | uint8 | 通道号，1~16 |
| `gain` | double | 增益，0.08~10.0 |
| `deathT` | uint8 | 探测器死时间设定，单位 10 ns |
| `Threshold` | uint16 | 触发阈值，低于阈值的波形不计入能谱 |
| `detIpPort` | 定长字符串(17) | 探测器 IP 地址 |
| `specDeltaT` | uint32 | 能谱刷新时间，单位 ms |
| `specLen` | uint32 | 能谱长度，当前为 1024 |
| `waveTrigMode` | uint8 | 波形触发模式：0 定时触发，1 普通触发 |
| `waveLen` | uint32 | 波形长度 |
| `TShapeEnable` | bool | 梯形成型使能 |
| `TShapeD1` | uint16 | 梯形成型时间常数 D1 |
| `TShapeD2` | uint16 | 梯形成型时间常数 D2 |
| `TShapeRise` | uint8 | 梯形成型上升沿点数 |
| `TShapePeak` | uint8 | 梯形成型平顶点数 |
| `TShapeFall` | uint8 | 梯形成型下降沿点数 |
| `HV_Enable` | bool | 高压使能 |
| `HV_Out` | uint16 | DAC 高压输出电平，0~4095 |
| `AlphaGammaUpper` | uint16 | α/γ 甄别上阈值，1~8192 |
| `AlphaGammaLower` | uint16 | α/γ 甄别下阈值，1~8192 |

`deathT` 是下发给探测器的死时间参数。`Spectrum` 第 4 列是每一帧实际上报的死时间，两者不是同一个量。
