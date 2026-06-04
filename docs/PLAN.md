# RecordLab ROS 架构重构计划

## 1. 总体目标

严格按照上传的架构图重构为两个仓库：

- `/home/hyren/Recordlab_host`：C++ 实现，作为稳定 Host 仓库。这里放 UI、Agent 管理、Watchdog、DataReceiver、DataRegistryServer、ScriptsActuator、Host 内部消息总线、进程/线程管理和中间件适配层。Host 不随 BSP、NVIZ、UR、新设备、新脚本业务变化而修改。
- `/home/hyren/Recordlab_nodes`：Python 实现，作为业务 Nodes 仓库。这里放 `agents_config.json`、Python node、设备适配、业务脚本、XREAL SDK 相关 Python 进程等。
- `/home/hyren/echo_message_system`：中间件仓库，Host 直接调用 C++ 版本，Node 直接调用 Python 版本。执行中确认 C++ 高层 API 原先偏 Master 发现和动态端口，Python 高层 API 偏固定端口直连，因此已修改 C++ 版本中间件，补齐固定端口 ActionClient 与 Subscriber 能力，使 Host 不再在自身仓库里重写一套 Python wire protocol。
- `/home/hyren/RecordLab_Py_old`：只读参考。重构优先按新架构图设计，只在字段格式、可复用算法、测试样例上参考老项目，不按老项目结构机械迁移。
- `/home/hyren/RecordLabC`：只读参考。架构错误的c++版本，但是有很多新功能的具体实现

## 2. 仓库目录结构

`/home/hyren/Recordlab_host`：

```text
Recordlab_host/
  CMakeLists.txt
  README.md
  config/
    host_runtime.json
  include/
    recordlab_host/
      app/
        recordlab_host_app.h
      ui/
        main_window.h
        entry_page.h
        agent_page.h
        script_page.h
        data_page.h
      agents/
        agent_config_loader.h
        agent_manager.h
        base_agent.h
        main_agents.h
      lifecycle/
        watchdog.h
        agent_health_state.h
      communication/
        echo_action_client.h
        echo_topic_subscriber.h
        data_registry_server.h
        message_json.h
      data/
        data_receiver.h
        sensor_queue.h
        frame_decoder.h
      scripts/
        scripts_actuator.h
        script_process.h
      bus/
        host_message_bus.h
        host_message.h
        blocking_queue.h
      common/
        logger.h
        process_handle.h
        thread_loop.h
  src/
    app/
      recordlab_host_app.cpp
    ui/
      main_window.cpp
      entry_page.cpp
      agent_page.cpp
      script_page.cpp
      data_page.cpp
    agents/
      agent_config_loader.cpp
      agent_manager.cpp
      base_agent.cpp
      main_agents.cpp
    lifecycle/
      watchdog.cpp
      agent_health_state.cpp
    communication/
      echo_action_client.cpp
      echo_topic_subscriber.cpp
      data_registry_server.cpp
      message_json.cpp
    data/
      data_receiver.cpp
      sensor_queue.cpp
      frame_decoder.cpp
    scripts/
      scripts_actuator.cpp
      script_process.cpp
    bus/
      host_message_bus.cpp
      host_message.cpp
      blocking_queue.cpp
    common/
      logger.cpp
      process_handle.cpp
      thread_loop.cpp
  tools/
    `recordlab_host_app`.cpp
    recordlab_cli.cpp
  tests/
    test_agent_config_loader.cpp
    test_agent_manager.cpp
    test_echo_action_client.cpp
    test_echo_topic_subscriber.cpp
    test_host_message_bus.cpp
    test_data_registry_server.cpp
    test_data_receiver.cpp
    test_watchdog_state_machine.cpp
    test_script_process.cpp
  host_scripts/
    install_dependencies.sh
    start_recordlab.sh
```

Host 仓库不放 `agents_config.json`。Agent 配置描述的是业务 node 类、Action 连接入口、topic、脚本和参数，属于 Nodes 仓库。

`/home/hyren/Recordlab_nodes`：

