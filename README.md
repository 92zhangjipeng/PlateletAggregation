# PlateletAggregation（血小板聚集分析仪）

`C:\newPlateletAggregation` 是一个独立的 Qt Widgets 工程（qmake 构建）。

## 工程结构

| 文件 | 说明 |
|------|------|
| `PlateletAggregation.pro` | qmake 工程文件 |
| `main.cpp` | 程序入口 |
| `mainwindow.h/.cpp` | 登录页（用户名/密码/登入/取消）+ 主界面（水晶功能区 / 模组可视化 / 底部状态栏 / 切换用户） |
| `settingsdialog.h/.cpp` | 系统设置对话框：基础配置（样本质量/加样扫码/通道状态/用户管理）+ 维护密码 + 维护菜单 + 模组控制页 + 仪器参数页 |
| `appconfig.h` | QSettings 基础参数：样本质量、血浆模式吸PRP高度、加样扫码、测试项目组合、测试模式、机型、通道测试模式、按机型的通道启用状态 |
| `modulecontrolwidget.h/.cpp` | 模组控制页：3 个模组 Tab、温度/调光/电流/转速/读回/日志 |
| `modulerackwidget.h/.cpp` | 单个模组的伪 3D 绘制控件：4 路圆柱、试管插拔、进度环、每秒 AD、马达旋转、顶部温度 |
| `moduleoverviewwidget.h/.cpp` | 主界面模组总览：按机型横向显示 1~3 个 `ModuleRackWidget`，全局测试状态与曲线弹窗入口 |
| `reagentbottlepanel.h/.cpp` | 模组下方试剂余量面板：11 个伪 3D 试剂瓶、0~100% 液位 |
| `testsession.h/.cpp` | 全局“开始测试”状态单例：有通道测试时 true，供点击玻璃管弹曲线使用 |
| `testcurvewidget.h/.cpp` | 单通道实时曲线控件：顶部样本信息、中部曲线、底部互斥「测试曲线/集聚曲线」勾选框 |
| `testcurvedialog.h/.cpp` | 曲线弹窗：绑定某个 `ModuleRackWidget` 通道并实时刷新 |
| `CustomTitleBar.h/.cpp` | 公用自定义标题栏：图标、标题、最小化/最大化/关闭、拖动、双击最大化 |
| `FramelessDialog.h/.cpp` | 带自定义标题栏的无边框对话框基类，后续对话框继承它 |
| `FramelessWindowSupport.h/.cpp` | 无边框窗口边缘拖拽缩放（Windows `WM_NCHITTEST`） |
| `AppIcon.h` | 程序/窗口/标题栏统一图标入口 |
| `SerialPortManager.h/.cpp` | 仪器串口：自动查找 VID/PID、连接、15 字节帧收发与对齐 |
| `ModuleAdPoller.h/.cpp` | 10Hz 轮询模组通道 AD，按机型轮询 1~3 个模组，每秒滤波输出一次 |
| `mainboardstatus.h` | 主板状态帧 V1.0.1 解析结果结构体 |
| `mainboardstatusicons.h/.cpp` | 状态栏小图标：S2 外清洗液/废液/4 托盘/测高，异常闪烁 |
| `mainboardpoller.h/.cpp` | 串口连接后每秒读取主板状态，解析状态位/耗材/刷卡信息 |
| `consumablesdialog.h/.cpp` | 耗材管理对话框：集中显示主板上报的刷卡/耗材/有效期信息 |
| `testdatastore.h/.cpp` | 测试数据 SQLite 存储：样本、PPP/试剂、PRP0、PRP1~300；当天样本号生成/查重 |
| `instrumentcoordinates.h/.cpp` | 仪器坐标表：原点/清洗孔/弃杯孔/通道/试管/试剂位/样本孔的 XY 与 3 个 Z 轴高度 |
| `coordinatefileio.h/.cpp` | 坐标 JSON 读写：导出/导入工位坐标，提供启动自动恢复的默认坐标文件 |
| `applogger.h/.cpp` | 应用日志：按天写文件，保留 15 天，接管 qDebug/qInfo/qWarning 等 |
| `motionprotocol.h/.cpp` | 运动协议 0x17：15 字节帧构帧/解帧（单动作） |
| `motioncontroller.h/.cpp` | 单动作运动发送接口：XY/抓手/试剂针/样本针/血样泵/试剂泵/复位 |
| `coordinateverificationcontroller.h/.cpp` | 定位校验流程：三个 Z 复位完成门禁 → XY 移动 → 目标 Z 下降 |
| `medicalwidgets.h/.cpp` | 全局医疗风格控件：MedicalComboBox / MedicalCheckBox / MedicalSpinBox / MedicalDoubleSpinBox |
| `perfusioncontroller.h/.cpp` | 开机灌注流程：Z 复位 → XY 原点 → 样本泵/试剂泵排空、吸满、清洗、回吸 |
| `resultstablewidget.h/.cpp` | 自绘测试结果表格：表头、斑马纹、网格线、行点击 |
| `resultsinfowidget.h/.cpp` | 结果信息标签页：最近测试结果自绘表格 + 刷新 |
| `querywidget.h/.cpp` | 查询标签页：按样本号/患者姓名过滤测试结果 |
| `sampletraymodel.h/.cpp` | 试管盘状态模型：最多 4 盘 × 60 支，状态机 + QSettings 持久化 |
| `tubetraydialog.h/.cpp` | 试管盘状态维护界面：3D 玻璃试管架（60 孔/透明杯/磁棒）、按机型水平排列、更换盘 |
| `addsamplewidget.h/.cpp` | 加样界面：左侧双层 3D 采血管架 + 右侧加样列表（待定/待测/测试中状态、确认入队、孔位拖动、增删改、右键删除、测试项目组合（可增删改）、扫码、测试模式配对） |
| `testsamplequeue.h/.cpp` | 待测样本队列 `TestQueue`：确认后的样本按 FIFO 入队，供测试流程 `dequeue()` 取用 |
| `CommunicationWorker.h/.cpp` | 通讯线程工作对象：串口、自动重连、模组 AD 轮询、主板状态轮询 |
| `MachineModel.h` | KS600/KS800/KS1200 与模组数量映射 |
| `DetectionModule.h/.cpp` | 检测模组协议 V1.1.0 的组帧、解帧接口 |
| `img/suowei.png` | 原始公司图标 |
| `img/appicon.png` / `img/appicon.ico` | 方形程序图标（窗口/对话框/标题栏）与 exe 图标 |

## 图标与自定义标题栏

- 图标统一由 `img/appicon.png` 提供（由 `img/suowei.png` 生成方形图标）：
  - `main.cpp` 调用 `QApplication::setWindowIcon()` 设置全局图标；
  - `MainWindow` / `FramelessDialog` 同时设置窗口图标；
  - 自定义标题栏左侧显示同一图标；
  - `PlateletAggregation.qrc` 把 PNG 编进资源，`RC_ICONS = img/appicon.ico`
    设置生成的 exe 文件图标。
- 主窗口和设置对话框都不使用系统默认标题栏：
  - `CustomTitleBar` 公用：程序图标、标题、最小化/最大化-还原/关闭、
    按住拖动窗口、双击最大化/还原；
  - 主窗口 = `CustomTitleBar` + 登录页/主界面；
  - `FramelessDialog` = `CustomTitleBar`（只有关闭按钮）+ 内容区，
    `SettingsDialog` 已改为继承它。
- 后续新增对话框继承 `FramelessDialog` 即可自动带自定义标题栏：

```cpp
class MyDialog : public FramelessDialog
{
public:
    explicit MyDialog(QWidget *parent)
        : FramelessDialog(tr("我的对话框"), parent)
    {
        auto *content = new QWidget(this);
        // ... 在 content 里布局 ...
        setContentWidget(content);   // 或往 contentLayout() 里添加
    }
};
```

- 无边框窗口在 Windows 上由 `FramelessWindowSupport` 处理 `WM_NCHITTEST`，
  四边/四角可拖动缩放；主窗口和对话框都支持。

## 操作流程

1. 启动进入**登录页**（用户名 / 密码 / 登入 / 取消）。
   开发阶段不校验用户名和密码，任意输入或直接点「登入」都进入主界面；
   首次启动时「取消」关闭程序；**登录过一次后**，从主界面底部「切换用户」
   返回登录页再点「取消」会回到主界面，不会退出软件；测试状态保持不变；
   登录主界面后，底部状态栏右侧「切换用户」链接可返回登录页换用户（串口保持连接）；
2. 串口连接从**程序启动时**就在后台进行：按 **VID 0x0483 / PID 0x5740**
   自动查找仪器串口，固定波特率 **1,000,000**，每 2 秒自动重试；
   连接状态显示在主界面**底部状态栏**（正在查找 / 已连接 COMx / 连接异常）；
   联机后底部状态栏同时显示每秒解析出的主板状态摘要，鼠标悬停可看完整状态；
3. **30 秒内未连接**仪器 → 弹出「串口连接异常」提示，程序继续自动重连；
   **30 秒之后才连上** → 弹出提示，要求关闭软件后重新启动；
4. 主界面顶部功能区右侧「设置」→ 切换到主界面的「设置」标签页，**默认显示「基础配置」页**：
   仪器信息、样本质量、加样扫码、按机型的通道启用状态、用户管理，以及
   右上角「维护」按钮；
   - 加样列表中存在**待测或测试中样本**时，样本质量不能修改；点击单选项会
     提示并自动恢复原值（通过 `AddSampleWidget::activeSampleCount()` 判断）；
   - **血浆模式吸PRP高度**：`20~99 mm`，默认 `32 mm`，保存到
     `basic/plasmaPrpAspirateHeight`；血浆模式不测高，添加样本时「吸样高度」
     直接使用该值；修改后刷新所有**待定**行，已确认/测试中样本保持确认时高度；
5. 点「维护」→ 弹出维护登录框；输入维护密码（**默认 `1`**，
   定义在 `settingsdialog.cpp` 的 `kMaintenancePassword`）后进入「维护设置」菜单；
