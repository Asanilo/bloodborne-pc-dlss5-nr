# 玩家统一试玩入口

本机准备好的小型入口为 `out/bbport-play`（主工作区），源码入口为
`python3 launcher/play_nrbridge.py`。两者复用既有 managed entry、GPU 独占锁、
Helper 检查、私有存档事务与正常资源退休，不建立第二套运行后端。
本机路径保存在忽略的 `out/player-preview/profile.json`，模型、DLL、Wine prefix 和存档不进 Git。
首次配置依赖已有受管运行环境；该脚本不下载或打包专有运行时。

```bash
# 日常：Old、NR + DLSS SR Quality、FG 初始关闭，游戏内可开启 x2
out/bbport-play --memory-model old
# 同参数 New 对照
out/bbport-play --memory-model new
```

默认原生鼠标，1080p Quality、60帧上限、离线。启动进入可操作场景后首次 F8 开启 NR，
后续 F8 切换 NR 效果；Delete 打开设置，独立切换 FG 和 SR 档位。
FG 初始关闭时仍保留已选 DLSS x2 后端，正常游戏内开启，无需重新拼装 Helper。
切换 FG 后端或 MFG 倍率需要正常退出再启动；开关和支持的尺寸/档位可运行中切换。

其他选择沿同一入口：

```bash
out/bbport-play --memory-model old --fg dlssg --multiplier 6 --sr balanced --output 2560x1440
out/bbport-play --memory-model new --nr off --sr quality    # 真 SR-only，不启动 NR Helper
out/bbport-play --memory-model old --sr native             # NR-only、FG Off
out/bbport-play --memory-model old --fg fsr31               # 1080Q FSR x2 回退
```

DLAA/Quality/Balanced/Performance/Ultra Performance、菜单支持的宽高比与输出尺寸均可选择；
NR execution 独立，默认1280×720。3X–6X自动选择已验证的 GPU 提交通路。
不是所有笛卡尔组合均已实测。FSR 与 CPU FG 回退仍保留实际固定输入约束，
NR-only + FG 的源帧接线尚未支持；不能将其写成已验收组合。
1440p 的现有 M9 证据证明内部 SR/FG 输出2560×1440，最终窗口读回仍1920×1080，
不等于物理1440p扫描显示验收。[Old矩阵](DLSS_MFG_M9.zh-CN.md)及[New矩阵](DLSS_MFG_M9_NEW.zh-CN.md)记录实际范围。

玩家试玩进度仅写入 `out/player-preview/user`；研究场景在 `out/m8`、`out/nr-dev/runs` 下，
正式安装及正式存档不改写。受管入口再为每场生成私有副本，只有正常退休才同步回试玩目录，
异常时保留本轮副本和证据。请正常关闭窗口，等待终端打印最终状态后再启动下一场。

试玩时先在静态高亮边缘观察 NR Off/On，再做慢转镜、快转镜和植被观察；
分别开启/关闭 FG，注意 R（真实帧）与 G（生成帧）的边缘、亮度、重影和节奏。
比较真实 FPS 与鼠标操作响应，不能把 Present 数量当成扫描显示帧数或输入延迟。
F8 是效果切换，完全不启动 NR 的对照使用 `--nr off`。

M10 新策略默认关闭且未整体合流，M6 原生 Vulkan 架构线暂停。
Smooth Motion 不进入玩家默认配置；Old 冷启动 WSI WRITE_AFTER_PRESENT 仍为独立待查问题。
本入口的最低退出证明仍包括 Native 最后使用、Helper GPU/FD/model、逐代 FG 退休和无私有残留。
