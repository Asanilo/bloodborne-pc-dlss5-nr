# NRBridge 稳定版本研发规划

2026-10-15，按用户本轮正式规划执行。本文件记录完整周期、当前目标与阶段验收；运行安全与当前事实仍以开发根 `CURRENT.md` 为唯一现行工程规范。它不授予旧目录删除权，也不替代真实 GPU 退休证据。

## 2026-10-20 最新用户决定：Native不合主线，Helper合入linux-nr，源码迁至各自worktree

本节覆盖下面早先“原主目录检出开发分支”的备选方案。Native master保持3630bc52，不合入开发分支；日常Native仍检出dev/persistent-visual-history-20261015，工作树已由普通Git move迁到 `/home/arinp/Code/bloodborne_pc/.worktree/nrbridge`。Helper已由linux-nr eaada4c2正常快进接纳f2c8607c，合入前本地检查点tag为 `nrbridge/linux-nr-before-helper-20261020`；日常开发树保持dev/nrbridge-helper-convergence-20261019，已迁到 `/home/arinp/Code/dlss5_linux/OptiScaler-Linux-NR/.worktree/nrbridge`，后续同范围准备修订正常合回linux-nr。没有改origin/upstream/sky关系、origin默认分支或重写历史。

nrbridge-dev现在只保留锁定依赖、私人配置、安全记录、构建与测试资料；native/helper旧源码路径已不存在，不创建兼容symlink继续维持旧源码中心。历史运行日志及来源记录不改路径，当前运行组合仍5635804/7f69/9a825，不自动选迁移后新编译产物；所有GPU/FD/末用/HOLD约束保持。

迁移前保存两树2288项文件/链接指纹，Git move后逐项相同，普通Git状态干净；原注册worktree身份和开发分支保留。旧位置构建缓存不能当新位置冷构建，另写新输出验证。Helper默认SDK/JSON选择已改为此前校验复制的inputs/workspace-return-20261020，Native Helper委托默认指向新Helper工作树。核心模型/Encode/Evaluate/Resolve、Native渲染及协议未修改。

需要回退源码位置时，先确保两树无独有未提交内容，用普通git worktree move把目标移回原路径，Git负责更新指针，不手改gitdir。linux-nr的旧源码可从检查点另开回退分支；不reset/强推已发布主线。迁移没有切运行构建，运行配置无需回退。当前授权不包含删除gpu-share旧实验、历史故障证据或更换正式游戏/存档；这些保持原样。

## 2026-10-20 归位现有 Fork：执行顺序与确认边界

用户的新目标覆盖旧“两棵长期nrbridge-dev源码树”布局：Native日常源码回到 `/home/arinp/Code/bloodborne_pc`；完整OptiScaler及 `nrbridge/` 回到 `/home/arinp/Code/dlss5_linux/OptiScaler-Linux-NR`；`nrbridge-dev`只作为锁定inputs/localinputs、build、runs和测试资料根。沿用现有Fork与全部remote，不新建仓库/clone，不重写历史/强推。本轮不升级上游、不制作AppImage、不修亮尾；后续两个项目分别跟进上游的规划保留。

### 核查起点与本轮非破坏性准备

- Native主目录master3630bc52干净；开发分支核查起点f09d5941，master为祖先，0/177提交差，309个路径变化。旧实验代码仍以默认OFF隔离，历史提交数或可快进不能代替主线验收。
- OptiScaler主目录linux-nr eaada4c2干净；Helper分支f2c8607c以它为祖先，0/24提交差，变化只在nrbridge/29路径和poc/187路径。origin默认仍是dlss-neural-rendering；不擅改默认分支。ci/linux-nr-build另有fc876dde尚未纳入Helper分支，不能随意丢弃。
- 当前运行仍只选Native5635804 / Helper7f69 / forwarder9a825；新编译只写全新输出，不替换运行配置、pins或旧HOLD。回滚依据是这些保留产物和来源记录，不承诺已退休c8二进制仍可用。
- 本轮只把旧NGX SDK与JSON头校验复制到 `inputs/workspace-return-20261020/{ngx-sdk,nlohmann}`；原件不移除。工具链和sysroot仍分别复用dlss5_linux/build/toolchain与bloodborne_pc/out/deps，都是数据输入，不是新的源码中心。
- Native Runner从脚本位置确定源码根，私人ROOT/runs及安全检查保持。NR构建通过显式prepared-root向CMake/编译器传入FSR/imgui/LibAtrac9，不再创建/替换源码树gitlink symlink。依赖全清单/补丁核验保持，普通无prepared-root构建仍走原submodule流程。主目录现有干净submodule不需deinit、删除或被改补丁。
- 当前主目录FSR与锁定准备树有真实补丁差异，不能直接换用其未打补丁内容；新构建必须继续显式选择校验过的prepared输入。本轮冷构建来自现有开发工作树，仅证明准备方案和编译闭包；尚不等于主目录归位验证或实机回归。

### 本轮准备验证结果

Native准备提交eefc19b完成一次空输出完整构建；移除3条旧构建symlink后复验发现ATRAC9头仍绑定源码相对目录，92e89fe仅修头搜索路径，随后运行C重新编译/链接通过，未无意义重编GPU目标。最后195项CPU通过。旧symlink已精确核对目标并换回空gitlink目录，目标输入未删，逆操作保存在prepared-symlink-rollback.json；原Fork主目录及两棵过渡树的普通git status均干净，不再依赖ignore-submodules隐藏旧链接问题。

Helper通过显式新SDK/JSON根独立构建34f9c3919a939c79，实际编译依赖内旧nr-runtime SDK路径为0；PE及build manifest留在数据根，没有切换现用7f69。当前运行保护清单4235项与退休时相同，另2项是此前已推送f2c8607c的说明文档，逐字核对该提交；没有改写旧保护记录来制造MATCH。主分支引用/remotes不变，无游戏/Wine/GPU运行。

精简实际结果见 `build/workspace-return-20261020/preparation-result.json`；复制清单、首轮测试夹具失败和去链接后的ATRAC9编译失败日志均保留。此处是准备验证，不是原主目录已归位或主线可直接合并的宣告。原目录冷构建、完整OptiScaler对应回归和必要运行验证仍是后继门禁。源码位置/脚本身份变化可能使旧scope失效，按既有流程建立有效上下文，不改旧HOLD。

### 安全归位顺序（本轮不切换主目录、不合并）

1. 完成上述依赖路径准备、CPU回归和新输出冷构建，保存actual input/command/artifact记录。保留主目录、开发树、所有子模块未提交状态；源HEAD与master/linux-nr检查点写入本地inspection.json。不得批量stage本地gitlink。
2. 源码位置与主分支合并分开处理。确认迁移窗口、所有相关树均无独有未提交代码后，可先让旧nrbridge-dev工作树detach在已记录开发HEAD，再在原Fork主目录switch同一开发分支。Git不允许同分支同时检出两树；不使用ignore-other-worktrees、force或手工改gitdir。旧树暂留作detached回退，不急于移除。
3. 在两个原Fork主目录按下列命令重做独立冷构建/CPU与协议导出校验，确认实际编译源码路径位于原Fork；运行资料仍写nrbridge-dev。不能复制旧CMakeCache冒充冷构建。届时通过显式 `NR_HELPER_ROOT` / `--helper-root` 选择归位Helper，检查并收口仍有的legacy默认路径。
4. 验证后才提交主分支合并操作供确认。Native master当前满足祖先条件，候选是 `merge --ff-only dev/persistent-visual-history-20261015`，但需先审默认Legacy OFF、源补丁/协议闭包、加载补丁及冷构建/回归，不能据可快进直接执行。保留旧master3630bc52指针检查点；不推master直到批准。
5. OptiScaler推荐继续以现有linux-nr为Linux长期主线，nrbridge/为独立Helper子目录，保留dlss-neural-rendering及upstream/sky关系。先审187个历史poc路径是否有构建/回归依赖及ci分支，再决定Helper整支快进或单独整合；本轮不合入linux-nr或改origin默认分支。Helper冷构建不代表完整OptiScaler DLL/DS2功能回归，合入前需补对应现有回归。
6. 原Fork归位验证后，才逐项退役旧detached源码worktree及无人引用的临时构建；正常git worktree remove，不force。删除独有资料、移动已pin运行输入或切换实际运行构建，另列精确操作/回滚并等确认。清理以df净变化验收，不把同卷移动当释放。

