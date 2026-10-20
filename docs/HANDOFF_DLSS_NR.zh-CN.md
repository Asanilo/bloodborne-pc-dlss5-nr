# NRBridge 开发交接

现行功能和限制见 [Preview 状态](NRBRIDGE_PREVIEW_STATUS.md)，玩家入口见
[中文 README](../README.zh-CN.md)。本页不包含私人路径、存档或机器取证。

- Native 和 Helper 分别维护于 bloodborne_pc 与 OptiScaler-Linux-NR，不合并为一个仓库。
- Native 开发分支为 `nrbridge`；master/testing 保留。
  上游0.6提交8185b88和gameplay7265d3b均在当前历史中，不需要重复合并。
- 源码 HEAD、干净构建产物、实际 GPU 验证产物分别记录。当前参考 Native c91bf94，
  Helper源码713f8ac9／PE1eddac56，forwarder9a825。后续文档和路径清理不算新实机基线。
- Helper 是模型执行与协议真源；Native 使用带出处和 SHA 的协议导出。
  不手改导出头，不因路径整理更新协议或模型。
- 日常启动统一走完整启动器和受管 Runner，私人存档正常退休后同步。
  实验冷追踪不再强制用于普通游玩。
- 未知 GPU 末用、FD 所有权或当前boot HOLD 必须阻止危险复用。历史失败证据保留，
  不能通过改日志、假 CONSUMED 或清守卫绕过。

构建参数与依赖选择见 [Native构建](../tools/nr_dev/README.md)；仓库布局、worktree和
保留数据见 [工作空间说明](WORKSPACE.md)。原始交接记录、故障日志和具体本机部署身份
已留在 Git 忽略的私人开发数据中，不是公开机器的可直接执行步骤。