```text
Recordlab_nodes/
  pyproject.toml
  README.md
  config/
    agents_config.json
  recordlab_nodes/
    __init__.py
    common/
      commands.py
      topics.py
      message_types.py
      logger_config.py
      rate_limiter.py
      motion_detector.py
      data_registry_client.py
    core/
      base_node.py
      main_node.py
      node_runtime.py
      publishers.py
      record_writers.py
    nodes/
      imu_sim/
        imu_sim_node.py
        dataset_device.py
        csv_data_reader.py
        imu_data_player.py
      bsp/
        bsp_main_node.py
        bsp_raw_node.py
        bsp_device.py
      nviz/
        nviz_node.py
        nviz_state_detector.py
        shell/
      ur/
        ur_controller_node.py
      pc_shell/
        pc_shell_node.py
    scripts/
      record_ur_gt_3dof.py
      record_ur_gt_3dof_batch.py
      check_environment.py
      localhost/
        play_video_on_secondary_screen.sh
        stop_video.sh
    node_scripts/
      imu_simulation_record_demo.py
  tests/
    test_base_node_contract.py
    test_main_node_contract.py
    test_node_runtime.py
    test_agent_config_contract.py
    test_imu_sim_node.py
    test_imu_sim_recording.py
```

不再为每个 node 单独设计 `manifest.json`。原因是 `agents_config.json` 已经是 Nodes 仓库的业务配置入口，里面只声明 node class、Action 连接入口、topic、启动参数和脚本路径；如果再给每个 node 增加 manifest，会造成同一信息分散在两个配置里，Host 也需要多读一层业务文件，不符合 Host 稳定、Nodes 负责业务配置的边界。

## 3. 运行时进程与线程

### 3.1 Host 进程

Host 常驻进程只有一个：

- 进程 P0：`recordlab_host_app`

`recordlab_host_app` 是 C++ 进程，包含 6 类线程。

线程 T0：UI 主线程。

- 运行代码：`recordlab_host_app.cpp`、`main_window`、`entry_page`、`script_page`、`data_page`。
- 职责：Qt GUI、入口页、主 Agent 选择、脚本页、单步命令页、数据显示刷新。MainWindow 直接持有 `HostMessageBus`、`AgentManager`（T2 线程）、`DataReceiver`（T3）、`ScriptsActuator`（T5），通过 30Hz QTimer 轮询总线并将消息转换为 Qt 信号分发给子页面。
- 通信方式：只向 Host 内部消息总线发布命令消息或订阅状态消息。UI 不直接连接设备 node，不直接订阅设备 topic，不直接执行业务命令。
- 进程清理：`recordlab_host_app` 启动时调用 `ProcessHandle::killByCmdlinePattern("recordlab_nodes.core.node_runtime")` 清理上次运行残留的 node_runtime 进程。关闭时 MainWindow 析构函数按序销毁 ScriptsActuator → DataReceiver → AgentManager（AgentManager::stop() 内通过 ProcessHandle::terminate() 先 SIGTERM 进程组、3 秒后 SIGKILL）。
- 已删除：`ImuRuntimeBridge` 曾作为中间 God Object 承担 Logger 初始化、AgentManager/DataReceiver 创建与生命周期管理、同步阻塞等待等越界职责，现已将其功能拆入 MainWindow 与 App 入口。

线程 T1：Watchdog 线程。

- 运行代码：`watchdog.cpp`、`agent_health_state.cpp`。
- 职责：维护架构图中的状态机：`DISCONNECTED`、`INITIALIZING`、`HEALTHY`。
- 通信方式：通过 Host 内部消息总线向 AgentManager 发布 `check`、`init_device`、`estop` 请求；通过消息总线接收 AgentManager 返回的结果。
- 行为：`DISCONNECTED` 下周期性 `check`；`check` 成功后进入 `INITIALIZING` 并触发主 Agent 的 `init_device`；初始化成功进入 `HEALTHY`；`check` 失败或 `estop` 回到 `DISCONNECTED`。

线程 T2：AgentManager 线程。

- 运行代码：`agent_config_loader.cpp`、`agent_manager.cpp`、`base_agent.cpp`、`main_agents.cpp`、`echo_action_client.cpp`。
- 职责：从 `/home/hyren/Recordlab_nodes/config/agents_config.json` 加载 Agent 配置，创建 Agent 对象，启动/停止 Python node 进程，维护 Agent 到 node/action/topic 的映射。
- 通信方式：对内通过 Host 消息总线接收 UI、Watchdog、ScriptsActuator 的请求；对外通过 `echo_message_system` C++ 版本的 ActionClient，与 Python node 使用的 Python ActionServer 通信。
- 设计约束：AgentManager 只知道 Agent、通用 node runtime 启动方式、node class、Action 连接入口、params、topic 名和端口，不知道 BSP/NVIZ/UR 的内部业务，也不预先知道某个 node 支持哪些业务 cmd。

