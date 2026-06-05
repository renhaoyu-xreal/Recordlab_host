# RecordLab Host 仓库业务耦合全面检查报告

## 一、发现的业务耦合问题总览（10 类，涉及 14 个文件）

| #    | 耦合点                   | 文件                                                         | 严重度   | 核心问题                                                     |
| ---- | ------------------------ | ------------------------------------------------------------ | -------- | ------------------------------------------------------------ |
| 1    | IMU类型映射/UI列表硬编码 | `sensor_workspace_widget.cpp`                                | **严重** | UI 层硬编码 7 个 IMU 通道(type 1~5,12~13)、`camera_data`/`motion_status` 特殊处理、`cam_data` 结构解析、曲线数据自行缓存 |
| 2    | 设备特定错误文案         | `watchdog.cpp` L340-341                                      | **高**   | Watchdog 硬编码"眼镜状态异常"等中文文案                      |
| 3    | 命令超时硬编码           | `agent_manager.cpp` L21-32                                   | **中**   | `start_device`→90000、`init_device`→30000 等写死             |
| 4    | 数据解析器硬编码         | `data_receiver.cpp` + `echo_topic_subscriber.cpp`            | **中**   | `type_vector6_fast`、`cam_data` 结构、`streamKeyFor()` IMU type 字段 |
| 5    | 相机共享内存常量         | `camera_shared_memory.cpp`                                   | **中**   | `kCameraCount=2, kSlotCount=4, kSlotSize=4MB` 编译期常量     |
| 6    | 默认脚本列表硬编码       | `script_page.cpp` L87-88                                     | **中**   | `record_ur_gt_3dof.py`, `check_environment.py`               |
| 7    | 命令下拉列表硬编码       | `data_page.cpp` L53-63                                       | **中**   | 8 个命令名写死在 UI 代码中                                   |
| 8    | 路径硬编码（6+ 文件）    | `app/`, `agent_proxy.cpp`, `scripts_actuator.cpp`, `script_page.cpp`, `data_page.cpp` | **中**   | `third_party/Recordlab_nodes`、`python3.10`、`logs/`、`data/` 等散落各处 |
| 9    | Topic 字段提取硬编码     | `main_window.cpp` L167-170                                   | **低**   | `duration_ns / 1e9`、`time_delay_ns / 1e6`                   |
| 10   | 应用标题/中文标签        | `entry_page.cpp`、`workspace_page.cpp`                       | **低**   | "RecordLabC 控制中心"、"录制时长"、"时间延迟"                |

---

## 二、每个耦合点的最优解耦方案

### 1. `sensor_workspace_widget.cpp` — 严重耦合

**方案**：配置驱动的 UI 生成 + 插件化数据解析器

```
agents_config.json 新增 sensor_layout 字段：

"sensor_layout": {
  "imu_data": {
    "display_name": "IMU数据",
    "channels": [
      { "type": 1, "label": "陀螺仪0", "data_indices": [0,1,2], "unit": "rad/s" },
      { "type": 12, "label": "温度0", "data_indices": [0], "unit": "°C" }
    ]
  },
  "motion_status": {
    "display_name": "运动状态",
    "ui_widget": "label",
    "state_colors": { "moving": "green", "standing": "gray" }
  }
}

UI 侧：从 agent config 读取 sensor_layout → 动态创建列表项、实时值行、曲线面板
曲线缓存 → 移入 SensorQueue
```

### 2. `watchdog.cpp` — 设备错误文案

**方案**：错误码 + 配置模板

```
Watchdog 发布: { "error_code": "INIT_DEVICE_FAILED", "severity": "critical", "params": {} }
agents_config.json: { "error_messages": { "INIT_DEVICE_FAILED": { "title": "${device_type}状态异常", "message": "..." } } }
UI 查配置渲染文案，Watchdog 不包含任何业务文字
```

### 3. `agent_manager.cpp` — 命令超时

**方案**：配置驱动

```json
"commands": { "start_device": {"timeout_ms": 90000}, "init_device": {"timeout_ms": 30000}, ... }
"default_timeout_ms": 5000
```

