# NR 持久历史：云端源码与 CPU 回归

> 2026-10-28 · 基线 `nrbridge@b270c4e` · 独立分支 `cloud/nr-temporal-correctness-20261028`。
> 本轮没有 NVIDIA GPU、私有 NR 模型、游戏或本地采集证据。结果是源码契约与 CPU 数值验证，不是 GPU、画质或 NVIDIA 等价性验收。

## 本轮代码修复

`NativePersistentHistory::RecordCandidate` 原先只检查候选各帧的 `params` 彼此相同，另外检查 observed clocks 的 `params` 与 current 相同，缺少这两组之间的比较。一个内部一致、但模型参数版本错误的 retained request 可以通过该类自己的准入检查。

原先的时间门限只有 `latest_time - origin_time > 200`，没有检查各 catch-up 时刻是否有限、是否倒退。中间时刻为 NaN 或倒退时，仍可通过并形成无效／负的 `DeltaMs`。

这些是独立准入契约的缺口；正常 producer 另有 immutable tuple、模型重建 reset 和单调时钟保护。**未证明它们在玩家现场发生，也不将其描述为闪烁／亮尾的根因。**

反向复现直接提取 `b270c4e` 的旧 admission 源码块：全链 request.params=8 而 current/clock.params=3、中间 clock=NaN、以及总体 elapsed 为正但中间 1016→1008ms 倒退，三组旧代码均接受，新函数均拒绝。

修复将该检查提取为 [`native_persistent_history_admission.h`](../gpu/render/native_persistent_history_admission.h)，生产路径与 CPU 测试调用同一个函数：

- retained frame、current frame 和 observed clock 的参数版本必须对应。
- 所有参与时刻必须有限、非负、非倒退；总延迟仍为闭区间 `0..200ms`。
- 成功结果携带已检查的各帧时刻，直接用于 catch-up dispatch。
- request epoch/history 与独立 visual history 不直接比较，允许正常 request rollover。

准入拒绝发生在 finalize 旧 transition、创建 borrowed views 或记录新候选之前。不改变四条 motion link 上限、像素 TTL、双 slot 复用／退休、显示过渡、Helper 协议或玩家配置。Legacy、nearest、12 帧／200ms 的默认值保持原样。

## 源码路径与研究边界

| 环节 | 本轮核对的行为 | 仍不能推出的结论 |
|---|---|---|
| Admission | request 最多 4 条 link；逐帧要求连续、正确 previous、参数和 visual lineage；reset floor 拒绝旧 origin | CPU admitted 不等于 GPU 候选有效 |
| TTL | Capture 从 origin age=0 起；每个 Advance 累加一帧和真实 dt；Adopt 保留采纳像素的年龄 | 降低 TTL 不应修改 request admission 上限或刷新 age |
| 双 slot / bank | 两个资源 slot 由同 Native timeline 末用 proof 保护；active bank 0/1、target bank 2/3；最长路径 10 次 history dispatch | CPU 生命周期检查不能证明 Vulkan 同步或正常退出 |
| Fractional motion | motion 转换会写入 RG16F；默认 nearest；可选 cubic 逐非零 tap 检查同源年龄、深度、颜色和 TTL | nearest 量化误差不构成默认开启 cubic 的依据 |
| 深度边缘 | previous depth 来自相机变换；边缘需要至少三个独立 cardinal temporal supports | 沿 Z 运动的物体可能保守丢历史；不能据此放松 depth gate |
| 发布 | residual 存入 RGBA16；Compose 后 Return 做整帧数值检查和 half 发布，保留 raw alpha | CPU 模拟不能代替真实图像存储舍入、GPU 浮点和 NVIDIA 参考 |

`test_reference.py` 的旧简化 oracle 另有负值／NaN age 未拒绝的问题；本轮修正测试模型，使年龄边界与 shader 一致。修复迁移前目录引用，使 candidate capture 的既有测试重新参与运行。

## CPU 复现

仅需 Python 3 和 C++20 编译器，无需 Vulkan 设备或私有资产：

