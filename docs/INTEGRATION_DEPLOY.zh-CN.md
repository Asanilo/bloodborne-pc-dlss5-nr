# Preview 安装与回退原则

安装目录包含玩家自己的游戏、配置和存档；开发仓库与它分开。
当前功能见 [中文 README](../README.zh-CN.md)，打包说明见
[AppImage构建](../packaging/nrbridge/README.md)。

```text
<install>/
  launch.sh
  bbport.ini
  config/bbport-launcher/settings.json
  data/                         # 玩家数据和NRBridge运行目录
  packages/                     # 当前AppImage及上次包
  runtime/current -> releases/<release>
  runtime/previous -> releases/<previous-release>
  runtime/releases/
```

新包先在独立目录核验身份、依赖、私人存档和正常退出，再切换入口。
`runtime/current` 切换不应覆盖旧 release；旧 AppImage 保留作回退。
自定义 runtime 可能含包里没有的本地修改，不能因为旧 AppImage 还在就假定它能精确重建。

安装版 launch.sh 和 `packages/` 布局的直接 AppImage 启动会读取原配置。
显式指定的环境路径优先；不会自动导入私人诊断 seed 或登录用户账号。
游戏、存档、模型、SDK与凭据不进入包。GE／模型／NGX按本机选择引用，不能在清理开发
目录时误删仍在使用的外部依赖。

回退前先正常关闭所有受管游戏，确认资源退休且当前上下文无HOLD，再恢复保存的
旧包与current引用。具体路径和SHA应记录在私人部署清单，公开文档不固定个人目录。
不要 reset/force push 共享历史，不用全局 clean 清理安装数据。