6. 维护设置菜单用 `QGridLayout` 排列 7 个模块入口：
   样本针参数、试剂针参数、抓手参数、模组控制、打印设置、坐标维护、仪器参数；
   - 样本针/试剂针/抓手/打印设置目前是占位入口，点击会提示“尚未实现”；
   - 「← 基础配置」返回基础配置页，右上角显示「维护模式」标记；
   - 「仪器参数」页面包含：
     - **机型**：KS600 / KS800 / KS1200，保存到 `instrument/machineModel`；
       切换后主界面、加样页、通道状态和坐标页按新机型刷新，下次启动沿用；
     - **测试模式**：PPP+PRP（每样本 2 管血、2 个连续孔）或 PRP
       （每样本 1 管血、1 个孔），默认 PRP，保存到 `instrument/testMode`；
       存在待测/测试中样本时同样锁定，修改会提示并恢复原模式；
     - **通道测试模式**：同通道测试 / 不同通道测试，保存到
       `instrument/channelTestMode`；该参数目前只保存，测试逻辑后续接入；
7. 点「模组控制」→ 切换到模组控制页（模组 Tab 数量按机型启用）：
   - 温度控制：目标温度、应用温度、保存参数，显示读回温度；
   - LED 调光：4 路统一调光值写入；
   - LED 电流：逐路写入（模组1=CH1~4，模组2=CH5~8，模组3=CH9~12）；
   - 电机转速：逐路「启动 / 停止」，以及「全部启动 / 全部停止」；
   - 读回：通道 AD 每秒更新一次滤波值，另可手动读转速、读电流；
   - 通信日志：显示手动下发帧和应答；每秒轮询帧不刷日志；
8. 点「← 返回」回到维护设置菜单；设置内容现在位于主界面「设置」标签页内，
   通过顶部「设置」按钮或标签栏切换，不再弹出独立窗口。

## 主界面顶部功能按钮与标签页

主界面顶部为完整功能区 + 标签页内容区：

- 内部使用标签页管理 **主页、结果信息、耗材管理、试管盘、查询、质控、加样、设置、日志**，但**标签头隐藏**，全部通过顶部功能按钮切换；
- 左侧 6 个按钮：**主页、结果信息、耗材管理、试管盘、查询、质控**；
- 中间：圆形 **开始** 按钮（水晶径向渐变 + 播放图标）；
- 右侧 6 个按钮：**日志、暂停/继续、复位、清洗、加样、设置**；
  日志按钮紧贴开始按钮右侧，左右数量对称；
- 顶部按钮高度增加到 **72px**（`QToolButton`，图标在上、文字在下），圆形开始按钮保持 90px；
- 左右两组按钮均为水平 `Expanding`，按 **1:1** 分左右空间；组内按钮等宽铺满，不再固定 64px 宽；
- 顶部按钮采用半透明渐变、白色高光边和清爽的医疗蓝配色；
- 「切换用户」不再放在顶部：改为底部状态栏右侧的 **切换用户** 文字链接；
- 当前已接入的行为：
  - 「主页」：切回主界面仪表盘（模组可视化 + 试剂瓶余量）；
  - 「开始」：模拟模组1 CH1 开始测试；开始后按钮禁用，直到测试结束/复位才恢复；
  - 「暂停」：暂停模拟计时和马达，按钮变为「继续」；再次点击继续；
  - 「复位」：停止模拟并清空所有通道状态，同时恢复「开始」按钮；
  - 「设置」：切换到「设置」标签页；
  - 「日志」：打开/刷新「运行日志」标签页，显示当天日志文件末尾 512 KB，
    可打开日志目录；
  - 「耗材管理」：切换到「耗材管理」标签页，集中查看主板上报的耗材信息；
  - 「试管盘」：切换到「试管盘」标签页，维护 4 个试管盘状态；
  - 「结果信息」：切换到结果信息标签页，用自绘表格显示最近测试结果；
  - 「查询」：切换到查询标签页，按样本号/患者姓名筛选测试结果；
  - 「质控」：切换到质控标签页，当前为“功能待接入”占位；
  - 「加样」：切换到加样页，左侧显示双层 3D 采血管架，右侧显示加样列表；
    采血管液体颜色跟随基础设置里的样本质量（全血 / 血浆）；
  - 「清洗」：底部状态栏提示“功能待接入”。

原有的 `ConsumablesDialog` / `TubeTrayDialog` / `SettingsDialog` 通过
`FramelessDialog::embedAsPage()` 去掉窗口属性和自绘标题栏后嵌入标签页，
因此不再以弹窗形式出现；标签页内容外层带 `QScrollArea`，小窗口下可滚动。

## 基础配置页

系统设置默认进入「基础配置」，包含：

- **仪器信息**：机型、模组数量、通道数量、通讯状态；
- **样本质量**：互斥 `全血` / `血浆` 两个单选项，保存在 QSettings；
  - 通道1模拟测试时：全血模式显示 PRP 金黄色，血浆模式显示 PPP 浅黄色；
- **加样扫码**：勾选「每添加一个样本必须扫描/输入条码」，保存在
  `basic/addSampleScanBarcode`，默认关闭；
  - 开启后每次「添加样本」都会立即把光标放到新行的条码单元格；
    如果表格中还有条码为空的样本，再次添加会被拦截并提示先扫描/输入，
    避免加多个样本后条码与样本对应不上；
  - 关闭时添加样本不要求条码，条码可以留空；
  - 两种模式下都可以手动输入条码，扫描枪按键盘输入 + 回车即可；
- **通道状态**：按当前机型显示 4/8/12 个通道的「启用/禁用」勾选框；
  - 禁用后主界面该通道显示**红色叉号**，不参与测试；
  - 通道状态按机型分别保存到 QSettings；
  - 正在模拟的 CH1 被禁用时会立即停止模拟并恢复「开始」按钮；
- **用户管理**（`QGroupBox`）：
  - 列表显示用户名、权限；
  - 支持按用户名查询、新增、修改、删除；
  - 权限分「管理员」/「操作员」，修改/删除会保证至少保留一个管理员；
  - 密码用随机盐 + SHA-256 保存，界面不回显密码；
  - 首次运行自动创建默认管理员：`admin` / `1`；
  - 当前登录页仍为开发模式（不校验用户名密码），后续可接入这里的用户表；
  - **存储方式：不是数据库**。用户记录以 JSON 数组保存在
    `QSettings("security/users")` 中；Windows 默认位置为注册表
    `HKEY_CURRENT_USER\Software\PlateletAggregation\PlateletAggregation`。
    测试数据单独使用 SQLite，试管盘状态使用 QSettings。

## 主界面模组可视化

主界面内容区使用 `ModuleOverviewWidget`，内部按机型横向显示 1~3 个
`ModuleRackWidget`：

- 一个模组固定 **4 路通道**，4 个竖直玻璃圆柱表示通道；
- 试管可由测试流程接口插入 / 拔出（带滑动动画），试管上显示全局通道号；
- 试管内液体为偏黄色的 **PRP**（或更浅的 **PPP** 血清），液体内有一个小磁棒；
- 每个圆柱顶部有**进度环**和百分比，用于显示该通道测试进度；
- 每个玻璃管**管口**显示该通道**每秒 AD 值**（如 `AD 2300`）；
- 圆柱下方的小马达由测试流程接口旋转 / 停止，运行时有转子转动和绿色指示灯；
  马达旋转时，试管内的小磁棒同步旋转；
- **鼠标点击马达不会启动/停止马达，点击玻璃管也不会插拔试管**；
- 每个模组**顶部居中**显示模组温度（如 `37.0 ℃`）；温度来自
  `ModuleAdPoller::ModuleAdData::temperatureCelsius`，无效时显示 `温度 --`；
- 机型默认 **KS1200**，显示 3 个模组；可在「维护设置 → 仪器参数」中设置并
  保存，软件启动时从 `instrument/machineModel` 恢复；读取到真实机型后调用
  `MainWindow::setMachineModel()` 也可自动切换为 1/2/3 个模组；
- 主界面内容放在 `QScrollArea` 中，窄窗口/低分辨率下模组会自动横向收缩；
  高度不足时可纵向滚动到下方试剂余量区，模组不会被裁掉。

### 通道1测试模拟（临时演示）

进入主界面后点击顶部圆形 **开始** 按钮，会模拟 **模组1 CH1 正在测试**：

- 自动插入试管，小磁棒位于试管最底部；
- 根据基础设置里的样本质量显示 PRP（全血）或 PPP（血浆）液体；
- 启动 CH1 马达，磁棒跟随马达角度旋转；
- 每秒生成一个模拟 AD 值并刷新管口 `AD xxxx`；
- 同步生成测试曲线和集聚曲线；
- 单个试剂测试固定 **300 秒 / 300 个 PRP（AD）点**，进度环从 8% 逐步走到 100%，
  300 秒后显示“完成”并结束测试状态；每个样本反应时间为 **5 分钟**；
- 开始后「开始」按钮禁用，直到测试结束或点「复位」才恢复；
- 「暂停」按钮会停止模拟计时和马达，按钮变为「继续」；「复位」会停止并清空模拟；
- 模拟入口在 `MainWindow::handleStartTest()` 中调用
  `ModuleOverviewWidget::startChannelOneSimulation()`；接入真实测试流程后替换为真实启动逻辑即可；
- 模拟期间，真实串口的每秒 AD 不会覆盖 CH1（见 `mainwindow.cpp` 的 AD 同步逻辑）。

### 全局“开始测试”状态

`TestSession::instance()` 保存全局测试状态，也可以直接调用：

```cpp
window.setTestingActive(true);    // 有通道开始测试
window.setTestingActive(false);   // 所有通道测试结束
```

为 true 且某通道玻璃管内已插入试管时，点击该玻璃管会弹出对应通道的
实时曲线窗口；否则点击玻璃管不做任何操作。检测模组标题右侧会显示
「● 开始测试 / ● 未开始测试」状态。

