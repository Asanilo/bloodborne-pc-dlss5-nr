# bbport 0.6 全功能移植难度与冲突审查

## 范围与方法

2026-10-20 用户追加要求覆盖首轮的收益筛选：所有功能均评估移植难度和冲突；允许采用比本地更完整的
上游鼠标、改键、中文、在线和DLC实现。本文件续接`BBPORT_06_GAMEPLAY_REVIEW.md`，首轮四项窄移植保留。
开发仍在独立`dev/gameplay-upstream-06-20261020`，起点`b6cb562`；原Native/Helper分支和现用
5635804/7f69/9a825运行组合不切换。原GX/PM4、GPU资源/同步/Shader、NR/协议、底层帧率时序边界仍有效；
允许审查不等于授权立即改写这些模块。当前没有GPU、Wine、真实联机或真实存档测试。

源码比较对象为共同祖先`10baf9d`到上游`8185b88`，并逐功能对照当前Fork，包含中间
`c7e3e55`、`3c57da4`、`72e5c8c`。`git diff --name-status -z`核算1163个变更记录：
89修改、673新增、126重命名、275删除。重命名按目标目录归类，不能把新增目录全算新功能。

覆盖所有功能族，按实际函数/数据通路评估；网络与输入做深入审查，渲染/Shader/vendor大重构做代表性
源码和边界审查。没有声称1163个文件逐行审计，也没有把Release Notes中的性能数字当本机结果。
难度：**低**为独立脚本/配置；**中**为输入或文件接口与状态机；**高**为跨模块ABI/guest hook/资源生命周期；
**很高**为架构替换、在线世界同步或整个渲染链。以下“可移植”是技术判断，不是已完成状态。

## 输入与本地化：不以“本地已有”代替比较

| 功能 | 上游实际实现与本地差异 | 难度 / 决定 |
|---|---|---|
| 原生鼠标视角 | `runtime_pad.c:apply_mouse_look`接`BbGuestHooks::CameraLookAdd`，相机更新detour一次消费弧度；本地`mouse_axis/sample_host`经8位右摇杆 | 高；推荐独立原生相机通道，保留旧回退。先解决清空、模式切换、未消费增量和签名验证，不整文件覆盖 |
| 摇杆式鼠标回退 | 上游60ms衰减累计与可调死区；本地delta/dt、±24补偿、截断，无残差 | 中；上游平滑能减脉冲，但引入停鼠尾随，不直接作为“不跟手”修复或默认升级 |
| 复合键 | 上游单modifier+主键与plain抑制；本地无modifier结构，录入工具也只取第一个keydown | 中；本轮实施运行时+录入闭环，扩展多标准modifier并兼容旧MouseLeft和上游Mouse Left名称；不静默换布局 |
| 快速短按 | 上游`key_latch`保存短按；本地HostInput只保留当前状态 | 中；在本地mutex状态里实现latch，不能搬上游跨线程volatile数组。需定义按下/释放与pad样本的对应关系 |
| 滚轮绑定 | 上游每方向60ms脉冲、队列上限8；本地BbHostInput无wheel字段 | 中；需协调window→snapshot→pad与录入，不只接受Wheel字符串；可独立输入提交 |
| DS3式默认布局 | 上游增加dodge/sprint/jump语义；本地Space确认、Shift闪避、Ctrl慢走等 | 中；推荐可选预设与显式迁移，不自动覆盖玩家INI。复合键不等于新增奔跑/跳跃状态机已完成 |
| 摇杆平滑/斜向移动 | 上游`key_stick`70ms渐变；本地WASD直接满量程并有慢走 | 中；需要响应曲线与归一化CPU测试、实机体验，保留即时模式供选择 |
| 奔跑/闪避/跳跃分键 | 上游`keyboard_circle`生成按下/释放脉冲，依赖游戏菜单状态 | 中高；是输入状态机，独立审查动作时序，不与FPS Hook或鼠标迁移捆绑 |
| Esc菜单内返回 | 上游options_latched避免同一按键打开又关闭；本地Esc固定Options | 中高；依赖原生菜单状态检测。独立验证进入/退出/焦点/长按 |
| 菜单鼠标指针 | 新`gpu/games/bloodborne_menu_pointer.cpp`读取GFx对象/列表，hover/click/scroll | 高；需提取guest对象/可见性/引用寿命，不能只切SDL光标。与NR overlay文本输入及F8/F9事件优先级并审 |
| F1释放/点击捕获 | 上游窗口相对模式、100ms settle；本地失焦/文本/NR菜单清空且使用mutex | 中；保留本地隔离，增加显式释放可独立迁入；避免丢弃真实移动或重新捕获时跳镜 |
| 按设备提示图标 | 新`bbport_button_prompts`与`render/vk_button_prompts.cpp`，包括资源加载线程和Vulkan上传 | 高；可移植，需图像/上传/descriptor/线程退出审查，属GPU边界 |
| 自定义SDL光标 | `bbport_cursor.cpp`生成图案并管理SDL cursor | 低中；可独立可选开关，覆盖失败/销毁/系统光标恢复，不依赖GPU提示图像 |
| 控制器与触点 | 本地已有校准/热插拔/震动；上游双触点累积与本地slot式touch_ids冲突 | 中；校准等保留，双触点先补跨样本身份匹配；首轮尝试已撤回，不能直接重放 |
| 启动器中文 | 本地译文层叠加中文控制/保存增强；上游更多新页面译文 | 低；随实际迁入页面吸收对应译文。b6cb562对比原始ZH_CN表，上游独有39条、本地独有5条，另有本地英文fallback ZH表 |
| Native菜单中文 | 上游不存在`gpu/shim/bbport_zh.h`，只有英语/俄语选择；本地有中文与NR中文控件 | 中；本地实现覆盖更完整，保留并对新增功能补译。不是因为本地所有权而拒绝上游 |
| 21游戏语言/菜单语言继承 | 上游21个PS4语言码、显式语言选择标志；首轮已适配中文 | 低；已移植e21ad50/bd3f0e1；文本资产仍由游戏发行版提供 |

