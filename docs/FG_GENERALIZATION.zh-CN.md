# FG 通用化：独立于 NR 的动态输入

2026-10-25，基于 `nrbridge@b80ed18` 继续开发，复用 FG 工作树。

## 第一批修复

`0c92436` 修复普通启动器的一处耦合：没有显式设置 `nrbridge_dimensions` 时，
过去只有 NR 开启才自动为非默认 SR 档位、输出尺寸启用动态输入协议。
NR 关闭后，相同的 DLSS GPU FG 配置可能退回固定 1280×720 → 1920×1080 约束。
现在 GPU FG 按实际 SR 输入/输出选择动态协议，`--sr-only` 仍保留，
不会为了启用 FG 启动 NR Helper。CPU Bridge 的固定输入约束不变。

CPU 回归覆盖 NR OFF、无 NR 尺寸 opt-in 的 1080 Balanced、1440 Quality、
16:10 Performance、1440 DLAA；31 项启动器检查及 Native 增量 `--test` 通过。

## 实机证据

FG Native 封存 `0c92436`，复用既有匹配 FG Helper、DLL、prefix 和私有测试存档。
会话 `fg-independent-new1440b-3x-r15`：New Memory、1440p Balanced、DLSS MFG 3X，
**NR 完全关闭，nrbridge_dimensions=False**，自动转发实际动态输入契约。
实际 guides 1506×848 → FG output 2560×1440；此场窗口的最终 encoded 图像实际为1243×699，
不是内部1440p图像或物理扫描输出的尺寸。

- 未创建 NR Helper、未借用 NR prefix；FG Helper 实际生成并进入 Native Present。
- 8 秒窗口：282 真实 / 553 生成 Present，完整批次顺序错误0、失败 Present0。
  此窗口含启动抖动（真实间隔最大约303ms），不作为稳态性能优化结论。
- 16张实际 Present 读回中4个完整3X帧组全部图像不同；保留源帧、索引和epoch映射。
- rc=0、`PASS_NORMAL_TEARDOWN`、Native last-use=true、residuals=[]，receipt retired。
  正常关闭完成后已将GPU窗口交还NR线。

此场未开启 validation layer，不宣称 core/sync validation PASS；旧公共 WSI 问题仍保留。
内部1440p资源通过不等于物理1440p扫描显示验收；Present数量也不是输入延迟。

本机证据位于公共 `nrbridge/out/nr-dev/runs/fg-independent-new1440b-3x-r15`，
FG `out/fg-independent-new1440b-3x-r15-checks.json` 和
`out/fg-next-evidence/r15-independent-3x/` 保存统计及无损图像。

第一批仅修复上述启动器尺寸耦合；同日继续完成的 Native 源修复及设置见下节。

## 第二批：Native HDR 源与游戏内设置

`c44ec08` 修复新玩家入口的另一处 NR 耦合：1080p Native HDR 路径启用尺寸协议但 NR
尚未 arm 时，过去 Presenter 使用 NR Present 记录的零源编号，拒绝实际已生成的图像。
现在已采样的真实 LDR 图像通过实际 VideoOut 地址、图像 UID、生产 tick、尺寸与源帧 epoch
绑定一次性 FG 内容标记；Presenter 消费该标记，NR C1 账本、Evaluate 与持久历史不变。
没有用当前帧计数直接替代内容证明，也没有取消 GPU 完成等待。

`a184697` 增加游戏内 FG 后端选择与 2X–6X 倍率：

- 当前后端和启用状态独立显示；后端选择在正常退出后保存、下次启动生效，明确需要重启。
  Smooth Motion 保持不可用并禁选，旧退出故障没有绕过。
- 已验证 DLL + GPU DMA-BUF + consumer-queue-v1 可运行中改变倍率。倍率属于不可变资源代，
  切换时先保留当前深度/运动输入，再正常退休旧 Helper、共享输出及 Native 消费者，
  建立新 Feature/资源代。没有新增完整游戏帧缓存。
- 2X 的 submit-ready/CPU 回退保留；这些配置更改倍率需重启，不伪装成可热切换。
- FG 专属偏好按启动配置 owner 隔离，只在正常 GPU/boot 退休后原子写入。
  不修改 NR/SR、模型、原始启动设置或存档；外部配置被用户改动时不覆盖。
