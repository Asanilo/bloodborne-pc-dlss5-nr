# Linux 键鼠与运行维护

开发源码与实际安装目录分离。此机使用 `/home/arinp/bloodborne`；运行包、游戏、
`bbport.ini`、启动器配置、存档及日志都保留在该目录。当前主线基线与复现方式见 [开发交接](HANDOFF_DLSS_NR.zh-CN.md)。

远端 `origin` 指向个人 fork `git@github.com:Asanilo/bloodborne_pc.git`，
`upstream` 保留 `https://github.com/deadinside28/bloodborne_pc.git`；没有推送代码。

## testing / 0.5-pre4 合并

下列本地功能已与上游 testing 整合，统一到本地 master；原独立 worktree 完成归档后清理。
原生 DLSS SR 沿用上游实现，打包 NVIDIA SDK 310.9.1 运行库；Native AA 对应 DLAA，
Quality 对应 1.5 倍超分。只有能力检测失败才回退 FSR 3.1，不再一律拒绝 DLSS。
安装器支持 `--stage-only` 与 `--activate RELEASE`，先验证候选包再切换版本。
本次构建、测试、部署的记录见 `INTEGRATION_DEPLOY.zh-CN.md`。

## 之前稳定版的验证记录（2026-10-10，非此次 testing 验收）

- 完整原生构建和 C 运行时测试通过；设置、输入快照、超分能力、运动历史与 UI 合成
  C++ 测试通过；Python 测试 111 项通过。中文启动器窗口完成显示检查。
- 原生运行包已安装到 `/home/arinp/bloodborne/runtime/current`，默认入口仍是根目录
  `launch.sh`。原运行包及 `launch-before-native-20261010T060739Z-22dc72b9.sh` 可供回退。
- RTX 4060 Laptop GPU 实机诊断通过。最近连续三次默认参数启动，在启动画面预热后
  均达到约 60 FPS，记录窗口最差帧为 16.9–17.5 ms；每次由 15 秒测试看门狗退出，
  退出码 142 是主动超时，不代表正常关窗验收。日志在 `setup-history/native-repeat-*.log`。
- 更早三次启动出现过约 1 FPS 和 GPU 主机拷贝等待；之后未复现，原因尚未查明。
  因此不能视为长期稳定性验收通过。复现时可直接运行上面的旧版启动脚本。
- 八次短启动均生成了备份，当前存档与首次启动前备份的文件内容校验一致。
- 真实战斗画质／帧时间、实体手柄、完整键鼠操作和两个管理器的启动退出尚未验收。
  第三、四阶段只有分析工具、能力回退和接入评估；未宣称完成 DLSS、FG 或 HDR 校准。

## 已实现

- 鼠标相对位移控制右摇杆视角，左键轻攻击、右键开枪／左手攻击、中键锁定，F 重攻击。
- WASD 移动，左 Ctrl 按住慢走；空格／小键盘 Enter 确认，Shift 返回或闪避，Esc／Enter 打开游戏菜单。
  IJKL 是方向键。原生背包菜单仍使用按键导航。
- Insert 打开移植版设置。设置／文本输入／失焦时释放鼠标并清空游戏键鼠状态，恢复焦点
  后丢弃捕获前的鼠标事件，按键需重新按下。设置中可立即关闭鼠标捕获。
- 启动器支持键盘与鼠标混合绑定、动作名称、冲突提示、逐项重置、默认键鼠和旧键盘预设。
  灵敏度、垂直反转和键盘转向速度下次启动生效。鼠标映射受游戏右摇杆最大转速限制。
- 启动前自动备份 `user_dir/savedata`，自动备份保留最近 10 份。备份带 SHA-256 校验。
  恢复前创建独立安全副本；运行中提交的恢复请求在进程退出后执行，异常终止时在下次
  启动前执行。进程锁防止两个受管理的游戏会话同时写同一存档目录。
- 启动器提供备份选择／恢复、存档目录、日志目录、管理器入口导出。修改存档目录后刷新备份列表。
- 启动器常用设置与游戏内画质设置支持简体中文。游戏内中文字体使用系统 Noto CJK，
  或 `BB_CJK_FONT` 指定的字体；缺少字体时需要安装中文字体。少数开发／FSR 资源构建诊断仍回退到英语。
- 启动诊断检查缺失动态库、Vulkan 1.3 硬件显卡，并报告型号、API 和显存堆大小。
- 更新安装器创建版本目录和 `runtime/current` 链接，保留前一版本、原始启动脚本及数据。
  原 AppImage 内的本地修复仍保留；原生包使用宿主运行库，不复用 Nix 专属库路径。
- 原生 DLSS SR 可用；缺少运行库或不支持时提示并回退 FSR 3.1。XeSS 尚未接入；不支持 HDR 输出时保持 SDR。
- `scripts/frame_report.py` 分析真实渲染帧时间的中位数、P95、P99、最差帧、长帧数量，
  并列出慢帧对应的原始计数。显存字段是渲染器跟踪的图像／缓冲分配量，不等同于整卡占用。

