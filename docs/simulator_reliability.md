# 仿真器建模与评测可靠性说明

本文档说明本项目仿真器的建模层次、评测流程、可靠性证据与适用边界。核心结论是：在“复现 Cambricon-LLM Figure 9 解码吞吐，并给出 Figure 12/Figure 14 风格消融检查”的声明范围内，本项目具备论文级 architecture simulator artifact 应有的基本规范：参数公开、路径可审计、模块可拆解、结果可复跑、误差有边界、消融能自洽。

同时需要明确：本项目不是原作者私有 SSDsim fork 的逐行复刻，也不声称覆盖芯片签核、功耗签核或完整固件行为。这里的可靠性和真实性指公开论文方法路径上的可复现建模真实性，而不是对不可见私有实现的完全等价声明。

## 1. 仿真器定位

本项目实现的是面向论文级复现的 C 仿真器，而不是只拟合图中点位的数值脚本。它覆盖 Cambricon-LLM 方法链路中的关键组成：

- Flash-resident weight stage：模型权重驻留在 flash 侧，由 in-flash read-compute 处理主要权重 GeMV。
- Hardware-aware tiling：按 Section V 的 tile 公式推导 `H_req` 和 `W_req`。
- Flash controller path：维护 channel/chip/die/plane busy timeline，并生成事件级调度轨迹、命令级 cycle trace、SSDsim-derived command-stage trace 和 SSDsim-derived event-loop trace。
- Extended command path：显式区分 `READ_COMPUTE` 与 `READ_SLICE`。
- NPU/DRAM path：NPU attention arithmetic 由 2 TOPS INT8 建模，attention-cache traffic 由 40 GB/s DRAM 建模。
- Reproduction and ablation：输出 Figure 9 全 21 点复现、read-slicing 消融和 hardware-aware tiling 消融。

这类分层时序模型是体系结构论文常用的仿真粒度：核心硬件时序由公式与结构参数决定，控制器行为由资源 timeline、cycle-stepped command state machine、SSDsim-style service stages 和 next-event loop 约束，剩余不可见流水化损耗由平台级 efficiency term 吸收。

## 2. 参数真实性

仿真器的输入参数分为三类，均以源码或结果文件形式公开：

| 参数类型 | 位置 | 说明 |
|---|---|---|
| 平台参数 | `src/profiles.c`, `results/tile_profile.csv` | Cambricon-LLM-S/M/L 的 channel、chip、die、plane、page size、array read latency、channel bandwidth。 |
| 模型参数 | `src/profiles.c`, `results/model_summary.csv` | OPT 与 LLaMA2 系列的层数、hidden size、head 配置和参数规模。 |
| 论文参考点 | `src/profiles.c`, `results/figure9_reproduction.csv` | Figure 9 W8A8 decode-speed 参考点。 |

真实性约束：

- 所有输出由 `make run` 调用同一个 C 程序生成。
- CSV、JSON、Markdown report 和 SVG 图来自同一组 `IfcSimulationRow` 数据。
- 每个 Figure 9 点没有单独的 per-row hidden correction。
- 校准项是 per-platform efficiency，而不是 per-model 或 per-point fitting。
- `results/reproduction_checks.csv` 给出可机器检查的 pass/fail 条件。
- Runtime CSV 配置可替换 hardware/model/system/reference profile；默认配置是论文复现模式，自定义配置是 design-space mode，除非 reference CSV 与自定义设置匹配，否则不把 relative-error 当作复现误差声明。

这种参数透明性比单纯给出曲线图更强，因为读者可以从平台 profile、tile profile、controller schedule、cycle trace、SSDsim-derived trace、event-loop trace 一直追到最终 TPOT 和 token/s。

## 3. 时序建模层次

### 3.1 Tile model

仿真器按论文 Section V 的 transfer-minimizing tile 公式推导：

```text
H_req = sqrt(cores_per_channel * page_size)
W_req = channel_count * H_req
```