- 新玩家入口默认恢复偏好，显式 `--fg` / `--multiplier` 优先。
  首次无偏好时仍按原规则保留 DLSS x2 待命、初始关闭。
- `8cfac6a` 使设置页复用 Real/FG/Present 独立计数，避免把总 Present 平均间隔误标为真实 FPS。
  `3944e81` 为新玩家入口透传默认关闭的 `--fg-profile`。
- 后续 SR-only 配置还会丢弃继承的 NR prefix 租借参数，避免关闭 NR 后仍依赖 NR prefix。

### 实际验收

`fg-ui-new1080q-live-r16`：Native `a184697`，New1080Q。未 arm NR 时复现原始零 FG 源
问题；同场正常启动 NR 后立刻实际生成。随后2X→3X→6X→2X及实际菜单点击3X，
共五代倍率 **2/3/6/2/3** 均真实生成并正常退休。实际选择下次启动 Off，保持菜单打开
直接正常关闭，脏设置成功刷新，FG 偏好最终保存 Off/2/disabled；真实新入口 CPU 准入
确认下一次恢复 Off。NR prefix 正常归还。

`fg-native-independent-new1080q-r17`：修复 Native 封存 `3944e81`，实际 New、1080Q，
NR Helper 从未启动。成功生成 Present 的 **FG 源为正、NR 源仍为0**。
旧开发环境令此场仍借还了一次 NR prefix；后续已单独消除这项无用租借，CPU 回归通过。

| 短窗口，NR完全未启动 | 真实FPS | 生成FPS | Present/s | 真实间隔P95 ms |
|---|---:|---:|---:|---:|
| 2X，5秒 | 54.8 | 54.8 | 109.6 | 20.04 |
| 3X，3秒 | 47.0 | 94.3 | 141.3 | 23.90 |
| 6X，3秒 | 36.0 | 180.3 | 216.3 | 29.73 |
| FG Off，3秒 | 60.0 | 0 | 60.0 | 18.13 |
| 恢复2X，3秒 | 55.3 | 55.7 | 111.0 | 20.03 |

统一 New、60限帧、Immediate、Q1，未开 validation；`--fg-profile` 仅采样已有区间。
窗口不是严格热状态/相机锁定的性能基准；生成与真实数量在窗口边缘允许相差少量。
全部窗口完整批次顺序错误0、失败 Present0，NR记录0。
6X的16张实际读回保留完整 Native source/index/count/epoch/Present 映射，两个完整组
全唯一；示例位移约2.54/4.49/6.70/8.95/10.91/13.36px，按时间顺序递增。
216.3 Present/s 不等于216.3屏幕显示FPS，也不代表输入延迟改善。

两场均 rc0、PASS_NORMAL_TEARDOWN、Native lastuse=true、residuals=[]、receipt retired。
R16的另一组旧读回虽然像素保存完整，但对应Native映射未及时保留而随日志轮转丢失，
不把它记为源帧映射通过；R17及时保留实际映射完成替代验证，未改写失败证据。
正常GPU退休与图像/validation验收是独立结论；公共Old冷启动WSI问题仍保留。

本机统计 `out/fg-native-independent-new1080q-r17-checks.json`，图像和实际映射
`out/fg-next-evidence/r17-native6/`、`out/fg-native-r17-mapping.log`。
游戏内截图 `out/fg-ui-r16-*.png`；R16/R17完整结果在公共 NRHOME 的同名 runs 下。
以上仍为 HDMI 最终 encoded1920×1080，未扩大物理显示尺寸验收范围。

FSR多尺寸、NR-only+FG与进一步延迟优化继续独立推进，未由本批宣布完成。

## 第三批：新玩家入口的离线 F6 相机

`a08b17e` 将已有 BBSC 的相机输入和运动数学接到 Native 字节校验 hook，
不加载联机模块。输入实现使用独立命名空间，继续调用既有鼠标所有权/epoch 机制。
退出相机恢复 director 的原调试相机指针和开关；失效映射会退出并释放输入。
新玩家入口默认开启，`--cinematic-camera off` 可使用较旧 Native。
运行前检查 Native 相机能力；不把不能工作的 F6 静默当成已启用。

CPU：9项 preview、17项 player settings、实际输入实现五种构建变体、现有捕获恢复、
新增实际相机驱动移动/原游戏相机隔离/指针恢复/失效映射，以及 Native `--test` 通过。
驱动测试的自身进程 `process_vm_readv/writev` 需在允许这些系统调用的环境运行。