## 启动和维护

```bash
/home/arinp/bloodborne/launch.sh         # 设置界面
/home/arinp/bloodborne/launch.sh --play  # 直接游戏
```

选择“简体中文”启动器语言后，下次启动游戏时游戏内菜单也会采用中文；若之前已在
游戏内选择过语言，则游戏内保存的选择优先。

备份默认放在所选存档目录的 `backups` 下。此机为
`/home/arinp/bloodborne/data/saves/backups`。手动备份和恢复前的安全副本不参与 10 份自动轮换。
不要绕过 `launch.sh` 启动多个写同一存档的运行程序。

```bash
python3 scripts/save_manager.py --user /home/arinp/bloodborne/data/saves list
python3 scripts/save_manager.py --user /home/arinp/bloodborne/data/saves backup
python3 scripts/save_manager.py --user /home/arinp/bloodborne/data/saves restore BACKUP_NAME
```

本地构建用 `bash build.sh --test`；依赖见 `shell.nix`。此机额外开发依赖仅解包到
`out/deps`，未安装系统软件。构建与执行使用同一输出库的测试必须串行，避免测试读取
链接器尚未完成的 `.so`。

```bash
python3 scripts/install_native.py --root /home/arinp/bloodborne
```

非系统目录中的额外动态库用重复的 `--library FILE` 指定。更新后 `runtime/previous`
指向之前的原生版本；首次安装生成 `launch-before-native-时间戳.sh`，可用该脚本启动保留的旧包。
安装器不会替换无法识别的自定义启动入口。

## Steam 与 Lutris

`managers/bloodborne-pc.desktop`、`bloodborne-lutris.json` 及 SVG／PNG 图标和封面由
启动器的“导出管理器入口”生成。PNG 转换需要 `rsvg-convert`。

Steam 通过“添加非 Steam 游戏”选择 `/home/arinp/bloodborne/launch.sh`，启动选项填写
`--play`，不要强制 Proton 兼容工具。使用导出的 PNG 设置图标和自定义封面。
这里遵循 [Steam 官方添加流程](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)，
不直接改写运行中的 Steam 快捷方式数据库。

Lutris 用“从本地安装脚本安装”选择导出的 JSON，使用原生 `linux` runner。
脚本依据 [Lutris 安装器格式](https://github.com/lutris/lutris/blob/master/docs/installers.rst)
设置执行文件、参数和工作目录。两个管理器均复用同一启动入口和备份流程。
入口已经生成；通过两个管理器实际启动、退出的验收尚未完成。

## 画质及高级功能评估

已有渲染路径包含物体矢量、透明效果响应遮罩、UI 合成与 TAA。当前修改没有把未经固定
场景对比的着色器改动当作画质提升。清晰度／平衡／性能预设沿用现有真实内部渲染比例。

DLSS：本分支保留上游 `gpu/dlss_bridge` 与 `vk_dlss.cpp` 原生 NGX Vulkan 实现，
并保留审计分支的 `BB_DLSS_AUDIT=1` 诊断日志。支持与否由运行时检测决定。
DLSS 超分与 NR 是不同功能；本次没有接入 NR，也未修改 OptiScaler。

FSR FG：依赖子模块已有 `ffx-vulkan::fsr3-vk-framegeneration-3.1.6` 和 presenter policy，
主渲染器尚未接线。[AMD 文档](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
要求 UI 合成与插帧呈现节奏处理。下一步要接入无 UI 场景、深度／矢量、插帧交换链和
同步，覆盖关窗／失焦／改分辨率／启停回退，并分别统计渲染帧与呈现帧和输入延迟。
当前始终不插帧，菜单明确说明 FPS 为真实帧。

HDR：本次仅补强不支持时回退 SDR。尚未完成峰值亮度／纸白校准及 HDR 显示器验收。
Steam Overlay 运行库冲突尚未解决；添加游戏入口不依赖 Overlay。

## 实机验收方法

1. 同一存档、场景、天气和行走路线，固定输出分辨率；分别记录原生 TAA、FSR 3.1
   清晰度／平衡／性能。冷缓存与热缓存分开记录。
2. 每组预热后至少录制 60 秒：静止细节、快速转向、战斗粒子、透明效果和进入新区域。
   使用 `BB_PAD_RECORD`／`BB_PAD_REPLAY` 重复相同操作；同时记录截图／视频和整卡显存占用。
3. 勾选“保存日志与帧时间文件”，运行：

   ```bash
   python3 scripts/frame_report.py --warmup 5 /home/arinp/bloodborne/data/logs/*.frames.csv
   ```

4. 比较静态细节、拖影和遮挡边缘，以及 P95／P99、超过 33/50 ms 的长帧。启动菜单的
   帧率不能替代战斗场景性能，也不能以显示帧率代替输入延迟。
5. 实体手柄需要手测热插拔、断线重连、振动与键鼠交替。自动化已覆盖虚拟手柄路径，
   不等同于所有实体设备验证。键鼠还需手测 Alt-Tab、文本输入、设置开合与完整战斗流程。