线程 T3：DataReceiver 线程。

- 运行代码：`data_receiver.cpp`、`echo_topic_subscriber.cpp`、`sensor_queue.cpp`、`frame_decoder.cpp`。
- 职责：订阅固定 topic 和动态注册 topic，做频率统计、抽帧、格式转换、UI 缓存更新。
- 通信方式：通过 `echo_message_system` C++ 版本的 Subscriber 订阅 Python node 使用 Python Publisher 发布的 topic；转换后的数据写入 `sensor_queue`，并通过 Host 消息总线发布数据更新事件。
- 处理内容：IMU、图像元数据、record timer、time delay、motion status、自定义注册数据。

线程 T4：DataRegistryServer 线程。

- 运行代码：`data_registry_server.cpp`。
- 职责：接收 Python node 动态数据注册请求，例如某个 node 新增自定义数据名、数据类型和端口。
- 通信方式：对外使用 `echo_message_system` C++ 版本提供注册服务；对内通过 Host 消息总线通知 DataReceiver 添加订阅。

线程 T5：ScriptsActuator 线程。

- 运行代码：`scripts_actuator.cpp`、`script_process.cpp`。
- 职责：启动、停止、监控 Python 实验脚本进程；收集脚本日志、进度、退出码。
- 通信方式：通过 Host 消息总线接收 UI 的脚本请求和发布脚本状态。脚本需要控制设备时，不绕过 Host，不直接访问 Host 内部对象，而是通过 Agent 命令入口触发通用 `cmd + params`。

可选短生命周期进程：

- 进程 P0-cli：`recordlab_cli`。
- 用途：调试 Agent 配置、发送一次性命令、验证 node 是否在线。
- 线程：单主线程即可。
- 约束：不承载业务状态，不替代 ``recordlab_host_app``。

### 3.2 Nodes 进程

Nodes 仓库运行时不是一个总进程，而是多个独立 Python 进程。

进程 P1..Pn：每个启动的业务 node 一个独立进程。

- 示例：`imu_sim_node`、`bsp_main_node`、`bsp_raw_node`、`nviz_node`、`ur_controller_node`、`pc_shell_node`。
- 每个 node 进程由 Host AgentManager 根据 `agents_config.json` 启动。

Python node 进程的主线程运行通用 `node_runtime.py`，不运行具体 node 文件的 main。具体 node 没有独立启动入口；它只是 Python 类，由 runtime 根据 `agents_config.json` 中的 `node_class` 导入和实例化。BaseNode 和 MainNode 只是供具体 node 继承的抽象父类，不负责解析 CLI、不创建 Publisher、不创建 ActionServer、不维持进程生命周期。

每个 Python node 进程包含以下运行单元：

主线程：通用 node runtime。

- 运行代码：`recordlab_nodes.core.node_runtime`。
- 职责：解析 Host 传入的 `agent_name` 和 `agents_config.json` 路径，读取对应 Agent 配置，import `node_class`，实例化具体 node 对象，创建 ActionServer 和 Publisher，把具体 node 的方法绑定为 command handlers，注册信号处理，维持进程生命周期。
- 启动方式：Host 不直接执行 `imu_sim_node.py`、`bsp_main_node.py` 或 `nviz_node.py`，而是统一执行 `python -m recordlab_nodes.core.node_runtime --config <agents_config.json> --agent <agent_name>`。

线程 N1：ActionServer listener 线程。

- 运行代码：`echo_message_system` Python `ActionServer`，由 `node_runtime.py` 创建。
- 职责：监听 Host Agent 发来的 goal，解析 `{"cmd": "...", "params": {...}}`，把命令分发给具体 node 注册的 handler。
- 通信方式：直接调用 `echo_message_system` Python 版本的 ActionServer API。

线程 N2..Nk：命令执行线程。

- 运行代码：具体 node 的命令 handler，例如 `init_device`、`start_device`、`start_record`。
- 职责：执行耗时命令，避免阻塞 listener。
- 通信方式：通过 Python ActionServer 发送 result/feedback；通过 node 内部锁、事件或队列与设备线程通信。

线程 ND：设备采集线程。

