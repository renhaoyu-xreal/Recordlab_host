# Nebula 使用手册

## 1. 适用场景

本手册对应主 Agent：

- `nebula_trial`

适用于 Nebula 相关录制实验。

## 2. 进入方式

1. 启动 `./RecordLabHost.sh`
2. 在首页选择 `nebula_trial`
3. 进入主页面后使用 `脚本执行`

不要点击 `虚拟节点` 页。

## 3. 推荐脚本

常用默认脚本：

- `nebula/record_nebula_simple_test.py`
- `nebula/record_nebula_pipeline_test.py`
- `nebula/record_ur_nebula_batch.py`

选择建议：

- 单次基础录制：优先用 `record_nebula_simple_test.py`
- 流程验证：使用 `record_nebula_pipeline_test.py`
- UR 联动批量实验：使用 `record_ur_nebula_batch.py`

## 4. 推荐操作流程

1. 选择 `nebula_trial`
2. 确认手机与电脑在同一个网络环境
3. 在 `脚本执行` 页选择对应脚本
4. 启动脚本
5. 等待录制和文件回传完成

Nebula 的停止录制和文件回传可能比其他节点更慢，请耐心等待。

## 5. 正常现象

- 数据区主要显示录制相关状态，而不是大量实时传感器曲线
- `stop_record` 之后可能还要继续拉取文件
- 脚本停止前会有较长的收尾阶段

## 6. 常见问题

### 6.1 `init_device` 失败

常见原因：

- 手机和电脑不在同一个 WiFi

处理：

1. 先检查网络
2. 再重新运行脚本或重新初始化

### 6.2 停止录制很慢

这通常是正常现象，因为程序可能仍在等待 Nebula 侧停止录制并回传文件。

### 6.3 录制结束但文件还没看到

处理：

1. 先确认脚本是否真的结束
2. 再查看日志里是否有回传完成的信息
3. 不要过早关闭程序

### 6.4 UR 联动脚本不适合普通单机实验

没有机械臂环境时，不要运行 `record_ur_nebula_batch.py`。

## 7. 用户注意事项

- Nebula 对网络环境敏感，优先确认 WiFi。
- 停止录制后不要立刻关闭程序。
- 不要点击 `虚拟节点`。
