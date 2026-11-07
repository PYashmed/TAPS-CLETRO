# Fine-Grained and Time Deterministic Transmission Paradigm for Mega Low Earth Orbit Constellations with Hardware-in-the-Loop Simulation

This repository is an open-source release of the code and data for the paper *Fine-Grained and Time Deterministic Transmission Paradigm for Mega Low Earth Orbit Constellations with Hardware-in-the-Loop Simulation*.

本仓库公开论文《Fine-Grained and Time Deterministic Transmission Paradigm for Mega Low Earth Orbit Constellations with Hardware-in-the-Loop Simulation》的代码与数据。

## Repository Structure

The reproduction package is under `satnet3d/`. See the [project README](satnet3d/README.md) for build instructions and experiment profiles.

- `satnet3d/src/` contains the OMNeT++ modules for link delay configuration, link-layer forwarding, queueing and hardware-in-the-loop integration.
- `satnet3d/simulations/xw/` contains the network definition (`xw.ned`), the HIL configuration (`hil.ini`) and six traffic configurations for BE, JRS and JRS-TER experiments. Its `offline/`, `results/` and `scripts/` directories contain offline scheduling materials, simulation results and experiment scripts, respectively.
- `satnet3d/Makefile` and `satnet3d/LICENSE` provide the build rules and license terms.

## 仓库结构

实验复现包位于 `satnet3d/`。编译步骤和实验配置说明见[项目 README](satnet3d/README.md)。

- `satnet3d/src/` 包含链路时延计算、链路层转发、队列管理和硬件在环接口等 OMNeT++ 模块。
- `satnet3d/simulations/xw/` 包含网络拓扑定义（`xw.ned`）、硬件在环配置（`hil.ini`），以及 BE、JRS 和 JRS-TER 三种方案的六组业务配置。其下的 `offline/`、`results/` 和 `scripts/` 分别存放离线调度相关文件、仿真结果和实验脚本。
- `satnet3d/Makefile` 定义项目构建规则，`satnet3d/LICENSE` 说明许可条款。
