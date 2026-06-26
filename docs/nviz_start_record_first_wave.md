# NViz 空闲数据与录制首波数据说明

## 目标

本次调整让当前版本 NViz 同时满足两个要求：

1. 不执行录制脚本时，设备只要已经完成 `start_device`，UI 上也能持续看到实时数据。
2. 开始录制时，`start_record` 会像旧版本一样在录制启动点重启一次 `pilot`，让录制文件尽量覆盖到新的第一波数据。

## 调整后的行为

### 1. 空闲态也有 UI 数据

当前版本仍然保留现有 `start_device` 行为：

- `start_device` 会完成 3DoF 配置下发
- `open_pilot_gf.sh` 会恢复正常自启动脚本
- 设备重启后 `pilot` 会自动起来
- `NvizNode` 的 TCP/UDP 接收服务始终常驻

因此即使没有执行录制脚本，只要 NViz 设备已经准备好，UI 上就能正常看到：

- IMU
- time_delay
- motion_status

这部分数据属于“实时显示数据”，不等于已经开始写盘。

### 2. `start_record` 负责抓录制首波数据

为了尽量贴近旧版本的录制体验，当前版本在 `start_record` 时新增了两步：

1. 先把 `recording` 状态和 CSV writer 打开
2. 再执行 `gf_3dof_start_record.sh`

这样 shell 里一旦重启/拉起 `pilot`，重启后的第一波数据到达主机时，写盘线程已经在工作了。

## 关键实现

### `NvizNode.start_record()`

当前顺序为：

1. 创建录制目录
2. 设置 `recording = True`
3. 启动 CSV writer
4. 执行 `gf_3dof_start_record.sh`
5. shell 成功后保留录制状态
6. shell 失败则回滚录制状态并停止 writer

这样做的目的，是让 shell 触发的 `pilot` 重启不再发生在“写盘之前”。

### `gf_3dof_start_record.sh`

当前脚本新增了：

1. 先停止当前正在运行的 `pilot`
2. 保留旧版的 `online_calibration_pre` 备份逻辑
3. 再重新启动 `pilot`

这样 `start_record` 不再只是“对已经运行中的 `pilot` 再执行一次 `./pilot &`”，而是真正把录制起点和 `pilot` 新一轮启动对齐。

## 与旧版本的关系

旧版本的核心特点是：

- 平时 `nviz_node` 就可以收到数据
- 但录制开始时，`start_record` 附近会显式启动 `pilot`

当前版本没有完全回退到旧版本的设备生命周期设计，而是保留了：

- 空闲态有实时 UI 数据
- watchdog 负责设备准备

同时只把“录制首波数据”这个能力补回到 `start_record`。

## 影响

### 正向影响

- UI 空闲态显示不受影响
- 录制开始时更容易捕获到新一轮 `pilot` 启动后的首波数据
- shell 失败时会自动回滚，不会留下假录制状态

### 注意事项

- `start_record` 现在会主动重启一次 `pilot`，录制开始点附近会有一次短暂数据切换
- `record_timer` 仍以稳定进入录制后的 IMU 时间戳为准，不以 shell 启动瞬间为准

## 相关文件

- `third_party/Recordlab_nodes/recordlab_nodes/nodes/nviz/nviz_node.py`
- `third_party/Recordlab_nodes/recordlab_nodes/nodes/nviz/nviz_assets/shell/gf_3dof_start_record.sh`
- `third_party/Recordlab_nodes/tests/test_nviz_node.py`
