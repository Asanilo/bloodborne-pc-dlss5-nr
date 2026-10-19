# bbport 0.6 游戏侧审查与窄移植

## 身份与范围

2026-10-20，Fork `Asanilo/bloodborne_pc`；起点 Native 开发分支
`dev/persistent-visual-history-20261015@feda4a1`，独立工作树
`.worktree/gameplay-06`、分支 `dev/gameplay-upstream-06-20261020`。
上游 `deadinside28/bloodborne_pc` 的 master 实查为
`8185b88d80573fa300595d5c54450183b4109935`，共同祖先
`10baf9df25f106da9013433a2e7348839baf917a`。审查覆盖此祖先之后
`c7e3e55`、`3c57da4`、`72e5c8c`、`8185b88` 的实际源码，不能只比较旧 upstream ref
`3c57da4`，否则会漏掉本地尚未接纳的 0.5 改动。

运行基准仍为 Native `5635804` / Helper `7f69` / forwarder `9a825`；本分支 HEAD、
CPU 测试和新编译产物不等于游戏/GPU 验证。原 Native/Helper 分支、master、运行配置、
模型、真实存档和历史证据保持原样。本轮不合并整个 0.6、不打包 AppImage、不启动 GPU。

## 实施前功能矩阵

| 优先级 / 功能 | 本地与上游实际源码 | 决定 |
|---|---|---|
| P1 游戏文本语言 | `launcher/bbport_launcher.py:LANGUAGES` 本地9项含简繁中文；上游8185b88扩为21个PS4语言码；`runtime_services.c:language/system_param` 已直接传递BB_LANGUAGE | 窄移植列表和对应译文，保留本地中文区域解锁；不新增游戏文本资产 |
| P1 菜单语言跟随 | `bbport_settings.cpp:Load/Save` 本地任何保存都会固化menu_language；上游增加menu_language_chosen；本地另有中文和NR菜单 | 只适配偏好标志和语言选择，保留中文及所有NR逻辑 |
| P1 双触摸板点击 | `runtime_pad.c:touch_click` 本地每次覆盖触点；72e5c8c开始累积左右且去重 | 单函数移植与CPU正反回归，保留本地键鼠映射 |
| P1 文件挂载容量 | `runtime_file.c:MAX_MOUNTS` 本地16，启动基础映射与16个保存槽共享；8185b88改64 | 单常量移植，用临时目录验证所有保存槽路径映射及旧接口 |
| 已有：键鼠/控制器 | `runtime_pad.c`、`host_input.h`、`window.cpp`、启动器已有鼠标按钮/移动/灵敏度/反转/慢走、控制器选择/重连/震动、focus与overlay隔离、预设/冲突提示 | 不重复覆盖。上游新默认DS3式布局、鼠标wheel与modifier chords、run/dodge/jump分解另评估，避免静默改老配置 |
| 已有：中文与存档 | `bbport_zh.py`、`bbport_zh.h`、`patches.py:language_patches`；`save_manager.py`和启动器备份/排队恢复、`runtime_file.c`原子保存 | 保留。本轮不以0.6替换文件或迁移真实存档 |
| P2 启动检查/地区/模组 | 上游 `game_check.py`识别9个零售地区及已内置Lance补丁的eboot；`mods.py:content_root`增加地区包装目录；本地已有严格1.09哈希检查 | 值得后续窄移植；正确哈希原本已不按地区拒绝，不能宣称新地区已实机验证 |
| P2 保存音效补齐 | 上游 `runtime_savedata.c:bloodborne_sound_hack`加CUSA03179；既有本地默认音效flag修补与原子保存机制未变 | 有独立收益，但会写保存内容，本轮先审查记录，不碰真实存档 |
| Astra：输入提示/鼠标相机Hook | 新 `bbport_button_prompts`接`gpu/render/vk_button_prompts.cpp`图像/上传/屏障；上游鼠标look接guest detour；本地走HostInput快照 | 只读，需资源寿命/guest hook审查及GPU验证 |
| Astra：效果/宽高比 | `bbport_effect_flags.h`、`guest_hooks`、`bbport_game_menu`热切效果/SSR初始化、宽高比联动输出与camera/UI；设置结构也混入FG | 不直接移植，现有NR固定分辨率/typed mailbox不能被普通设置改写 |
| Astra：加载/兼容/帧率 | FX init clear、提交CB副本、Laurence job/cache、内存模型、F3/帧率Hook、更多draw workers | GX/PM4/所有权/时序边界，只列建议，不能用本轮CPU通过宣称解决旧故障 |
| 暂缓：在线 | `gpu/bbnet`/`bbnet20`、BBSC、THD、`runtime_net/services`、启动器登录/线程/存储认证 | 复杂架构与网络生命周期集成，不能只加UI开关；不接入账户/服务器 |
| 暂缓：通用runtime扩展 | `runtime_compat/ngs2/rewatch/toypad`、NID解析与平台兼容、DLC addcont mount | 更广导出/ABI/路径与生命周期范围；原本已有The Old Hunters entitlement，不以新增mount冒充首次DLC支持 |
| 后续打包 | Vulkan宿主库/EGL/pressure-vessel、GE发现、GPU选择、DLSS库安装UI | 打包优化单独开展，涉及当前锁定运行依赖/选择，不混入本轮游戏侧移植 |

## 验证与交付

本节在实施前建立；实际提交、测试结果、回滚和剩余问题在完成后追加。
所有新测试使用合成输入或临时目录，普通构建写独立输出，不能选用稳定产物运行游戏。
