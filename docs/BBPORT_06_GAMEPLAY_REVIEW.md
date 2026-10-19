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
| 暂缓：双触摸板点击 | `runtime_pad.c:touch_click` 上游72e5c8c累积左右且去重，但本地`touch_ids`按数组下标保留身份 | 窄移植测试通过后复核发现冲突：左右同按再松左，右触点继承左ID；CPU已复现，撤回该项，不扩大改造 |
| P1 文件挂载容量 | `runtime_file.c:MAX_MOUNTS` 本地16，启动基础映射与16个保存槽共享；8185b88改64 | 单常量移植，用临时目录验证所有保存槽路径映射及旧接口 |
| P1 启动器参数 | `launcher/bb-launcher.sh` 本地nix-shell回退丢失所有参数；上游3c57da4使用数组与bash `%q`转义 | 窄移植，使缺少系统GTK的环境也能接收`--play`；合成启动器验证参数及特殊字符 |
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

矩阵先于实施建立，以下按源码复核收口。所有新测试使用合成输入或临时目录，
普通构建写独立输出，不能选用稳定产物运行游戏。

| 实际有效提交 | 上游来源 | 验证及限制 |
|---|---|---|
| `e21ad50` 21项游戏语言选择 | `8185b88` #249 | 21个唯一PS4语言码0..20，英文/中文/葡文标签齐全；原2项i18n回归通过。仅选择游戏已有语言，不提供缺失文本资产，不承诺每个发行版全部可用 |
| `bd3f0e1` 菜单语言跟随启动器 | `72e5c8c` #134 | actual-source settings-test通过继承en→zh_CN→ru、明确中文优先及其他设置保存。旧文件已有menu_language继续作为明确偏好，不自动迁移或抹除 |
| `abf2b42` 文件挂载表64项 | `8185b88` | actual-source file-mods-test通过5个基础映射+16个保存槽、表满拒绝/替换/卸载复用、mod只读/保存可写。只扩容；不改变存档内容和算法 |
| `854f4a8` nix-shell参数转发 | `3c57da4` | 真实wrapper配合python/nix-shell替身，两条路径均通过；覆盖--play、空格/引号/空参数/命令替换文本，bash -n通过 |

`febfee0`曾单独移植双触点累积且窄测试通过，但复核发现本地`touch_ids()`按紧凑数组下标
维持身份。actual-source `pad_read_state`序列“Tab+Backspace→松开Tab”实测右触点ID
从3变2，连续触点身份错误；已由`b8ae128`完整撤回该项源码和新增测试，保留两次提交及
CPU失败证据。本轮最终`runtime_pad.c`与起点逐字相同。不将函数级测试通过说成完整输入验收；
下一输入任务需先定义合成/真实SDL触点的跨样本身份匹配，再评估modifier chords、wheel和双触点。

旧源码`feda4a1`负对照：新增settings、file-mods回归分别在“继承语言被固化”“第12个保存
映射失败”处拒绝；旧wrapper被参数/引号回归拒绝。被撤回的touch累积负对照也保留，不能据此
忽略后续身份冲突。首轮Native空目录构建完整编译/链接，但`build.sh --test`的runtime-test命令
漏传prepared LibAtrac9头路径而失败；这是测试配方缺口，未修改构建脚本。补齐该include参数
另行编译现有CPU测试，最终Native wrapper不带`--test`重新构建。首次失败日志保留，不写成全绿。

## Astra只读交接与暂缓依据

- **已有提交副本，不重复搬CP。** 本地`gpu/shadps4/video_core/amdgpu/liverpool.cpp:SubmitGfx`
  已持有`SubmittedCopy`直到解码，并有queue→submit共同发布和逐队列退休。上游新
  `gpu/cp/command_processor.cpp:SubmitGfx`的`force_copy`供FX私有临时命令使用；整体替换会
  丢失本地补强。上游先释放queue锁、再单独更新submit计数是源码差异风险，未在上游实测竞态。
- **优先审FX初始化是否还有缺口。** 上游`bbport_guest_hooks.cpp:FxInitClear/InstallFxInitClear`
  和`bbport_fx_init_clear.h:Plan`接管三个2×2 RG16F零clear，调用
  `gpu/platform/gnm/gnm_driver.cpp:SubmitFxInitZero`私有WRITE_DATA。先与本地
  `bbport_gx_render_owner.h:Patch`的config[0x72]=0同步交接比较，不能直接叠加提交路径。
- **Laurence拆分评审。** `JobSlotFix/JobSlotDrop/InstallJobSlotFix`先证明成功/失败/重试/销毁
  恰好返还一次；`ResourceCacheBacking/InstallResourceCacheSize`另涉及512MiB额外direct
  pool、`runtime_extra_direct_map`与GPU范围追踪，须独立预算/释放/末用审计。
- **输入提示及图形设置。** 新`gpu/render/vk_button_prompts.cpp`创建图像/上传/屏障，
  提示加载也有线程；上游mouse look走guest相机detour。本地HostInput路径保留。
  `ApplyHotEffects/WriteHotCode`运行时写码与renderer对象身份、`SetDisplayAspect`联动camera/HUD/
  Scaleform和实时输出extent，都需要单独审查；NR固定输出/历史/typed mailbox不可由普通菜单绕过。
  F3依赖`bbport_frame_rate.cpp:PatchImage/FrameStep`整套dt/pacer/cloth/奔跑时序Hook，继续暂缓。
- **DLC不是成熟替代。** 上游`runtime_content.c:addon_unmount`为空，mounted[16]无锁且不回收、
  失败前先占槽；`runtime_file.c:game_path`只保护/app0、/hostapp，未保护/addcontN。上游未新增
  mount/unmount测试。现有Old Hunters entitlement保留，不能把新mount当首次DLC支持。
- **保存错误处理仍有限。** 上游`runtime_savedata.c:mount`只打印文件映射失败，已占slot不回滚，
  最后仍返回成功。本轮扩容减少容量不足，不宣称完整失败恢复。新增地区声音flag白名单另作临时
  保存fixture测试后再决定，不修改真实存档。
- **在线继续暂缓。** BBSC/THD/shadNet不只缺少启动器页，还涉及NP导出优先级、线程、认证与退出。
  `runtime_ngs2/compat/rewatch/toypad`等通用扩展并非Bloodborne本轮的独立必要收益。

## 回滚与下一轮AppImage

四项有效功能均可在本独立分支用`git revert <提交>`单独撤回；整体不采用时让原NRBridge开发
分支保持原位即可，无运行配置迁移。不要恢复已撤回的双触点提交，除非同时完成身份修复。

下一轮AppImage只需按选择纳入launcher脚本/译文和新Native；Helper、wire协议、模型无新增
依赖，保存目录/格式不变。语言资产由玩家现有游戏提供。新Native仍是BUILD_ONLY，正式选用前
交Astra按现行运行流程验证；不是0.6全量版，也没有制作包或切换运行组合。
GPU选择、宿主Vulkan/EGL/pressure-vessel、GE发现和DLSS库安装留给独立打包优化，
本轮不更换锁定依赖，不研究新驱动或升级模型。

最终构建身份、CPU总结果及证据位置在本报告末尾补记；文档提交不冒充实际编译来源。
