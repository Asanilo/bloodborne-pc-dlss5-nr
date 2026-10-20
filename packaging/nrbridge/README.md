# bbport NRBridge 私人 AppImage Preview

这是私有 NRBridge AppImage Preview 的打包入口。当前默认输入是 Native
`c91bf9467604c36d70d97dd200844ae1ac8e8428`、Helper PE
`1eddac56e4de26c0`（源码 `713f8ac9`；wire 1 / interface_revision 4 / 64-slot
daily；PE `b372b1f1…`）、forwarder `9a8251430ccfa68a`。打包器
**不在代码里校验这些固定值**：它读取所选
Native 的 `source-head.txt`/`artifacts.sha256`/`build-inputs.json`、Helper 的
`build.json` 和 forwarder 的 `build.json`，按其中实际 manifest 核验身份、协议和
SHA，并把结果写入 AppImage 的 `nrbridge-bundle.json` 供 launcher 读取。后续新
Native/Helper（导出 wire 1 / revision 4，daily bootstrap wire 2 / 64 槽）只需传显式路径。

它不是公开发布物：游戏、存档、凭据、专有 NR 模型（`nvngx_dlssnr.dll`）、Wine/GE
均不进入 AppImage。旧验证产物只读，打包只复制、不覆盖。已知问题与当前有限验收
边界见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)（新 boot 有限 NR/标题恢复/正常退出通过；
旧 HOLD 保留；着色闪/亮尾未修；online 与真实谏言未验；独立进程存档重载已通过）。

包内 `build_status: BUILD_ONLY` 只是打包构建元数据，不代表外部实际 GPU/online
验收，也不应据此把预览描述为 stable。

## 构建

```bash
bash packaging/nrbridge/appimage.sh
# 只生成并检查 AppDir：
bash packaging/nrbridge/appimage.sh --skip-appimage
```

工具只落在 `out/tools/`（appimagetool + 私有 venv 里的 patchelf），不装系统。
默认读取：

- Native：`/home/arinp/Code/nrbridge-dev/build/baselines/c91bf94...`
- Helper：`build/helper-runtime-model/1eddac56e4de26c0/wine_runtime_model.exe`
- forwarder：`build/convergence/forwarder/9a8251430ccfa68a/nvngx.dll_dlssnr.dll`
- 在线库：`out/online-deps-20261020/root/usr/lib`
- 编译期 sysroot 库：`out/deps/root/usr/lib`

用 `--native-build/--helper/--forwarder/--online-deps/--sysroot-lib` 覆盖。

## AppImage 的结构与裁剪

- 复用上游 `packaging/default.nix` 的组成：launcher、scripts、patches、GTK4/Adwaita、
  Adwaita/icons、Mesa/驱动交给宿主、CJK 字体、GTK 不含 GStreamer 管线。
- 只收集 ELF `NEEDED` 闭包（Native、`libbbgpu`、`libbbnet(20)`、DLSS bridge、
  Python/GI、GTK、pixbuf loader），glibc 与 NVIDIA/AMD 驱动库留给宿主。
- 用隔离的 `patchelf` 删除开发绝对 `RUNPATH`，并检查 `readelf -V` 前后一致；
  `bb-probe` 只保留 `$ORIGIN/gpu`。
- `usr/share/doc/bbport/appdir-manifest.json` 记录每个文件的大小/SHA，以及
  “模型/游戏/存档/凭据未打包”的明确标记。
- Helper 的源码 HEAD 与 PE manifest `source_head` 由新 manifest 读取；包内
  `nrbridge-preview.json` 和生成的 Helper `build.json` 保留派生说明，不冒充同源
  重建；协议版本（wire/revision）由两份 manifest 动态比较，只接受 wire 1 /
  interface 3 或 4。
- AppImage 根部的 `nrbridge-bundle.json` 是唯一的运行时身份来源；launcher 不写死
  Native SHA、Helper build id 或协议 revision。
- daily 判定使用 Native 的精确 marker `NRBRIDGE_NATIVE_DAILY_RING64_V2`，且
  Helper 必须 `wire_version=1`、`interface_revision=4`。旧 Native/Helper 仍可
  `--check`，但 `daily` 会被明确拒绝；Helper exports SHA 全部从新 `build.json`
  读取，不写死。
- 每日会话状态在 `NRBRIDGE_HOME/state.json`：上次 `PASS_NORMAL_TEARDOWN` 且运行
  输入未变时自动 `--continue-session`；HOLD/无结果/输入变化时不自动续用，保留
  `runs/<session>/profile/user/savedata` 并提示玩家选择 `--new-game` 或明确的
  `--continue-session`。首次运行没有状态时使用空 seed 新游戏。
