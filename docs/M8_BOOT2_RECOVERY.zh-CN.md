# M8 新 boot 基础恢复验收（2026-10-24）

本轮 Native 为 `2f566db8684931fd21d1d0814099cb34b001c650`，FG Helper 为
`42bc6f742909dcc64489cb65c50b6179db36bef0`。这是短恢复验收，不是完整 M8/M9 或画质验收。

## 准入与环境

实际 boot UUID 从 `969a5e8b-1879-4063-b5e3-bb1c74ef4ce5` 变为
`13089ecc-b77a-4df6-9de7-d9bb557a840a`。持有既有 GPU 独占锁检查：无私有活跃 owner，
新 boot guard 准入通过，当前 boot 内核日志没有匹配的 GPU 驱动错误。

按既有 `prefix_recovery.py` 执行恢复，原目录按 inode 移回 source，状态为
`RETURNED_AFTER_REBOOT`。旧 `m10-new1080q-cold-r1` 的 HOLD/result/audit 未改写；
result SHA256 仍为 `f3419149c6d561438b477ba89b414955da87528e5bbfcf18ae8c1c1a18e15423`。
此次移动不意味着旧异常会话获得了正常 GPU 退休证明。

复用已有 Native、FG Helper、NR Helper 和 Wine prefix。私有设置为
`out/m8/settings-new.json`、`out/m8/quality.ini`，存档在私有 `out/m8/user` 链内备份与同步。
没有修改正式安装或正式存档，没有复制 AppImage/SDK/大型构建树。

## 实测

会话：`m8-boot2-1080q-dlss2-r1`。实际世界画面已查看，render1280×720/output1920×1080，
DLSS Quality，NR→SR + DLSS FG x2，`submit-ready-v1`，显式 core+sync validation。

实际运行证据：

```text
MEMORY_MODEL_ACTIVE_V1 model=new in_place=1 layer=1 host_import=1 write_tracking=0 constants_in_place=0
```

F9 在 SDL/overlay 接受后，daily canonical root 检查通过；三帧及 candidate 均完成
`actual_GPU_complete=1`，writer_finished=1、write_failed=0、unknown=0、all_ready=1。
总结为 captured=3、completed_writer=1，最终 staging=0、writer_joined=1。
这是修正后合法 F9 请求的实机证明；非法 admission 分支本轮未重新注入。

F9 后的独立5秒窗口：NR selected103/103，真实 Present103、生成 Present102，配对102，
失败0、顺序错误0。设备显存5546/8188 MiB，其中 Native4777 MiB、两个 Helper约332/331 MiB。
这个窗口包含诊断后的运行状态，不作为性能或 Old/New 对照结论。

完整 stdout 捕获27539214字节、轮转3次；独立累计计数 VUID0、sync hazard0、failure_latched=false。
正常关闭返回0，`PASS_NORMAL_TEARDOWN`、native_lastuse_proof=true、residuals=[]；
审计为 `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`，包含961个 finalized jobs。
DLSS 每代终态、Native source/cache/SR 最后使用及 Helper GPU/FD/model 证据通过，prefix `RETURNED`。

## 已验证的私有启动配置

取得下一次 GPU 独占权并通过既有 guard 后，可复用以下入口：

```bash
cd /home/arinp/Code/bloodborne_pc/.worktree/nrbridge
source out/m4-m5-integration/env.sh
python3 out/m8/play.py new-dlss2 m8-next-1080q-dlss2 --gpu-validation
```

进入实际区域后按受管提示开启 NR。启动早期 NR 未 arm 的阶段不应记作 NR 联动通过。
当前 GPU 已交给 M9，此命令仅供后续恢复使用，本轮没有再次执行。

## GPU 交接与后续边界

关闭后再次持锁检查：锁可独占、holders=[]、该会话 receipt 为 retired、guard.admit 通过，
当前 boot 内核仍无匹配的 GPU 驱动错误。已通过 Codex 原生线程通信正式交给 M9，
先验收 Old Memory SR-only1080Q2X，再推进 Old 多尺寸/MFG。

M9 实测期间根集成线保持 Native 二进制及源码 HEAD 不变，只准备 CPU 接口与后续 New 测试。
New1440 scaled source 修复、MFG3X–6X、长时间区域切换及修复后同构建 Old/New 对照仍待后续实机。
M10 算法候选未因此获得画质通过。

交接后 M9 的 `m9-old1080q-2x-r3` 已报告 SR-only 实际 generated Present，但菜单/Helper 创建前
出现 WSI `SYNC-HAZARD-WRITE-AFTER-PRESENT`。初次读取当前日志为10条，实际 selected=Immediate，
fullscreen=true；本轮健康恢复窗口 fullscreen=false。该差异是调查线索，尚未证明因果。
M8 已保存小型上下文到 `out/m8/recovery-boot2/m9-wsi-observation.json` 并接管公共 WSI 调查。
不能将该 M9 会话写成 sync 全清，不能仅凭此报告推导驱动故障；其最终计数/退休以 M9 原会话审计为准。

此前 HOLD 的直接原因是项目会话未取得正常最后使用证明；当时未记录到 NVIDIA Xid/hang。
本轮也没有观察到真实驱动故障。项目隔离策略、缺失完成证明和内核驱动故障必须分别记录；
不能从 HOLD 或残留进程消失推导“必须整机重启”或“正常退休”。

本机证据：`out/m8/recovery-boot2/{admission,handoff}.json`、`world-active.png`、`observe.txt`，
以及 `out/nr-dev/runs/m8-boot2-1080q-dlss2-r1/` 的 manifest/result/retirement-audit/game-log-state
和 `dlssg-ipc-terminal/g1/`。F9 证据留在该会话原位置，没有复制大目录。