实际 `fg-f6-new1080q-r18-live`：Native `a08b17e`，New / 1080Q / DLSS FG2X，
NR 全程关闭且未租借 NR prefix。F6后 E 升高，实际相机 Y 从 -27.226 到 -23.970；
方向键转镜有效，再次 F6 返回原镜头，前后截图角色位置一致，HUD正常。
真实生成帧持续 Present；截图已有计数约54–56真实/同数量生成，不作为正式性能比较。
本场不宣称 NR ON、物理扫描输出、输入到屏幕延迟或全部相机功能均完成验收。

正常关闭 rc0、PASS_NORMAL_TEARDOWN、Native lastuse=true、residuals=[]、
FG draw/Present GPU complete与资源释放证明齐全、receipt retired。
本机 `out/fg-f6-r18-{start,raised,turned,returned}.png` 和 `out/fg-f6-r18-stop.log`
保留视觉及退休证据；未创建新 prefix、依赖树或 AppImage。

## 第四批：FSR x2 多尺寸与 SR 档位

`e3ae653` 复用现有 FSR context/depth/HUDless/输出重建和真实完成等待，
开放统一启动器与新玩家入口的多尺寸 FSR x2。FSR 输出现在绑定实际采集源帧及显示图像，
Presenter 还检查前一真实帧配对；不以 NR ledger 代替 FG 内容身份。
共享的 guide 域判断只接受同尺寸当前/上一真实源，不替代 GPU 完成或 NR 历史证明。
旧 Native 缺少动态源能力会在启动前明确拒绝。FSR 仍只支持 x2。

实际 `fg-fsr-variable-r19`，Native `8c8cc57`（包含尚独立验收的 NR-only 接入）：
New / DLSS SR / FSR3.1 HUDless，NR执行固定1280×720，统一60限帧。

| 模式，3秒短窗口 | 真实FPS | 生成FPS | 真实间隔P95 ms |
|---|---:|---:|---:|
| 1440p Quality，NR未启动，1706×960输入 | 51.33 | 51.33 | 20.48 |
| 1440p Quality，NR开启 | 45.66 | 46.00 | 25.72 |
| 1440p Balanced，NR开启，1506×848输入 | 48.00 | 48.00 | 24.26 |
| 1920×1200 Performance，NR开启，960×600输入 | 56.00 | 56.00 | 21.51 |

各窗口失败Present/配对顺序错误均0，NR开启窗口分别137/137、144/144、168/168真实帧
实际选用NR。运行中切换输出/档位后，实际新尺寸context与新源生成Present均得到确认。
16张转镜时真实/生成图保留完整源编号映射，七个完整组全部图像不同；
示例中间帧/下一真实帧位移约11.23/22.27px。HUD截图无明显错位。
原有高速转镜植被漂移仍为已知算法缺陷，本批不宣称修复。

这些是相同场景的功能短窗口，不能与历史Old路径60/120数据直接作性能回归结论。
数值为成功Present返回时间，未测物理扫描、光学输入延迟或全部运动画质。
内部1440p不等于物理屏幕1440p：该场截图为1920×1080，16:10读回为1728×1080。
FSR3.1.6保留Experimental入口，本批未新增该版本的多尺寸GPU验收。

该场最后的NR-only首次尝试未生成，已如实保留失败记录并正常退出；这与上表通过的
SR+FG组合分开处理。后续 `fg-native-fsr-r20` / Native `abec32d` 又确认1080Q恢复SR、
FG关闭/恢复成功，NR开启时分别54.33/60.00/55.00真实FPS，均正常配对。

两场rc0、PASS_NORMAL_TEARDOWN、Native lastuse=true、residuals=[]、
NR prefix RETURNED、FG GPU退休/资源释放齐全、receipt retired。
本机 `out/fg-fsr-variable-r19-checks.json`、`out/fg-native-fsr-r20-checks.json`、
`out/fg-next-evidence/r19-fsr-variable/` 保留统计、PNG及映射。

## 第五批：NR-only + FSR / DLSS FG，MFG倍率及恢复