- 在线默认 **offline**。需要在线时显式传
  `--online-pool shadnet|thd --online-account-config <canonical launcher settings.json>`
  （`shadnet` 可加 `--bbsc`；`thd` 不允许 BBSC），entry 原样转发给 daily runner。
  GUI 的 “Discord login / Online settings” 直接打开上游 bbport launcher 的
  Online 页（`BB_LAUNCHER_PAGE=online`），用户自行登录；entry 不调用任何会打印
  token 的 CLI，也不打印账号/token。runner 的 `daily_online.py` 只复制当前 pool
  白名单到私有 0600 profile，并在 manifest 中脱敏。若打包的 runner 缺少这些
  flag，entry 会拒绝，不会静默降级离线。

## 普通启动（首次运行）

双击 AppImage（有显示服务器）会打开一个最小 GTK4/Adwaita 窗口：显示包内
Native/Helper/协议/forwarder 身份，选择本机游戏、Wine/GE、模型和 NGX 路径，
`Check` 做完整静态校验，`Launch daily` 在终端里执行规范 daily 入口。无显示或
`--daily/--check/--setup` 时走同一套 CLI。GUI 还有 “Joystick mouse fallback”
开关，透传 `--joystick-mouse`；默认仍是 `run.sh` 的 `BB_NATIVE_MOUSE=1`。


AppImage 的 `AppRun` 调用 `launcher/nrbridge_entry.py`。它创建可写数据根
`NRBRIDGE_HOME`（默认 `~/.local/share/bbport/nrbridge-home`），把校验过的 Native
四产物、Helper、forwarder 和闭包库复制进去，然后调用协调方的规范入口：

```bash
tools/nr_dev/play.py --daily --local-inputs <home>/localinputs/runtime.json \
    --native-build <home>/build/baselines/c91bf94...
```

它不另造生命周期、预算或退休规则。首次需要显式选择：

```bash
./bbport-NRBridge-preview-x86_64.AppImage --setup \
  --game-dir /path/to/CUSA03173 \
  --ge-files /path/to/GE-Proton/files \
  --model /path/to/nvngx_dlssnr.dll \
  --ngx-dir /usr/lib/nvidia/wine \
  --system-ngx /usr/lib/libnvidia-ngx.so.615.78.08
```

`--check` 只做静态校验；`--print-command` 只打印将执行的 daily 命令（包含自动
续用/在线 flag，不打印凭据）。模型会显示 SHA 并做 PE 校验，但不会被复制。外部
游戏/GE/模型路径在配置里以规范绝对路径保存；运行包不包含任何旧 worktree 路径。

后续 Native/Helper/forwarder 更新时：

```bash
bash packaging/nrbridge/appimage.sh \
  --native-build /path/to/build/baselines/<new-clean-sha> \
  --helper /path/to/build/helper-runtime-model/<new-build>/wine_runtime_model.exe \
  --forwarder /path/to/build/convergence/forwarder/<new-build>/nvngx.dll_dlssnr.dll
```

打包器会比较 Native 与 Helper manifest 中的 `wire_version`/`interface_revision`/
`export_manifest_sha256`；不匹配会拒绝。构建的输入路径、原始/重写后 SHA 和最终
AppImage SHA 另写在 `out/nrbridge-appimage/build-report.json`，不进入发行包。

## 当前验收状态与缺口

- 包内 `build_status: BUILD_ONLY` 是打包构建元数据，不是 GPU/online 验收结论；
  当前只是有限验收，不应表述为 stable。
- 新 boot `7fb96dc9…`：同字节 `c91bf94…` Native + `1eddac56…` Helper 的受控
  真实 NR 会话在 scene-ready 后 **611.12 s** 正常关闭；**4765 finalized /
  4696 captured / 69 obsolete / 0 skipped**；Feature create/release 各 **1**；
  `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`，wait **0**、holders 空、
  kernel PASS、bootguard retired；标题驻留约 **121 s** 后 scene 恢复，同一
  Helper/session 继续处理新的 NR 任务。
- 旧 boot `54bbe…` 的 HOLD 仍作为历史证据保留，不因新 boot 正常退出而追认解决。
- r2 独立新进程从自动 continue 进入 **Clinic / "300 echoes"**；基础存档写入与
  独立进程重载已通过：复制时源存档与 manifest inventory 一致，启动后
  `userdata0010` 正常更新；不声称 postboot 全字节不变。SR-only 重载已
  `PASS_NORMAL_TEARDOWN`；no-peer 真实末用 PASS、holders 空、kernel PASS。
- 着色闪与亮尾未修；F8 SR 的单次观察不能外推。online 仍未登录，真实谏言未验证；
  数小时长跑与更多场景不在本次验收范围。
- 当前新包使用 Native `c91bf94`（含 `NRBRIDGE_NATIVE_DAILY_RING64_V2`）与 Helper
  `1eddac56`（revision 4），`daily_ready=true` 仅表示打包/协议前置条件满足，不是
  GPU 验收。旧 6753999/7f69 包仍可 `--check`，但 `daily` 会被拒绝；不要用旧包
  宣称 daily 可运行。

此私人闭包按本机 CachyOS 构建，部分依赖需要 GLIBC 2.44；旧发行版兼容性未验证。本机无 Nix，本候选采用上游组成和裁剪规则收集 ELF 闭包，没有声称完成 Nix bundler 验收。
