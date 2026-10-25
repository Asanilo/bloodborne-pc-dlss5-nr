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
实际 guides 1506×848 → FG output 2560×1440；最终 HDMI encoded 图像仍为1920×1080。

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

后续继续开发游戏内后端/倍率设置、F6离线相机、FSR多尺寸和NR-only+FG。
本批仅修复上述已定位并实测的启动器尺寸耦合，不把所有NR关闭/运行时切换问题一并声称解决。