### 测试流程驱动接口

```cpp
ModuleOverviewWidget *overview = window.moduleOverview();

// moduleNumber = 1~3，localChannel = 1~4，globalChannel = (module-1)*4+localChannel
overview->setModuleTemperature(1, 37.0);      // 模组顶部居中温度；传 -1 显示“温度 --”
overview->setTubeInserted(1, 1, true);        // 模组1 CH1 插入试管（动画）
overview->setChannelStatus(1, 1, QStringLiteral("样本:0001[AA]"));
overview->setChannelProgress(1, 1, 42);       // 进度环 42%
overview->startMotor(1, 1);                   // 启动马达
overview->stopMotor(1, 1);                    // 停止马达

// 通道状态：空闲 / 测试中 / 异常 / 禁用
overview->setChannelRunState(1, 1, ChannelRunState::Testing);
ChannelRunState state = overview->channelRunState(1, 1);

// 曲线窗口顶部信息：样本、试剂、时间、PRP 实时值
// PRP 值传 -1 表示等待实时值；测试中会随每秒 AD 自动刷新
overview->setChannelTestInfo(1, 1, QStringLiteral("0001"),
                             QStringLiteral("AA"), 0, -1);

// 每秒 AD：更新玻璃管口 AD 和曲线头部 PRP 实时值；
// 测试状态下同时追加到“测试曲线”
overview->setChannelAdValue(1, 1, 2300);

// 集聚曲线数据由测试算法显式追加（x 轴单位为秒，y 为百分比）
overview->appendChannelAggregationPoint(1, 1, 10.0, 12.5);

// 新一轮测试可清空曲线；插入新试管时也会自动清空上一轮曲线
overview->clearChannelCurve(1, 1);
```

通道状态使用 `ChannelRunState`：`Idle`（空闲）、`Testing`（测试中）、
`Abnormal`（异常）、`Disabled`（禁用）。`setChannelRunState(..., Disabled)`
等价于禁用该通道（停止马达、拔出试管、清空曲线）；设置其它状态时如果通道
处于禁用状态会先自动启用。`ModuleRackWidget` 还提供同名接口和
`channelRunStateChanged()` 信号。

### 试剂余量面板

主界面模组下方新增 `ReagentBottlePanel`（位于底部状态栏之上，内容区可滚动）：

- 共 **11 个** 伪 3D 试剂瓶，从左到右：
  1. `S1清洗液`：按实拍白色塑料瓶绘制——切角方瓶身、正面凸起面板、
     短粗螺纹圆口、开放瓶口；
  2. `AA左`、3. `AA右`：按实拍透明玻璃小瓶绘制——圆柱瓶身、圆肩、
     窄颈、顶部翻边瓶口；
  4. `ADP左`、5. `ADP右`；
  6. `COL左`、7. `COL右`；
  8. `EPI左`、9. `EPI右`；
  10. `RIS左`、11. `RIS右`；
- 试剂瓶为透明玻璃质感，瓶内液位按 **0~100%** 从底部填充，并显示名称与百分比；
- S1 瓶保留白色塑料瓶身，液位以半透明方式显示；
- 名称均为左右成对：`AA左/AA右`、`ADP左/ADP右`、`COL左/COL右`、
  `EPI左/EPI右`、`RIS左/RIS右`；
- 默认给出一组演示液位（100/85/82/.../36%），后续接入真实余量时直接调用：
  ```cpp
  ReagentBottlePanel *reagent = window.reagentPanel();
  reagent->setLevel(ReagentBottlePanel::S1Clean, 100.0);
  reagent->setLevel(ReagentBottlePanel::AA_Left, 85.0);
  reagent->setLevel(ReagentBottlePanel::AA_Right, 82.0);
  reagent->setLevels({100, 85, 82, 74, 68, 61, 57, 52, 47, 41, 36});
  ```
  传入值会自动限制在 0~100%。

### 实时曲线弹窗

点击测试中的玻璃管会打开 `TestCurveDialog`，内容为 `TestCurveWidget`：

- 顶部信息：`样本0001 AA 300S PRP=2300`（PRP 值为试剂测试的实时 AD 值，
  每秒刷新，不再显示孔位、也不再是固定数字）；
- 中间自绘实时曲线，x 轴为秒，y 轴为 AD 或集聚百分比；
- 底部两个互斥 `MedicalCheckBox`：「测试曲线」显示 AD 原始值，「集聚曲线」显示百分比；
- AD 由主界面 `ModuleAdPoller` 每秒同步到玻璃管口；测试状态下会自动追加到
  该通道的测试曲线，曲线窗口打开时实时刷新；
- 集聚曲线由后续测试算法通过 `appendChannelAggregationPoint()` 接入。

```cpp
// 也可以直接打开某个通道的曲线窗口
TestCurveDialog *dialog = new TestCurveDialog(
            overview->moduleRack(1), 1, overview);
dialog->show();
```

## 测试数据存储（SQLite）

每个试剂测试固定 **300 秒 / 300 个 PRP 点**，每个样本反应 5 分钟。
`TestDataStore` 使用 SQLite 保存完整测试数据，数据库默认路径：

```text
QStandardPaths::AppDataLocation/platelet_test_data.db
```

Windows 通常为：
`C:\Users\<用户>\AppData\Roaming\PlateletAggregation\PlateletAggregation\platelet_test_data.db`

采用“样本 → 测试 → 点”三张表，而不是一张 6×301 列的宽表：

| 表 | 主要字段 | 说明 |
|---|---|---|
| `samples` | `id`、`sample_no`、患者信息、`sample_type`、`machine_model`、`operator_name`、`test_time`、`created_at` | 一个样本一行 |
| `sample_tests` | `sample_id`、`test_index`、`reagent`、`prp0`、`points_count`、`reaction_seconds=300`、模组/通道、`status` | 一个 PPP 或一个试剂一行；最多 1 个 PPP + 5 个试剂 |
| `test_points` | `test_id`、`second`(1~300)、`prp_value`、`aggregation_percent` | 每个 PRP1~PRP300 一行 |

- `sample_tests.test_index`：0=PPP，1~5=试剂；`reagent` 只允许
  PPP/AA/ADP/COL/EPI/RIS，且同一测试内不可重复；
- PRP0 保存在 `sample_tests.prp0`，PRP1~PRP300 保存在 `test_points`；
- 外键 `ON DELETE CASCADE`，并提供 `v_sample_test_summary` 汇总视图；
- 当前通道1模拟测试在自然跑满 300 秒后会自动保存一条试剂测试记录
  （数据库自检也覆盖了 PPP + 5 个试剂、每个 300 点的完整场景）；
- 测试数据使用 Qt SQL（`QT += sql`），运行时需要 Qt5Sql 与
  `sqldrivers/qsqlite.dll`；
- 可通过 `window.testDataStore()` 取得存储对象，调用
  `saveSample()` / `loadSample()` / `recentSamples()`；
- 加样页用 `nextSampleNumber(QDate)` 取 `samples` 表中当天
  `TyyyyMMdd` 前缀的最大序号 + 1，用 `sampleNumberExists()` 在修改样本号时
  查重；两个方法只读数据库，不写入样本。

## 试管盘状态维护

`SampleTrayModel` 维护最多 **4 个试管盘 × 60 支 = 240 支试管**：

- 机型：KS600=2 盘（0~119）、KS800=3 盘（0~179）、KS1200=4 盘（0~239）；
- 每盘 **10 行 × 6 列 = 60 支**，全局编号：
  盘1=0~59、盘2=60~119、盘3=120~179、盘4=180~239；
- 试管状态：**空闲 → 分配 → 已加样 → 已使用**；
  非空闲状态也可以释放回空闲，回收后重新分配；
- 状态机由模型统一维护，非法跳转会被拒绝；界面或测试流程都通过同一套接口：

```cpp
SampleTrayModel *tray = window.trayModel();
int tube = tray->firstFreeTube();        // 找当前机型第一个空闲试管
tray->allocateTube(tube, "0001");        // 空闲 -> 分配
tray->markSampled(tube, "AA");           // 分配 -> 已加样
tray->markUsed(tube);                    // 已加样 -> 已使用
tray->clearTube(tube);                   // 回收 -> 空闲
```

- 顶部「试管盘」按钮打开维护界面：按当前机型盘数**水平并排显示**
  （KS600=2 盘、KS800=3 盘、KS1200=4 盘），不再使用 Tab；
  每盘 **10 行 × 6 列 = 60 孔**；
- 试管盘绘制为**伪 3D 透明玻璃架**：透视台面、玻璃厚度、侧面高光和反射条纹；
  每个孔内绘有透明小试杯，杯底为椭圆并带小磁棒，管号用杯口白色标签显示；
- **空闲**：透明玻璃杯，孔内插着杯子；
- **分配**：杯口圆环变为琥珀色标记；
- **已加样 / 已使用**：表示杯子已被夹走，孔内不再绘制试杯，只保留带状态色的空孔；
- 悬停显示样本/试剂/更新时间；点击试管可打开维护菜单：
  分配样本、标记已加样/已使用、释放为空闲；
- 每盘下方只有一个「更换」按钮：将该盘全部试管恢复为空闲（用于换新盘）；
  不再提供“清空当前盘 / 全部清空”；
- 更换规则：
  - **测试进行中禁止更换**，会弹窗提示等待测试结束；
  - 仪器未连接、未读到主板试管盘余量、或余量为 0 时拒绝更换并提示；
  - 正常时连续弹出**两次确认**，第二次确认会显示“余量由 N 板减为 N-1 板”；
  - 确认后：该盘恢复空闲，试管盘余量减 1，并通过主板 0x16 写命令
    更新主板数量（位置 6，缓存写入，比例 521，与老上位机一致）；
- 状态通过 QSettings 持久化（键 `sampleTrays/tubes`），软件重启后恢复；
- 测试流程后续应在样本分配、加样、测试完成时调用上述接口，界面只做维护兜底。

