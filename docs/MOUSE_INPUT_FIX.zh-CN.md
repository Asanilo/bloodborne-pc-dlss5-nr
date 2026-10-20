> Historical note: local paths and machine identifiers are redacted for publication.
> Original evidence is preserved in private development data; this is not a new acceptance run.
> Current player instructions are in the project README.

# 鼠标视角修复（2026-10-10）

修复最初在 `.worktree/dlss-input-integration` 验证，后续已纳入本地主线；
当前基线见 [开发交接](HANDOFF_DLSS_NR.zh-CN.md)。下方测试产物路径现相对于仓库主目录。

## 根因与修复

本机 niri / Xwayland / SDL 3.4.18 的实际鼠标事件进入了 SDL 队列，但
`event.motion.timestamp > capture_started_ns` 判断把位移全部拒绝。
失败日志中事件 timestamp 为 `20503152`，捕获起点为 `205137959`，
当前 SDL 纳秒时间为 `7228715585`；随后事件每约 260 ms 只增长约 260，
与本地纳秒计时不可直接比较。这里记录观察结果，不认定所有 SDL 后端均如此。

`gpu/shim/window.cpp` 取消跨时钟比较，在成功切换相对鼠标模式时使用
`SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION)` 丢弃排队位移，并清空相对鼠标累积量。
失焦、设置菜单和文本输入仍通过 HostInput 阻断并清空键鼠状态。
`gpu/shim/sdl_window.h` 删除不再使用的捕获时间戳。

`BB_INPUT_TRACE=1` 可输出原始事件、接受的位移、捕获状态和最终右摇杆值；默认关闭。
日志入口仍为启动器的日志目录。诊断结束后删除此环境变量，避免持续刷日志。

## 可执行回归

在已配置好依赖的 GPU 构建目录执行：

```sh
cmake --build out/gpu --target window-input-test
SDL_VIDEODRIVER=x11 out/gpu/window-input-test
SDL_VIDEODRIVER=wayland out/gpu/window-input-test
```

动态依赖位于非标准路径时，沿用构建时的 `LIBRARY_PATH` / `LD_LIBRARY_PATH`。
需要相应桌面会话。测试只创建窗口，不运行游戏，不读写存档。
该目标不加入无显示器测试套件。

`tests/test_window_input.cpp` 验证很小的后端时间戳仍能传递新位移、停止移动归零、
失焦时丢弃位移、恢复焦点无残留、关闭/重开捕获时丢弃旧位移并接受新位移。
预期输出：`PASS: backend timestamps, idle, focus loss/restore, capture off/on`。

## 本机结果

- 编译和 `build.sh --test` 成功；115 项 Python 测试通过。
- X11 和 Wayland 窗口回归均通过，日志在 `out/input-fix/window-regression-*.log`。
- 使用隔离配置与存档副本，在猎人的梦境发送临时虚拟鼠标的相对位移。
  修复前原始事件到达但 `mouse_samples=0`；修复后右摇杆由 `128,128`
  变为 `187,128`，停止后回到中立，实际画面确认视角转动。
- 失败日志：`out/integration/game-quality-1791627921704586554/run.log`。
- 成功日志：`out/integration/game-quality-1791628016513303277/run.log`。
  同次运行 DLSS Quality 成功执行 2910 帧，测试在 65 秒看门狗到期退出（142）。
  这不是正常退出或完整战斗画质验收。
- 画面证据：`out/input-fix/before.png` 与 `out/input-fix/after.png`。

候选部署包：`${HOME}/bloodborne/runtime/releases/20261010T102648Z-6ad9496e`。
保留旧包，通过安装器原子切换 `runtime/current`，不覆盖用户设置和存档。
这一包也带有本轮先前完成的 Esc→Options 修正与中文语言入口，用户语言选择不自动更改。

## 游玩复查

重新从现有 Lutris 入口启动，在可操作场景左右/上下移动鼠标，停止后确认视角停止。
用 Insert 打开移植版设置检查光标，再关闭设置并继续转动；切出、切回检查无残留转向。
游戏原生背包等菜单仍使用按键导航，本修复没有加入原生菜单鼠标点击。