### 鼠标“不跟手”的可证机制

本地`HostInput::Read`一次读取后清零delta；窗口独立线程收事件，循环有2ms休眠，不是每个GPU帧才收事件。
`sample_host`使用相邻pad读取间隔（夹在1–100ms），不是该批鼠标事件覆盖的时间。
`mouse_axis`在灵敏度1时，20px/16ms得到125+24=149，超出127后饱和；更大甩鼠仍输出255。
微动有固定死区补偿，读到delta后的后续空样本立即回中。以上机制可从源码/算式确认；
没有据此伪造实际相机延迟或把全部主观感受归因到一个环节。

上游原生通道将每像素0.0015rad在相机更新处消费，绕过摇杆限速/死区/量化，更符合相对鼠标。
但`LookSite=0x143cde0`的16字节签名只验证覆盖的指令，仍依赖rbp/r13布局及控制流；
100ms不消费后切摇杆的策略必须防止同一增量双用。上游捕获关闭未显式清look_delta，
短菜单切换可能留下增量，这是待测风险，不是已发生故障结论。

Astra进一步确认：纯相机转角detour并不等于修改dt/pacer，属于现有输入授权可实施的候选；
无需因“guest hook”一词一概排除。应在guest尚未执行、镜像仍可写的`bbgpu_patch_image`安装，
不要复用运行期GPU内存hook安装器。先构造stub、设RX、再发布跳转；校验完整16字节及rbp/r13
上下文，恢复原指令本应保留的RFLAGS/XMM2/向量状态。若调用C++必须保存全部活跃状态及red zone，
不能套用caller-saved假设。固定epoch/sequence mailbox以短mutex临界区提交/重置，相机try_lock消费；
reset清旧epoch、取消旧pending，已经消费的那次更新允许完成。未消费超时只把**后续新位移**送摇杆，
恢复native用零位移heartbeat，不把fallback位移同时作为探测包。上游返回false仍写入look_delta的行为
需替换，不能原样搬入。本轮完成方案审查，尚未生成或安装该detour，也未宣称鼠标手感已修复。