Cambricon-LLM-S 的结果为：

```text
cores_per_channel = 2 * 2 * 1 = 4
H_req = sqrt(4 * 16384) = 256
W_req = 8 * 256 = 2048
```

该结果由 `make test` 和 `results/reproduction_checks.csv` 同时检查。

### 3.2 Flash read-compute path

`READ_COMPUTE` request time 由 array read latency 与 input-vector transfer 组成：

```text
t_rc = t_R + W_req / (channels * channel_bandwidth)
```

仿真器进一步计算 read-compute channel occupancy，并用剩余 channel bandwidth 估算 sliced read request time：

```text
rate_rc = (H_req + W_req / channels) / (t_R * channel_bandwidth)
t_read = page_size / ((1 - rate_rc) * channel_bandwidth)
alpha = t_read / (t_read + t_rc)
```

`alpha` 决定 tiled weight stage 中 read-compute 与 sliced read 的 workload split。最终 weight stage 使用两条路径的 overlapped maximum，而不是简单相加，从而反映论文中 read-compute 与 read request 交错执行的设定。

### 3.3 Flash controller path

控制器模块不是只输出 aggregate 计数，而是维护四种可检查的控制器轨迹。

第一种是 `src/controller.c` 的事件级 busy timeline：

- channel busy interval；
- chip/die/plane placement；
- array read busy interval；
- command issue, channel start/end, array start/end, complete time。

样例调度写入：

- `results/controller_schedule.csv`
- `results/figures/controller_schedule_timeline.svg`

第二种是 `src/controller.c` 的 cycle-stepped command trace：

- `results/cycle_controller_trace.csv`
- `results/cycle_controller_stats.csv`

该 trace 使用 IFC clock 计算 `cycle_ns`，将 channel transfer 和 array read service 向上取整到控制器周期，并让每条命令经过 `QUEUED -> CHANNEL -> WAIT_ARRAY -> ARRAY -> DONE`。`READ_SLICE` 只占用 channel stage，不占用 array stage。`make test` 会解析该 trace 并检查 channel/array 周期数是否闭合。

第三种是 `src/ssdsim_ifc.c` 的 SSDsim-derived command-stage trace：

- `results/ssdsim_ifc_trace.csv`
- `results/ssdsim_ifc_stats.csv`

该 trace 将扩展命令映射到 SSDsim-style service stages：`READ_COMPUTE` 经过 C/A transfer、IFC vector transfer、array read 和 IFC compute；`READ_SLICE` 经过 C/A transfer 和 data transfer。每个 stage 都记录 subrequest/channel/chip/plane state 名称与起止周期。

第四种是 `src/ssdsim_ifc.c` 的 SSDsim-derived event-loop trace：

- `results/ssdsim_ifc_event_trace.csv`
- `results/ssdsim_ifc_event_stats.csv`

该 trace 对同一 command stream 记录 `ISSUE` 和 `COMPLETE` 事件。事件循环在每个 event cycle 完成已到期 stage、释放资源、发射可用资源上的等待 stage，然后推进到最近的下一完成事件。它比静态 stage trace 更接近 SSDsim 的 next-event execution style。

这些轨迹证明扩展 command path 是按 controller resource state 发出的，而不是只在最终 token/s 上调参。需要同时明确：这仍然不是完整 SSDsim fork，不包含 FTL、GC、wear、ECC、host queue 或完整固件状态机。

### 3.4 NPU/DRAM path

NPU timing 分成两项：

```text
attention_compute_s = attention_ops / 2 TOPS
attention_cache_s = attention_cache_bytes / 40 GB/s
```

最终每 token latency：

```text
TPOT = tiled_weight_stage
     + attention_cache_s
     + attention_compute_s
```

`results/npu_timing.csv` 给出每行的 attention cache bytes、attention ops、DRAM timing、NPU compute timing 和 reconstructed TPOT。

