# Recordlab_master

RecordLab 的 ROS 风格控制平面 Master。

这个仓库刻意让 `MasterServer` 保持克制：

- 负责注册和发现 node、topic、service、action、type、param。
- 发布 graph event。
- 跟踪 node lease，状态为 `alive` / `stale`。
- 不启动 node。
- 不在 Master 进程内执行 Python 脚本。
- 不恢复设备。
- 不转发 IMU、图像或视频 payload。

高频数据应该使用 `ShmRingBuffer`。Master 只保存共享内存 transport descriptor，不接触真实数据。

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
```

`recordlab_script_runner` 是普通长驻 node（`/script_runner`），不是 `MasterServer` 的一部分。

它负责脚本执行状态、日志、当前行号和 workflow 事件：

- action：`/script_runner/run_script`
- service：`/script_runner/stop_script`
- topic：`/script_runner/status`
- topic：`/script_runner/log`
- topic：`/script_runner/progress`
- topic：`/script_runner/workflow`

这些运行状态只保存在 `recordlab_script_runner` 进程内。`MasterServer` 只保存注册发现信息，不保存脚本状态，也不执行脚本。