## 加样界面

顶部「加样」按钮切换到 `AddSampleWidget`。页面为**左右布局**：
左侧是双层 3D 血样区，右侧是**加样列表**。样本质量只影响高度列名称和
采血管里的液体颜色；测试模式（PPP+PRP / PRP）影响每个样本占用的孔数。

### 加样列表（右侧）

右侧列上方（卡片外）是「＋ 添加」「确认」两个按钮：两个按钮等宽铺满
右栏、最小高度 44px、14px 粗体；添加为浅蓝渐变，确认为深蓝渐变。
卡片内上方是快速加样栏，下方是 `QTableWidget`：

- **样本号**：默认按 `T+yyyyMMdd+4位序号` 生成（如 `T202611210001`），
  自动显示在输入框里；可以修改后再添加，但**长度必须固定为 13 位**
  （`T` + 8 位日期 + 4 位序号），不足/超过 13 位会提示并拒绝添加或修改；
- **条码**：扫码枪直接扫入或手动输入，带清除按钮；**只允许数字**；
  在条码栏按回车或点上方「＋ 添加」即把当前样本号 + 条码加入列表；
- **加样扫码**（基础参数开关）开启时：条码栏必须非空才能添加；
  列表里存在空条码样本时再次添加会被拦截并定位到空条码行；
- 关闭加样扫码时：条码可以留空；仍可双击列表中的条码单元格手动输入；
- 添加成功后：条码栏清空并自动重新聚焦，样本号输入框自动更新为下一个号，
  方便连续扫码/连续加样；
- **「确认」按钮**：对列表中所有“待定”行做一次完整性校验（样本号不重复、
  扫码开启时条码非空、**测试项目不能为空**、孔位有效、队列内样本号不
  重复），通过后统一改成 **待测**，并按确认顺序加入 `TestQueue` 待测队列；
  提示“已确认 N 个样本进入待测队列（当前队列 M 个）”。确认后的行只读，
  不能改样本号/测试项目/条码/孔位，但可以删除（删除待测行会同步移出队列）；
- 列表列从 0 开始编号，固定为：

| 列索引 | 名称 | 说明 |
|---|---|---|
| 0 | 状态 | `待定`（灰点）/ `待测`（橙点）/ `测试中`（蓝点）；测试中的行不能删除 |
| 1 | 样本号 | `T + yyyyMMdd + 4位序号`，固定 13 位；双击可修改，长度不符会失败 |
| 2 | 条码 | 只允许数字 |
| 3 | 测试项目 | 双击打开选择器：可直接选已有组合，也可自由勾选 AA / ADP / COL / EPI / RIS；显示为 `AA+ADP` |
| 4 | 识别高度 / 吸样高度 | 全血显示「识别高度」，由后续测高结果填入；血浆显示「吸样高度」，自动取基础设置中的**血浆模式吸PRP高度** |
| 5 | 样本位置 | **血样孔号（全局 0~119）**；PRP 模式为单个孔（如 `10`），PPP+PRP 模式为两个连续孔（如 `10+11`） |

- 「删除选中」：支持多选，确认后删除；删除后孔位自动释放；
- **右键菜单**：在表格任意行右键，未选中时先选中该行；已多选时保持多选，
  选「删除选中」即可批量删除；只要有“测试中”的行就拒绝删除并提示；
- 「修改样本号」：选中行后点击，或**双击样本号单元格**，弹出输入框修改；
  输入长度不是 13 位会被提示，输入框会重新打开直到输入合法号码或取消；
- **测试项目选择器**（双击测试项目单元格）：
  - **已有组合**：组合列表来自 `basic/testItemCombinations`，勾选一个即代表
    使用该组合；组合按确认顺序显示为 `AA+ADP`、`AA+ADP+COL` 等；
  - **添加组合**：点「添加组合」，在子对话框中勾选试剂，组合名称自动按
    AA/ADP/COL/EPI/RIS 顺序生成；重复组合会提示并直接选中已有项；
  - **修改组合**：勾选已有组合后点「修改组合」，修改其试剂组成；与已有组合
    重名时拒绝修改；
  - **删除组合**：勾选已有组合后点「删除组合」，删除后当前选择会转成同样的
    临时组合，测试项目不会丢失；
  - **自由组合**：直接勾选 AA / ADP / COL / EPI / RIS 中的任意项；
  - **互斥规则**：勾选已有组合会自动清空自由勾选；勾选任一自由试剂会自动
    取消已有组合。最终要么使用一个已有组合，要么使用自己勾选的临时组合；
  - 未选择任何测试项目时「确定」不可用；确认样本时测试项目为空也会被拦截；
- 高度列只读；条码可双击编辑；
- **血浆模式**：新增样本自动把「吸样高度」填为基础设置里的
  `血浆模式吸PRP高度`；修改该设置后，所有**待定**行刷新为新值，
  已确认/测试中样本保留确认时的值；切回全血只清除血浆自动填的高度，
  不覆盖后续测高结果；
- 样本号规则：`T` = 测试，中间 8 位日期，最后 4 位当天序号，总长度固定 13 位；
- 新增样本号默认从测试数据库 `samples` 表取**当天同前缀最大序号 + 1**
  （`TestDataStore::nextSampleNumber()`）；测试数据库未打开时提示并阻止新增；
- 修改/自定义样本号要求**为 13 位（T + 8 位日期 + 4 位序号）、列表内不重复、
  测试数据库中也不存在**（`TestDataStore::sampleNumberExists()`）；
  长度不符或重复时弹窗并保留原值；
- 样本位置不能重复：
  - **PRP 模式**：每个样本 1 个孔；
  - **PPP+PRP 模式**：每个样本 2 个连续孔，位置自动分配（0+1、2+3、4+5……），
    第一个孔为 PPP、第二个孔为 PRP；
  - 位置列本身只读，不能输入；**待定行点击「样本位置」单元格**后进入
    血样区拖动模式：
    - 该样本的采血管从原位隐藏，并在目标孔画**蓝色虚线管身 + 孔口双圈 +
      光晕 + “移动中”标签**，血样区底部提示左键放置/右键取消；
    - 采血管跟随鼠标吸附到目标孔；PPP+PRP 模式一次移动两根管，保持
      同一组同一行的“左 PPP、右 PRP”配对；
    - 左键放置到空孔；右键或 Esc 取消；目标孔被占用时管身/提示变红，
      不能放置，换一个孔即可；
    - 只有**待定**样本能移动；待测/测试中样本点击位置列会提示不能移动；
- 测试模式切换（维护设置 → 仪器参数）后返回加样页，已有行会按新模式
  自动重新分配孔位，并重建待测队列；样本数超过新模式容量时，超出的行
  不分配孔位并提示；
- 加样列表当前只保存在内存中，尚未写入测试数据库；测试完成后由测试流程
  调用 `TestDataStore::saveSample()` 入库。**“测试完成自动删除并把结果写入
  结果表”预留到后续测试逻辑和结果表改造时实现**。

### 待测队列（供测试流程接入）

`testsamplequeue.h/.cpp` 提供 `QueuedSample` 和 `TestQueue`：

```cpp
struct QueuedSample {
    QString sampleNumber;
    QString barcode;
    QStringList testItems;   // AA/ADP/COL/EPI/RIS
    QString height;
    QStringList positions;   // 全局孔号，PRP 1 个，PPP+PRP 2 个
    bool pppPrp;
    QDateTime queuedAt;
};

TestQueue *queue = addSampleWidget->testQueue();
QueuedSample sample;
if (queue->dequeue(&sample)) {
    addSampleWidget->setSampleTesting(sample.sampleNumber, true);
    // 启动测试……
}

// 测试结束或取消、样本回到待测时：
addSampleWidget->setSampleTesting(sample.sampleNumber, false);
```

- `enqueue()` 校验样本号非空且队列内不重复；`removeBySampleNumber()` 用于
  删除待测行/取消测试；`dequeue()` 按确认顺序 FIFO 取队首；
- `setSampleTesting(sampleNumber, true)` 把行改成**测试中**并移出待测队列；
- `setSampleTesting(sampleNumber, false)` 把行改回**待测**；如果队列里还没有
  该样本（例如刚测试完取消），会重新入队；
- 加样页通过 `testQueue()` / `testQueue() const` 暴露队列，主窗口测试流程
  后续直接从这里取样本，不需要再解析表格。

### 双层采血管架（左侧）

- 血样孔数量跟随当前机型：
  - KS600：6 组（A~F）× 10 孔 = 60 孔；
  - KS800：8 组（A~H）× 10 孔 = 80 孔；
  - KS1200：12 组（A~L）× 10 孔 = 120 孔。
- 每组固定 **5 行 × 2 列 = 10 孔**，组名 A/B/C……沿台面从左到右排列；
- 孔号**跨组连续编号**：A 组 0~9，B 组 10~19，C 组 20~29……
  第 1 行左边为组内第一个号，右边为第二个号，依此类推；
  加样列表的「样本位置」直接使用这个全局孔号（如 `10`）；
- 结构为**上下两层**：
  - 上层是一块平板，平板上开圆孔用于插采血管；组名标签在平板上方；
  - 下层是一块托板，板面有对应每个孔的托底凹槽/支撑圈，
    四周有前挡边和侧挡边，上下层由左右侧板和后板连接；
- **默认血样区是空的，不插管**；只有在加样列表中添加了样本、并且该样本
  分配了孔位后，对应孔位才动态画出采血管；删除样本或修改孔位时同步移除/移动；
  PPP+PRP 模式下每个样本会在两个孔位各画一支管；
- 当前选中样本行的采血管会高亮（蓝色光晕/描边），PPP+PRP 时两个孔都高亮；
  待定行进入拖动模式后，原孔高亮取消，改为目标孔上的**强蓝虚线预览 +
  “移动中”标签**，目标被占用时变红；
