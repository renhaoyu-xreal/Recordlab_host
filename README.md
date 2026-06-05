# Recordlab_host

RecordLab 的主程序仓库，负责启动 UI、管理 Agent、调用脚本、接收数据并和 `echo_message_system` 通信。

## 架构边界

- `Recordlab_host` 是稳定 Host 仓库，只负责 UI、Agent 管理、Watchdog、DataReceiver、脚本进程和 Host 内部消息总线。
- `third_party/Recordlab_nodes/config/agents_config.json` 是业务配置入口，声明 node class、Action 端口、topic、encoding、parse mode、UI 频率和 QoS。
- `agents_config.json` 支持 `shared.exposed_commands / commands / sensor_layouts / ui_bindings / error_messages / topic_sets`，agent 字段可用字符串引用共享块，避免重复配置。
- `echo_message_system` 是中间件仓库，只提供跨语言 Action/Topic 和通用 QoS/Options，不包含 RecordLab 业务 topic 判断。
- 高频预览等 sensor topic 使用配置化 latest-only/depth=1 语义；录制链路由 Nodes 内部 writer 保存原始数据，不受 UI preview QoS 影响。
- 节点可通过 metadata `{role:"host_cookie"}` 的 `node_cookie` topic 上传 `{key,value,isDisplay}`，数据 + 命令页只展示节点允许展示的条目。

## 首次安装

```bash
git clone https://github.com/renhaoyu-xreal/Recordlab_host.git
cd Recordlab_host/host_scripts
./install_dependencies.sh
```

安装脚本会自动完成这些事情：

- 克隆 `echo_message_system` 到 `third_party/echo_message_system`
- 克隆 `Recordlab_nodes` 到 `third_party/Recordlab_nodes`
- 检查并安装系统依赖，包括 Python 3.10、Python venv、CMake、Qt6 等
- 创建 `.venv-py310`
- 安装 `recordlab_host`、`echo_message_system`、`Recordlab_nodes` 需要的 Python 依赖
- 安装 `third_party/xreal_glasses/xreal_glasses-0.4.3-py3-none-any.whl`
- 编译 C++ host 程序

## 启动软件

```bash
cd Recordlab_host/host_scripts
./start_recordlab.sh
```

启动脚本会先校验依赖，清理之前残留的 RecordLab 进程，然后打开 UI。

## 更新代码

如果用户只克隆了 `Recordlab_host`，推荐直接重新运行安装脚本。它会更新 `third_party` 下的依赖仓库，并重新安装依赖、重新编译：

```bash
cd Recordlab_host
git pull

cd host_scripts
./install_dependencies.sh
```

也可以手动分别更新三个仓库：

```bash
cd Recordlab_host
git pull

cd third_party/Recordlab_nodes
git pull

cd ../echo_message_system
git pull
```

手动更新后仍建议重新运行：

```bash
cd Recordlab_host/host_scripts
./install_dependencies.sh
```

## VSCode 查看多个 Git 仓库

先运行过安装脚本，确保下面两个目录已经存在：

```text
third_party/Recordlab_nodes
third_party/echo_message_system
```

然后用 workspace 打开：

```bash
cd Recordlab_host
code RecordLab.code-workspace
```

这样 VSCode 的 Source Control 面板会同时显示三个 Git 仓库：

- `Recordlab_host`
- `Recordlab_nodes`
- `echo_message_system`

## 脚本执行

软件运行时，脚本功能会使用 `third_party/Recordlab_nodes/node_scripts` 下的脚本。

`third_party/Recordlab_nodes/config/agents_config.json` 里可以为每个主 Agent 配置 `default_scripts`，用于节省用户手动导入脚本的时间。

## Release 打包

```bash
./scripts/package_release.sh
```

输出 `dist/RecordLabHost` 和 `dist/RecordLabHost-linux-x86_64.tar.gz`，包含 Host、Nodes、echo_message_system 与运行时配置。

## 手动构建和测试

一般用户不需要手动执行这些命令，开发调试时可以使用：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