归位后构建命令模板（只有第二步执行后才从原目录使用）：

```bash
D=/home/arinp/Code/nrbridge-dev
NR_SOURCE_DEPS_ROOT="$D/inputs/source-deps" \
NR_BUILD_ENV_ROOT=/home/arinp/Code/bloodborne_pc/out/deps \
BB_BUILD_DIR="$D/build/workspace-return-20261020/native-home-cold" \
  bash /home/arinp/Code/bloodborne_pc/tools/nr_dev/build.sh
python3 /home/arinp/Code/dlss5_linux/OptiScaler-Linux-NR/nrbridge/build.py \
  --ngx-sdk-root "$D/inputs/workspace-return-20261020/ngx-sdk" \
  --json-root "$D/inputs/workspace-return-20261020/nlohmann"
```

失败回退：不删除失败输出。在未改主分支引用的源码归位阶段，先把原Fork主目录detach在已记录开发HEAD，再使旧工作树switch回原开发分支，最后原Fork主目录switch回master3630bc52或linux-nr eaada4c2；所有步骤先确认干净，不reset/clean。新构建从未成为运行选择，运行产物/配置无需回退。若以后批准主分支合并，先保留旧分支指针；不擅自reset已推主线，公开回退另行确认。

### gpu-share-poc-20261010 是否可删

这棵工作树与master同为3630bc52，没有独有提交，但有4份独有未跟踪文件（README/REPORT/run.py/share_fixture.c，共72566B），已校验复制到 `inputs/workspace-return-20261020/gpu-share-source-preserved`，原件保留。du约1.3GiB；Btrfs实测Total/Exclusive均约1.24GiB，主要是两套旧Wine prefix，不是当前Native编译。当前显式运行/编译闭包不选此路径，但历史文档引用不等于可删除全部故障资料。

结论：属于可退役候选，**本轮不能直接git worktree remove，更不能force**。待独有源码有受控Git归档或确认采用校验副本、旧out内用户/故障附件完成审批后，才正常移除；分支poc/gpu-share-20261010保留。不能把保存4份源码等同于整个out已获得删除授权。实际占用及备存SHA见 `build/workspace-return-20261020/`。

## 2026-10-20 清理后现行入口

用户确认：当前工作是为下一开发阶段整理工作空间、源码边界、依赖和构建文档；尚未指定立即转入AppImage或其他功能。后续继续跟进bbport和OptiScaler上游的规划保留。历史“本轮不升级上游”仅限定对应执行阶段，不是永久停止维护。届时基于当时真实上游差异选择移植内容，独立验证，不覆盖现有可玩基线，也不将OptiScaler整个项目缩减为NRBridge Helper。

本文件保留工程与实机历史，下面2026-10-19及更早段落中的“所有固定产物保留”“build/native仍为3608”“Legacy Helper可回滚”等，仅代表当时状态，**已被后续清理取代**。当前只部署Native5635804、Helper7f69、forwarder9a825；旧c8/Alpha/3608/Legacy二进制已退役，Git源码引用保留。旧boot scope已失效。最新构建、输入依赖、新接手者限制与启动命令以 [运行说明](../tools/nr_dev/README.md) 和本机开发根 `CURRENT.md` 为准。

Native/Helper代码及构建归属分离，不等于工作区已可直接搬迁：当前Git worktree仍依赖原主仓库元数据，构建默认引用既有sysroot/SDK，运行配置仍指向design内只读资产/seed。不得整目录删除这些依赖。下面历史角色分工不另行覆盖当前会话的协调授权。

## 2026-10-19 工程收敛：三仓库边界与 Sol 独立任务

本节是本轮唯一执行计划，优先于下文画质研发安排；长期五阶段规划保留。本轮不跟进上游0.6、不修亮尾、不制作AppImage、不再去重。目标是先收敛源码归属、依赖和可重建基线，再清理失去依赖的工作空间。Astra负责边界、跨仓接口、合入和GPU验收，Sol负责具体工程执行；普通源码整理、CPU测试与隔离构建连续推进，不逐项请批。

### 当前交付与下一执行点

S0–S5已整合完成；用户批准的精确旧空间清理和本轮自建输出收尾均已执行。新组合通过真实Clinic加载、NR与正常退休，源码/依赖归属已经闭合到两个开发树。当前不是亮尾优化或日常发布验收；下一轮可用此基线准备AppImage Preview，不能把构建成功等同于可分发安装包。

| 仓库 / 长期目录 | 现行责任 | 边界 |
|---|---|---|
| bbport / `nrbridge-dev/native` | 游戏加载/PM4、Native Vulkan/FD/末用、持久历史/Shader、SR/Present、typed Runtime Control和ImGui；构建/Runner/私人会话 | 不再拥有第二份Helper实现，不从design加载普通运行代码或配额 |
| OptiScaler Helper / `nrbridge-dev/helper` | `nrbridge/`独立Helper、NGX/forwarder、Encode/Resolve、模型参数重建/ACK/退出；版本化协议及生成配方 | 不接管Native视觉历史、游戏UI或另造Runner；不整体升级OptiScaler |
| `nrbridge-design` | 跨仓架构、交接索引、历史PASS/FAIL/HOLD及观测证据 | 旧游戏资产/seed可显式只读引用，原目录不作为当前源码真源 |

协议真源为Helper的 `nrbridge/protocol/`，Native消费版本/SHA导出副本并在构建前核验，双端原wire/sidecar/tuple/ABI语义保持。Native重复runtime C++和旧协议测试已移走，实际180行peer退休验收原字节保留；旧import/CLI仅作兼容薄层。

### 实际源码、构建、运行与回滚

- Native主线 `dev/persistent-visual-history-20261015`。实际新GPU构建来源 `563580452e46ea253cecbf75c24ac65579ee031b`，完整四产物只读固定 `build/baselines/<SHA>`。后续Helper委托/公开play/文档提交不冒充重新编译的Native。
- Helper分支 `dev/nrbridge-helper-convergence-20261019`，源码 `cea5160aab380272dfe52ab2db31691e77531c18`；PE `build/helper-runtime-model/7f69c1e556810b2b/wine_runtime_model.exe`，SHA `e2bab49df70e333a71c986fbacbfbcad0e9988d3c28a274866d9c409d0c7074f`。build manifest记录1366项实际transitive编译输入。
- 新forwarder `build/convergence/forwarder/9a8251430ccfa68a/nvngx.dll_dlssnr.dll`，SHA `1196236b8edf7a1e4920f7681a4085fbd86551b6c2bbf35211cce2eccba46f0f`；26项导出与原件匹配，已随新Helper真实运行，不只BUILD_EXPORTS_PASS。
- 私有配置 `localinputs/runtime-s3-20261019.json`，SHA `68e398c0880f13e4545bc9fe2e24fd45d4fe1ddefa5ce088b114b993ba6bccb7`；独立10717项运行pins锁定GE/sysroot/NGX及Helper/model/seed等。正常显式模式不再读旧f527源码、历史SDK头或旧预检PE作为运行准入。实际其他系统依赖仍须匹配，非自动可移植安装包。
- Native构建的source-deps与SDK/sysroot环境分根，完整清单漂移在输出副作用前拒绝；使用已准备版本，不隐式升级/复制/补丁。默认 `NRBRIDGE_LEGACY_EXPERIMENTS=OFF` 排除19项旧cpp和10项旧host shader；仍需状态/退出/计时已独立。ON只完成受影响对象/Shader编译，不宣称完整ON运行。
- 固定玩家画质参考 `c8fb9dfdb7d80f444ddfdcf72c6483dba140963f`、e08eacf/9fcd4cc与旧3608四产物均保留；`build/native`仍是3608。Legacy Helper06ac、旧forwarder/61pins保留。完整旧回滚单元见 `build/convergence/rollback-inputs.json`；不能仅换Native却混配新资源，也不能用回滚绕过当前GPU检查。
- Helper锁定Git SPV及其正确历史HLSL/DXC配方，SPV→C头逐字重生成/spirv-val通过。本机未执行HLSL→SPV重新编译（缺Linux DXC），这是明确保留的工具链限制。Native冷构建指空输出＋已有锁定依赖，非空机器自动获取模型/SDK。