### 4. `data_receiver.cpp` / `echo_topic_subscriber.cpp` — 解析器硬编码

**方案**：TopicParser 插件接口

```cpp
class TopicParser {
    virtual ParsedValue parse(const json& raw) = 0;
    virtual string streamKey(const json& raw) = 0;
    virtual size_t estimateBytes(const json& raw) = 0;
};
// 工厂按 parse_mode 创建：ImuStreamParser / CameraStreamParser / GenericJsonParser
```

### 5. `camera_shared_memory.cpp` — 相机常量

**方案**：共享内存头部自描述（ROS 消息自描述理念）

```
共享内存头部: { magic, version, camera_count, slots_per_camera, slot_size }
Reader 从头部读取布局参数，不使用编译期常量
```

### 6-9. 脚本列表、命令列表、路径、topic 字段

**方案**：全部配置化

- 脚本列表 → `agents_config.json` 的 `default_scripts`（已有字段，消除 fallback 硬编码）
- 命令列表 → `agents_config.json` 的 `exposed_commands`
- 所有路径 → `host_runtime.json` 扩展 + `PathResolver` 单例
- Topic 字段映射 → `ui_bindings` 配置或 TopicParser 返回标准化信号

---

## 三、与 `docs/各个模块作用及其范围.md` 中 `[ ]` / `[~]` 功能的一体化解法

文档中标记的未完成/部分完成功能，和业务耦合问题是**同一套架构缺陷的两面**——耦合是因为缺乏抽象层，缺失的功能正是那些抽象层。

| 缺失功能                                         | 关联的耦合                                            | 统一方案                                                     |
| ------------------------------------------------ | ----------------------------------------------------- | ------------------------------------------------------------ |
| **DataRegistryServer `[ ]`** node 动态注册数据流 | UI 硬编码 topic 列表、DataReceiver 静态订阅           | node 启动时注册数据流 → DataRegistry 通知 DataReceiver 动态订阅 → 通知 UI 动态生成列表项 |
| **SensorQueue `[~]`** curve_buffer 等            | sensor_workspace_widget 自行维护 `curve_history_` map | SensorQueue 提供 `createCurveBuffer()` / `getCurveBuffer()` 接口 |
| **master_CLI `[ ]`** 脚本通过 Host 请求          | 脚本直连 Python ActionServer                          | 脚本 → master_CLI → bus → AgentManager → AgentProxy          |
| **统一弹窗 `[~]`** ui.dialog.request             | scripts_actuator 中 `QMessageBox` 直调阻塞 UI 线程    | 统一 DialogRequest/Response 协议，ScriptsActuator 发事件，UI 弹窗后回传 |
| **HostMessageBus `[~]`** topic/filter 订阅       | 只能按 consumer 队列消费，无法按消息类型过滤          | 增加 `subscribe(pattern)` 机制                               |
| **录制状态 `[~]`** 结构化录制状态                | 从命令结果推断                                        | 独立 `recording_state` topic                                 |
| **动态 topic `[~]`**                             | 依赖静态 `topics` 配置                                | DataRegistryServer 动态注册                                  |

---

## 四、统一的三层架构蓝图

```
配置/描述层
├─ agents_config.json (单一真相源：sensor_layout, commands, error_messages, exposed_commands, topics, ...)
└─ host_runtime.json (宿主环境：路径, python_bin, ...)
        │
协议/抽象层
├─ TopicParser 接口 + 工厂     (解决解析器硬编码)
├─ DataRegistryServer          (解决静态订阅 → 动态发现)
├─ SensorQueue 完善接口        (解决 UI 自行缓存数据)
├─ DialogRequest 协议          (解决弹窗耦合)
├─ master_CLI 协议             (解决脚本直连)
└─ HostMessageBus filter 机制  (解决按 target 消费局限)
        │
实现层
├─ UI: 动态生成，零硬编码业务知识
├─ AgentManager: 超时/命令列表从配置读取
├─ DataReceiver: 动态订阅 + parser 工厂
├─ Watchdog: 只出错误码
└─ ScriptsActuator + master_CLI: 统一 IPC 通道
```

---

## 五、建议实施顺序