## 游戏、保存、内容与通用runtime

| 功能 | 实际源码 / 冲突 | 难度 / 迁入条件 |
|---|---|---|
| 9个零售地区与Lance预打补丁提示 | `game_check.py:problem/explain`；正确1.09哈希原本已优先放行 | 低；可迁入诊断/ID，不宣称新增地区实机通过；保留严格镜像核验及中文提示 |
| 地区包装模组/外部补丁ID | `mods.py:content_root`、`patches.py:BLOODBORNE_IDS`增加地区 | 低；可独立移植，需正反fixture；保留本地language_patches，不引入FPS/热效果默认变化 |
| 存档声音flag补CUSA03179 | `runtime_savedata.c:bloodborne_sound_hack`补白名单 | 低中；临时保存fixture证明仅改0x204E、短文件/禁用/其他标题不写，然后独立提交 |
| 文件挂载容量 | `runtime_file.c:MAX_MOUNTS`64 | 低；首轮abf2b42已移植；上游savedata映射失败只打印仍成功，不是完整错误恢复 |
| 原子保存/备份恢复 | 原子写/metadata/memory本地已有；Fork额外有save_manager轮换/校验/排队恢复，上游没有 | 已有；保留更完整本地恢复能力；在线接入也必须使用复制保存fixture |
| DLC mount/info/unmount | `runtime_content.c`新增，当前Old Hunters entitlement另已存在 | 中；可移植，但补title/service/label校验、锁、成功后发布、真实卸载/回收、失败回滚与只读保护，详见下节 |
| 菜单列表遮挡修复 | `runtime_file.c:fix_option_layout/menu_layout_file`解析GFX后生成memfd副本 | 中；与新增GFx菜单联动。需恶意/截断/未知格式测试；不改游戏原文件 |
| 40位线程句柄 | `runtime_thread.c:low_record`新低地址池、new_thread使用，来自#193分析 | 高；游戏hybrid lock会截断40位。应独立移入低地址分配，不能连带线程/内存重构；分配失败回退到任意calloc仍不保证40位，应显式拒绝 |
| TLS与导入scope泛化 | `link_libc/link_modules`匹配任意r64的FS→GS；`import_scopes.py`规范化；runtime_nid_is放宽scope | 中高；本地已有精确scoped resolver测试。需负对照冲突/错误scope/数据符号，不能为网络简单全部放宽 |
| POSIX/Win32兼容 | `runtime_mutex/rwlock/sema/time/rtc`主要删除Windows guard、改scope与时钟宏；`platform/win32`新后端 | 高；Linux现有同步算法不等于缺失；Windows目标应独立构建选项/CI，保持Linuxmonotonic及GS/TLS语义 |
| EventFlag/附加系统与对话框 | 新`runtime_compat.c`含AND/OR、clear、timeout、cancel；离线PSN/对话框返回契约 | 中高；可分按需导出模块。删除期间waiter存在时上游会泄漏对象；需stale handle/取消/删除/并发回归及无网络假成功 |
| NGS2音频图 | `runtime_ngs2.c`sampler/ADPCM/ATRAC9→submix/master，reverb湿声未实现，filters等只接参数 | 高；可选模块、流式队列/格式边界/销毁并发/音频golden测试。prepared Atrac9头路径需适配，不能等同完整NGS2 |
| USB Toypad | `runtime_toypad.c`libusb ABI、虚拟标签、TEA及用户文件 | 中高；独立可选功能；协议、校验、标签文件持久化、ABI与取消/超时fixture。不是因非Bloodborne而不审查 |
| 调试assert/blur clamp | `runtime_kernel.c:raise_exception`按caller范围忽略部分0xa0020007；guest hooks另夹blur采样 | 高；忽略assert不能证明输入有效。caller范围/内存安全/浮点边界分别验证，属于底层游戏/渲染审查 |
| 崩溃采样/watchpoint | `runtime_rewatch.c`perf_event所有线程硬件watch；kernel采样扩8192槽和定时线程，probe诊断 | 中高；可选诊断，不默认启用。FD/mmap/线程退出、信号安全、guest映射可读性与本地正常退休/HOLD流程并审 |

