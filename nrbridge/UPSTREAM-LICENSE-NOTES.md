# 归档及依赖许可边界

OptiScaler fork 根 LICENSE 为 GPL-3.0。归档 helper、production adapter、build_c2.py 保留原 SPDX GPL-3.0-or-later；transport/control/fixture_util 保留自身 MIT SPDX。未从许可证不兼容的 layer/helper 项目复制源码。nlohmann JSON 仅通过既有只读头文件路径及 SHA 使用，MIT；本归档不复制其头文件。GPL/MIT 源码依既有声明复用，保留 SPDX 和根许可证。

Vulkan SDK headers、NVIDIA NGX SDK headers、OptiScaler production SPIR-V/forwarder、GE loader/DXGI/NVAPI、host NGX、NR model 均通过 c2-runtime-pins.json 记录的本地只读路径复用。NVIDIA SDK/model/runtime 不是新归档的 GPL 源码，不加入 SDK 或模型 PE，不以 GPL 许可声称可再分发这些 vendor 依赖；本实验只使用机器上已有安装和模型资产。PE 模型仍为原路径 historical-dependencies/dll/runtime/nvngx_dlssnr.dll / SHA6eb209e7…；PE 不是 ELF，运行路径保持 Wine Vulkan。无游戏文件/存档、构建输出或运行像素证据进入提交。