正常入口为 `tools/nr_dev/README.md`、`build.sh`、`prepare_helper.py`、`prepare_runtime_inputs.py`、`play.py`/`runner.py`。Helper兼容包装器默认只委托独立仓库；显式 `--legacy-rebuild` 只能写全新legacy输出，不覆盖旧PE/frozen manifest。公开play可传既有scope和精确预算/trace，启动前只读验证、Runner持锁复检；未新增审批状态或放松末用。

### 实机整合验收

| run | 实际覆盖 | 结果 |
|---|---|---|
| `dev-converged-sr-03` | 新Native实际Clinic加载、SR、no-peer退出 | 正常末用/退出 |
| `dev-converged-off-02` | 新Helper/forwarder真实准备、24 skip | GPU/FD/Helper正常退休 |
| `dev-converged-skip-01` | persistent四bank、24 skip | GPU/FD/Helper正常退休 |
| `dev-converged-nr-01` | 24真实Evaluate，23 captured、1 obsolete | GPU/FD/Helper正常退休 |

四轮均returncode0、native_lastuse_proof=true、无watchdog/信号/残留，退出后当前boot内核无新增GPU错误，实际日志确认同步GXRender交接。`sr-01`只在菜单、`sr-02`在创建run前因时长拒绝、`off-01`零armed job，不能算加载/NR覆盖。旧HOLD和失败证据保持，有限加载回归不意味着间歇性PM4/Xid31/float冻结已全部解决。PD＋冷trace/读回不作为热性能验收，亮尾/闪烁没有新增改善结论。

下一公开受控session `play-converged-20261019` 已准备但未启动，240/360秒、source7200，绑定真实resident前序 `dev-converged-nr-01`。README有完整命令；新boot、任何证据/身份漂移或新HOLD都拒绝，不能复用消费过的session。旧普通入口默认预算未变，当前scope不允许旧存档续接；日常长会话和安装体验留给后续阶段。

### 已执行清理与保护例外

用户批准 `cleanup-plan.json` SHA `9e912aba05bda56c9827a3035536e8c722e4082dc39c6cbc4f637f6c77658073` 和 `cleanup-review.json` SHA `8c867db868d0d913c6a103c96797ed101a6b1b4cbf4cbd58c1932465d9f71ea9` 绑定范围：132目录（72 CMake对象、32正常退休案例缓存、28Python缓存）全部完成，0失败/跳过；environment@f2b1cb3、gpu-banks@e053c6f正常退役，分支和Git对象保留。567受保护文件SHA/身份未变。执行记录 `build/convergence/cleanup-execution-20261019T115250Z.json`。

该窗口df Used净减少4813090816B（约4.48GiB），Available净增加4745785344B（约4.42GiB），含同期构建写入，不将原7.375GiB独占逻辑extent宣称为物理释放。已去重165个prefix未重做。另退役本轮convergence/convergence-legacy临时树并删除本轮四隔离构建目录内可再生对象和重复产物；保留各自来源/日志/manifest、活动forwarder及所有固定baseline。本轮输出精确781文件done、0失败，405保留文件及新baseline校验不变；该窗口整卷Used净减1113931776B（约1.04GiB，非独占归因）。记录 `cleanup-current-build-execution-20261019T124654Z.json`，SHA `5bc919ed5330270c797ef6a48f174bc4d85ffe799434615d66605f17d49b5195`。两棵本轮代理树均无force正常退役，全部分支/refs保留。

保护例外明确保留：

- `worktrees/shader@bb3152d` 的独有未跟踪 `tools/persistent_history/test_candidate_fusion_design.py`。
- design原dirty `execution-ledger.json`（SHA `427f3760c59dc6a26661b53e953e08d1bdfa327ad83677f0bdb23389c551587e`），未stage/改写。
- 旧OptiScaler runtime的11个未跟踪文件；现用shim已有受控新Git副本，原件仍保护。
- 旧 `build/helper/wine_async_presr_play.exe` 与frozen manifest的未解释SHA差异，不重写记录或删除原件。
- 正式游戏/真实存档、稳定源码、模型/SDK/GE、所有固定基线、旧原始捕获/FAIL/HOLD/停止证据与未另行授权的历史worktree。

长期布局是两棵主开发树，统一inputs/localinputs/build/runs；新代理树按需创建并正常退休。工作区“收敛干净”指当前代码边界、默认构建和已批准可再生空间，不意味着删除受保护历史或全盘所有旧目录。

### 给下一位 Sol 的独立任务边界

先读根 `CURRENT.md`、本节和 `tools/nr_dev/README.md`；按实际主线HEAD续接，禁止回到旧bb8重做传输或重复S0–S5。Native与Helper两个长期树保留，前述临时执行树已整合退休；新任务确有并行需求才从主线建立一棵短期worktree。

Astra拥有架构、跨仓接口、CURRENT/规划、整合和GPU排他调度。Sol只在明确分配文件/自己的临时树实施；Helper独立任务限 `helper/nrbridge/`，Native构建/Runner任务限 `tools/nr_dev`，渲染/Shader另分所有权，不并写。普通CPU、隔离构建及自身可再生输出收尾连续推进，不逐小项审批；现行既有GPU检查和记录不放松。

验收：本地源码状态清楚、依赖漂移/路径异常在副作用前拒绝、对应CPU正反回归通过、干净源码与工具/产物身份可追溯；渲染/生命周期更改必须由Astra实际GPU回归。保留旧基线并显式选择新产物，不覆盖现用或frozen记录。禁止修改正式游戏/真实存档/系统配置、强推/删历史分支、git clean/reset --hard、批量stage gitlink symlink或私有资产；新增旧目录删除仍需精确授权。

以下为历史阶段快照，保留当时事实；其中“当前/下一步”不覆盖本节工程收敛结果。

## 2026-10-19实机进展：加载恢复与NR回归

**2026-10-19重启后实机验证。** 新boot `199949cd-b649-4b57-b99f-a502f91a2118`，用户报告PD供电，sysfs确认USB-C PD（20V/4.5A）；功耗遥测590.01W异常，不用于功耗判断。实际build `3608e21` 在五轮均确认同步交接补丁日志并成功加载Clinic：`dev-gx-owner-sr-01`正常no-peer退出；`dev-gx-owner-off-01`完成24 skip；`dev-gx-owner-skip-01`完成25 skip；`dev-gx-owner-nr-01`完成24 Evaluate（23 captured/1 obsolete）；`dev-gx-owner-capture-01`计划240秒，完成1172 Evaluate（1168 captured/4 obsolete），实际转镜画面已观察。五轮均PASS_NORMAL_TEARDOWN、真实末用证明通过、无残留，退出后完整当前boot内核检查无新增GPU错误。