## 在线与DLC：进入移植队列，不再按收益排除

| 层级 | 难度 | 具体工作与完成门槛 |
|---|---|---|
| bbnet19兼容模块 | 高 | 150条路径；旧shadp2p/NP导出及BBSC glue。保留可选构建，不能当bbnet20无修改替换 |
| bbnet20基础shadNet | 高 | 194条路径；protobuf/protoc、OpenSSL、zlib、miniupnpc、JSON、Boost/fmt/magic_enum、bbgpu兼容层。ABIv2 guest线程/TLS/errno、必需符号清单、init失败原子性和mapped→ready→start顺序 |
| THD后端 | 高 | Discord设备码/token、matching/session/NP回调与固定guest signaling gate。dispatcher/poller/keepalive的停止、join、请求代际与取消后不回写旧登录结果 |
| BBSC无缝联机 | 很高 | 动态写guest代码、持久会话、世界同步、cinematic事件过滤、保存持久化；所有hook站点与本地Gnm/menu/GX补丁比对，复制存档下死亡/传送/Boss/断线/退出恢复 |
| 在线launcher/辅助服务 | 中高 | 模块选择与pins、地址配置、登录取消、0600且原子凭据存储、环境/日志脱敏、可选本地gameserver服务生命周期；不自动登录/改路由/启动外部服务 |
| NR整合退出 | 很高 | 先停新在线回调、撤销队列、join，证明不再访问guest，再与既有GPU/Helper末用闭合；不以进程退出替代GPU完成 |
| DLC文件挂载 | 中 | title限定目录、label路径安全/长度/symlink策略；16槽同步与重复挂载语义；成功后发布，失败回滚；实际unmount并定义已打开FD语义；ABI输出24字节info/16字节mountpoint |

具体缺口来自源码，不是泛化风险：`runtime_net.c`在init成功前就设置module_handle；
bbnet.h没有统一stop接口；THD `thd_session.cpp`的dispatcher永久等待、poller/keepalive detach，
Stop没有join；`thd_matching2.cpp`清部分指针但不统一撤销队列闭包；BBSC还有延时detached线程。
上游shadNet client自带Stop/join，不代表整套module生命周期闭合。

DLC上游遍历任意title寻找label，mounted[16]无锁、失败前占槽、unmount为空；
`runtime_file.c:game_path`只保护/app0和/hostapp，/addcontN没有注释所称的只读保护。
迁入须测试错title、恶意label、容量耗尽/失败重试、并发挂卸、卸载复用及所有写入口拒绝。

执行顺序：隔离模块构建与ABI/mock失败测试 → 补stop/join/代际与断线重启 → THD、BBSC各自验证 →
真实双端协议/场景验证 → NR整合。模块加载成功不是联机通过。许可证源码声明：shadPS4/BBSC
GPL-2.0-or-later，THD GPL-3.0-or-later且直接编入bbnet20，需单独记录对应源码和依赖交付、
提供不含THD的构建选项；这里只核对来源声明，未作法律合规结论或外部咨询。

## 渲染、内存、性能与架构：完整纳入，但本轮只读