| Phase                | 内容                                                         | 解决耦合                               | 补全功能                           |
| -------------------- | ------------------------------------------------------------ | -------------------------------------- | ---------------------------------- |
| **Phase 1** 基础设施 | PathResolver + TopicParser 接口 + agents_config schema 扩展  | 路径硬编码 (#8)                        | —                                  |
| **Phase 2** 动态发现 | DataRegistryServer + DataReceiver 动态订阅 + UI 动态生成     | UI 硬编码 (#1) + 解析器硬编码 (#4)     | DataRegistryServer + 动态 topic    |
| **Phase 3** 协议统一 | master_CLI + DialogRequest + HostMessageBus filter           | 弹窗耦合                               | master_CLI + 统一弹窗 + bus filter |
| **Phase 4** 完善收尾 | SensorQueue buffer、Watchdog 错误码、命令超时配置、相机 shm 自描述 | 剩余耦合 (#2, #3, #5, #6, #7, #9, #10) | SensorQueue + 录制状态             |

## 六、剩余业务耦合处理状态

本轮已处理原第六节列出的 8 处耦合，状态如下：

| 项 | 原问题 | 当前状态 |
| -- | ------ | -------- |
| 1 | 命令超时 fallback 硬编码 | 已迁移到 `agents_config.json.shared.commands.standard_device_timeouts`；`AgentConfig::commandTimeoutMs()` 只读取配置和通用默认值。 |
| 2 | 默认 exposed commands 硬编码 | 已迁移到 `agents_config.json.shared.exposed_commands.standard_device`；Host 不再生成业务命令列表。 |
| 3 | 默认错误消息模板硬编码 | 已迁移到 `agents_config.json.shared.error_messages.standard_device_errors`；缺省为空对象。 |
| 4 | `agent_proxy.cpp` / `scripts_actuator.cpp` 内部 `python3.10` fallback | 已统一由 `RuntimeConfig::python_bin` 传入；模块内部不再自行猜解释器。 |
| 5 | `recordlab_nodes.core.node_runtime` 硬编码 | 已新增 `RuntimeConfig::node_runtime_module`，启动与残留进程清理都从运行时配置读取。 |
| 6 | UI topic/list fallback | 已删除 IMU 默认列表、实时值默认 7 行、`camera_data` active-tab 路由硬编码和 `imu_data` 首次接收特殊日志；列表/active-tab topic 来自 `sensor_layout`。 |
| 7 | `script_page.cpp` 默认脚本列表硬编码 | 已删除构造函数默认脚本；脚本列表只来自当前 agent 的 `default_scripts` 或用户导入。 |
| 8 | Echo client 名称硬编码 | 已改为由 `AgentProxy` 使用 agent 的 `action_name` 派生 client 名。 |

仍需关注的非阻塞残留：

- `SensorWorkspaceWidget` 仍包含 IMU 向量、温度、motion label、camera preview 的渲染器逻辑。这属于当前 UI 对数据 schema 的认识，不再作为默认列表来源；后续可进一步将 `ui_widget` 映射为独立 renderer。
- `handleCameraData()` 仍认识 `cam_data` / shared-memory image payload。建议后续把相机 payload schema 写入 `sensor_layout.camera_data` 或 topic metadata。
- `main_window.cpp` 保留 `record_timer/time_delay` 兼容 fallback；首选路径已是 `ui_bindings`。当所有配置都具备 `ui_bindings` 后可删除 fallback。

本轮新增：

- `DataReceiver` 维护节点 cookie 表：`{key,value,isDisplay/is_display}`，topic 通过 metadata `{role:"host_cookie"}` 标识，UI 的“数据 + 命令”页展示 `isDisplay=true` 的条目。
- `Recordlab_nodes` 新增 `TOPIC_NODE_COOKIE` 和 `BaseNode.publish_cookie()`，BSP node 初始化/启动成功后上传 FSN、MCU 固件版本等节点自有信息。
- Python `MotionDetector` 已从单帧阈值判断迁移为老项目 RecordLabC 的滑动窗口标准差分类思路。
- 打包脚本已迁移到 Host：`scripts/package_release.sh` 打包 Host、Nodes、echo_message_system 和运行时配置。