- 运行代码：具体设备类，例如 `imu_data_player.py`、`bsp_device.py`、`nviz_state_detector.py`。
- 职责：从 CSV、SDK、socket、shell、UR 控制器等来源采集数据。
- 通信方式：通过 callback 或内部队列把原始数据交给具体 node；具体 node 负责转换为 topic 消息并通过 Python Publisher 发布。

线程 NW：录制写入线程。

- 运行代码：`record_writers.py`。
- 职责：第一版先保留节点内基础录制写入，用于 IMU MVP；后续如果拆出独立录制 node，也必须作为业务 node 放在 Nodes 仓库，而不是改 Host。
- 通信方式：通过内部队列接收采集线程送来的数据批量写入文件。

可选线程：

- 图像解码线程：用于图像流解码或抽帧。
- 状态检测线程：例如 BSP/NVIZ state detector。
- Shell/SDK 管理线程：用于启动外部程序、读取 stdout/stderr、监控退出状态。

进程 PX：Python 实验脚本进程。

- 由 Host 的 ScriptsActuator 启动。
- 脚本本身是独立进程，不嵌入 Host。
- 脚本可以通过 Agent 命令控制已有 node，也可以作为 Python Publisher 发布实验事件 topic。
- 如果脚本需要长期占用 SDK 或自己发布数据，应在 `agents_config.json` 里注册成一个可启动 node 或脚本 agent，而不是写进 Host。

## 4. 进程与线程通信方式

Host 内部通信：

- Host 内部必须实现 C++ 线程安全消息队列组成的消息总线：`host_message_bus`。
- UI、Watchdog、AgentManager、DataReceiver、DataRegistryServer、ScriptsActuator 都是消息总线的生产者或消费者。
- 每类线程拥有自己的阻塞队列；消息总线根据 `target`、`topic` 或 `message_type` 投递消息。
- Host 内部消息必须是结构化消息，例如 `request_id`、`source`、`target`、`agent_name`、`cmd`、`params`、`timeout_ms`、`payload`。
- Host 内部不传递设备对象、不暴露 Python node 实例、不让 UI 直接访问 Agent 内部状态。

Host 到 Python node 控制通信：

- Python node 直接调用 `/home/hyren/echo_message_system` 的 Python 版本 API。
- Host 使用 `echo_message_system` C++ 版本的 ActionClient；Host 仓库内的 `echo_action_client` 只是薄封装，负责把 Host 内部 `cmd + params` 请求转换成中间件 Action goal，并等待 result。
- 控制命令仍使用 Action 语义：goal、feedback、result。node 使用 Python ActionServer，Host 侧适配器负责发送 goal 并等待 result。
- 双方通过 `agents_config.json` 约定控制命令应该发送到哪里，也就是 Action 连接入口：host、端口，或中间件发现所需的 action 名称。这里不约定业务 cmd 列表。
- Goal 的业务 payload 固定为 JSON：`{"cmd": "<cmd_name>", "params": {...}}`。
- Result 的业务 payload 固定为 JSON：`{"success": true|false, "message": "...", ...}`。

Python node 到 Host 数据通信：

- Python node 直接调用 `echo_message_system` Python Publisher 发布 topic。
- Host 使用 `echo_message_system` C++ 版本的 Subscriber 订阅固定端口 topic；Host 仓库内的 `echo_topic_subscriber` 只是薄封装，负责 JSON 解析和回调转发。
- 双方通过 `agents_config.json` 约定 topic 名、端口、编码和数据 schema。
- 高频数据第一版仍走中间件现有能力；后续如果发现 C++/Python 版本协议边界仍不统一，优先在中间件 C++/Python 版本中修正，而不是在 Host 写业务私有协议。

动态数据注册通信：

- Python node 使用 Nodes 仓库的 `DataRegistryClient` 发起注册。
- Host `DataRegistryServer` 使用中间件 C++ 版本暴露注册服务。
- 注册内容包括 `data_name`、`data_type`、`port`、`node_name`。
- Host 收到注册后，通过 Host 消息总线通知 DataReceiver 添加订阅。

脚本通信：

- Host ScriptsActuator 负责创建脚本进程、监控进程、收集日志和进度。
- 脚本控制设备时通过 Agent 命令，不直接操作 Host 内部 C++ 对象。
- 脚本需要发布实验事件时直接调用 `echo_message_system` Python Publisher，并在 `agents_config.json` 或 DataRegistryServer 中暴露给 Host。