| 功能/变更族 | 主要源码入口及本地冲突 | 难度 / 移植方式 |
|---|---|---|
| 自有Command Processor | `gpu/cp`14条、删除旧Liverpool路径；调度/队列/退休接口变化 | 很高；逐功能移入，保护本地queue→submit共同发布、逐队列退休、退出证明 |
| 递交CB副本/FX初始化clear | 上游SubmitGfx force_copy与SubmitFxInitZero；本地已有SubmittedCopy和同步GXRender补丁 | 高；已有副本不重复做，先判断FX是否仍缺口，禁止整个CP覆盖 |
| Laurence slot返还 | guest_hooks JobSlotFix/Drop | 高；独立证明失败/取消/重试/销毁恰好返还一次，再实机 |
| Laurence缓存/堆扩容 | ResourceCacheBacking、runtime_extra_direct_map、patches GX heap | 很高；512MiB额外direct pool与256KiB→1MiB guest heaps不是普通配置，需预算/映射/释放/末用 |
| GX空node保护 | InstallGxNodeGuard在0x2199310失败时跳过post并保留pending | 高；可能改变任务提交语义；先有故障因果与恢复协议，不能用跳过掩盖坏命令 |
| 新内存模型默认所有GPU | run.sh BB_PC_MODEL、layer/memory/buffers/GuestMemory路径 | 很高；不改当前NR默认。层内trap、writeback、CPU/GPU同址与本地共享资源保护共同验证 |
| 写trap/twins/VRAM回迁 | `gpu/layer`、`buffers`10条、`memory`7条及runtime_memory准备写入 | 很高；page锁序、GPU helper不等待命令线程、篡改页归属/代次、FD与退休边界 |
| texture GC/分配失败等待 | `textures`25条及render resource管理 | 高；8GB门限/4GB回收/逐资源真实末用，不能仅调GC门限迁入 |
| GCN定义与Shader重编器 | `gcn`16条、`shader`110条，大量旧目录移除/重命名 | 很高；ISA/IR/SPIR-V语义与shader corpus；本地NR host shader路径不能被替换 |
| raster/pipeline/cache | `render`66条，拆成raster_*与pipeline_store等 | 很高；draw状态、descriptor/CB生命周期、缓存版本与本地SR/NR输入链逐项对齐 |
| 真实occlusion/采样缩放 | render occlusion与CP结果回写 | 高；查询池/结果缓冲与pass scale、异步回写和guest可见完成 |
| 错位/角落闪帧 | `render/rasterizer.cpp`在BeginRendering后写push constants，避免occlusion compute覆盖 | 高；值得独立适配现有rasterizer，须确认所有重开pass/分支，GPU复现验证 |
| present/resize/HDR | vulkan swapchain、render presenter等待stage与重建 | 高；源资源、Present、owner退休与本地退出流程强耦合 |
| null V#、decoder/format asserts、cache兼容 | shader/raster资源表边界、pipeline缓存序列化 | 高；用本地捕获/合成负例映射具体修复，不以“上游不assert”替代正确资源 |
| 光柱/tiling/大buffer加速 | textures detiler、buffer residency、GPU light shaft目标copy路径 | 高；CPU/GPU布局、cache一致性与目标格式，需本机对照，不能引用上游AMD模拟数字作本机提升 |
| Polaris labels/RADV HiZ | platform gnm/CP label路线、run.sh芯片判别 | 高；改变guest完成时机或驱动行为。上游也未在目标Polaris验收，不默认注入当前环境 |
| draw准备worker扩展 | render/thread pool按CPU线程数增长 | 高；与本地GX同步/CB所有权共同验收，不能只改线程数量 |
| 宽高比/camera/HUD/viewport | guest_hooks SetDisplayAspect、games菜单与upscale输出 | 很高；本地NR固定extent、MV/jitter/history/捕获与HUD共同调整；单独分支 |
| 热效果/Bloom/SSAO恢复/SSR启动 | effect_flags、WriteHotCode、renderer options；SSR资源尺寸仍需启动补丁 | 高；对象身份和执行中改码、帧边界/typed mailbox；保留启动方式到生命周期闭合 |
| FSR4.1.1 scene reset | upscale/vk_fsr4 temporal资源reset及clear刷新 | 高；与本地game_only reset/history不同，先映射重置含义；不更新模型 |
| DLSS与NGX设备回退 | vulkan/instance创建失败重试与DLSS支持标志 | 高；不能让Runtime Control仍认为NR可用；设备/扩展/feature两路验收，不升级NVIDIA模型 |
| Frame Generation | `upscale/vk_frame_generation.*`，上游仍隐藏 | 很高；Present时序/额外图像/退休及NR历史，保持只读单列，不以隐藏UI表示无架构影响 |
| 帧率与F3 | frame_rate PatchImage/FrameStep、dt/pacer/cloth/奔跑等48处 | 很高；明确原修改禁区。与鼠标输入解耦；禁止只搬F3输入开关 |
| platform/common抽离与Windows | platform44条、common47条，GNM/videoout/avplayer/event queue、Win32 POSIX | 很高；不是目录重命名即完成。按OS/媒体/事件接口迁入，保持Linux锁定工具链 |

