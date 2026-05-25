# Recordlab_master

`Recordlab_master` 是 RecordLab 新架构的 ROS 风格控制面。

本仓库同时包含稳定的 `system_nodes` 和 `tool_nodes` 实现。它们运行时仍是普通 node，
只通过 Master 注册/发现，不属于 `MasterServer` 内部能力。这样开发设备 node 的仓库
不需要修改 GUI、launcher、recorder、watchdog、health、lifecycle 这些稳定基础设施。

`MasterServer` 只负责：

- node、topic、service、action、type、param 的注册和发现。
- graph event 发布。
- node lease 跟踪，状态为 `alive/stale/offline`。
- 保存 topic 的 transport descriptor，例如 `tcp_pubsub` 或 `shm_ring_buffer`。

`MasterServer` 不负责：

- 启动 node。
- 执行 Python 脚本。
- 保存业务状态或设备 start 参数。
- 自动恢复设备。
- 转发 IMU、图像或视频 payload。

## 模块

```text
recordlab_master      MasterServer、registries、GraphEventBus、LeaseManager
recordlab_core        MasterClient、NameResolver、TypeRegistry、NodeBase、ScriptRunner、日志支撑
recordlab_echo        Service、Action、Topic、ShmRingBuffer、StdioChannel
recordlab_xreal_runtime XREAL Python SDK bridge 适配层，独立可选库，不进入 MasterServer
recordlab_system_nodes RecorderNode、Watchdog、HealthMonitor、LifecycleManager、LauncherNode
recordlab_tool_nodes  recordlab_gui 配置和工具 node 支撑
```

`recordlab_echo` 是新架构统一通信模块，不等于旧 `third_party/echo_message_system`。旧 include wrapper 会短期保留，但新代码应直接使用 `recordlab_core/*` 和 `recordlab_echo/*`。

## 构建

```bash
cmake -S /home/hyren/Recordlab_master -B /home/hyren/Recordlab_master/build
cmake --build /home/hyren/Recordlab_master/build
ctest --test-dir /home/hyren/Recordlab_master/build --output-on-failure
```

## 工具

```bash
/home/hyren/Recordlab_master/build/recordlab_master
/home/hyren/Recordlab_master/build/recordlab_master_cli list nodes
/home/hyren/Recordlab_master/build/recordlab_master_cli list topics
/home/hyren/Recordlab_master/build/recordlab_script_runner
/home/hyren/Recordlab_master/build/recorder_node
/home/hyren/Recordlab_master/build/watchdog_node
/home/hyren/Recordlab_master/build/health_monitor
/home/hyren/Recordlab_master/build/lifecycle_manager
/home/hyren/Recordlab_master/build/recordlab_launcher
/home/hyren/Recordlab_master/build/recordlab_gui
```

`recordlab_script_runner` 是普通长驻 node，运行时注册为 `/script_runner`，不是 `MasterServer` 的一部分。

它提供：

- action：`/script_runner/run_script`
- service：`/script_runner/stop_script`
- topic：`/script_runner/status`
- topic：`/script_runner/log`
- topic：`/recordlab/user_log`
- topic：`/script_runner/progress`
- topic：`/script_runner/workflow`

脚本运行状态只保存在 `/script_runner` 进程内。Master 只保存发现信息。

## 日志系统

Recordlab 有两套日志，边界必须分清：

- 用户可见日志：`/recordlab/user_log`，消息类型 `recordlab_msgs/UserLog`，字段包含
  `timestamp_ms/source_node/level/category/message/details`。`level` 只允许 `INFO`、`WARN`、`ERROR`。
  GUI 只展示这套日志。开发者必须显式发布用户日志，系统内部日志不会自动进入 GUI。
  默认同时追加到 `${RECORDLAB_LOG_ROOT}/YYYYMMDD/user/<source_node>.log`，未设置时使用
  `Recordlab_master/logs/YYYYMMDD/user/<source_node>.log`。
- 系统/组件日志：`recordlab_core/logger.h`，提供 `RL_LOG_DEBUG/INFO/WARN/ERROR`。
  默认写到 stderr 和 `${RECORDLAB_LOG_ROOT}/YYYYMMDD/system/<component>.log`，未设置时写到
  `Recordlab_master/logs/YYYYMMDD/system/<component>.log`。
  这套日志用于 Master、system/tool node、device node 和组件调试记录，不面向普通用户展示。

`/script_runner/log` 短期保留兼容旧订阅方；新的 GUI 使用 `/recordlab/user_log`。

## XREAL Runtime

`recordlab_xreal_runtime` 是独立 CMake target，封装 XREAL Python SDK worker、设备 catalog 和 SDK probe。
它可以被 `Recordlab_nodes` 链接复用，但不链接进 `MasterServer`，不注册 node，不进入 Master graph。