## 5. Agent 与 Node 配置接口

`agents_config.json` 位于 Nodes 仓库，是业务 Agent、node class、Action 连接入口、topic 和脚本的统一配置入口。它不声明业务 cmd 列表，Host 也不校验业务 cmd 是否存在：

```json
{
  "agents": {
    "imu_simulation": {
      "name": "imu_simulation",
      "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
      "process_type": "python_node",
      "subnode_host": "127.0.0.1",
      "action_name": "imu_simulation_actions",
      "goal_port": 5690,
      "feedback_port": 5691,
      "data_port": 16510,
      "root_path": "data",
      "init_device_params": {
        "read_path": "data/samples/imu_0.csv"
      },
      "init_device_pause_duration": 0.1,
      "default_scripts": [
        "imu_simulation_record_demo.py"
      ],
      "topics": [
        {"name": "imu_data", "encoding": "json"},
        {"name": "record_timer", "encoding": "json"},
        {"name": "time_delay", "encoding": "json"},
        {"name": "motion_status", "encoding": "json"}
      ],
      "custom_params": {}
    }
  },
  "primary_agents": ["imu_simulation"]
}

```

Host 默认从 `/home/hyren/Recordlab_nodes/config/agents_config.json` 读取，也允许通过命令行参数覆盖配置路径，便于测试。

Host 只校验 `agents_config.json` 的结构，不校验业务 cmd 语义。脚本或 UI 发起的任意 `cmd + params` 都由 Agent 原样转发给对应 node；node 支持就执行，不支持就返回失败。新增 topic、新 node、新脚本通过 Nodes 仓库配置表达，新增业务 cmd 只需要在具体 node 代码里注册/实现。

`default_scripts` 是主 Agent 的可选配置，用于 UI 进入该 Agent 后自动加载常用脚本，减少用户每次手动导入脚本的操作。脚本实际文件位于 Nodes 仓库 `node_scripts/` 下；Host 只负责列出和启动脚本进程，不把脚本内容写进 Host。

## 6. 抽象类与具体 node

`BaseNode` 是抽象父类，只声明最基础的 node 控制接口，不实现命令逻辑、不创建中间件对象。

`BaseNode` 抽象接口：

- `check(params)`
- `estop(params)`
- `get_agent_topics(params)`
- `get_root_path(params)`
- `shutdown()`

`MainNode` 继承 `BaseNode`，也是抽象父类，只声明采集类 node 应该具备的生命周期接口，不实现设备逻辑、不实现录制逻辑。

`MainNode` 抽象接口：

- `init_device(params)`
- `start_device(params)`
- `stop_device(params)`
- `release_device(params)`
- `control_device(params)`
- `start_record(params)`
- `stop_record(params)`

具体 node 类负责实现这些接口，例如 `ImuSimNode` 实现 CSV 读取、IMU 播放、IMU 发布和录制。`node_runtime.py` 负责导入具体 node 类，把具体 node 的方法绑定到 ActionServer，把具体 node 的发布请求绑定到 Publisher。

业务 node 可以注册任意新命令。Host 不保存业务命令列表，不为新命令增加 C++ enum，也不通过配置预声明业务命令，只做通用转发。

第一版 IMU topic schema：

```json
{
  "type": 1,
  "timestamp_ns": 123456789,
  "data": [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]// data0-data5
}
组装形式
Timestamp Type Data0 Data1 Data2 Data3 Data4 Data5

Imu数据结构：
Name: str
Type: int
Timestamp: double
Data0: double
Data1: double
Data2: double
Cam数据结构：
Name: str
Type: int
Timestamp: double
frame_id: uint64_t
Width: int
Height: int
data_list: vector<int>
Time数据结构：
Name: str
Timestamp: double
Duration: double
Delay数据结构：
Name: str
Timestamp: double
Duration: double
自定义数据结构double：
Name: str
Timestamp: double
Data: double
自定义数据结构Vector3d：
Name: str
Timestamp: double
Data0: double
Data1: double
Data2: double
```


第一版静态 topic 使用 agent 级 `data_port`，topic 名称放在 ZeroMQ multipart 第一帧：

- `imu_data`
- `camera_data`
- `record_timer`
- `time_delay`
- `motion_status`

## 7. 实施步骤

- 目前已完成ui和imu_sim节点，计划完整迁移bsp节点

## 8. 测试计划

Nodes 单元测试：

