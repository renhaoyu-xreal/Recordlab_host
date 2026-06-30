# BSP 使用手册

## 1. 适用场景

本手册对应主 Agent：

- `glasses_bsp_node`

适用于 BSP 眼镜设备的 IMU / 相机相关录制。

## 2. 进入方式

1. 启动 `./RecordLabHost.sh`
2. 在首页选择 `glasses_bsp_node`
3. 进入主页面后使用 `脚本执行`

不要点击 `虚拟节点` 页。

## 3. 推荐脚本

常用默认脚本：

- `bsp/record_bsp_imu.py`
- `bsp/record_bsp_imu_cam.py`
- `bsp/record_bsp_imu_static.py`
- `bsp/record_bsp_imu_dynamic.py`
- `bsp/record_ur_bsp_GF_imu_RGBcam.py`

选择建议：

- 只采 IMU：优先用 `record_bsp_imu.py`
- 采 IMU + 相机：优先用 `record_bsp_imu_cam.py`
- 静态实验：优先用 `record_bsp_imu_static.py`
- 动态实验：优先用 `record_bsp_imu_dynamic.py`
- UR 联动实验：使用 `record_ur_bsp_GF_imu_RGBcam.py`

## 4. 推荐操作流程

1. 选择 `glasses_bsp_node`
2. 在 `脚本执行` 页选择对应实验脚本
3. 启动脚本
4. 观察日志和脚本流程
5. 等待录制完成
6. 确认 `stop_record` 完成后再结束实验

## 5. 手动命令流程

如果必须手动操作，推荐顺序：

1. `init_device`
2. `start_device`
3. `start_record`
4. `stop_record`
5. `stop_device`
6. `release_device`

## 6. 正常现象

- 选中 BSP 后可以看到 IMU 数据刷新。
- 使用带相机的脚本时，界面可能显示相机画面。
- 停止录制后，设备和程序可能还要收尾几秒。

## 7. 常见问题

### 7.1 `init_device` 失败

处理：

1. 断开眼镜设备。
2. 等待界面恢复到可重试状态。
3. 重新连接眼镜。
4. 再运行脚本或重新执行 `init_device`。

### 7.2 相机画面异常或没有画面

处理：

1. 先确认使用的是带相机的脚本。
2. 检查设备连接是否稳定。
3. 如果只是 IMU 实验，没有相机画面属于正常。

### 7.3 UR 联动脚本不适合普通单机实验

如果没有机械臂、视频播放或配套流程，不要运行 `record_ur_bsp_GF_imu_RGBcam.py`。

## 8. 用户注意事项

- 先选对脚本，再启动实验。
- 没有开发人员指导时，不要使用 UR 联动脚本。
- 不要点击 `虚拟节点`。
