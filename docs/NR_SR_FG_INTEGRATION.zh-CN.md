# NR + DLSS SR + FSR FG 集成实验

状态：**第一阶段可玩 Experimental 已通过实机；GPU 构建为 `0dff031`。模糊对照进行中。**
集成分支 `integration/nr-sr-fg` 从 FG `05aecdf` 合入 NR `47665ac`；两条已验收分支保留。
目标配置为 1080p Quality、Old memory、NR 310.9.16 SF Preset B、HUDless FSR 3.1 x2。

## 独立入口

复用现有已封存的 Preset B 本地配置、Helper 和模型，不复制依赖或修改玩家安装：

```bash
export NRBRIDGE_HOME=/absolute/private/runtime-root
python3 tools/nr_dev/play.py --daily --fg-native --experimental-fg fsr31-hudless \
  --native-build "$PWD/out" --local-inputs "$NRBRIDGE_HOME/localinputs/preset-B.json"
```

`--fg-native` 是显式集成选择；缺少它时 FG 仍只允许原有 SR-only 实验。
`--sr-only` 可关闭 Helper/NR，`--fg-validation` 启用 Vulkan core/sync validation。
Preset A/B 与旧模型沿用各自封存 local-inputs；不在会话中替换模型文件。
F8 沿用 NR 的真实窗口开关。FG 沿用菜单及本轮私有 `fg-control` 的一次性 0/1 请求。
只有通过现有独占锁、boot/HOLD 守卫、输入身份校验才会启动游戏。

## 接口与不变量

- 原 `!frame_generation` 门限保护的是 HDR 原生 Pre-SR 域。只有显式原生 FG 路径，且
  原有 HDR、1280×720 输入、1920×1080 输出、Quality、jitter、无待消费台账门限均满足，
  才允许 NR 与 FG 并用。其他 scaled/legacy 路径保留限制。
- 原生 FG 开关不再强制切到 LDR SR。真实 HDR Run 完成 SR/merge 后采集当前深度副本、
  motion、相机、reset 和 `BbStats::gpu_frames`；此时尚无最终 LDR 颜色。
- 在首个已识别的 UI draw 前绑定同一真实源、精确格式和尺寸的 LDR 目标并复制 HUDless。
  显示 pass 的只读 fragment image 地址必须包含该目标的 guest 地址，才允许插值。
  未命中 UI、空颜色、辅助图像或无有效场景时丢弃生成输出并重置 FG 历史。
- 生成输出携带独立 FG 编号、真实源编号和目标 VideoOut 地址。TakeOutput 一次消费，
  地址不符也作废；原生 Presenter 还须匹配原有 NR Present 台账的真实源编号。
- 插值仍在 display-copy 边界、下一次真实 OnFrameStart 前记录；生成 Present 的
  `is_game_frame=false`。它不执行 NR Evaluate、BeginSource、视觉历史推进或 jitter 推进。
- 正常关闭先冻结 NR 新任务，再由 draw/present owner 各自实际等待并释放 FG，随后完成
  NR source/cache/SR 的既有退休协议。联合验收必须同时具备 FG 与 NR 两组实际证明。

## 当前失败与 CPU 修复

首次 GPU 集成试验在尚未命中 UI 的真实场景中，让两个空颜色句柄比较相等而进入
Interpolate，随后 fallback 记录了空 VkImage barrier，进程 signal 11 / rc139。
没有启动 Helper，没有退休证明；会话结果为真实 HOLD，不能认为资源安全退休。
失败日志、构建差异和历史 HOLD 均保留；系统重启后通过新 boot 预检才继续，未清除旧守卫放行。

CPU 修复增加生产路径空图像/guide/尺寸防护、实际显示读取绑定、真实源与 VideoOut
核验，以及错误输出的一次作废。原生 HDR 的 NGX 输出 barrier 也包含 transfer-clear
访问域；重启后的两轮 validation 未再见该 transfer-clear WAW 或空 VkImage 报告。
纯 CPU contract 测试覆盖原始双空句柄、错图像、缺场景/context、FG 关闭、真实源错配。
CPU 验证不代表画质、正常退出或 GPU 运行已通过。

## 第一阶段实机结果

同一 GPU 构建 `0dff031`、RTX 4060 Laptop、驱动 615.78.08：

| 私有会话 | 成功真实/生成 Present | 结果 |
|---|---:|---|
| integration-native-smoke-r2 | 3590 / 1132 | 原生 SR-only，1132 对同源生成→真实配对 |
| integration-presetB-r2 | 7372 / 4705 | 联合 validation，620 次 Evaluate/Resolve 完成 |
| integration-presetB-playable | 9369 / 7055 | 联合热测，806 次 Evaluate/Resolve 完成 |

三轮均 `PASS_NORMAL_TEARDOWN`、rc0、无残留、无 watchdog/监督器信号，并分别具备
FG draw、FG Present 和 NR Pre-SR 的实际退休证明。热测日志轮转后保留的 3594 对配对无
顺序错误；累计数取最后成功 Present 台账，不能把保留日志样本数冒充整轮配对数。

F8 的真实 SDL/Overlay 回执确认 revision 1→2→3 与 cache/raw/cache 切换；FG 独立
关闭时 NR/SR 继续运行，恢复只重置 FG。覆盖转镜/短移、游戏菜单、全屏 resize、
1 秒 Native 暂停后首个恢复 dispatch 的 gap reset，未绕过 GPU 退休。
热测采样真实间隔中位数 19.27 ms、全部 Present 10.98 ms，生成/真实比 1.0；仅为 CPU
成功 Present-return 间隔，不是物理 scanout、端到端输入延迟或整轮平均 FPS。

实际生成图像已按 Present 顺序捕获，无损 PNG 往返验证后删除等价 raw 重复件；
生成像素不等于相邻真实帧。运动采样必须核对实际 pad 生效时间，不能仅以请求名判定
画面正在运动。画质仍为 Experimental，运动边缘可能重影。

validation 仍报告既有 WSI WRITE-AFTER-PRESENT、着色器接口 Location、sRGB view usage、
dynamicRenderingUnusedAttachments、部分游戏 draw layout 问题；不声明 validation 全净。
第一阶段新增约 1.6 GiB 私有运行目录/PNG，未打包或复制 Native 构建、SDK、模型。

## 第二阶段

同分辨率、同 SR 路径对照 FG 开关，区分生成/真实图像与后续显示缩放；再比较
HUDless/whole-frame、静态/转镜/快速运动，并仅在 SR-only 下测 1440p。
不为模糊对照扩展 NR 分辨率，不预设必须修改 FSR 插值算法。