- 采血管是**敞口管**，顶部没有管帽：管口画玻璃壁和深色内腔；
- 采血管穿过上层平板后，管身下段在上下层之间的开放区域可见，并落在下层
  托板的支撑凹槽中；上层孔口前侧内壁遮住穿板部分，形成插入深度；
- 鼠标悬停空孔显示全局孔号、组名、行/左右侧；悬停采血管显示样本液体说明，
  PPP+PRP 模式下会标注该管是 PPP 还是 PRP；
- **PPP+PRP 模式**：每个样本的两支管按“第一个孔 PPP、第二个孔 PRP”绘制；
  PPP 管上层使用**更透亮的淡黄色**，PRP 管按样本质量颜色显示；
- 采样质量对应颜色（`basic/sampleQuality`）：
  - **全血**（`wholeBlood`）：两支管**下层都是红色血液、上层都是淡黄色**
    （约下部 60% 血液 / 上部 40% 血浆），管口下方留出约 32% 的空管；
    PPP 管的上层改用更透亮的淡黄色，PRP 管上层为普通淡黄色；两支管在
    下层托板处的可见管段均为红色；
  - **血浆**（`plasma`）：上层和下层可见管段**全部淡黄色**，液位较高；
    PPP 管使用更透亮的淡黄色，PRP 管为普通淡黄色；
- 样本质量在设置页修改后，再次进入加样页或切换标签时会重新读取并刷新
  管色和高度列名称。

## 仪器坐标表

`InstrumentCoordinates` 用于在内存中保存整台仪器的定位坐标：

- 3 个 Z 轴：`Gripper`（抓手）、`ReagentNeedle`（试剂针）、
  `SampleNeedle`（样本针）；
- 三根 Z 轴在水平方向错位安装，同一个目标位置要让不同工具垂直对准，
  XY 是不同的；因此每个目标位置保存 **3 组工具坐标**，每组包含：
  - 该工具对准目标时的 X / Y；
  - 该工具在这个 XY 位置的 Z 下降高度；
- 特殊位置：原点、清洗孔位、弃杯孔位；
- 数组位置（下标从 0 开始）：
  - 通道：最多 12 个；
  - 试管：最多 4 盘 × 60 = 240 个；
  - 试剂位：10 个，命名顺序为
    `AA左、ADP左、EPI左、COL左、RIS左、AA右、ADP右、EPI右、COL右、RIS右`；
  - 样本孔：最多 120 个；
- 按机型自动限制有效数量：
  - KS600：4 通道、2 盘 120 试管、60 样本孔；
  - KS800：8 通道、3 盘 180 试管、80 样本孔；
  - KS1200：12 通道、4 盘 240 试管、120 样本孔。

```cpp
InstrumentCoordinates coords(MachineModel::KS1200);

// 抓手对准 CH1（抓手自己的 XY）
coords.setChannelXy(0, InstrumentCoordinates::ZAxis::Gripper,
                    100.0, 200.0);
coords.setChannelZ(0, InstrumentCoordinates::ZAxis::Gripper, 35.0);

// 样本针对准 CH1（XY 与抓手不同）
coords.setChannelXy(0, InstrumentCoordinates::ZAxis::SampleNeedle,
                    102.5, 198.0);
coords.setChannelZ(0, InstrumentCoordinates::ZAxis::SampleNeedle, 42.0);

// 定位：取出指定工具的 XY + 下降 Z
const auto target = coords.locateChannel(
            0, InstrumentCoordinates::ZAxis::SampleNeedle);
if (target.valid()) {
    // XY 移动到 target.x / target.y，样本针下降到 target.z
}
```

Z 值为该工具在这个 XY 位置的下降高度；如果下位机使用相对量，
可用 `target.z - 该工具原点Z` 计算。

### 坐标维护页面

入口：**设置 → 维护 → 维护设置 → 坐标维护**。

- 左侧树按当前机型列出：原点、清洗孔位、弃杯孔位、通道 CH1~CHn、
  试管盘/试管、试剂位（AA左、ADP左、EPI左、COL左、RIS左、AA右...）、样本孔；
- 每个目标右侧显示“已设置 / 部分设置 (n/3) / 未设置”；
- 右侧按工具分三行：**抓手、试剂针、样本针**，每行分别填写该工具自己的
  X、Y 和 Z 下降高度；勾选「启用」表示该工具参与该位置的定位；
- 因为三根 Z 轴水平位置不同，同一个目标位置的三行 X/Y 通常不同；
- 「保存坐标」写入 `InstrumentCoordinates` 并立即保存到默认坐标文件；
- 「导出坐标」把当前机型所有已设置的坐标保存为 JSON 文件，默认文件名
  `coordinates_<机型>_<时间>.json`；
- 「导入坐标」读取 JSON 文件，确认后覆盖当前内存坐标；文件机型与当前机型
  不一致时按当前机型范围导入，超范围的位置跳过，越界数值忽略并在结果中提示；
  导入结果同样立即写回默认坐标文件；
- 坐标表与主窗口共享：`window.instrumentCoordinates()`，后续测试流程
  可直接用 `locate...()` 取 XY 和 Z 高度；
- 默认坐标文件为**程序目录**下的 `platelet_coordinates.json`
  （即与 `PlateletAggregation.exe` 同目录），软件启动时由
  `MainWindow::loadInstrumentCoordinates()` 自动读取到内存；
  文件不存在或损坏时只写日志，不阻塞启动；
- 旧版本曾把坐标文件放在 `AppDataLocation`，首次启动时若程序目录没有文件、
  旧文件存在，会自动复制到程序目录，避免升级后丢失已录入坐标；
- 左侧树下方的「定位校验」区是运动协议接入前的 UI 骨架：选择工具后显示
  当前目标的 X/Y/Z；点击「触发移动」只通过 `motionRequested(MoveTarget)`
  信号把目标交给外部，当前不会向仪器下发运动命令。

### 坐标文件格式

导出文件为 UTF-8 JSON，单位 mm，示例：

```json
{
  "format": "PlateletAggregationCoordinates",
  "version": 1,
  "machineModel": "KS1200",
  "unit": "mm",
  "exportedAt": "2026-11-21T18:30:00",
  "targets": [
    {
      "kind": "channel",
      "index": 0,
      "name": "CH1",
      "tools": {
        "gripper": {
          "xyValid": true, "x": 100.0, "y": 200.0,
          "zValid": true, "z": 20.0
        },
        "sampleNeedle": {
          "xyValid": true, "x": 101.5, "y": 199.5,
          "zValid": false, "z": 0.0
        }
      }
    }
  ]
}
```

- `kind`：`origin` / `cleaningHole` / `discardCup` / `channel` / `tube` /
  `reagent` / `sampleWell`，`index` 从 0 开始；
- `tools`：`gripper` / `reagentNeedle` / `sampleNeedle`，可只出现其中一组；
- `xyValid` / `zValid` 为 `false` 时对应坐标不生效；导入会先清空全部内存坐标；
- 导出只写入当前机型下已设置过坐标的工位，文件可在同机型设备间复制使用。

### 定位校验（先复位 Z 再动 XY）

坐标维护页左侧树下方提供「定位校验」区：

- 工具下拉框：抓手 / 试剂针 / 样本针；
- 目标显示当前树选中位置，X/Y/Z 来自 `locate(kind, index, axis)`；
- 「触发移动」要求 XY 和 Z 都已设置，点击后由
  `CoordinateVerificationController` 执行以下流程：
  1. 同时下发三个 Z 轴复位命令（默认速度 100）；
  2. **只有三个 Z 轴都收到“执行完成”报告后，才发送 XY 移动**；
  3. XY 收到“执行完成”后，再让所选工具下降到目标 Z；
- 任何一步收到错误/故障码或超时（Z 复位 20s、XY 30s、Z 下降 30s）
  都会立即中止；Z 轴未全部复位时绝不会发送 XY；
- 进度显示在底部状态栏，失败时弹窗提示；完成后工具停在目标 Z 不自动抬起，
  下次触发会重新先复位三个 Z 轴。

## 运动控制接口（单动作）

`motionprotocol.h/.cpp` 负责功能码 0x17 的 15 字节帧构帧/解帧，
`motioncontroller.h/.cpp` 负责把命令排队到通讯线程发送。
主窗口通过 `window.motionController()` 取接口。

```cpp
auto *motion = window.motionController();

// XY 联动移动到坐标表里的抓手位置 (100, 200)
motion->moveXy(100, 200, MotionProtocol::XyReference::Gripper);

// 单轴下降
motion->moveGripperTo(20);
motion->moveReagentNeedleTo(35, MotionProtocol::Area::Test, true);
motion->moveSampleNeedleTo(42);

// 复位
motion->resetXy();               // 默认速度 720
motion->resetGripper();          // 默认速度 100
motion->resetSampleNeedle();

// 血样泵（1000ul）/试剂泵（100ul），行程 0~7200
motion->moveBloodPump(7200, true);      // 吸样
motion->moveReagentPump(3600, false);   // 吐样
motion->resetBloodPump();               // 默认速度 2160
```

协议要点：

| 轴号 | 含义 | 从机地址 |
|------|------|----------|
| 0 | Z1 抓手 | 0x04 |
| 1 | Z2 试剂针 | 0x04 |
| 2 | Z3 样本针；发到主控板时同一轴号为 1000ul 血样泵 | 0x04 / 0x05 |
| 3 | 100ul 试剂泵 | 0x05 |
| 4 | XY 联动 | 0x05 |

- 帧长固定 15 字节，功能码 0x17，命令编号 1~255 自动轮转；
- ctrl 字节：axis(bit7~5) | operation_mode(bit4~3) |
  cmd_status(bit2~1) | type(bit0)；
- 4 字节距离为小端有符号整数；XY/Z 与坐标表同为整数 mm，
  柱塞泵行程 0~7200；
- `cmd_size` 默认 0，与旧上位机一致；
- 单动作只负责发送，不等待执行完成；发送结果看 `commandSent`，
  仪器报告帧看 `reportReceived`；组合动作（先 XY、再下针、再泵等）
  后续在流程层组合这些方法；