候选已通过本轮有限加载/NR/正常退出回归，不再是BUILD_ONLY；不能把五次成功等同于所有间歇性故障消除，旧0x284、Xid31与float冻结仍各自OPEN。连续NR描述性窗口133.218秒、7568实际Present，56.80FPS/P95 24.61ms/P99 25.91ms/最长33.73ms，1次source gap计入；整卡峰值3895MiB（含其他应用）。PD供电、冷trace及Present读回影响整轮，不作热性能回归或与旧AC数据直接比较。未观察到F9输入，未获得本轮同源raw/模型/历史/SR捕获；亮尾归因及画质改善仍OPEN。固定c8fb9df四产物SHA与只读权限保持。下一步恢复有界动态画质采样和同供电条件热对照；仍使用现有真实退出前序与实时设备检查，不改旧HOLD、不开新审计体系。详细证据见`analysis/pm4-overlap-20261016/gx-render-owner-validation-20261019.json`。

## 2026-10-19执行收敛：所有权候选与已批准全量去重

主任务保持PM4根因修复。静态链已从低层setter追到实际共享对象：主线程global资源copy取得的`renderer->field10->field08`由GXRenderThread初始化为`backend+0x130`，与绘制任务共享。`1d96b19`明确设置config[0x72]=1，经renderer配置复制给GXRenderThread；`21b6aa0`在此模式kick新job后直接返回，event68直到job回调返回、清空job指针后才置位。原有主线程可继续使用共享parent的交叠窗口与实机CS/graphics同CB证据相符。实际fieldb0对象的回调已锁定为`219d220`，逐项同步调用`21b2fb0`；该路径完成CPU命令构造/提交后才返回event68，没有发现提前派发子worker的分支。此结论不代表全部GXWorker空闲或GPU完成；实际失败更深caller与候选实机行为仍待验证。

当前代码候选在载入私有镜像时核验完整dispatch与配置初始化指纹，仅将factory config[0x72]初值改为0，复用游戏原有同步模式；不改setter、命令包、GXWorker创建或GPU完成判断。模式在上下游保持一致，正式游戏文件不改，固定c8fb9df不改。CPU执行私有实际配置store与dispatch机器码：原async1允许caller在job完成前返回；原sync0及候选均阻止此交叠，覆盖状态更新/等待分支和版本不匹配不写入。事件替身不证明所有子worker或真实GPU完成；候选代价可能是CPU前后帧重叠减少，必须实测加载成功率、正常退出及热性能。

候选`3608e21fa1bb7510103e098acbfaa22b72d985d6`已完整构建，干净来源及四产物SHA核验通过，尚无本候选GPU结果。当前boot的PM4 HOLD保留；用户已同意去重完成后重启并恢复受控验证。重启后实时复核设备、内核、持有者与依赖，先验证SR-only加载/正常退出，再递进NR生命周期与画质。在Continue前必须确认`GX_RENDER_OWNER completion_dispatch=synchronous`实际应用日志；不匹配则正常停止。CPU源码开发和去重不等待GPU，不重复逐步骤请批。

全量去重已按用户批准计划`e557c6f657f0ba55a3fda7c527f8bef96ea007a639b5a3c0cbaa62eb988e65ed`完成：165个prefix、299635对windows普通文件，done299635、failed0、remaining0，最终301337个参与文件metadata核验通过，每对执行前后完整SHA核验。仅FIDEDUPERANGE，独立inode、内容、元数据和HOLD状态保留，不删除/覆盖文件、不碰注册表/存档/日志/固定构建/稳定源码树。执行窗口/home的df Used净下降38.011GiB，可用空间86.699→124.615GiB；包含同期写入影响，不将98.931GiB逻辑重复量或逐树shared之和冒充物理释放。执行账本和完整核验仅保存在私有cleanup-review的dedup-preparation目录，未作为公开资产推送。批处理已结束，下一项用户操作为重启以建立新的GPU验证上下文。

以下2026-10-18进度及阻塞分析保留当时事实；审批、当前候选和恢复安排以上述2026-10-19状态为准，不把候选CPU通过写成游戏加载恢复或发布完成。

## 阶段快照与固定基线（2026-10-18）

长期顺序保留：**稳定画质与性能 → 工程化 → 日常可用发布 → 空间整理 → 高级功能及上游扩展**。当前主分支为 `dev/persistent-visual-history-20261015`；实际诊断构建为 `cfeaf0994bc6a4d3e3943c205f730d95dbe902f7`，干净来源和四产物SHA已校验。本文后续文档提交不改变该实际构建身份，也不表示新增实机验收。

玩家认可的固定画质参考是 `c8fb9dfdb7d80f444ddfdcf72c6483dba140963f`，标签 `nrbridge-stability-candidate-20261016`，四项只读实际构建位于开发根 `build/baselines/<完整SHA>`。本轮再次核验字节与只读权限。较早的 `e08eacf` Alpha与`9fcd4cc`固定Nearest也保留。诊断构建不替代这些参考。

真实NR720→SR1080、单共享job跨进程传输、四bank/独立持久历史、跨age4合法显示、运行时菜单与模型重建已完成过实机验证，用户确认“明显改善，操作流畅”“菜单正常，NR正常”。Delete打开菜单，F8快速旁路；它们共享SR时域历史，不是严格同帧独立A/B。早期Clinic动态参考60.30FPS；c8fb9df同轮动态结果57.86FPS、P95 24.49ms、P99 26.19ms、最长28.62ms，整卡峰值4104MiB（含其他应用）。不同运行不得混作性能趋势。

| 规划阶段 | 当前交付事实 | 尚未达到的验收 |
|---|---|---|
| 一：画质与性能稳定 | 可玩Alpha、可选重投影、区域过渡/边缘验证、真实NR与正常退休参考 | 加载不可靠；亮尾/闪烁未解决；本轮无新的玩家画质或热性能提升结论 |
| 二：工程化 | 独立开发根、构建/Runner、typed control与部分生命周期边界已有 | 旧实验路径收敛、干净环境发布构建、统一故障恢复未完成 |
| 三：日常使用与发布 | 私人启动、菜单/配置、正常结束后的私人续玩已有 | 长于现有Helper容量的安全会话、更多场景/战斗/死亡传送、正式发布验收未完成 |
| 四：空间整理 | 新增Sol中等强度负责精确去重准备 | 未授权实际旧文件去重/覆盖/删除；不阻塞加载修复 |
| 五：高级功能/上游 | 保留长期方向 | 尚未进入实施，不追赶上游或扩大菜单 |

## 当前两个阻塞与下一决定性工作

**P0：加载期命令缓冲所有权。** 新失败 `dev-restart-sr-01` 使用实际build `9add7f3`，在Continue加载时exit23，Helper/NR未启动。2184B提交副本与解码副本逐字及SHA一致，首坏字在0x7c，为0x1e6；与较早0x284失败分别保存。实机trace确认主线程39713的CS由直接caller `0x8026cd5b1` 写入，同时另一线程39958的VS/PS关联同一CB `0x10282a2850`。实际wrapper先读cursor，调用HLE写入后才对当前cursor累加；调用内采到的cursor已偏离cmd。采样非原子，不能单凭时间交叠判定缺锁位置。

实际CS/PS wrapper机器码加当前HLE builder的CPU强制交错已复现相同CS首错0x7c/0x1e6；双方两种串行顺序正常。背景SET_CONTEXT序列为合成负载，这证明机制足以致错，未冒充实际背景生产者。当前最强假设是更上层错误共享CB或缺少任务交接；本次直接CS caller不支持此前模板patcher作为这次失败的首要解释。旧0x284、Xid31与guest float冻结仍不能合并为同一根因。

