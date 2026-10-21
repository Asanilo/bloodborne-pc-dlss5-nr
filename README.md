# Bloodborne · DLSS 5 NRBridge

<p align="center">
  <strong>English</strong> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ru.md">Русский</a>
</p>

![Bloodborne · DLSS 5 NRBridge · Native Linux](docs/assets/nrbridge-banner.svg)

<p align="center">
  <strong>Return to Yharnam. On Linux.</strong><br>
  DLSS 5 NR + DLSS SR · Native mouse camera · Persistent visual history · Full launcher
</p>

<p align="center">
  <a href="#features">Highlights</a> · <a href="#getting-started">Get started</a> ·
  <a href="#how-it-works">How it works</a> · <a href="#docs">Build &amp; docs</a>
</p>

**NRBridge brings DLSS 5 NR to Bloodborne’s native Linux port.**
Built on [bbport 0.6](https://github.com/deadinside28/bloodborne_pc), this fork connects
independent NR inference to native DLSS SR, alongside direct mouse control, composite
key bindings, Chinese settings and save management—all accessible from the full launcher.

![Bloodborne running NRBridge on Linux](docs/assets/nrbridge-gameplay.png)

<p align="center"><sub>Captured in-game with NRBridge enabled. The bottom-right overlay shows the live NR state.</sub></p>

<a id="features"></a>

## A new rendering path for Yharnam

| 🟢 NR + DLSS SR | 🌙 Persistent visual history |
| :--- | :--- |
| A separate Helper runs the NR model and feeds its results back into native rendering, with in-game NR controls. | NR results are retained and refreshed across frames, supplying valid visual history to subsequent SR frames. |
| **🖱️ Native keyboard & mouse** | **🎛️ The full launcher** |
| Direct camera control, a menu pointer, composite bindings, button prompts and Chinese settings. | Graphics, input, Mods/patches, online options and saves in one place, with automatic backups and restore. |

Also retained from bbport 0.6: controller support, aspect ratios and in-game graphics
settings, DLC/content mounting, and **21 game-language choices**; actual translations
depend on your game files. The Hunter’s Dream, Seamless Hunt and optional BBSC
controls are integrated, with online content undergoing further validation.

**Working in-game:** NR rendering, native mouse input, scene/title recovery, save
write and reload, and normal exit. The player confirmed more responsive mouse look,
correct stopping, F1 release and recovery from menus.

<a id="getting-started"></a>

## Start your hunt

### You will need

- **Linux x86-64**, a compatible NVIDIA GPU, and Vulkan/NGX driver support.
- Your own **Bloodborne 1.09** game files.
- Local Wine/GE, an NR model, and matching Helper/forwarder builds; see the
  [dependency and build guide](tools/nr_dev/README.md).

### Launch

With a prepared AppImage or installation, open the AppImage or `./launch.sh`:

1. Choose your game and save directories, then configure the local **NR runtime inputs**.
2. Leave **NR** and **native mouse** enabled; select DLSS Quality and 1920×1080.
3. Start the game, enter a controllable scene, then press **F8** to enable NR
   without leaving the game. The launcher's confirmation button remains available.
4. When finished, let progress finish saving before closing the launcher.

<details>
<summary><strong>Hotkeys and graphics setup</strong></summary>

| Key | Action |
| :--- | :--- |
| F1 | Release mouse capture; click the game to recapture |
| F8 | First press enables NR in-game; later presses toggle the NR effect |
| Delete | Open NR controls; select Apply after changing parameters |
| Insert / L3+R3 | Open bbport settings |

The compact HUD shows FPS and NR status. Open **Delete → Detailed NR diagnostics**
for the full counters. Toggling the effect keeps the NR service running.

The current NR profile uses DLSS Quality, 1920×1080, 16:9 or Auto, jitter and object
motion enabled. Choose Old or Auto memory; under NR, Auto selects the matching old
model. Turn NR off to use ordinary upscaler settings.

</details>

<details>
<summary><strong>Known issues and compatibility</strong></summary>

- NR shading flicker, valid-history drops and bright trails can still occur.
- Community messages, bloodstains, phantoms and BBSC multiplayer are undergoing
  validation; controller, real-DLC and wider-area coverage is also expanding.
- Some current AppImage dependencies require GLIBC 2.44; older distributions need
  further compatibility work.
- FSR 4.1.1 DLL conversion is currently unavailable; prepared resources remain selectable.

See [Preview status](docs/NRBRIDGE_PREVIEW_STATUS.md) for details.

</details>

<a id="how-it-works"></a>

## How NRBridge fits into the game

bbport runs Bloodborne’s x86-64 game code directly on the Linux CPU and translates
graphics to Vulkan. NRBridge adds a division between rendering and model inference:

```text
Game image & motion → NR Helper → Persistent visual history → Native DLSS SR → Display
```

Native handles game rendering and history updates; the Helper runs the NR model.
A versioned protocol connects them. Native and
[OptiScaler-Linux-NR](https://github.com/Asanilo/OptiScaler-Linux-NR) remain separate
repositories so each can follow its upstream independently. Game files and model
dependencies are supplied by the user.

<a id="docs"></a>

## Build and explore

The default branch is **`nrbridge`**:

```bash
git clone --recursive --branch nrbridge \
  https://github.com/Asanilo/bloodborne-pc-dlss5-nr.git bbport-nrbridge
cd bbport-nrbridge
```

| Looking for | Start here |
| :--- | :--- |
| Dependencies, Native and Helper builds | [Developer guide](tools/nr_dev/README.md) |
| AppImage packaging | [Package guide](packaging/nrbridge/README.md) |
| Bindings, Chinese UI and saves | [Player guide](docs/PC_FEATURES.zh-CN.md) (Chinese) |
| Progress and known issues | [Preview status](docs/NRBRIDGE_PREVIEW_STATUS.md) |
| Workspace and branch layout | [Repository maintenance](docs/WORKSPACE.md) |
| Changes in bbport 0.6 | [Upstream changelog](docs/CHANGES_0.6.md) |

Want to explore Yharnam on Linux? **⭐ Star the project** to follow its progress.
Hardware reports, bug reports, translations and code contributions are welcome.

## Credits

Thanks to [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)
for bbport and [shadPS4](https://github.com/shadps4-emu/shadPS4) for the renderer and
shader translator’s foundations. See [component credits](gpu/VENDOR.txt) and their
licenses. Please report NRBridge-specific issues to this fork.

Bloodborne belongs to its respective rights holders. This project is not affiliated
with Sony Interactive Entertainment, FromSoftware, NVIDIA or AMD. See [LICENSE](LICENSE).