- 所有命令走现有通讯线程 `CommunicationWorker::sendFrameRequested`，
  不在 GUI 线程直接操作串口；
- 坐标维护页的定位校验由 `CoordinateVerificationController` 串行驱动：
  三个 Z 复位全部完成回执后才发 XY，XY 到位后才下针。

## 结果信息与查询

「结果信息」和「查询」都是隐藏标签头的标签页，通过顶部按钮切换：

- **结果信息**：`ResultsInfoWidget` 内嵌 `ResultsTableWidget`，完全用
  `QPainter` 自绘表头、斑马纹行、网格线和文本，列包括：
  样本号、患者、样本类型、试剂数、检测时间、机型；
- **查询**：`QueryWidget` 顶部提供样本号/患者姓名关键字输入，
  「查询」按关键字过滤，「清空条件」恢复全部记录；
  下方复用同一个自绘表格；
- 两个页面进入时自动从 `TestDataStore::recentSamples(500)` 刷新，
  结果信息页和查询页也都有刷新按钮；
- **质控** 目前为空白占位页，等待后续接入功能；**加样** 已接入右侧加样列表
  和左侧按机型绘制的双层 3D 采血管架，详见下节。

## 全局医疗风格控件

`medicalwidgets.h/.cpp` 提供四个自绘子类，全软件的下拉框、复选框和数字输入框
统一使用：

- `MedicalComboBox`：`QComboBox` 子类；
- `MedicalCheckBox`：`QCheckBox` 子类；
- `MedicalSpinBox`：`QSpinBox` 子类；
- `MedicalDoubleSpinBox`：`QDoubleSpinBox` 子类。

样式与交互：

- 白底、浅灰蓝描边、6px 圆角；焦点用 `#2C7BBF` 2px 描边；
- 数字框/下拉框最小高度 32px，复选框最小高度 30px，适合触屏/戴手套操作；
- 复选框为自绘 18px 圆角方框：选中时蓝底白色勾，支持半选横线；
  悬停/按下/焦点/禁用都有独立状态；
- 禁用态灰底灰字；下拉弹窗白底、浅蓝选中行；
- 自绘下拉箭头和上下按钮，不随 Windows 主题变化；
- 滚轮不改变数值或下拉选项，滚轮事件交给外层页面滚动，避免误操作；
- 数字框内部仍使用 QLineEdit，保留输入校验、全选、键盘上下键等原生能力；
- 已替换的复选框：基础设置通道启用/加样扫码、测试项目多选对话框、
  实时曲线底部「测试曲线/集聚曲线」、坐标维护工具启用勾选。

## 关闭确认

主窗口重写了 `closeEvent`：点击标题栏关闭按钮或 Alt+F4 时会弹框确认；
如果当前有通道模拟测试在运行，会额外提示“关闭软件会中断测试”。
选择“否”则保留窗口，选择“是”才真正退出。

## 日志

`AppLog` 在程序启动时接管 Qt 日志，按天写入文本文件：

- 目录：程序目录下的 `logs/`，文件名 `PlateletAggregation_yyyy-MM-dd.log`；
  程序目录不可写时自动回退到 `QStandardPaths::AppDataLocation/logs`；
- 保留最近 15 天（含当天）：启动时和跨天换文件时自动删除更早的日志；
- 每行格式：`yyyy-MM-dd HH:mm:ss.zzz [级别] 内容 (文件:行号)`；
- 顶部「日志」按钮打开「运行日志」页：只读等宽字体显示当天日志文件末尾
  512 KB，支持「刷新」和「打开日志目录」；
- 已记录的主要事件：程序启动/退出、操作员登录/切换用户、串口连接/断开/错误、
  维护登录成功/失败、测试开始/暂停/继续/复位、坐标加载/保存/导入/导出、
  定位校验请求、加样样本新增/删除/样本号修改；
- 安装方式：`main.cpp` 调用 `AppLog::install()`，退出前 `AppLog::uninstall()`；
  其他位置直接使用 `qDebug()/qInfo()/qWarning()/qCritical()` 即可；
- 日志写入带互斥锁，通讯线程等后台线程记录日志也是安全的。

## 构建运行

用 Qt Creator 打开 `PlateletAggregation.pro`，选择 Qt 5.10 套件构建运行即可；
也可以在命令行里 shadow build：

```bat
mkdir build
cd build
qmake ..\PlateletAggregation.pro
mingw32-make        :: MinGW
:: nmake / jom     :: MSVC
release\PlateletAggregation.exe
```

工程已加入 `QT += sql`；发布运行时需要同时部署 `Qt5Sql.dll` 和
`sqldrivers/qsqlite.dll`。测试数据保存在用户 AppData 目录，用户信息与试管盘
状态仍使用 QSettings（Windows 注册表）。

已用 Qt 5.10.0 MinGW 编译并做过界面截图验证；已验证登录 → 主界面 → 切换用户 →
登录、设置对话框/模组页自定义标题栏、标题栏拖动与双击最大化、无边框窗口四边缩放、
exe 图标、30 秒连接超时提示和模组串口收发；AD 部分已验证 KS600/KS800/KS1200
的 1/2/3 模组轮询、10 次采样后每秒中位数/均值滤波、ModulePanel 每秒刷新、
模组1/2 的 CH1~4 / CH5~8 编号显示，以及通讯线程的 queued 跨线程收发和启停。
本次新增的基础配置 → 维护密码 → 维护菜单流程、主界面 KS1200 三模组
伪 3D 可视化（黄色 PRP/PPP、试管底部磁棒、进度环、管口 AD、马达）、
水晶风格顶部功能区（结果信息/耗材管理/试管盘/查询/质控 + 圆形开始 + 暂停/复位/清洗/加样/设置）、
开始按钮测试期间禁用、切换用户取消回到主界面、样本质量与按机型通道启用、
通道禁用红叉、用户管理增删改查与权限、通道1模拟测试、全局“开始测试”状态、
点击测试中玻璃管弹出实时曲线弹窗、测试/集聚曲线切换，以及底部连接状态栏和
「切换用户」链接，也已在 Qt 5.10 MinGW 下构建并截图验证。
主板状态解析也通过合成 15 字节状态帧做了校验，并在主界面底部状态栏验证了
正常/故障两种显示。
本轮又验证了放大后的图标功能区、模组顶部温度、无主板时状态栏显示「未连接」，
以及模组下方 11 个 3D 试剂瓶面板在 1240×800 与 960×640 下的布局/滚动表现。
最新一轮还验证了按实拍形状绘制的 S1 切角方瓶、透明玻璃试剂小瓶、
AA/ADP/COL/EPI/RIS 左右成对名称、
状态栏 7 个主板状态小图标（含异常红色闪烁），以及耗材管理对话框在实时/
等待两种数据源下的显示和实时刷新。
本轮新增验证：
- 单个试剂 300 秒 / 300 个 PRP 点的模拟与曲线轴；
- SQLite 测试数据库：PPP + 5 个试剂 ×（PRP0 + 300 点）保存/读取自检通过；
- 5 个左侧顶部功能按钮在 1240×800、960×640 下布局正常；
- 试管盘维护界面：4 个盘、每盘 60 支、状态计数和颜色验证；
- 主窗口关闭确认逻辑已接入（存在测试时提示会中断测试）。

## 主板状态轮询（地址 0x05，功能码 0x16）

串口联机后由 `MainBoardPoller` **每秒**发送一次主板状态读取命令：

```
05 16 FE 00 01 00 00 00 00 00 00 00 00 00 00
```

- 只解析从机地址 `0x05`、功能码 `0x16`、命令编号 `0xFE` 的状态报告；
- 状态字节：
  - `[4]`：type、cmd_status、Clean、cup1~cup4；
  - `[5]`：Waste、height、fan、cool、pump1/2、ThreeWay1/2；
  - `[8]`：mfc_status / reg_type；
  - `[9]`：刷卡瓶数；
  - `[10]`：耗材余量状态 / 耗材种类；
  - `[11..12]`：耗材余量（低字节在前）；
  - `[13..14]`：试剂有效期年/月；
- 底部状态栏摘要显示：命令错误、S2清洗液不足、废液满、托盘缺失、读卡失败/刷卡异常等；
  - 正常：绿色「主板：正常」；
  - 等待/运行中：黄色；
  - 故障：红色；
- 鼠标悬停在主板状态上可查看完整信息：外部清洗液S2、各托盘、废液、测高、蜂鸣器、
  制冷、隔膜泵、三通阀、MFC、刷卡类型/瓶数、耗材状态/种类/余量、有效期和原始帧；
  耗材的集中管理入口为顶部「耗材管理」；
- 托盘数量按机型检查：KS600=2、KS800=3、KS1200=4；
- 每秒主板状态应答不写入模组通信日志，只刷新状态栏；
- 协议来源：`主板协议内容.txt`（主板信息 V1.0.1）。

### 状态栏状态小图标

主板摘要右侧固定显示 **7 个** `MainBoardStatusIconBar` 小图标：

1. 外部清洗液 **S2**（水滴）；
2. **废液**（废液桶）；
3、4、5、6. **托盘 1~4**（圆盘内编号）；
7. **测高检测**（上下箭头 + 基准线）。

- 正常：蓝色图标 + 绿色状态点；
- 异常：图标/状态点变红，并按 **430ms** 间隔闪烁；
- 托盘未使用：灰色显示；
- 鼠标悬停每个图标可查看具体状态，例如「托盘3：缺少」「测高检测：失败」；
- 串口断开或等待状态时图标恢复灰色等待；主板每秒状态到达后自动刷新。

### 耗材管理界面

顶部「耗材管理」按钮打开非模态 `ConsumablesDialog`，集中显示主板读到的耗材信息：

