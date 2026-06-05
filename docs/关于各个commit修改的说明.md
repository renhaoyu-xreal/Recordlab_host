# 关于各个commit修改的说明

按照对架构修改程度的顺序进行说明

## 1.将topic数据传输中的多端口改为单端口

在之前的python版本中就是多固定端口的，为什么要更改呢。

首先配置简单，没必要为每个topic分配一个端口。

从之前的

```
{"name": "imu_data", "port": 16510}
{"name": "record_timer", "port": 16520}
{"name": "camera_data", "port": 16515}
```

改为

```
"data_port": 16510,
"topics": [
  {"name": "imu_data", "encoding": "json"},
  {"name": "camera_data", "encoding": "json_binary"}
]
```

端口量减为1个，因此更不容易端口冲突，之后的扩展也方便。

而且ZMQ本就支持topic filter ，也就是同一个socket上发很多topic，subscriber根据topic名进行过滤。

最后，改为统一的port后，如果之后想要加入node的动态注册功能，不用分配/记录/释放一个新port。

## 2.Agentmanager所接收和转发的消息和消息负载

存在两种消息大类

通用类消息和专门的消息

很多消息比如start_device，stop_record等等，都是可以放进cmd通用命令的——实际上原来的大部分命令也是这样。

```
payload: {request_id, agent_name, cmd, params, priority, silent}
```

得强调一下负载其实没有什么校验，想怎么写都行。这些只是建议，可以修改。



agent_name 当前active_agent_

params可能的参数，默认空

priority和silent语义是

- priority: 以后队列调度用，比如 Watchdog check 可以 high。
- silent: 不产生 UI 人读日志，比如 Watchdog 周期 check 不要刷屏。

都没怎么用，但是日后可能用的上





## 3.Watchdog error状态

这是我根据新的状态机图加上自己的理解进行修改的。

error代表主机端和设备端状态不一致。加上这个状态是必要的。

不过是否加一个error后自动清理会比较好，或者说必须？



## 4. 引入AgentProxy

这个就是之前的Agent，我觉得agent这个词有点难理解，所以改为AgentProxy，意味着这是node在host侧的代理。

除了符合需求外，比起直接让agentmanager去掌管node，架构会清晰很多。

不用在agentmanager内里塞各种东西，agentmanager只需要路由命令给agentproxy就可以了。

一个agentproxy去管理一个node,也比较自然。



# 5.ScriptsActuator和logger

这些都是小的修改，比如加了个script_started与script_finished相对应。让日志详细一些，到时候方便查。

你觉得这里面的nviz和android符合架构要求吗

但是android_node好像不是主agent啊，这不对，他就应该是主agent

不过，我记得android有不少是依托于nviz的文件的