共享 upscaler 接入独立提交 `b5e4c8f`，启动准入 `8c8cc57`，场景深度修正 `abec32d`。
SR为Off时，真实场景在既有UI合成域提供颜色，独立生成Motion并采集相机跟踪的场景Depth；
不能使用首次HUD绘制的空/小stencil附件。菜单和读档帧不进入此采集。
NR原有真实源编号、历史写回、admission及TTL不变；生成帧仍只由Presenter消费。
倍率、源编号、DMA-BUF所有权、实际完成和消费ACK继续复用已验证机制，未增加完整游戏帧排队。
同时修复 `sr-control` 四字段请求漏掉provider的条件，支持实际Native/DLSS切换。

在主工作区调用现有新入口（沿用本机profile）：

```bash
out/bbport-play --nr on --sr native --fg dlssg --multiplier 3
out/bbport-play --nr on --sr native --fg fsr31
out/bbport-play --nr off --sr balanced --output 2560x1440 --fg fsr31
```

`--sr native` 是关闭SR，场景按输出分辨率渲染，与DLAA不同。
NR/FG保持独立开关；DLSS当前GPU Q1模式下2X–6X菜单切换会安全重建FG资源/历史。
旧Native不含 `FG_WITHOUT_SR_V1` 时启动前明确拒绝，CPU Bridge保留其1080Q固定输入回退。

`fg-native-fsr-r20`：Native `abec32d`，New，Native场景1920×1080。
NR未启动时FSR为60真实/约120Present；NR-only+FSR为51.66/103.33，实际155/155真实帧选用NR。
恢复1080Q SR、FG关闭/恢复也通过，见上一批数据。

`fg-native-dlss-r21`：同Native、New、相同场景、NR执行1280×720、60限帧、Q1。
以下各3秒窗口，未开validation：

| 模式 | 真实FPS | Present/s | 真实间隔P95 ms | NR实际选用 |
|---|---:|---:|---:|---:|
| Native场景+DLSS3X，NR未启动 | 45.67 | 137.33 | 23.78 | 0 |
| NR-only+DLSS3X | 40.00 | 120.00 | 29.59 | 120/120 |
| NR-only+DLSS6X | 32.67 | 196.66 | 34.40 | 98/98 |
| NR-only+DLSS2X | 47.00 | 93.66 | 23.83 | 141/141 |
| DLSS2X，F8旁路NR | 47.67 | 95.33 | 23.10 | 0/143 |
| 恢复NR+1080Q SR+DLSS2X | 50.00 | 99.66 | 22.94 | 150/150 |

各窗口失败Present和配对顺序错误0。3X转镜捕获16图、五个完整组全部不同，
两张生成图与下一真实图位移示例约7.99/16.86/24.42px，映射与真实源编号完整。
实际四代资源为Native3X→Native6X→Native2X→SR2X，ledger为g1–g4且全部正常退休。
F8恢复NR、F6升高/返回也完成。
采样设备总显存峰值：R19为5514、R20为5556、R21为5681MiB / 8188MiB，非完整进程峰值保证。

Native场景与Quality的游戏渲染像素数不同，不将上表差值归因于FG自身损耗。
196.66 Present/s不等于实际显示帧率，源到Present也不是输入到屏幕延迟。
同一3秒SR恢复窗口抽样中，producer真实完成等待中位约8.44ms、Helper Evaluate队列区间
约5.29ms、Native响应等待约8.55ms；这些区间重叠，不能相加作为算法或端到端开销。
本批没有宣称MFG吞吐量优化收益，后续仍应针对producer依赖和实际pacing控制等待优化。

`fg-native-validation-r22`：同Native，NR-only+DLSS2X开启Native core/sync validation，
实际生成、NR选用、转镜和FG关闭/恢复通过，Native保留日志未出现VUID/validation error。
该场不是性能测试，不替代全部Vulkan或Wine Helper内部的验证。
R20/R21/R22均rc0、PASS_NORMAL_TEARDOWN、Native lastuse=true、residuals=[]、
prefix RETURNED、receipt retired，FG draw/Present实际完成及资源释放证明齐全。
本机证据 `out/fg-native-dlss-r21-checks.json`、`out/fg-native-dlss-r21-mapping.log`、
`out/fg-next-evidence/r21-native-mfg3/`、`out/fg-native-r22-check.log`。
仍需玩家确认长期游玩、快速转镜画质及主观操作手感；不重新承诺FSR植被伪影已消失。