- 顶部四个摘要卡：**耗材种类、耗材余量百分比/进度、试剂有效期、刷卡瓶数**；
- 「各试剂剩余总量」：按试剂显示剩余总量（**瓶**）、左侧/右侧单瓶余量百分比；
  试管剩余总量使用 **板** 为单位；
- 「主板耗材明细」：读卡器 MFC 状态、刷卡类型、刷卡瓶数、耗材余量状态、
  耗材种类、耗材余量（0~512 / 百分比）、试剂有效期、最近更新时间；
- 数据来源：
  - 主板真实状态到达后显示绿色「实时数据」；
  - 断开/等待时显示灰色「等待主板状态」；
  - 各试剂总量/单瓶余量来自主板逐个读取命令（见下节）；
- 对话框非模态，主板每秒上报后实时刷新；切换到登录页时自动隐藏；
- 当前版本为**只读展示**，更换耗材、写卡/充值等写操作需要后续接入主板命令。

### 各试剂耗材余量逐个读取

串口连接后，`MainBoardPoller` 每 **500ms** 发送一条耗材读取命令，
循环读取 13 个位置（命令号 100~112）：

- 位置 0~7：AA、EPI、ADP、COL、RIS、内部清洗液 S1、试管、外部清洗液 S2；
- 位置 8~12：AA1、EPI1、ADP1、COL1、RIS1，对应右侧试剂瓶的单瓶余量；
  即除 5 种试剂整体余量外，再额外读取 5 次右侧瓶位。

读取命令为 15 字节帧（功能码 0x16，字节 4 = 0x01 表示读）：

```
05 16 <100+位置> 00 01 00 00 00 <位置<<4> 00 <位置<<4> 00 00 00 00
```

应答沿用主板 15 字节帧：

- `[8]` 低 4 位 mfc 工作状态，高 4 位刷卡数据类型；
- `[9]` 剩余总量：试剂/清洗液为 **瓶**，试管为 **板**；
- `[10]` 低 4 位 cons_status，高 4 位 Types_of_cons
  （0 AA、1 EPI、2 ADP、3 COL、4 RIS、5 内部清洗剂、6 试管、
  7 AA1、8 EPI1、9 ADP1、10 COL1、11 RIS1、12 NONE）；
- `[11..12]` 单瓶剩余比例 0~512（0~100%，低字节在前）。

界面把 0~4 的整体瓶数作为“剩余总量”，`[11..12]` 作为左侧单瓶余量；
8~12 的应答作为对应试剂右侧单瓶余量。

### 联机启动校准（灌注 / 升温 / 调光）

坐标文件在**登录前**就已读入内存；用户登录后、首次串口连接成功时，
主界面显示半透明蒙版并禁止操作，自动执行：

1. **开机灌注**：先执行 `PerfusionController`（见下节），
   三根 Z 轴复位完成后才允许 XY 移动；
2. **模组升温**：向所有活动模组下发目标温度 **37℃**，等待各模组温度都
   **≥37℃**；升温超过 **5 分钟**仍未达到则中止启动校准并提示；
3. **调光**：各通道下发调光值 **3300**，**等待 AD 稳定后再判断**：
   - 调光后至少等待 4 秒；连续 3 次采样变化不超过 3 才认为稳定，最多等 20 秒；
   - 稳定后偏差仍超过 **±100** 才弹窗提示可能存在遗留试杯；
   - 点确定后用模拟进度代表丢杯（真实丢杯动作后续接入），随后重新按 3300
     调光，并重新等待 AD 稳定后再判断；
   - 重新调光后仍超差则提示并停止自动校准；
4. **LED 电流微调**：先重新读取一次各通道当前 LED 电流，再按 AD 与 3300 的
   大小关系逐点调整：AD 小于 3300 就把电流 +1，大于 3300 就把电流 -1，
   允许误差 **±3**；每调整一次电流都等待约 4 秒让 AD 变化稳定后再判断，
   没有轮数上限，直到对准为止；若电流到 0 或 600 仍无法对准则弹窗提示；
5. **保存参数**：所有通道对准后，对每个模组执行「保存参数」，下次开机可直接复用。

蒙版文字依次为「开机灌注中」→「模组升温中」→「控温调光中」；
校准完成后自动撤掉蒙版。登录前即使仪器已连接也不会开始启动校准；
断线会中止本次校准、取消正在执行的灌注并撤掉蒙版，重新连接后重新开始。

### 开机灌注（perfusionAction）

`PerfusionController` 移植老软件 `QUIUtils::perfusionAction` 的主流程：

- 三根 Z 轴依次复位（样本针 → 试剂针 → 抓手）；
- XY 负速度复位 → 零位 (0,0) → **样本针原点 XY**；
- 样本针下到**坐标表「原点」里样本针的下降高度 Z**，血样泵排空 → 复位 →
  吸 **4320**（60% 量程）→ 再次下针 → 排空并清洗 **6000ms** → 复位 →
  回吸空气 **360**；
- XY 到**试剂针原点 XY**，试剂针下到**坐标表「原点」里试剂针的 Z**，
  重复同样泵流程（吸满 **7200**，清洗 **6000ms**，回吸空气 **720**）；
- 每一步收到“执行完成”报告后才发下一条；任一步报错、故障码非 0 或超时
  立即中止；前三条一定是 Z 复位，**Z 未全部复位时不会发送 XY**；
- 原点 XY 和 Z 取自坐标表「原点」的样本针/试剂针工具坐标；
  XY 或 Z 未设置则中止并提示；高度由维护人员现场调整；
- 老函数中 `enoughReagent=true` 时追加的 `cleaningDoubleNeedleAction`
  （清洗双针、清洗液量/比例相关）尚未移植，后续需要清洗液阈值和试剂比例配置。

#### 开机灌注的作用

- **排空气、建立液柱**：久置或新开机后针管、泵腔和管路里可能有空气，
  直接吸样会造成体积不准、液面探测误判；灌注先把泵腔和针管润湿、排气；
- **清除残留**：排掉上次留在泵和针里的液体，避免样本间/试剂间交叉污染
  和试剂被稀释；
- **形成空气隔断**：流程结束回吸 360/720 的空气段，减少针尖滴液和污染；
- **动作自检**：逐条确认 Z 轴、XY、大小泵都能执行并回报“执行完成”，
  尽早暴露卡滞、通信异常；
- **防撞**：前三条固定为 Z 复位，全部完成回执后才发 XY，避免针在下方时横移。

它**不能替代**清洗双针（清洗液池）和液面探测校准；本次按你的要求
只接灌注，不接清洗动作。

## 串口通讯说明（重要）

- 仪器串口固定 **1,000,000 / 8N1 / 无流控**，DTR/RTS 置位，与现有仪器一致。
- 程序启动即按 **VID 0x0483 / PID 0x5740** 自动查找端口，连接状态显示在主界面
  **底部状态栏**；未连接时每 2 秒重试，掉线后自动重连。
- 启动后 **30 秒**仍未连接：弹出「串口连接异常」提示，继续自动重连；
  如果超时之后才检测到仪器并连上，会提示“请关闭软件后重新启动”，
  以保证仪器初始化时序正确。
- 所有设备共用一套 **15 字节帧**：从机地址 1~3=检测模组、4=Z 轴、5=主板；
  接收端默认接受功能码 0x15/0x16/0x17，后续新增功能码调用
  `SerialPortManager::registerFunctionCode()` 注册。
- 接收按“从机地址 1~5 + 已知功能码”对齐，错位自动丢字节重同步，
  完整帧通过 `SerialPortManager::frameReceived(QByteArray)` 抛出。
- 模组控制页 `frameReady` → `SettingsDialog::sendFrameToInstrument()`
  → `CommunicationWorker::sendFrameRequested()`（queued 到通讯线程）
  → `SerialPortManager::sendFrame()`；未连接/发送失败通过 `sendResult` 回日志。
- 通讯线程收到的非周期应答通过 `CommunicationWorker::frameReceived` 转给
  `ModuleControlWidget::handleIncomingFrame()`，更新温度、转速、电流读回值。
- 主板状态轮询已接入 `MainBoardPoller`，与模组轮询在同一个通讯线程运行；
  后续运动控制可继续按同样方式在 `CommunicationWorker` 中增加轮询/解析，
  复用 `SerialPortManager::sendFrame()` 和 `frameReceived`。

## 模组通道 AD 采集与滤波

- **每 100ms（每秒 10 次）**给每个活动模组发一次 `readTemperatureAndChannels()`
  （15 字节帧）；
- 活动模组数：KS600=1、KS800=2、KS1200=3。机型读取协议接入前默认
  **KS1200（3 个模组）**，也可在维护设置 → 仪器参数中修改并保存；
  协议接入后调用 `MainWindow::setMachineModel()`，
  模组控制页会自动启用/禁用对应 Tab；
- 模组与通道映射：模组1=通道1~4，模组2=通道5~8，模组3=通道9~12；
- 滤波：每路通道维护 1 秒采样窗口（正常约 10 个样本），只接受 2000~3300 的
  AD 值：
  - 1~2 个样本取平均；
  - ≥3 个样本取中位数（去掉突发尖峰）；
  - 本秒无样本时保持上一次滤波值；
  - 每累计 10 次采样（约 1 秒）输出一个值并清空窗口；
- ModulePanel 的「通道 AD」标签只由每秒滤波结果刷新，不跟着每一帧抖动；
- 轮询应答通过 `ModuleAdPoller::isPeriodicReply()` 识别，不写入模组通信日志，
  避免每秒刷屏；手动「读温度/通道」的应答仍正常显示。

### 通讯线程

- `MainWindow` 创建 `QThread` + `CommunicationWorker`，`SerialPortManager`、
  `ModuleAdPoller`、`MainBoardPoller` 都运行在通讯线程里；
- 自动重连（每 2 秒）、10Hz AD 轮询和 1Hz 主板状态轮询都在通讯线程，
  不受界面卡顿影响；
- 界面通过 queued 信号槽交互：
  - 发命令：`sendFrameRequested()`；
  - 收数据：`frameReceived` / `moduleAdUpdated` / `mainBoardStatusUpdated` /
    `sendResult`；
