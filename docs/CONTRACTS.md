# RecordLab Host 约定

本文档是当前主机端边界的表。每个修改此处某一行的 PR
都应说明它修改了哪一项边界。

## PR 边界：引入 AgentProxy

此 PR 仅修改 AgentManager 的内部边界：

- `AgentManager` 拥有并路由 `AgentProxy` 实例。

- `AgentProxy` 拥有一个节点外壳：进程生命周期和动作命令通道。

- `DataReceiver` 仍然是 topic 订阅和数据更新的拥有者。

- `ScriptsActuator` 仍然是实验脚本进程的拥有者。

## PR 边界：Watchdog ERROR 状态

此 PR 仅修改 Watchdog 状态机和 AgentManager 的 `init_device_req`
转发：

- `Watchdog` 负责 `DISCONNECTED / INITIALIZING / HEALTHY / ERROR` 状态。
- `ERROR` 表示主机端与设备端状态可能不一致，需要实验人员人工处理。
- `ERROR` 不触发自动 cleanup、release 或 reset。
- `AgentManager` 收到 `init_device_req` 后使用 `AgentConfig.init_device_params`
  调用 `AgentProxy.cmd("init_device", params)`。
- `AgentProxy`、`DataReceiver`、`ScriptsActuator`、`SensorQueue` 不因本 PR 改变边界。

## PR 边界：ScriptsActuator 生命周期事件

此 PR 只完善 `ScriptsActuator` 的脚本生命周期事件，不改变
`AgentManager`、`Watchdog`、`DataReceiver` 边界。

- `SCRIPT_STARTED` 只表示脚本进程已经由 `QProcess::started` 确认启动。
- `LOG_ENTRY` 继续用于人可读日志展示，不作为状态机判断依据。
- `SCRIPT_FINISHED` 继续表示脚本进程退出。

## PR 边界：Logger 定位字段与进程输出来源

此 PR 只增强日志定位信息和进程输出来源，不改变
`AgentManager`、`Watchdog`、`AgentProxy`、`ScriptsActuator` 边界。

- `Logger::log()` 支持可选结构化 context，旧接口保持兼容。
- `script_output` 保留 `text`，增加脚本来源字段。
- `process_output` 用于本地 node stdout/stderr 的结构化来源。
- `log_entry` 继续用于人可读日志展示。

## PR 边界：单 data_port 多 Topic

此 PR 只修改 topic 数据传输端口契约，不改变 AgentManager、Watchdog、
ScriptsActuator 或 DataRegistryServer 边界。

- 每个 agent 配置一个 `data_port`。
- 同一 agent 的多个静态 topic 共用一个 Python/ZMQ PUB socket。
- topic 名称仍作为 ZeroMQ multipart 第一帧，payload 作为第二帧。
- 配置中的 `topics[].port` 不再兼容；出现该字段时加载失败。
- `agent_activated.topics[].port` 是 Host 补出的运行时有效订阅端口，
  值等于该 agent 的 `data_port`。

## 消息类型

| 类型              | 生产者                          | 消费者                  | 载荷                                                         |
| ----------------- | ------------------------------- | ----------------------- | ------------------------------------------------------------ |
| `activate_agent`  | UI                              | AgentManager            | `{agent_name}`                                               |
| `shutdown_agent`  | 生命周期                        | AgentManager            | `{}`                                                         |
| `cmd_request`     | UI、Watchdog、未来的 master CLI | AgentManager            | `{request_id, agent_name, cmd, params, priority, silent}`    |
| `cmd_result`      | AgentManager                    | 原始调用方、UI 可见副本 | `{request_id, agent_name, cmd, success, message}`            |
| `init_device_req` | Watchdog                        | AgentManager            | `{request_id, agent_name}`                                   |
| `estop`           | Watchdog                        | AgentManager            | `{agent_name}`                                               |
| `agent_activated` | AgentManager                    | UI                      | `{agent_name, success, message, subnode_host, topics, init_device_params}` |
| `watchdog_state`  | Watchdog                        | UI                      | `{agent_name, state, reason, consecutive_failures}`          |
| `log_entry`       | 主机模块                        | UI                      | `{message}`                                                  |
| `run_script`      | UI                              | ScriptsActuator         | `{script_path, agent_name}`                                  |
| `stop_script`     | UI                              | ScriptsActuator         | `{}`                                                         |
| `script_started`  | ScriptsActuator                 | UI                      | `{script_id, script_path, agent_name, pid}`                  |
| `script_output`   | ScriptsActuator                 | UI                      | `{text, stream, process, script_path, pid, script_id}`       |
| `script_finished` | ScriptsActuator                 | UI                      | `{script_id, script_path, pid, exit_code}`                   |
| `process_output`  | AgentManager/ProcessHandle      | UI                      | `{text, stream, process, pid, agent_name, node_name}`        |
| `topic_data`      | DataReceiver                    | UI                      | `{topic_name, value, frequency_hz, first_message}`           |