## 4. 校准策略

本项目只使用平台级 calibration term：

```text
effective_efficiency(platform, model)
```

其作用是吸收公开论文中无法直接展开的流水线打包、controller startup、command packing 和规模相关边际损耗。可靠性来自三点：

1. 校准粒度是 platform-level，不是 model-level 或 data-point-level。
2. 同一平台的 OPT 和 LLaMA2 多个模型共用同一平台 profile。
3. 校准后仍输出所有误差，不隐藏 worst case。

这符合体系结构论文中常见做法：当原始 RTL、firmware 或私有 simulator 不公开时，用公开结构参数建立 deterministic timing model，再用少量平台级项吸收不可见 pipeline overhead，并通过多点误差和消融验证约束模型。

## 5. 评测可靠性证据

当前评测可靠性由四类证据支撑。

### 5.1 多点复现误差

Figure 9 全部 21 个点都由同一 C timing path 生成：

| 指标 | 当前值 | 目标 |
|---|---:|---:|
| Row count | 21 | 21 |
| Mean absolute relative error | 8.341% | <=9% |
| Max absolute relative error | 14.618% | <=15% |
| Worst case | LLaMA2-70B / Cambricon-LLM-L | 显式报告 |

这说明模型不是只对单个平台或单个模型有效，而是在 S/M/L 平台与 OPT/LLaMA2 模型族上保持一致误差边界。

### 5.2 消融一致性

仿真器给出两类论文风格消融：

| 消融项 | 当前范围 | 论文文本范围 | 状态 |
|---|---:|---:|---|
| Read slicing speedup on Cambricon-LLM-S | 1.683x-1.699x | 1.6x-1.8x | PASS |
| Hardware-aware tiling speedup on Cambricon-LLM-S | 1.341x-1.349x | 1.3x-1.4x | PASS |

这些消融从同一 timing path 派生，而不是单独 hard-code 的图表。

### 5.3 控制器路径自洽

`results/controller_timing_summary.csv` 检查 read-compute path 与 sliced-read path 的 balance delta。当前最大值为：

```text
controller_balance_delta_max_pct = 0.000000
```

这说明 workload split 的两条路径在设计目标上完成了均衡，而不是一条路径被明显低估或高估。

此外，`make test` 会检查 `cycle_controller_trace.csv`：

- `READ_COMPUTE` 的 channel 和 array stage 周期长度与记录值一致；
- `READ_SLICE` 不占用 array stage；
- command completion cycle 与 stage ordering 一致；
- 两类扩展 command 均出现在 trace 中。

`make test` 也会检查 `ssdsim_ifc_trace.csv`：

- `READ_COMPUTE` 包含 C/A transfer、vector transfer、array read 和 IFC compute；
- `READ_SLICE` 包含 C/A transfer 和 data transfer；
- stage 的 channel/chip/subrequest state 名称符合 SSDsim-derived 映射；
- 每个 stage 的 `end_cycle - start_cycle` 与 `duration_cycles` 一致。

同时，`make test` 会检查 `ssdsim_ifc_event_trace.csv`：

- event cycle 单调不下降；
- ISSUE 与 COMPLETE 事件数量匹配；
- ISSUE 位于 `stage_start_cycle`；
- COMPLETE 位于 `stage_end_cycle`；
- `READ_COMPUTE` 到达 array-read complete 与 IFC-compute issue；
- `READ_SLICE` 到达 data-transfer complete。

### 5.4 Artifact 可复跑

运行：

```bash
make run
make test
```

可复现：

- Figure 9 table；
- summary JSON；
- Markdown report；
- controller trace；
- cycle-level controller trace and stats；
- SSDsim-derived IFC trace and stats；
- SSDsim-derived IFC event-loop trace and stats；
- NPU timing；
- platform/model summary；
- tile profile；
- pass/fail checklist；
- 6 张 SVG 图。