**决定性缺口：** `0x26cd400`有11个静态直接调用点，现有一层caller不能选出主线程实际走的那一条，也没证明CB归谁。`cfeaf09`只在cursor/command冲突时给现有冷ring补最多6层raw RBP和实际写入地址的写前字；不新增钩子/后台线程，不改变执行同步。实际generated stub、寄存器保持、非法链/读取失败CPU验证及完整构建通过，尚未GPU运行。下一次安全诊断要用这条实际调用链锁定CB选取/交接点，再实施相应修复；不能先给单个setter加锁，假定它覆盖整段guest命令构造。

**P0：异常后的可证明退出。** 当前PM4断言调用 `_Exit(23)`，即使没有NR，游戏此前也可能有SR/Present GPU工作，无法留下正常owner/末用证明。新run因此为原始HOLD；无残留进程和无新增内核错误不等于末用完成。Runner当前会拒绝同boot未知状态，这是频繁重启的直接原因，不是PM4根因修复。

用户质疑重复重启后，当前执行调整为：**先离线评估可预先检测的坏包拒绝与正常停止边界，暂不继续要求用户反复重启，也不在现有未知上下文中启动GPU。** 需明确停止新提交、既有工作真实完成、guest等待/owner退出怎样闭合，再决定能否减少此类故障后的整机重启。此方案尚未实现，不能通过文档修改解除现有Runner门限。真实驱动故障、NR未知末用和CPU坏包分别处置，旧HOLD/Xid不改；若恢复证据仍不足，再说明具体原因请求新上下文。

## 哪些改变了行为，哪些仍只是定位手段

- `a3d3665`修复独立可复现的跨队列帧退休计数缺口：逐队列FIFO目标取代全局decoded总数，再走原真实GPU完成路径。CPU旧实现负对照成立；后续有限实机正常退出不能证明它修复全部PM4/Xid。
- `9add7f3`阻止隔离NR会话原地重启，菜单改为保存供新会话生效。原因来自 `dev-context-capture-01` 的真实日志：close_range后exec旧session，触发stale run，旧资源未正常退休。实际源码CPU验证已证阻断发生在FD/flag副作用前；本版实机在加载先触发PM4，尚未完成NR会话菜单回归。
- `e073fcd/e4ea8bd/cfeaf09`为定点观测与CPU复现，**不是加载根因修复**。`993138c`只是按真实历史boot集合核验隔离验证链，不解除HOLD。F9新增输入接受/拒绝反馈不等于采集成功。
- 上一boot的4次SR-only，以及off/skip、persistent/skip、resident各24个job正常退休仍有效。后续连续 `dev-context-capture-01` HOLD和本次加载HOLD也分别有效，不相互覆盖。137终止来源用户记不清，保持未知。

## 亮尾、恢复试玩与去重的边界

亮尾场景固定为Clinic门口灯下，角色基本不动、快速转镜，受光轮廓留下亮尾。现有CPU/Shader参考支持累计亮度漂移和合法旧bank过渡可能保留亮贡献，但没有实际故障像素证明；模型原始输出、MV/遮挡/jitter、历史接纳及下游SR积累都未排除。不合入未经验证的亮度锚，不全帧降NR或缩TTL。上次F9未出现对应接受revision/诊断初始化，没有完成同候选raw/Resolve及当前SR/Present采集。

恢复玩家试玩的关键条件是：安全上下文可核验、针对CB所有权的修复有因果验证、加载/正常退出连续回归通过。随后立即回到固定c8fb9df、SR-only、新候选的受光轮廓动态对照，再测不含冷采集的FPS/P95/P99/显存并交付玩家；不以全历史审计或空间清理作前提。

去重由Sol中等强度独立准备：只读旧prefix/生成输入，必要写入限定派生清单与自建小测试文件；优先内容相同的原地extent去重，保留独立inode和写入隔离。100.13GiB目前是DS报告的重复内容逻辑量，不是已核验物理释放承诺。旧文件实际去重/覆盖/删除仍需精确清单批准，不混入A组删除，不改变真实存档、固定构建或失败证据；模板初始化属于后续独立等价验证。

## 阶段一：画质与性能稳定（长期规划）

当前目标：交付相较固定Alpha更稳定、能直接启动和回退的可玩候选。

1. 用当前raw720、实际NR720、Depth、MV、jitter、原始贡献年龄、有效率、SR1080和Present建立证据链，分别定位重投影跳变、遮挡显露/轮廓鬼影、新候选亮度跳变、低频光照残留、反复重投影细节损失及后续SR历史影响。
2. 按独立变量实施结构性改进。首项研究当前最近邻残差重投影造成的亚像素跳变：只在同表面、同贡献来源的有效采样之间重建残差；原遮挡、颜色、TTL等硬门保留。另行评估候选更新融合与保守贡献年龄，不把旧贡献重标为新origin。3×3全邻域深度拒绝可能造成边缘反复raw，须用实际数据区分真实遮挡和过度拒绝后独立处理。
3. 保留SR-only、固定Alpha、候选三路独立运行，使用相同私人存档、配置、场景、分辨率、预热和可重复输入轨迹。720p HDR比较采用相同曝光/色调映射；最终1080p单独比较。F8是主观快速比较，不冒充独立双SR历史的同帧A/B。不得用生成图补证据。
4. 性能先测再改：记录历史各GPU pass、输入转换/Return/SR依赖、资源分配与带宽、Helper推理及任务周转；真实Present FPS/P95/P99/最长停顿和显存。高成本冷捕获/录像不计入热性能；不盲目增加job、bank或同步等待。
5. 不以延长TTL、重播旧整帧、递归编辑、全帧降强度或改写旧证据掩盖伪影。Fingerprint、区域融合或低频稳定化仅在证据和成本支持时引入；不因OptiScaler存在选项就移植。

验收：真实玩家对照；已解决和仍存在伪影分别记录；同场热性能/显存；正常GPU/FD/Helper退休；直接启动与固定基线回退。CPU/Shader/build PASS仅是进入GPU验证的前提。未知GPU状态HOLD，仅暂停受影响运行，给出具体恢复条件。

## 阶段二：代码整理与工程化

在稳定候选上作小步整理，避免大规模无收益重写：

- 识别P0/Q、旧缓存路径、重复状态机和临时适配代码；先解除构建/脚本/测试依赖，再考虑隔离或移除。
- 明确Persistent History、Native GPU所有权、Helper协议、Runtime Control、Runner边界；收敛实际末用和退出逻辑，不删除安全检查。
- 规范Shader源码、编译产物、ABI和参数来源；统一正常使用、调试、画质采集入口。
- 保留有价值的CPU、Shader、GPU生命周期及真实游戏回归；建立提交、版本、变更记录和回滚基线。

验收：从干净环境可重建并正常运行/退出的发布候选；固定Alpha和必要证据仍可恢复。历史实验不必全部纳入正常构建。

## 阶段三：长期使用与正式发布

- 简单可靠启动；菜单所有已实现设置真实生效，配置持久化，模型Apply有真实成功/失败反馈，异常回退不破坏游戏/存档。保留对比和必要诊断。
- 为当前约600秒Helper有界服务设计安全长会话：明确容量、服务续接/重建、故障处理及正常退出；不能仅删限制。验证场景切换、读档、暂停、死亡、灯火传送和较长游戏。
- 默认继续独立私人玩家配置和存档，自动延续策略、备份、恢复可验证。正式安装和真实存档不写入；未来支持须独立确认、备份及兼容验证。
- 整理开发分支、发布候选和可回退版本；完善README、安装/构建、硬件/驱动/GE或Wine/模型/SDK依赖、已知限制、启动/卸载/排障/日志、版本说明。
- 分清源码推送、可复现构建、可分发安装包。公开发布不包含游戏资产、私人存档、凭据，以及未确认可再分发的专有模型或运行库。