## 启动器、构建、打包与测试

| 功能 | 冲突与迁入条件 | 难度 / 状态 |
|---|---|---|
| GPU选择 | launcher按Vulkan名称保存，capabilities --gpus映射BB_GPU_ID；本地原本支持手工ID但无列表UI | 中；可独立迁入UI/枚举/mock测试，真实多GPU另验；当前运行pins不切设备 |
| 玩家自选DLSS库UI | launcher校验/复制LinuxNGX .so，WindowsDLL拒绝 | 中；可迁入，但私人Runner选用锁定路径，不得自动替换现用库；模型升级限制仍有效 |
| --play/菜单语言与参数 | launcher main set_language、bb-launcher argv回退 | 低；首轮已移植继承与wrapper；需要覆盖headless --play和GTK路径，不只窗口构造 |
| Vulkan/EGL/NVIDIA宿主库 | bbport_vulkan收libcuda/父目录nvidia库、EGL vendor与pressure-vessel环境清理 | 中高；逐变量保留用户override、驱动库版本与ELF类，和当前锁定依赖冲突分开；仅CPU mock不能证明设备成功 |
| Proton/FP8/FSR4工具 | tools/fsr4cap/proton AUR路径、capture_all FP8配方 | 中；路径发现可独立CPU测，Proton/模型实际工具链变化需另验，不更新NR模型 |
| AppImage依赖/体积 | packaging default.nix/appimage.sh，网络库/桌面路径/启动方式 | 中高；打包独立阶段，不制作本轮AppImage；保留许可证/对应源码/用户数据目录 |
| FSR submodule重打补丁 | 上游build.sh失败后checkout -- .重新打补丁 | 高冲突；本地prepared依赖全清单和输入只读要求不容自动重置。可实现显式准备流程，不能搬自动丢弃修改 |
| 通用build/test | 上游新增net测试、CMake日志；本地独立BUILD_DIR/协议校验/NR provenance | 中；保留本地可追溯构建。首轮--test漏Atrac9 include及probe符号缺口单列待修，不把手动适配当配方修复 |
| 保存恢复/manager与私有Runner | 上游无本地save_manager/manager_entries/typed NR scope/pins等 | 已有；新UI/网络模块接入这些机制，不能整体覆盖launcher、run.sh、probe或CURRENT |
| 文档与测试资产 | docs28条/tests25条/README与VENDOR来源说明 | 低中；按对应功能迁入测试与来源，旧失败/HOLD与首轮撤回证据保留，不以新文档重写旧结论 |

## 分步队列与本轮交付

1. 输入运行时与录入工具共同完成复合键CPU闭环；保持旧配置和更完整的本地中文/保存功能。
2. 原生鼠标相机独立候选：先完成Astra给出的mailbox/清空/唯一消费/签名/stub方案，再做源码与CPU验证；
   获得有效运行scope后做鼠标实际角位移/停鼠/锁定/菜单/过场对照，才能切玩家版本。