- `MachineModel`、`ModuleAdPoller::ModuleAdData` 和 `MainBoardStatus` 在
  `main.cpp` 里 `qRegisterMetaType()`，用于跨线程 queued 信号；
- 后续运动控制交互直接加到 `CommunicationWorker` / 同一通讯线程；
- 退出时 `MainWindow` 用 `BlockingQueuedConnection` 调 `stopCommunication()`，
  停掉串口和所有轮询后再退出线程。

---

# 检测模组通讯接口（协议 V1.1.0）

检测模组通讯模块只依赖 **Qt5 Core**（`QByteArray` / `QString`），
不包含串口收发逻辑。调用方负责把 `QByteArray` 送给下位机，并把收到的 15 字节
应答帧交给 `DetectionModule::parse()`。

| 文件 | 说明 |
|------|------|
| `DetectionModule.h` | 协议常量、帧结构、`DetectionModule` 类声明 |
| `DetectionModule.cpp` | 组帧 / 解帧 / 温度换算 / 参数钳位实现 |

每个模组一个 `DetectionModule` 对象：最多 3 个模组（地址 `0x01` / `0x02` / `0x03`），
每个模组 4 路通道。

---

## 1. 帧结构（固定 15 字节）

| 序号 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 1 | `slave_addr` | 1 | 从机地址：`01`=模组1、`10`=模组2、`11`=模组3（二进制写法，即 0x01/0x02/0x03） |
| 2 | `func_code` | 1 | 固定 `0x17` |
| 3 | `cmd_num` | 1 | 命令编号 `0x01~0xFF`，本类自动轮转 |
| 4 | `cmd_size` | 1 | 缓存命令容量；本类默认 0（实机现有下位机命令一直填 0） |
| 5 | `ctrl` | 1 | `cmd_func(5bit) << 3 \| cmd_status(2bit) << 1 \| type(1bit)` |
| 6 | `temperature_rep` | 2 | 温度原始值，实际温度 = 原始值 × 0.0625 |
| 7 | `adcvalue_rep` | 8 | 4 路通道值，每路 2 字节 |

`ctrl` 字节：

- `type`：`0` 命令（上位机发送 = 写），`1` 报告（上位机发送 = 读）
- `cmd_status`：`00` 等待，`01` 运行中，`10` 执行完成，`11` 命令错误（从机掉线）
- `cmd_func`：
  - `00000` 读取温度和通道数据
  - `00001` 读取 LED 电流 / 设置 LED 光值（按 `type` 区分）
  - `00010` 设置电机转速（可读可写）
  - `00011` 保存参数
  - `00100` 设置 LED 电流值

通道值含义（由 `cmd_func` 决定）：

| 命令 | 通道值含义 | 范围 |
|------|-----------|------|
| `00000` 读回 | 温度 + 4 路通道 AD 值 | 2000~3300 |
| `00001` 写 | LED 调光值（4 路可相同或逐路） | 2000~3300 |
| `00001` 读回 | 4 路 LED 电流 | 0~600 |
| `00010` 写/读 | 电机转速 | 700~1028（1028 停止，936 正常） |
| `00100` 写 | 4 路 LED 电流 | 0~600 |

> 字节序：实机现有代码（`GlobalData` / `QUIUtils`）的 16 位字段均为
> **低字节在前（小端）**，本模块保持一致。若新固件改为大端，只需修改
> `DetectionModule.cpp` 里的 `appendU16LittleEndian()` 和
> `readU16LittleEndian()`。

---

## 2. 快速上手

### 2.1 单个模组

```cpp
#include "DetectionModule.h"

using platelet::DetectionModule;

DetectionModule module1(DetectionModule::Module1);   // 0x01
module1.setTargetTemperature(37.0);                  // 目标温度 37 ℃，之后每个写命令都会带上

// 4 路调光 2400
QByteArray frame = module1.setDimming(2400);
emit WriteArryCommand(frame, QStringLiteral("模组1调光"));

// 通道 2 转速 936（正常旋转）
emit WriteArryCommand(module1.setSpeed(2, 936), QStringLiteral("模组1通道2转速"));
// 通道 2 停止
emit WriteArryCommand(module1.stopChannel(2), QStringLiteral("模组1通道2停止"));
// 再次按之前设置的 936 旋转
emit WriteArryCommand(module1.startChannel(2), QStringLiteral("模组1通道2启动"));

// 通道 3 LED 电流 300
emit WriteArryCommand(module1.setChannelLedCurrent(3, 300), QStringLiteral("模组1通道3电流"));

// 保存参数（含目标温度）
emit WriteArryCommand(module1.saveParameters(), QStringLiteral("保存模组1参数"));

// 读取温度和 4 路通道数据
emit WriteArryCommand(module1.readTemperatureAndChannels(), QStringLiteral("读模组1温度"));
```

### 2.2 三个模组、12 路通道

```cpp
DetectionModule modules[3] = {
    DetectionModule(DetectionModule::Module1),
    DetectionModule(DetectionModule::Module2),
    DetectionModule(DetectionModule::Module3)
};

// 全局通道 1~12 → 模组下标、模组内通道 1~4
DetectionModule &moduleOf(int globalChannel, int *localChannel)
{
    *localChannel = (globalChannel - 1) % 4 + 1;
    return modules[(globalChannel - 1) / 4];
}
```

### 2.3 解析应答

```cpp
QString error;
DetectionModule::Frame reply = DetectionModule::parse(receivedFrame, &error);
if (!reply.valid) {
    qWarning() << "解析失败:" << error;
    return;
}
if (reply.isError()) {
    // cmd_status == 11：从机掉线 / 命令错误
    return;
}
if (reply.isChannelReport()) {           // 00000 报告
    double temperature = reply.temperatureCelsius;
    int ch1Adc = reply.channel(1);       // 1~4 路，2000~3300
    // ...
}
if (reply.isSpeedReport()) {             // 00010 报告
    int ch1Speed = reply.channel(1);     // 700~1028
}
if (reply.isLedCurrentReport()) {        // 00001 报告
    int ch1Current = reply.channel(1);   // 0~600
}
```

`reply.cmdNum` 对应请求帧的 `module.lastCommandNumber()`（同一个模组内轮转，
回包可用于匹配请求）。

---

## 3. 接口一览

| 方法 | 作用 |
|------|------|
| `setTargetTemperature(℃)` | 只设置缓存目标温度，随后所有写命令都会带上 |
| `setTemperature(℃)` | 设置目标温度并立即下发一帧 |
| `setDimming(2000~3300)` | 4 路同一调光值 |
| `setChannelDimming(1~4, 值)` | 单路调光，其余通道 0 = 不修改 |
| `setLedCurrent(0~600)` | 4 路同一 LED 电流 |
| `setChannelLedCurrent(1~4, 值)` | 单路 LED 电流，其余通道 0 = 不修改 |
| `setSpeed(1~4, 700~1028)` | 设置单路转速并记住该值 |
| `setAllSpeeds(700~1028)` | 4 路设置同一转速 |
| `startChannel(1~4)` | 按记住的转速旋转 |
| `startAllChannels()` | 4 路按各自记住的转速旋转 |
| `stopChannel(1~4)` | 停止（转速 1028），不覆盖记住的转速 |
| `stopAllChannels()` | 停止全部 4 路 |
| `saveParameters()` | 保存参数（含目标温度） |
| `readTemperatureAndChannels()` | 读温度和通道数据 |
| `readLedCurrent()` | 读 4 路 LED 电流 |
| `readSpeed()` | 读 4 路转速 |
| `configuredSpeed(1~4)` | 查询记住的转速 |
| `parse(frame)` | 解析 15 字节应答帧 |

超范围参数会被钳位到合法边界，并通过 `lastError()` 返回提示；
通道号非法时接口返回空 `QByteArray`。调用方发送前可检查 `frame.isEmpty()`。

---

## 4. 温度设置说明

协议表里没有单独的“设置温度”命令，`temperature_rep` 字段在每个帧里都有，
协议注明“上位机发送的数值是温控目标”。因此：

- 本类缓存一个目标温度（默认 37 ℃），**每个写命令**（调光、电流、转速、保存）
  都会携带它；
- `setTemperature(℃)` 用“电机转速写命令 + 4 路通道 0”下发目标温度。
  通道 0 沿用固件“0 = 不修改”的约定，不会改变转速；
- 需要断电保存时调用 `saveParameters()`。

---

## 5. 帧示例（小端）

模组1，目标温度 37 ℃（原始值 592 = 0x0250），命令编号 1：
设置通道 2 转速 936（0x03A8）：

```
01 17 01 00 10 50 02 00 00 A8 03 00 00 00 00
```

- `01` 从机地址
- `17` 功能码
- `01` 命令编号
- `00` cmd_size
- `10` ctrl：cmd_func=00010，status=00，type=0
- `50 02` 温度 0x0250（小端）
- `00 00` 通道1 = 0（不修改）
- `A8 03` 通道2 = 0x03A8 = 936
- `00 00 00 00` 通道3、4 = 0（不修改）

模组2，调光 2400（0x0960，4 路相同）：

```
02 17 02 00 08 50 02 60 09 60 09 60 09 60 09
```

模组1，读温度与通道数据（type=1）：

```
01 17 04 00 01 00 00 00 00 00 00 00 00 00 00
```

应答示例（温度 37.5 ℃、4 路 AD 2400/2500/2600/2200）：

```
01 17 04 02 05 58 02 60 09 C4 09 28 0A 98 08
```

---

## 6. 集成

把 `DetectionModule.h` / `DetectionModule.cpp` 加入工程即可（需要 Qt5 Core，C++11 及以上）。
qmake 示例：

```qmake
HEADERS += path/to/DetectionModule.h
SOURCES += path/to/DetectionModule.cpp
QT += core
```

VS 工程把两个文件加入项目，确保包含目录能找到头文件。