- BaseNode 抽象接口约束。
- MainNode 抽象接口约束。
- node_runtime 命令分发。
- agents_config 结构校验。
- IMU CSV reader/player。
- IMU 发布格式。
- 录制文件输出。

Host 单元测试：

- Host 消息总线投递、订阅、阻塞等待、超时。
- 从 `/home/hyren/Recordlab_nodes/config/agents_config.json` 加载 Agent 配置。
- Host C++ `echo_action_client` 到 Python ActionServer 的互通。
- Host C++ `echo_topic_subscriber` 到 Python Publisher 的互通。
- 补充中间件契约测试目标：验证 C++ 高层 API 与 Python 高层 API 固定端口 Action/Topic 互通。
- DataRegistryServer 动态注册。
- Watchdog 状态机。
- AgentManager 启停 Python node 进程。

集成测试：

- 启动 Host。
- 启动 `imu_sim_node`。
- 发送 `init_device(read_path)`。
- 发送 `start_device`。
- 确认 Host 收到 `imu_data`。
- 发送 `start_record/stop_record`。
- 验证输出 CSV。

边界测试：

- 在 Nodes 新增一个 fake command，Host 不改代码也能转发。
- 在 Nodes 新增一个 fake topic，通过 `agents_config.json` 或 DataRegistryServer 被 Host 订阅。
- 验证 Host 不再保留自写 wire protocol，跨语言互通能力位于 `/home/hyren/echo_message_system` C++ 版本。

## 9. 明确约束

- Host 不拥有业务配置，`agents_config.json` 在 Nodes 仓库。
- Host 内部通信必须通过 C++ 线程安全消息队列实现的消息总线。
- Node 直接使用 `echo_message_system` Python 版本。
- Host 使用 `echo_message_system` C++ 版本与 Python 版 node 互通；Host 仓库不得保留自写 Python wire protocol。
- Host 不写 BSP/NVIZ/UR 专用逻辑。
- Host 不直接调用设备 SDK。
- Host 不保存业务命令枚举。
- Nodes 可以随业务变化而变化。
- 不为每个 node 单独设计 `manifest.json`。
- BaseNode 和 MainNode 是抽象父类，里面不实现业务逻辑。
- 中间件修改只用于 C++/Python 版本协议和高层 API 兼容，不承载 RecordLab 业务逻辑。
- 老 Python 项目只作参考，不作为新架构的边界依据。

## 10. 当前执行进展与新增测试目标

已完成 ：

- `imu_simulation` node MVP、Host UI 基础链路。
- BSP 主节点迁移到 Nodes 仓库方向已落地：`glasses_bsp_node` 作为 Python business node，经通用 `node_runtime` 启动；BSP 相关脚本入口迁入 `node_scripts/`。BSP RGB 底层能力未作为本轮主链路迁移，相关脚本入口保留并明确返回未迁移状态。

执行中发现并修正的问题：

- 删除 `ImuRuntimeBridge`：早期实现中 `ImuRuntimeBridge` 作为 UI 适配层，实际上充当了所有后端组件（AgentManager、DataReceiver、ScriptsActuator）的创建者、配置者和协调者，与 PLAN.md 中各组件独立线程的架构设计矛盾。已将 Logger 初始化上移到 `app/recordlab_host_app.cpp` 入口，AgentManager/DataReceiver/ScriptsActuator 生命周期移入 `MainWindow`，总线轮询和 Qt 信号转换也在 `MainWindow` 中直接完成。删除了 `include/recordlab_host/ui/imu_runtime_bridge.h`、`src/ui/imu_runtime_bridge.cpp` 和 `tests/test_imu_runtime_bridge.cpp`。
- 添加启动/关闭进程清理：启动时 `recordlab_host_app` 通过 `ProcessHandle::killByCmdlinePattern("recordlab_nodes.core.node_runtime")` 扫描 `/proc` 目录清理残留的 node_runtime 进程（先 SIGTERM，3 秒后 SIGKILL）。关闭时 `MainWindow::~MainWindow()` 按序销毁 ScriptsActuator → DataReceiver → AgentManager，后者通过 `ProcessHandle::terminate()` 先 SIGTERM 进程组后 SIGKILL 确保子进程完全终止。