3. DLC挂载独立补齐文件层只读与槽生命周期；无需先迁整个renderer或网络栈。
4. 在线先ABI/mock与停止协议，再分别THD、BBSC；与NR整合留在真实线程/资源末用均闭合后。
5. 高风险渲染、帧率、Shader、FG各自分支审查，不为追0.6一次合并。所有功能保留可实施路径，
   不再以体验收益小、非Bloodborne或本地已有为排除理由。

委派方式：通过`herdr`独立窗格，DeepSeek为已核实的`workbuddy/deepseek-v4.1-flash`、
**High / standard**；Astra为显式`gpt-6-astra`、**medium / service_tier=default**，只读。
没有复用根Agent high/fast配置，没有修改全局模型设置。WorkBuddy客户端提供上述模型别名，未向服务端
核验别名背后的日期构建号。根Agent负责录入工具、完整矩阵、实际diff检查与集成验证。
CLI服务层覆盖依据[OpenAI Docs配置参考](https://learn.chatgpt.com/docs/config-file/config-reference)；
本轮显式传入service_tier，不从根会话或全局fast配置继承。

## 本轮新增输入行为与验证

运行时支持逗号分隔的最多4个备选；每个备选可有最多8个物理左右Ctrl/Shift/Alt/GUI修饰键。
示例`Left Ctrl+C`、`Left Shift+Mouse Left`、`Left Ctrl+Left Shift+K`。
相同主键下，激活组合会抑制plain和较少modifier的组合；同具体度的重复映射仍可同时触发。
非法/重复modifier、缺主键、超出备选上限拒绝或不存储；不静默退化成普通键。
`Keypad +`仍是单个键；逗号键使用`Comma`/`Keypad Comma`别名，避免与INI备选分隔符冲突。

启动器“Assign”录入与运行时一起接通：按住modifier后按键盘/鼠标；modifier单独绑定等释放；
多个modifier无普通主键时按首次释放提交，以最后按下modifier作主键，两种释放顺序等价。
Esc、超时和失焦取消，不改原绑定；窗口失焦不能继续带着丢失modifier的状态保存普通键。
所有新modifier支持都是玩家显式配置；现有布局、鼠标算法、touch_ids、HostInput与游戏时序保持。
**修饰键自身的原动作仍保留**：若Shift原来绑定闪避，Shift+鼠标不会取消其闪避含义；
要只作修饰键，应在启动器先把原动作改到其他键。没有悄悄套用依赖新dodge/sprint/jump状态机的上游默认布局。

独立复核中Astra发现两处原实现问题并已修复：Comma输出与INI分隔符冲突、失焦丢modifier却继续录入；
同时补齐modifier-only释放顺序及自组合拒绝。CPU覆盖真实capture→INI→runtime解析→pad动作，
包括两种逗号键plain/组合、键盘/鼠标/8modifier/Keypad +、取消、长度边界、旧默认、具体度抑制及
虚拟控制器/触点ID/震动/overlay/focus回归。旧b6cb562运行时负对照在capture往返测试拒绝组合，
不是仅测试一份独立重写解析公式。

- 运行时提交`decca45`，录入/中文说明/往返测试提交`bff0602`；回滚整项先revert `bff0602`，
  再revert `decca45`，不改真实配置。源码与原Native/Helper运行基准保持分离。
- actual-source `pad-test`、`capture-test`、`chord_capture-test`、`host-input-test`通过；
  pad及capture往返UBSan通过，capabilities工具-Wall/-Wextra/-Werror编译通过，未执行GPU枚举。
- NR工具195项CPU通过；普通Python117项中89通过/28跳过；launcher相关4项通过。
  首轮已记录的`build.sh --test` Atrac9/probe夹具缺口未扩展修复，不宣称该配方已全绿。
- 完整Native wrapper构建及最终来源在下面收口记录补记；当前代码不自动切换运行选择。

证据为工作树`out/gameplay-06-input/`及DeepSeek独立`out/chords-dsv/`，不提交构建产物。
本文件不宣称在线/DLC/原生鼠标已移植或GPU手感已改善。
