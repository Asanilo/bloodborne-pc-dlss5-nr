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

F6离线相机、FSR多尺寸、NR-only+FG与进一步延迟优化继续独立推进，未由本批宣布完成。
