# Smooth Motion 交付给 MultiPass 候选（交接说明）

分支：`sm/present-lifecycle`（worktree `.worktree/sm`），HEAD `fb6a315`，已 rebase 到
`nrbridge` `ce175c1`，与主线无冲突。rebase 前 tip 保留为标签 `sm-pre-rebase`。
写入负责人：Sol（`wG:p3`）。本文件只做交接，不是验收结论。

> **合流前提（2026-10-29 用户要求，覆盖本文早前表述）**：SM 必须先在实机上通过用户验收，
> 才能合入 MultiPass 候选。本文第五节“效果未证明不阻塞源码合流”**不再是放行依据**；
> 在用户明确验收前，请勿继续源码合流。已提交内容保留，不 reset、不删除。

## 一、必须合入的功能提交

| 提交 | 内容 | 文件 |
|---|---|---|
| `c1760e3` | SM 退出生命周期修复（唯一必需的 C++ 改动） | `gpu/render/presenter.{cpp,h}`、`gpu/vulkan/swapchain.{cpp,h}` |
| `36e54ed` | 恢复 Smooth Motion 为可选后端 | `tools/nr_dev/fg_config.py`、`launcher/bbport_launcher.py`、`tools/fg_dev/test_unified_backend.py`、3 个文档 |

其余提交都是实验工具、文档或测量代码，**不阻塞源码合流**，可按需挑选或丢弃：
`e2c977d`、`9f28451`、`b09ea2c`、`a95d73d`、`88cef00`、`fc11c87`、`82596be`、`aba4396`、
`11b1410`、`29758db`、`f7b958b`、`af99f64`、`564ae10`、`962a74a`、`92d1768`、`ac6dcb9`、
`e23de99`、`2d61b71`、`fb6a315`。

其中 `92d1768` 的 prefix 修复只属于 `tools/sm_lab/acceptance.sh` 的验收编排（借用已退休
prefix 的 owner），**运行时的 prefix 语义没有改动**（`tools/nr_dev/prefix_lease.py` 未改）。

## 二、生命周期改动要点（`c1760e3`）

- `Presenter::PrepareFrame`：入口在 `prepare_gate_mutex` 下取租约；退出闸门关闭后直接返回
  `&blank_frame`，不再触碰 swapchain 相关状态。
- `Presenter::Present` 的 SM 退出分支：先关闸门 → `release()` 自己持有的帧（让已在等空闲帧的
  `PrepareFrame` 能结束）→ 最多等 15 s 让在飞租约归零 → 销毁 FG 图 → `RetireDriverLayer()`。
  超时打印 `SM_EXIT_HOLD reason=prepare_in_flight_timeout` 并置 `held`，不销毁、不伪造成功。
- `Swapchain::Drain()` 与实际销毁分离：`RetireDriverLayer()` = drain → 销毁视图/信号量 →
  销毁 swapchain → 再 drain；任一次 drain 失败直接返回 false，不销毁资源。
- 帧释放改为一次性（`released` 标志），提前释放不会重复入队。
- 不改提交顺序、生成帧先于真实帧、fence 只在最后一次真实 present 上 signal。

## 三、可选启动开关（`36e54ed`）

- 移除 `fg_config.py` 的 `SM_UNAVAILABLE` 两处 raise，标签去掉 “(unavailable)”；
- `--fg-backend smooth-motion` 与 `--smooth-motion` 均可选中；
- 防线保留：`smooth_motion.check_capability` 仍要求二进制含 `SM_EXIT_PROOF`；daily runner 的
  boot 收据仍拒绝 pending/HOLD 时的新会话。

## 四、直接冲突注意事项

- `gpu/render/presenter.cpp`：我的改动在 `PrepareFrame` 入口（约 239 行）和 `Present` 退出分支
  （约 750 行）；MFG 的 M1 测量（按其分支行号）在约 883–892（`LAT_MEASURE` 摘要）与 `PresentImage` 的
  968–1045（`measure.Allocate/Submitted/Returned/Failed`）。区域不同，文本冲突概率低。
  语义上要保持：`PrepareFrame` 闸门先于任何测量；`Allocate` 只对真实/生成的应用 present 调用。
- `gpu/render/presenter.h`：成员区相邻（`fg_images_mutex` 附近），可能是唯一的小冲突点。
- `gpu/vulkan/swapchain.{h,cpp}`：只有我的改动，MFG 声明未改。
- `tools/nr_dev/fg_config.py` / `launcher/bbport_launcher.py`：主线 `ce175c1`、`9cc0e8a` 改过
  启动器 GPU 选择；我的 rebase 已消化，Sol 在其上应用即可。

## 五、依赖与验证

- 无新增第三方依赖，`gpu/CMakeLists.txt` 未改。
- CPU 测试：`tools/fg_dev` 60 项、`tests` 启动器 43 项、`tools/sm_lab/test_prefix_plan.py` 9 项、
  `tools/sm_lab/test_frame_report.py` 7 项，全部通过。
- 实机证据（生命周期验收，**不构成插帧效果验收**）：新 boot `6df86d77` 上官方 daily 三轮
  `PASS_NORMAL_TEARDOWN`（`sm-acc-1029-174527-sm1`、`-174813-off2`、`-1752-sm3`），
  `SM_EXIT_PROOF` + `FG_EXIT_PROOF` + `NATIVE_ASYNC_PRE_SR_EXIT_PROOF`，无 DeviceLost。
- 未证明：驱动是否真的向显示端插帧（缺 scanout 侧帧身份，见原型文档的 MFG 评估节）。
  **这是合流前必须由用户验收的部分**；现有工具只能证明“层已启用并接管呈现”，不能证明
  “显示端出现了插帧”。