- **-------重要------：**当前 `echo_message_system` C++ 高层 API 与 Python 高层 API 原本不完全跨语言兼容。已按新的执行方向修改中间件 C++ 版本，而不是在 Host 仓库保留私有协议实现。
- 快速命令 result 可能遇到 ZMQ PUB/SUB slow-joiner 风险。高概率场景是 client 刚启动 result/feedback 订阅后立刻发送毫秒级返回的 `check/init_device` 等命令；正常运行中订阅已稳定，或命令本身耗时较长时概率较低。中间件层可以通过订阅 ready handshake、result 改为 REQ/REP、ack 后发布、短期 result buffer 等方式彻底解决。当前第一版只记录该风险，测试中在 client `start_listening()` 后增加启动稳定等待，不在 node runtime 里加入业务延迟。
- BSP 使用的 XREAL SDK 是 Python Qt/QObject 实现。修正方案不是改 Host，也不是让 BSP 拥有独立 main 入口，而是在 Python Nodes 通用 `node_runtime` 增加节点声明式 Qt 事件循环能力：具体 node class 可声明 `requires_qt_event_loop = True`，runtime 在实例化 node 前创建 `QCoreApplication` 并用 Qt event loop 承载 SDK 信号。Host 仍只启动 `python -m recordlab_nodes.core.node_runtime --config ... --agent ...`，不知道 Qt/XREAL/BSP。
- **-------重要------：**当Host 早期 `EchoTopicSubscriber` 只按 JSON parse topic payload，不适合 BSP camera 等二进制/图像 topic。修正方案不是把 BSP 图像流降级成只有元数据的“假 JSON”，而是在中间件/Host 适配层支持 topic `encoding`：Python `echo_message_system` 增加 `json_binary` 编码，允许结构化 JSON 中携带 bytes；Host `EchoTopicSubscriber` 和 `DataReceiver` 按 `agents_config.json` 的 topic encoding 订阅和解析。
- Host `AgentManager` 早期只适配单一 node 进程，切换 primary agent 时如果已有进程会复用旧进程，导致 `imu_simulation` 和 `glasses_bsp_node` 不能按配置切换。已修正为按 agent name 管理当前 node 进程，切换 agent 时停止旧进程、重置 ActionClient、启动目标 node runtime。
- **实现 T1 Watchdog 独立线程**：创建 `include/recordlab_host/lifecycle/` 目录，实现 `Watchdog` 类作为独立 T1 线程。状态机：DISCONNECTED（周期性 check）→ INITIALIZING（不 check，等待 init_device）→ HEALTHY（周期性 check，5s 间隔）。DISCONNECTED 下间隔 2s，3 次连续失败退回到 DISCONNECTED。`estop` 触发立即回退 DISCONNECTED。AgentManager 的消息路由改为 `publishResult`，根据请求来源回发 `CMD_RESULT`（同时发 UI 一份）。
- **修复 EchoActionClient 超时崩溃**：`sendCommand` 中 `sendGoal` 抛异常和 `cv.wait_for` 超时不再抛 `std::runtime_error`（导致 Host 崩溃），改为返回 `ActionResult`（success=false）。BSP 未连接眼镜时 SSH 连接超时不导致进程终止。
- **BSP 设备检查器重构**：`XrGlassesSSHManager` 从 `bsp_device.py` 内联移除，迁入 `recordlab_nodes/common/device_checker.py`。同文件新增 `LsusbChecker`，内置完整的 `usb_product_catalog`（11 款设备：Air/P55/Flora、Ada、Charlie、CORE、Gina、GF、Hylla、Core Pro、GS、Glory、Helen/Helen Pro），`lsusb` 输出与 catalog 匹配后返回设备名称、agent_name 和 default_connection。`BspDevice.check()` 默认使用 lsusb 检测，失败后 fallback 到 SSH。`ssh_preferred` 字段重命名为 `default_connection`。

下一批测试目标：

- Host AgentManager 多 Agent 同时启动、停止、重复启动幂等。
- node runtime 异常设备、端口占用时的错误传播。
- DataRegistryServer 动态 topic 注册和 DataReceiver 订阅链路。
- 中间件跨语言契约测试：继续增加 C++ ActionServer 到 Python ActionClient、Python Publisher 到 C++ Subscriber、C++ Publisher 到 Python Subscriber 的双向矩阵覆盖。
- UI 行为测试：补充脚本页和数据命令页切换时数据视图状态保持、运动状态样式联动、日志和 data 输出目录刷新、命令下拉框和动态命令注册。