验收：用户能正常启动、续玩、调整、退出，无需每次人工执行复杂实验流程；推送成功不等于发布完成。

## 阶段四：空间收敛

发布成果可靠备份并有源码/重建路径后，按当前依赖重新审查旧worktree、build、中间文件、Prefix、重复run、临时捕获缓存和废弃脚本/文档。长期保持少量主树、统一build/runs、临时代理树正常退休。

保留必要源码、独有观测、失败记录、性能基线和真实存档；核对精确路径、恢复方式、Git与ignored/untracked内容。超出现有明确删除授权的项目先提交具体清单审批。无需保证全部历史环境永远原地可启动，也不以清理阻塞成熟版本使用。本阶段未授权任何旧目录删除。

## 阶段五：高级NR与上游维护

发布稳定后按收益研究：真正顺序多次Model Evaluate、Style/Intensity/Local Structure/Local Tone、White Point及颜色/细节独立控制、Fingerprint/Context/AntiPop/自适应调度、模型分辨率与NR/SR管线、Advanced菜单。多次推理须真实模型调用和完整资源生命周期验证，不能用编辑倍增替代。

进入上游维护时重新获取当时最新bbport/OptiScaler提交、发布说明及源码差异，届时确定移植范围；不固定过时差异清单、不整体合并。升级与可玩基线隔离验证。

## 协作与状态管理

Astra负责架构、风险资源决策、接口整合、排他GPU运行及画质/性能/发布统一验收。按独立性委派Sol，明确文件所有权、输入、验收和整合条件；不并行修改同文件或重复扫描。发布后常规上游移植/兼容/回归优先Sol，系统性架构或GPU风险再交Astra。

当前执行阶段以本文首部为准；长期画质、发布与扩展目标保留。普通修复、构建、验证在授权范围内连续推进，不逐步申请方向；玩家主观反馈、真实未解决GPU风险、额外破坏性权限再提出。阶段汇报只说实际成果、证据、未完成项和下一目标，不以测试次数或新增开关数量作为成功标准。

## 历史阶段记录

以下保留当时的实施与验收事实；其中“当前”“下一步”不覆盖本文首部2026-10-19工程收敛状态。

阶段一实施进展：固定Alpha已独立冷采集并正常退休。首个双线性候选通过实际像素/年龄/退休检查，但重投影高频衰减风险明显，未作为画质升级验收；正在验证同表面同来源的限幅三次重建。候选融合暂保持设计评估，未将混合旧贡献改龄。所有GPU结论以对应run的实际build身份为准。

当前候选验收状态（首次稳定性迭代）：受限限幅Catmull-Rom已实装，20项CPU边界/锐度测试、SPIR-V与真实冷GPU读回通过。三组每组3帧中，像素finite、原alpha、真实贡献年龄及SR/Present关联通过；高频保留优于双线性但仍有损，不作为消鬼影证明。固定Alpha与候选各完成独立120秒热动态：57.63/54.91 FPS，P95 24.81/25.81ms、P99 26.38/27.19ms，最长124.42/29.63ms，整卡峰值4044/4152MiB，均零source缺口及正常退休。单轮最长停顿差不能宣称卡顿已修。候选约慢4.7%，**尚未成为正式稳定升级**；等待真实玩家比较平滑、残影与细节，接纳亮度跳变及遮挡边缘问题仍OPEN。完整原始与派生证据位于开发根comparisons/stability-20261015及其引用runs。实验build源码为ede2f24，固定Alpha为e08eacf；后续文档/工具提交不冒充已运行的二进制身份。

玩家对首次三次重建反馈“体感变化不是很大”，决定保留为可选项并恢复Nearest默认。Runtime Control v2新增History reprojection，安全帧边界切换并清逻辑历史，Helper不重建；旧v1配置向前兼容。下一项优先研究**新旧候选的区域过渡**：双方当前上下文均合法才过渡，保留两份独立true age和GPU末用，旧贡献到期及时退出；不能把混合旧编辑标成新origin。随后处理遮挡/MV边缘验证，避免错误残留与过度硬拒绝交替。减少重复重采样的anchor重建作为后续较大方案，先评估来源映射/资源成本，不立即扩充bank或功能菜单。

2026-10-16实施状态：当前可试玩候选Native为c8fb9df，标签nrbridge-stability-candidate-20261016；已完成区域过渡→保守边缘支持→18帧真实冷采集→三路120秒热动态→正常退休。独立bank混合与边缘实际保留已逐像素重构，贡献年龄未重标；玩家现反馈“我觉得可以了”，本版作为可玩参考保留；未宣称鬼影/闪烁解决。SR-only/固定Nearest/新候选分别69.35/58.15/57.86FPS，P95分别15.85/24.72/24.49ms、P99分别16.62/26.43/26.19ms；新旧NR平均差约−0.5%，单轮尾延迟或显存差不作收益证明。全部真实退休；SR-only一次source缺口计入耗时，两轮NR无缺口。完整本轮证据与限制在comparisons/stability-20261016/results.json，试玩和回退入口在tools/nr_dev/README.md。

加载PM4断言在固定旧基线也复现；guest float循环的上游来源仍未定位。保留原HOLD，仅有明确无NR ownership的失败按实际宿主复核追加fresh-context-only说明，不从失败存档续玩；没有修改旧失败或关闭安全检查。有界producer/提交快照冷诊断默认关闭并排除热评分。无帧退出owner推进已验证no-peer和真实NR正常停止；它不修复未知guest输入。后台约1FPS经固定基线前台恢复验证，热测已记录前台状态。玩家入口NR就绪后仅核验并聚焦自己的窗口。

锚点CPU原型83432ce显示避免反复残差插值的潜力，但直接增加4张RG32图需额外28.125MiB、超出192MiB预算；完整GPU实现需来源路由、安全flatten和资源角色重构。先以当前已可测的区域改进取得玩家反馈，再决定收益是否支持该项较大改动，不以延长TTL或整体降低NR掩盖伪影。

2026-10-16玩家验收补充：用户认可当前候选可继续使用，同时明确旧版已存在“光照环境下偏亮区域鬼影会比较严重，会有发光的拖影”，进一步定位为“角色移动时，受光的轮廓拖出亮尾”。将此列为独立OPEN：受光角色轮廓亮尾。当前c8fb9df四项实际Native产物已以只读Btrfs reflink固定到build/baselines/c8fb9dfdb7d80f444ddfdcf72c6483dba140963f并通过SHA/来源验证；不覆盖原始run结果。

下一窄任务：先用匹配受光角色移动的raw720→实际NR720→SR1080→Present采集与独立SR-only对照确定亮尾首现阶段。现有工具已能保存同源SR1080 HDR和Present真实像素，缺口是相应运动场景及必要来源归因，不重做捕获体系。源码显示Advance保留log编辑而逐帧更新raw guide；逐帧颜色门可能允许累计照明漂移，但这是待证假设，不是已定位根因。另一待证项是角色MV/遮挡对应，及SR对旧亮度的累积。先区分这些原因，再做局部失效/亮度可信度改进，不以整体降NR强度或全局缩TTL处理。F8即时旁路只作主观筛选，严格结论仍需独立SR历史。

2026-10-16加载可靠性进展： 玩家play-20261016-102125在pre-NR加载时PM4 type0断言exit23；原HOLD保留。失败发生在首次control观察PID/maps前，未伪造缺失身份；按捕获的干净c8fb9df源码/四产物、准确scene-first无NR日志、launcher退出及当前宿主检查追加fresh-context-only说明。当前默认Native构建为c359a29：统一submission_lock同步；idle/decode代次校验与清锁同一事务，IRQ仅通知；Gfx/ASC入队、计数和generation在同一queue→submit锁域发布；队列数量原子读取。保留原staging/退休语义，无新增全GPU等待，不跳过坏包，不改NR画质算法。实际源码强制交错CPU测试、75项Runner测试和全构建通过。dev-loading-epoch-01与dev-loading-epoch-nr-01均成功从Continue加载并正常退出，后者60秒动态实际578个job（573 captured/5 obsolete）结清且Native/Helper/GPU/FD正常退休，无残留。启用有界PM4冷诊断，不能当新热性能结果。这些是已证源码竞态修复及有限实际回归，不证明所有历史PM4坏包或guest float循环已有唯一根因修复。c8fb9df认可画质参考及旧失败证据保留；恢复玩家F8亮尾对照。

