# Nebula 手动排障手册

这份手册用于脚本自动流程失败时，把关键步骤拿出来手动确认。

文中的占位符含义：

- `<phone_ip>`：手机 WiFi IP，例如 `192.168.10.68`。
- `<usb_serial>`：USB 连接时 `adb devices` 里看到的设备号。

##  自动的统一准备

每次重启软件后的第一次测量，都按同一套准备流程做：

1. 手机和电脑连接到同一个 WiFi。
2. 用 USB 线连接手机和电脑。
3. 手机保持解锁亮屏。
4. 如果手机弹出“是否允许 USB 调试”，选择允许。并要允许无线调试
5. 等右上角变成 HEALTHY。
6. 拔掉手机和电脑的 USB 线。
7. 用线连接手机和眼镜。



机械臂连接过程

```
cd UR_controller
bash start_server.sh
```

打开程序，保持连接



同时控制板调为远程控制



但是如果还是出问题了，包括但不限于连接不上，无法录制等，那就按照下面的手动步骤进行排查。

## 1. 手动连接无线 ADB

先看当前设备：

```bash
adb devices
```

如果只看到一个 USB 设备，开启无线 ADB：

```bash
adb tcpip 5555
```

如果报 `more than one device/emulator`，说明电脑同时看到了多个设备，需要断开连接

```
adb disconnect
```

重新插拔数据线，然后输入命令

```bash
adb tcpip 5555
```

连接手机 WiFi 地址：

```bash
adb connect <phone_ip>:5555
```

再次确认：

```bash
adb devices
```

正常时应该看到类似：

```text
192.168.10.68:5555    device
```

常见异常：

- `offline`：手机还没有真正在线，重新确认手机解锁、USB 调试授权、无线调试授权。
- `more than one device/emulator`：断开连接，插拔数据线
- 没有 `device`：手机和电脑可能不在同一个 WiFi，或者手机没授权。

## 2. 手动初始化 Nebula

下面命令需要确认步骤一已经完成

放开 SELinux：

```bash
adb shell setenforce 0
```

确认状态：

```bash
adb shell getenforce
```

如果返回 `Permissive`，说明这一步正常。

推送 Nebula 配置：

```bash
adb push sdk_global.json /sdcard/Android/data/com.xreal.evapro.nebula/files
adb push sdk.json /sdcard/Android/data/com.xreal.evapro.nebula/files
adb push libNRTracker3D.so /sdcard/Android/data/com.xreal.evapro.nebula/files
adb shell mkdir -p /sdcard/Android/data/com.xreal.evapro.nebula/files/NRResource
adb push Config.json /sdcard/Android/data/com.xreal.evapro.nebula/files/NRResource/Config.json
```

## 3. 手动启动或重启 Nebula

先停止 App：

```bash
adb shell am force-stop com.xreal.evapro.nebula
```

再广播启动：

```bash
adb shell  am broadcast -a com.xreal.action.SETUP_WIZARD_FINISH -n com.xreal.evapro.nebula/ai.nreal.nebula.receiver.OOBECompleteReceiver
```

如果脚本里表现和手动命令表现不一样，优先用这一组命令确认 App 是否能被正常拉起。

## 4. 手动开启并确认 3DoF

使用手机 IP 连接 BP 端口 `9898`。

推荐使用已有脚本工具：

```bash
python /home/jhxie/Recordlab_host/third_party/Recordlab_nodes/scripts/ts.py <phone_ip>
```

进入交互后，发送：

```text
Send Data    app!@#3dof
```

查询当前模式：

```text
Send Data    app!@#trackingMode
```

正常返回：

```text
MODE_3DOF
```

如果返回：

```text
MODE_0DOF_STAB
```

就再发送一次：

```text
Send Data    app!@#3dof
```

然后继续查询：

```text
Send Data    app!@#trackingMode
```

直到返回 `MODE_3DOF`。

如果一直没有返回，或者返回为空，重点检查：

- 手机 IP 是否正确。
- 手机和电脑是否还在同一个 WiFi。
- Nebula App 是否已经启动。
- 手机和眼镜是否已经连接。

## 5. 手动检查 CSV 是否在增长

列出手机里的 CSV：

```bash
adb -s <serial> shell 'ls -l /sdcard/3dof_data/*.csv 2>/dev/null'
```

查看行数：

```bash
adb -s <serial> shell 'wc -l /sdcard/3dof_data/*.csv 2>/dev/null'
```

查看最新一行：

```bash
adb -s <serial> shell 'tail -n 1 /sdcard/3dof_data/*.csv 2>/dev/null'
```

正常情况下，应该有 mobile 和 air 两个 CSV 文件。

判断是否正在录制，不要只看文件是否存在，要隔几秒重复看行数。行数持续增加，才说明 CSV 正在写入。

## 6. 手动 pull 和清理 CSV

先在电脑上创建保存目录：

```bash
mkdir -p <local_dir>
```

把 CSV 拉到电脑：

```bash
adb -s <serial> pull /sdcard/3dof_data/<file>.csv <local_dir>/
```

如果有 mobile 和 air 两个 CSV，要两个都 pull 成功后再删除手机端旧 CSV。

删除手机端昨天和昨天以前的 CSV，当天 CSV 保留：

```bash
adb -s <serial> shell 'today=$(date +%y_%m_%d); for f in /sdcard/3dof_data/*.csv; do [ -e "$f" ] || continue; name=${f##*/}; day=$(printf "%s" "$name" | cut -c1-8); case "$day" in [0-9][0-9]_[0-9][0-9]_[0-9][0-9]) [ "$day" \< "$today" ] && rm -f "$f";; esac; done'
```

删除前请确认电脑目录里已经能看到刚刚 pull 下来的文件。



## 7. 报错对照表

| 报错或现象 | 优先检查 |
| --- | --- |
| `ADB Wi-Fi device did not come online` | WiFi 是否相同、USB 调试是否允许、`adb devices` 是否有 `device`。 |
| `No online ADB device` | 手机是否在线、授权是否已允许。 |
| `more than one device/emulator` | 重新连接，保证只存在一个设备或者命令里加 `-s <serial>`。 |
| `Nebula tracking mode is not 3DoF: <empty>` | BP 9898 没连上、App 没起来、IP 不对、眼镜连接异常。（插拔一下） |
| `等待CSV增长超时` | App 可能没开始写 CSV，先查 `/sdcard/3dof_data` 和 CSV 行数。 |
| `stop_record 失败` 或 `pull timeout` | CSV 文件大、WiFi 慢、ADB 断开；可以按第6 节手动 pull。 |
| `Host bridge command timeout` | 可能只是等待结果超时，不等于设备没执行；用 ADB、CSV、UR 实际状态确认。 |

## 8. 一套最短检查命令

如果现场只想快速判断问题在哪里，可以按顺序执行：

```bash
adb devices
adb connect <phone_ip>:5555
adb devices
adb shell getenforce
adb shell 'ls -l /sdcard/3dof_data/*.csv 2>/dev/null'
adb shell 'wc -l /sdcard/3dof_data/*.csv 2>/dev/null'
```

然后确认 3DoF：

```bash
python /home/jhxie/Recordlab_host/third_party/Recordlab_nodes/scripts/ts.py <phone_ip>
```

在交互里发送：

```text
Send Data    app!@#3dof
Send Data    app!@#trackingMode
```

看到 `MODE_3DOF`，并且 CSV 行数持续增加，说明 Nebula 侧大概率正常。