```bash
python3 tools/persistent_history/run_cpu_tests.py
python3 tools/persistent_history/temporal_step.py
```

统一入口自动发现目录中的 Python 和 C++ 测试；编译产物仅进入临时目录。单步探针分开报告 motion、捕获存储、采样、Compose／发布误差和拒绝原因；它是合成数据实验，不是 NVIDIA 参考或玩家画质评分。

探针直接提取当前 shader 的 `finite1`、`depthValid`、`sameSurface`、`depthResidual`、`cubicWeights` 并通过小 C++ adapter 执行。其他阶段是显式 FP32／half 的独立 oracle，不覆盖完整相机变换、packed colour guide、所有 cardinal-edge 检查或 GPU 调度；host libm、FMA、非正规数及真实 GPU 图像舍入仍有差异。

默认合成用例的 RNE 结果：motion `.2501` 量化为 `.25`；线性 ramp 的 nearest 采样误差为 `-.015625` log2、cubic 为零；捕获存储最大误差约 `2.16e-4` log2，发布相对目标最大误差约 `2.15e-4` RGB，发布本身新增约 `1.85e-4` RGB。这些数字只描述该解析用例；脉冲往返测试同时证明重复 cubic 仍会损失细节，不能据 ramp 结果宣称总体画质改善。

## 实际验证结果

- 持久历史统一入口：45 项 Python 测试、3 个 C++ 测试程序通过。
- `test_admission.cpp` 严格警告编译和 UBSan 通过；移除新增时钟／参数检查的变异均被回归捕获。
- `tools/nr_dev/tests/test_history_experiment.py`：4 项通过；`tests/nr_runtime/control_test.cpp` 编译、执行通过。
- 原样 `history.comp` 用 glslang 15.2.0 编译并经 `spirv-val --target-env vulkan1.1` 验证；checked-in GLSL／SPIR-V 的 provenance SHA256 匹配，嵌入的 SPIR-V 也通过验证。没有改写 shader 或已记录的编译身份。
- `git diff --check` 通过。
- `bash scripts/dev-build.sh --test` 在依赖检查处停止：缺少 `out/deps/bbport-env.sh`。因此没有完整 Native 构建结果，没有 Vulkan 设备执行，更没有 NVIDIA／游戏／视觉／正常退出验收。

## 本机最小 GPU 验收

1. 在此分支按 [`tools/nr_dev/README.md`](../tools/nr_dev/README.md) 构建 Native 并运行 `--test`，保存新构建身份。沿用已有私有 Helper／模型／profile／研究存档，通过现有 `play-experiment.sh` 入口选择该 Native；不要把源码变更视为已安装版本更新。
2. 先跑已有对照组合 **New / 1080p Quality / Preset B / FG Off / Legacy / nearest**。实际确认进入场景，做一次短跑停、缓慢分数像素转镜、遮挡／深度边缘经过。确认候选继续采纳，F8 能切回对照，主观检查闪烁、亮尾和亮暗边；如需定位，另开短诊断窗 `BB_NR_HISTORY_SAMPLE_INTERVAL=1`，保留原始日志和已有 capture。不要用 X11 录像或 Present 计数替代视觉判断。
3. 正常退出，要求既有 `PASS_NORMAL_TEARDOWN`、rc0、真实 Native／Helper 末用完成、`residuals=[]`、prefix RETURNED 和 receipt retired；遇 HOLD 走原恢复流程。只有以上通过，才扩到 NR-only、Old、1440p、NR+FG 或显式 cubic／fade-new 对照。此轮不支持扩大默认策略或直接更新正式安装。

GPU 回归最关键的观察是：正常候选没有因参数比较错误而全部被拒绝；request rollover 后历史仍连续；重置／模型版本切换后旧候选不能重新进入；正常退出与资源退休仍完整。若要解决 NVIDIA 的单步历史误差，还需要同版本、同输入、同分辨率参考和实际采集，云端合成探针无法填补这项证据。