**2026-10-16 11:01真实GPU故障：当前GPU工作暂停。** play-20261016-105938在c359a29完成274个真实job并正常退休；紧接play-20261016-110102在加载时卡住，内核于monotonic256375.069018s报告Xid31，PID435760(bb:window)，GR0_PBDMA1/HUBCLIENT_ESC，FAULT_PDE/ACCESS_TYPE_VIRT_READ，地址0xfe_1fefe000。故障早于scene-ready约35.283秒，无peer/frames/config/arm；用户明确手动结束，exit137原HOLD保留，不能追认为正常退休。kernel-gpu-faults.jsonl和gpu-fault-observation.json保存原始内核证据与事实，未创建fresh-context resolution。宿主已无项目GPU持有者，但这不证明故障根因或驱动恢复；inputs/host-safety.json已记录真实未解决故障并阻止新GPU测试，原安全快照另存，不可删HOLD或回滚规避。

仅离线推进：310300e修SubmitDone处理中覆盖新通知（旧实现已由actualsource交错负对照复现）；095c5fa将sampler GC析构移到真实Scheduler tick之后，避免仅按guest提交次数直接销毁。三项actualsource CPU并发测试及完整Native构建通过，实际build095c5fa；这两处缺口没有与本次Xid建立直接因果，不能称Xid已修复。未运行任何新GPU任务、设备reset或驱动改动。当前宿主未发现已安装的Vulkan验证层，不声称具备验证层覆盖。下一GPU步骤须先明确恢复到干净上下文并复核驱动/持有者，限定一次不启动Helper/NR的加载/停止诊断，观察真实完成和故障线索；不直接恢复连续NR试玩。重启属于保守隔离准备，不是根因修复，也不能将旧失败改为PASS。亮尾定位继续等待基础加载安全。


**重启后单次隔离验证（2026-10-16）。** 用户确认重启，实际boot为c0492968-c33c-4b74-91dd-adb9b79a5e8b。dev-postreboot-sr-01使用095c5fa实际Native构建、全新private seed、sky off/skipEvaluate；未启动Helper或NR。已从加载画面进入Clinic，实际raw→SR与Present连续推进至source7994，并通过正常no-peer退出及source/cache/SR GPU末用证明，returncode0、无watchdog和残留。当前boot完整内核复检未发现GPU错误，证据保存在该run的post-exit-kernel.jsonl/post-exit-observation.json。发送的两秒转镜输入在pad日志可见，但截图未证实镜头位移，不将其计为动态画质验收；本次冷诊断不计热性能。Runner90b5d7f只允许这一精确session/boot/build且禁止Helper/arm，未解除普通试玩阻断、未修改任何历史HOLD。一次成功不证明间歇性加载故障或Xid31根因已修复；下一步需针对加载/资源退休的剩余具体风险形成恢复依据，再恢复NR和亮尾测试。


2026-10-16继续执行结果：已修复并离线验证TSC首次校准竞态（4871089），当前a65e230在dev-loading-trace-01完成120秒SR-only加载/移动/正常退休，当前boot无GPU错误；未验证真实NR或亮尾改善。新增有界资源地址日志和原始寄存器快照，完整状态与证据见CURRENT.md最新条目。旧float归一循环不推进已CPU复现，输入生产者仍未知，不能与Xid合并归因。亮度锚CPU候选存在曝光误拒绝，暂不合入生产。阶段仍为加载可靠性与亮尾定位，不进入发布/清理，用户离线期间不代填玩家验收。


## 受光角色轮廓亮尾：限定定位结论（2026-10-16）

**结论：尚不能确定亮尾首次出现的管线阶段；当前不具备将亮度锚候选合入默认算法的证据。** 本轮不启动GPU，不修改NR算法、运行准入或原始捕获。只复用现有compare.py/readback.py核验、显示18帧，并做局部对应及正常曝光反例；不将普通样本通过或编辑覆盖率下降称为修复。

身份固定：分析起点HEAD `6638a83267f4b6d71c047a36364ba60420602c90`；实际构建 `a65e2302c35ba3382312a63dd52beb92dda637c7`（四产物/构建来源重新校验）；`git ls-remote`实查远端仍为 `c71018e6ee8c3c38ce5ad1b1b8e6a328bb257874`。后续本文提交只改文档，不意味着实际二进制变更。样本edge-cold-01来自c8fb9df，transition-cold-04来自13d6c27；c8fb9df至a65e230无Persistent History/Shader算法变更，但运行构建身份不能混写。edge样本实际frame-bound参数为Nearest、强度1、TTL12帧/200ms、颜色.5、深度.02、最低置信度.5；10个已关联origin的模型request/ACK一致，Style0/Intensity1/model_generation0。所有身份见 `analysis/bright-trail-20261016/sample-review/identity.json`。

### 现有证据能看到哪一级

| 阶段 | 已有证据与结论边界 |
|---|---|
| 当前raw720 | SHA验证的original-current720；受光袖口本来就亮，不能把正常高光标作鬼影 |
| 直接模型结果 | 只有早期cold02/03的candidate-once Resolve720读回；这是Helper Resolve图，不是SDK tensor。当前18帧的相同候选没有直接模型图，不能追溯模型是否已带尾 |
| 历史重投影／合成 | 当前active/target edit/meta、真实年龄和weight；final-guarded720是Compose/强度/guard/half后的图，绝不是直接模型输出 |
| 实际SR输入 | 18帧均由日志actual_input_image与readback handle、source关联，确实选择final；不靠host potential推断 |
| SR1080 | 同capture_seq/source/revision、readback路径、输出SHA与本timeline完成证据关联；不是独立SR-only反事实 |
| Present | 实际后处理游戏像素，在letterbox/overlay之前；与HDR的EV4显示变换区分。短窗不能证明用户动态亮尾首现于此 |

18帧（两个run各3组×3帧）的SHA、finite/alpha/有效历史、实际SR绑定与输出关联通过。人工查看六张分阶段角色ROI图：都是Clinic背影短窗，存在镜头/角色相对位移，但**未确认可明确标注且沿时间追踪的玩家亮尾**；这不等于证明不存在。玩家反馈对应的play-20261016-100653和runtime-player-02无这些像素读回。全库metadata仅96帧、最长连续6帧；现有窗口没有覆盖默认12帧历史全寿命及完整候选更替/到期。现有F8共享SR历史，不能补这个独立对照缺口。

edge的三个窗口内可跟踪一次adoption：ROI旧origin10185(age9)／新origin10191(age3)以1/3过渡，随后新origin为age4→5；另一组10356→10362类似；10744(age10)／10750(age4)以2/3过渡，随后5→6。该ROI没有观察到年龄刷新或把旧origin重标为当前。inactive target可能是旧快照，明确不拿current_source减它的age来猜origin。

12个近似轮廓/衣服/背景探针仅用于对应检查，并非故障mask：7个保留点的residual逐half位匹配前一source的MV+jitter nearest位置，真实age只加1；另外5个点的**前帧来源已经confidence0**，CPU颜色门也不通过，但不能把这种零化后的guide解释为最初的颜色拒绝原因。现有读回没有逐像素首次失效原因，也没有物体身份；因此没有排除错误MV、同色同深度跨物体取样或遮挡。详细数据为sample-review/correspondences.json，图像为同目录六张stages.png。