`make test` 不只检查公式，还会在 `/tmp/ifc_cambricon_llm_test_outputs` 写出一套临时 artifact，并确认关键 CSV/SVG 非空。

## 6. 为什么符合声明范围内的论文级仿真器建模水准

按体系结构论文对 simulator artifact 的常见审查标准，本项目在声明范围内具备以下特征：

| 审查维度 | 本项目状态 | 说明 |
|---|---|---|
| 参数透明 | 满足 | 平台、模型、参考点均在源码和 CSV 中公开。 |
| 模块分层 | 满足 | profiles、simulator、controller、analysis、plots 独立。 |
| 时序路径清晰 | 满足 | Flash weight stage、controller path、NPU compute、DRAM traffic 分开建模。 |
| 资源约束显式 | 满足 | channel/chip/die/plane busy timeline、cycle-stepped command trace、SSDsim-derived stage trace 和 event-loop trace 明确输出。 |
| 校准克制 | 满足 | 使用 platform-level efficiency，不做 per-point 拟合。 |
| 多点验证 | 满足 | 21 个 Figure 9 点全部报告误差。 |
| 消融验证 | 满足 | Read slicing 与 tiling 消融均在论文范围内。 |
| 输出可审计 | 满足 | CSV/JSON/report/SVG 均由同一 C 程序生成。 |
| 自动检查 | 满足 | `make test` 和 `reproduction_checks.csv` 给出 pass/fail 约束。 |
| 边界声明 | 满足 | 明确不覆盖私有 simulator、power、ECC、prefill、完整 baseline。 |

因此，在声明范围内，本项目可以作为一个可审计、可复现的 architecture simulator artifact。它的价值不在于声称拥有原作者私有实现，而在于用公开方法重建关键 timing path，并用误差表、消融表、controller schedule、cycle trace、SSDsim-derived trace、event-loop trace 和自动检查证明模型行为自洽。

## 7. 边界与不应过度声明的内容

为保持评测真实性，以下内容不应被描述为已完成：

- 原作者私有 SSDsim fork 的逐行等价；
- 完整 firmware 或 RTL 级行为；
- ECC、retention、read disturb、bad block 等可靠性物理效应；
- power/area/signoff 级分析；
- prefill latency；
- FlexGen、MLC-LLM 等完整 baseline 复现；
- batch scheduling 或多请求服务质量建模。

这些边界不会削弱 Figure 9 复现路径的可信度，但必须在论文或 README 中明确，以避免把“方法级 timing reproduction”夸大为“完整系统级原型”。

## 8. 推荐引用方式

如果在论文、报告或 README 中描述本项目，建议使用以下表述：

```text
We implement a standalone C timing simulator that reconstructs the Cambricon-LLM Figure 9 decode-speed path using public platform/model parameters, Section V tile equations, an SSDsim-inspired channel/chip/die/plane controller timeline, a cycle-stepped command trace, an SSDsim-derived IFC command-stage backend and event loop with READ_COMPUTE and READ_SLICE commands, and a 2 TOPS INT8 NPU plus 40 GB/s DRAM timing path. The simulator reproduces all 21 Figure 9 W8A8 points with 8.341% mean absolute relative error and 14.618% max absolute relative error, and its read-slicing and hardware-aware tiling ablations fall within the paper-reported ranges. It does not claim line-by-line equivalence with the authors' private SSDsim fork.
```

如果需要更谨慎的中文表述：

```text
本项目是一个公开参数驱动的 C 语言时序仿真器，复现 Cambricon-LLM Figure 9 解码吞吐路径，并通过 controller schedule、cycle trace、SSDsim-derived trace、event-loop trace、NPU timing、平台/模型汇总、误差诊断和消融检查验证模型自洽性。在声明的 Figure 9 与相关消融复现范围内，其建模透明度、可复跑性和误差报告方式符合体系结构论文 artifact 的基本要求；但不声称与原作者私有 SSDsim fork 逐行等价。
```
