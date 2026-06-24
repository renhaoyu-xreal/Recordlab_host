2、运动状态监测功能错误
3、清理无关代码
4、中间件中没有用到的消息格式

窗口宽一下
脚本中进行了多余的一次启动
失败，estop，重连，初始化，失败
数据选择还残留上次端口的数据
nviz固定1000hz不跳动

快速操作下，脚本通知watchdog和watchdog自己的start会不会产生竞态

莫名奇妙输出了一个脚本已停止
[2026-06-24 14:48:35.398] [SUCCESS] [COMMAND] start_device: start_device ready: 3dof config applied, pilot started, Glasses connected (169.254.2.1) (after reboot)
[2026-06-24 14:49:43.677] [INFO] [SCRIPT] 运行脚本: /home/hyren/Recordlab_host/third_party/Recordlab_nodes/node_scripts/nviz/record_ur_gt_3dof_batch.py
[2026-06-24 14:49:50.589] [WARNING] [SCRIPT] 脚本已停止
check失败就全部发stop目前阶段会有不少问题，例如初始化等阶段check就不会成功，脚本执行失败，stop眼镜对于用户也是不太好的方案，举例我这边的业务，同事的业务

不播放视频的情况下，移除这个流程显示