### 三类原因与最小候选

- **累计局部照明漂移：支持算法可发生，未支持本次实际亮尾归因。** 逐帧guide更新，原正log编辑仍保留；旧CPU例每帧-.08 stop可累计到-.96 stop而通过逐步颜色门。现有3帧样本缺origin到当前的完整raw链，不能测得故障处的累计漂移。
- **正常全局曝光：简单锚门有明确反例。** 11个受控图像/ROI场景独立重算一致；估计的分布式背景log亮度变化可把合成全局下降中的144个像素帧误拒绝降为0，并仍挡局部暗漂移。它只是亮度估计，不是引擎曝光。全场真实照明变化与曝光可完全同观测；合法NR提亮与过时亮尾也可完全同输入，候选仍会误拒绝合法效果。
- **MV／遮挡：没有排除。** 同色同深度错对应的反例仍漏检；显式深度遮挡反例沿原门失效，不表示真实游戏轮廓MV已正确。背景空间分歧或支持不足时估计必须放弃，不能把缺失估计当0或继续外推。

保留一个**未验收、未合入生产**的最小候选：仅对已证实的“移动轮廓旧正编辑”区域，以不可变origin亮度锚比较当前值，并减去可靠背景亮度变化；超过局部阈值才失效到当前raw，其他区域不变。保留所有原几何/颜色门、强度、真实年龄/TTL；old/target各自保留锚和来源。背景支持/连续性不足、曝光与真实照明不可辨、没有故障ROI证据时，不启用额外门。四bank R16F锚名义新增7.031MiB（总189.844MiB，不含VMA），但不因预算可容纳就实施。**正常曝光合成反例通过，合法提亮/同色错MV反例不通过，故当前不得作为默认修复。** 结果见exposure-results.json；拒绝率本身不代表画质收益。

### 补拍与安全前置

加载可靠性保持独立OPEN：TSC竞态修复及两次SR-only正常退休不关闭旧Xid31；本轮不扩展当前SR-only诊断范围或解除普通NR阻断。恢复条件须来自当前宿主/driver/所有者和资源检查，不伪改旧FAIL/HOLD。玩家暂不在电脑前，主观验收保留待办。

条件满足后复用现有捕获：固定私有场景/动作/模型配置；保留正常全局亮度变化对照和独立SR-only历史；同候选捕获origin raw及直接Resolve720，同时连续记录raw/Depth/MV/jitter、old/target edit/meta与逐像素失效证据、实际SR输入/输出/Present。窗口至少覆盖16个连续源帧，含亮尾开始、一次候选接纳及原贡献到期；期间若刷新了贡献，须按origin分别跟踪，不能用“过了12帧”冒充同一编辑到期。现有F9硬限3帧、底层当前MaxFrames4，不能把多组有空档的截图拼成连续窗口；补拍前仅在既有读回实现内设计有界复用或ROI窗口，核验真实末用与容量，不直接无界提高分配/复制全部画面，也不新建另一套Runner。缺失的同候选直接模型图、完整来源链、故障ROI与正常曝光控制补齐后，才决定该最小候选是否针对实际根因，再交玩家验收；尚不推进工程整理和发布。


### 玩家复现场景确认（2026-10-16，人工作业窗口）

用户原话：“门口灯下 快速转身 角色轮廓后方留下亮轮廓”；随后明确操作为“角色基本不动，快速转动视角”。因此复现动作固定为Clinic门口灯下原地快速转镜，不再把此反馈解释为角色快速改变移动方向。背景是否同时拖亮、F8独立对照仍未知，没有补填。此描述是玩家观察，不对应新GPU run或已标注故障像素。

下一次采样优先区分：静止角色在快速转镜下的镜头MV+jitter对应／遮挡显露、NR旧正编辑的空间滞留，以及下游SR对轮廓亮度的保留；不能仅因动作涉及镜头就排除模型或局部光照。固定同灯位、相机起止角和转镜方向/速度，分别保留转镜前、运动中、停止后的连续画面，并继续使用前述来源与正常曝光对照要求。

本次实时复查boot c0492968-c33c-4b74-91dd-adb9b79a5e8b，无项目holders、当前boot内核无新GPU错误。锁内普通preflight仍因旧fresh-context记录需要same boot而拒绝，host-safety保留旧Xid31未解决事实；没有改记录解除NR准入，也没有启动游戏。现行短窗与同候选模型采集缺口仍在，未承诺在人工作业窗口内完成资源改造。用户无需为补充描述运行旧版或绕过启动检查。


### 2026-10-16：有界恢复实测与再次加载失败（当前状态）

用户回到电脑后，完成当前boot/build9a32779的SR-only → off/skip（24）→ persistent/skip（24）→ resident（18 Evaluate，17 captured/1 obsolete）链，相关run为dev-paired-sr-01、dev-recovery-off-04、dev-recovery-persistent-01、dev-recovery-nr-01。均有真实正常退休、无残留及完整boot内核复检；没有将此冷诊断作为热FPS或亮尾改善证明。其余off-01/02/03只完成零job/未arm或no-peer退出，不计工作负载验证。

9a32779补齐F9的同候选origin raw/Depth与Helper acquired Resolve720，后接当前3帧管线；不是SDK原始tensor，也未解决16帧全寿命窗口。UI临时读回约232.03MiB，维持4帧底层上限；等待新候选10秒到期标INCOMPLETE。3项CPU验证与Native构建通过，但本轮没有实际F9配对成功证据：首次自动热键因缺xdotool未发送，随后玩家采集run在NR前失败。不得将已实现当作GPU采样验收。

286e8fa复用现有Runner增加`--context-recovery`精确记录入口，未建立新的执行系统。启动、Helper和arm均绑定同boot/实际build/依赖/原HOLD摘要/请求和当前锁/进程身份/完整内核证据；以前序实际逐job及GPU/FD退休审计递进。首次resident最多120source，后续7200source，最长240秒是这条恢复诊断的边界，非普通研发配额。普通play仍阻断，旧host-safety/Xid/HOLD原样。112项Python测试通过。

**当前阻断**：dev-bright-outline-player-01在场景就绪与Helper/NR前exit23/HOLD。最新宿主只读核对无项目holder、当前boot无新增GPU错误；不等于该run正常退休或故障已修。提交和解码的完整1488字节副本一致（另算SHA），首错0x284；记录的CS从0x27c开始，与前一0x274的reg0xa包payload重叠8字节。CS/PS/VS guest wrapper分别预留/推进25/40/29DW，HLE所有分支匹配，暂未发现尺寸ABI错误；PS生产者缺失。CS尾部NOP保留旧payload合法，不能当并发写入证据。独立离线核验在analysis/pm4-overlap-20261016，guest反汇编仅本地私有，不公开分发。

c1a702e补齐Set/Update/Embedded PS/VS及CS共10入口；固定256条生产者记录、40DW快照、最多4dump/64KiB，标明逻辑footprint、覆盖不完整及非原子快照。默认关闭不初始化大数组、不读guest/取时钟/锁；失败footprint0不读，guard-page CPU测试通过。完整Native构建通过，实际build c1a702e且source-diff为空；日志build/pm4-producer-build.log。该更改是定向诊断，不是根因修复，NR强度/TTL/算法未改。

下一步仅围绕重叠生产者、guest CB游标/提交所有权定位；需要实证支持的修复及新的实际上下文安全条件后再安排GPU。当前boot新HOLD会拒绝已有恢复记录，不能换session、改previous_boot或写假resolution绕过。旧Xid与float冻结仍独立OPEN；亮尾实采/改善、长期试玩和发布尚未完成，不要求玩家现在重复启动或再作无证据重启。
