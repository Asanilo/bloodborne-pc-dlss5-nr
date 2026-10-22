# NR + DLSS SR + FSR FG 集成实验

状态：**CPU 构建通过，尚未完成 GPU 验收，不能作为可玩版本交付。**
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
失败日志、构建差异和守卫均保留。当前 boot 不再启动 GPU 测试，需要系统重启后重检。

CPU 修复增加生产路径空图像/guide/尺寸防护、实际显示读取绑定、真实源与 VideoOut
核验，以及错误输出的一次作废。原生 HDR 的 NGX 输出 barrier 也包含 transfer-clear
访问域；该同步修复仍需重启后的 validation 实测。
纯 CPU contract 测试覆盖原始双空句柄、错图像、缺场景/context、FG 关闭、真实源错配。
CPU 验证不代表画质、正常退出或 GPU 运行已通过。

## 重启后继续

1. 新 boot 重新执行现有 preflight，保留历史 HOLD；先短测原生 SR/FG 的绑定与正常关闭。
2. 启用 Preset B，确认 cache_selected、实际 SR read、同源生成/真实 Present 配对，
   测试 F8/FG 独立切换、场景恢复和两组正常退休证明。
3. 完成第一阶段可玩验收后，再对照同分辨率 FG 开关、静态/转镜/运动、真实/生成帧、
   HUDless/whole-frame，并在 SR-only 下测试 1440p。此前不声称已定位模糊来源。