`cmd_request` 向后兼容旧载荷 `{cmd, params}`；当
缺少 `agent_name` 时，AgentManager 会使用当前激活的 agent。

## Agent 配置字段

| 字段                         | 是否必需 | 拥有者                  | 含义                                                         |
| ---------------------------- | -------- | ----------------------- | ------------------------------------------------------------ |
| `agents`                     | 是       | 节点配置                | agent 名称到配置对象的映射。                                 |
| `primary_agents`             | 是       | 节点配置                | 作为可选主 agent 显示的 agent 名称。                         |
| `name`                       | 可选     | AgentConfigLoader       | 默认使用映射中的 key。                                       |
| `node_class`                 | 是       | Python 节点运行时       | Python 节点类的导入路径。                                    |
| `process_type`               | 可选     | AgentProxy              | 默认为 `python_node`；`python_node` 会启动本地 `node_runtime`。 |
| `subnode_host`               | 可选     | AgentProxy/DataReceiver | 默认为 `127.0.0.1`；用于动作和 topic 连接的主机地址。        |
| `action_name`                | 可选     | 配置元数据              | 默认为 `<agent_name>_actions`；当前 EchoActionClient 使用固定端口。 |
| `goal_port`                  | 是       | AgentProxy              | Echo action 的 goal 端口。                                   |
| `feedback_port`              | 是       | AgentProxy              | Echo action 的 feedback/result 端口。                        |
| `data_port`                  | 是       | Python 节点/DataReceiver | Echo topic 数据端口；同一 agent 的所有静态 topic 共用此端口。 |
| `root_path`                  | 可选     | Python 节点             | 默认为 `data`。                                              |
| `init_device_params`         | 可选     | Python 节点命令         | 默认为 `{}`。                                                |
| `init_device_pause_duration` | 可选     | Python 节点/工作流      | 默认为 `0.0`。                                               |
| `topics`                     | 可选     | DataReceiver            | 静态 topic 列表；元素只包含 `name/encoding`，不包含 `port`。  |
| `default_scripts`            | 可选     | UI/ScriptsActuator      | 为所选 agent 列出的脚本。                                    |
| `custom_params`              | 可选     | Python 节点             | agent 专用的业务参数。                                       |

当前字段中不包括：`subnode_path`、`supported_devices`。

## 标准命令

| 命令           | 调用方       | 处理方                     |
| -------------- | ------------ | -------------------------- |
| `check`        | Watchdog、UI | Python 节点 action handler |
| `init_device`  | Watchdog、UI | Python 节点 action handler |
| `start_device` | UI/scripts   | Python 节点 action handler |
| `stop_device`  | UI/scripts   | Python 节点 action handler |
| `start_record` | UI/scripts   | Python 节点 action handler |
| `stop_record`  | UI/scripts   | Python 节点 action handler |

AgentProxy 只负责传输命令。命令的具体实现属于节点。

## Logger 定位字段

`Logger::log()` 支持可选结构化 context。优先使用以下字段定位错误：

| 字段         | 含义                               |
| ------------ | ---------------------------------- |
| `request_id` | Host 内部请求/响应匹配 ID。        |
| `agent_name` | 目标 agent。                       |
| `node_name`  | node 名称；当前默认等同 agent 名。 |
| `script_id`  | ScriptsActuator 为脚本运行生成的 ID。 |
| `cmd`        | agent/node 命令名。                |

## 标准静态 Topics

| Topic           | 生产者      | 消费者          | 编码          |
| --------------- | ----------- | --------------- | ------------- |
| `imu_data`      | Python 节点 | DataReceiver/UI | `json`        |
| `camera_data`   | Python 节点 | DataReceiver/UI | `json_binary` |
| `record_timer`  | Python 节点 | DataReceiver/UI | `json`        |
| `time_delay`    | Python 节点 | DataReceiver/UI | `json`        |
| `motion_status` | Python 节点 | DataReceiver/UI | `json`        |

以上静态 topic 使用所属 agent 的 `data_port`。当前主机边界中尚未实现动态 topic 注册。
