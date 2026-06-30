# Android 使用手册

## 1. 适用场景

本手册对应主 Agent：

- `android`

适用于 Android IMU 相关实验。

## 2. 进入方式

1. 启动 `./RecordLabHost.sh`
2. 在首页选择 `android`
3. 进入主页面后使用 `脚本执行`

不要点击 `虚拟节点` 页。

## 3. 推荐脚本

常用默认脚本：

- `android/record_android_imu_simple_test.py`
- `android/record_ur_android_imu_batch.py`

选择建议：

- 单次基础录制：优先用 `record_android_imu_simple_test.py`
- 与 UR 联动：使用 `record_ur_android_imu_batch.py`

## 4. 推荐操作流程

1. 选择 `android`
2. 确认手机与电脑连接正常
3. 在 `脚本执行` 页选择脚本
4. 启动脚本
5. 等待录制完成

## 5. 正常现象

- 数据区会显示 `Android IMU数据`
- 脚本可能会自动重启或重连 Android 侧服务
- 停止录制后需要短暂收尾

## 6. 常见问题

### 6.1 没有 Android IMU 数据

处理：

1. 检查主 Agent 是否选为 `android`
2. 检查手机连接状态
3. 重新运行脚本

### 6.2 录制脚本失败

处理：

1. 查看脚本日志中是否卡在 `start_record` 或 `stop_record`
2. 检查手机端服务状态
3. 必要时重连手机后再试

### 6.3 UR 联动脚本不适合普通单机实验

没有机械臂环境时，不要运行 `record_ur_android_imu_batch.py`。

## 7. 用户注意事项

- 普通实验优先使用简单录制脚本。
- 没有开发人员指导时，不要运行 UR 联动脚本。
- 不要点击 `虚拟节点`。
