# bbport 0.6 功能落地状态

本表续接 `UPSTREAM_06_INTEGRATION.md`，只核对已合入功能的构建、入口与证据，不重复上游审查。
源码合入、构建成功、CPU 可达和玩家实机通过分别记录；未以其中一项替代另一项。

| 功能 | 构建与入口 / 默认 | 实际验证及剩余项 |
| --- | --- | --- |
| 原生鼠标相机 | 0.6 直接相机路径＋候选安全模块；默认 off；受控 `--native-mouse` 绑定 v2 scope | 普通/ASan各7组及真实生产函数边界通过，Astra复核通过；尚未实机或玩家验收，不能称手感已修复 |
| 摇杆鼠标回退 | 保留；未选原生、站点不匹配或超时后的新输入可回退 | 前轮玩家确认可操作但不跟手；原生队列取消后不重送同一增量 |
| 菜单鼠标指针 / 捕获 | Native 编译；匹配 GFx 站点后默认 on，`BB_MENU_POINTER=0` 可关闭；F1/点击捕获 | 前轮菜单显示正常；hover/click/wheel 的真实操作验收仍需补，不能仅凭 Hook 安装日志认定 |
| 复合键 / 滚轮 / 提示图标 | pad、录入工具、launcher 与提示渲染均参与构建；提示按最近输入设备切换 | CPU 输入与录入回归通过；前轮真实键盘提示已观察；PS 手柄提示切换待实测 |
| 手柄 / 触点 / 体感 | SDL 热插拔、校准、双触点及体感源已合入并编译；依设备与设置启用 | pad CPU 回归通过；真实手柄、触点 ID 连续性及混合输入仍需设备验收 |
| 21 种游戏语言 | launcher 实际有 0..20 共21个唯一系统语言码，runtime 接收 `BB_LANGUAGE` | 实际选项清点通过；资源取决于发行版资产，不声称逐语言游戏显示全部实测 |
| 中文界面 | launcher 中文与 Native/NR 中文保留并编译；游戏简/繁体选择含既有地区解锁补丁 | launcher 翻译与中文地区补丁 CPU 回归通过；Native CJK 字形另依赖字体，AppImage 须带字体或显式 `BB_CJK_FONT` |
| DLC / AppContent | 现有查询与挂载源码已编译；本轮补 title/路径/只读/失败回滚/卸载边界 | 标题/路径/只读/失败/并发/卸载及旧FD/重载槽位的实际源码临时文件CPU与ASan通过；真实DLC挂载未验收 |
| 存档 / 恢复 | runtime 原子写与本地 save_manager 保留；私人 Runner 用 seed 副本 | 前轮 Continue 读取通过，save/launcher CPU 回归通过；真实写入后重载与更多存档场景待验收 |
| 旧在线模块 `bbnet` | 已补齐 JSON/miniupnpc，实际编译成功；仅 `BB_ONLINE` 请求才加载 | 禁网进程内 ABI/导入注册通过；真实会话及退出线程闭合未验收 |
| 新在线模块 `bbnet20` / THD | 两套库均实际编译；THD 需 `BB_NET_BACKEND=thd`；默认不启动会话 | shadNet20 与 THD 各自禁网 ABI/导入注册通过；未登录账号、未开始真实会话 |
| BBSC | 两在线库均包含其源码；需 `BB_BBSC=1` 才安装游戏 Hook | 只证明编译，未安装或执行 BBSC Hook；动态 guest patch 与线程停止/回调撤销尚待专项验收 |
| 宽高比 / 画面设置 | 上游设置菜单及运行时路径编译；受控 profile 显式16:9 | 前轮实际1280×720→1920×1080，玩家确认铺满且菜单场景正常；其它宽高比未扩展实测 |
| NR 运行时控制 | 保留 typed mailbox、模型 ACK、真实帧边界与资源末用合同 | `4b4fdcc` 有限四阶段回归通过；本轮新 Native 尚未因此自动获得实机验收 |
| AppImage 依赖 | 新在线依赖为独立目录，未改系统/sysroot；构建清单校验并记录内容 | 尚需 bundle miniupnpc/在线库、其动态依赖、CJK/提示资源、Python/GI 与启动路径回归；本轮不打包 |

在线构建：设置 `BB_ONLINE_DEPS_ROOT` 为含 `usr/` 与 `bbport-online-inputs.json` 的独立已校验 sysroot，
再运行既有 `tools/nr_dev/build.sh`。本机输入及包 SHA 位于 Fork `out/online-deps-20261020/`。
`online-abi-test <绝对库路径>` 仅注册导入，进程内 seccomp 拒绝 socket/connect/sendto/sendmsg；
未调用 session start，不读账号/真实存档。旧模块注册时会尝试检测服务器，该测试明确拒绝这些系统调用。
`tests/test_runtime_net.py` 另验证实际 runtime 仅在 init 成功后向模块发送 mapped/ready/start；
这不补齐旧 ABI 缺失的 stop/join，也不等于 THD/BBSC 的退出生命周期已经解决。

原稳定 `5635804 / 7f69 / 9a825`、私人运行配置和旧 HOLD 不变。新源码、实际构建和实际运行身份应分别核对；
未完成实机项目保留为待验，不因 Git 合入而标为完成。
