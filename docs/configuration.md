# Runtime Configuration

The simulator now supports two modes:

- Reproduction mode: no configuration files are passed. The built-in Cambricon-LLM Figure 9 profiles are used.
- Design-space mode: CSV files override hardware, system, model, and reference profiles while keeping the same C timing path.

The row shape is intentionally fixed to 7 model slots x 3 platform slots so the Figure 9 reports, plots, summaries, and checks remain comparable. A custom experiment can replace the values in those slots.

## CLI

```bash
bin/ifc_cambricon_llm \
  --output-dir results_scaled \
  --models-csv configs/example_models_mixed.csv \
  --platforms-csv configs/example_scaled_platforms.csv \
  --system-csv configs/example_system_fast_npu.csv \
  --reference-csv configs/default_references.csv
```

If a CSV is omitted, the built-in default profile for that category is used.

## Flash And IFC Platform CSV

File: `configs/default_platforms.csv`

Columns:

| Column | Meaning |
|---|---|
| `channels` | Number of flash channels. |
| `chips_per_channel` | Flash chip scale per channel. |
| `dies_per_chip` | Die scale per chip. |
| `planes_per_die` | Plane scale per die. |
| `compute_cores_per_die` | IFC compute cores per die. |
| `page_bytes` | NAND page size. |
| `array_read_us` | NAND array read latency. |
| `program_us` | Program latency retained for controller completeness. |
| `onfi_rate_MTps` | ONFI-style external transfer rate per channel. |
| `onfi_bus_width_bits` | External bus width per channel. |
| `ifc_frequency_MHz` | IFC compute core frequency. |
| `ifc_ops_per_core_cycle` | Per-core IFC operations per cycle. |
| `pipeline_efficiency` | Platform-level command packing and pipeline efficiency. |
| `footprint_penalty` | Scale-dependent footprint penalty. |
| `footprint_penalty_power` | Exponent for footprint penalty. |
| `unsliced_blocking_factor` | Blocking factor used by the no-read-slicing ablation. |
| `no_tiling_slowdown` | Slowdown used by the no-hardware-aware-tiling ablation. |

The effective channel bandwidth is derived as:

```text
channel_bandwidth_Bps = onfi_rate_MTps * 1e6 * onfi_bus_width_bits / 8
```

The IFC tile compute time is derived as:

```text
ifc_compute_time_s =
    tile_height * tile_width /
    (channels * chips_per_channel * dies_per_chip * compute_cores_per_die
     * ifc_frequency_hz * ifc_ops_per_core_cycle)
```

The read-compute request time uses the slower of vector transfer and IFC compute:

```text
t_read_compute = t_R + max(vector_transfer_s, ifc_compute_time_s)
```

The controller audit paths check platform dimensions before emitting traces. The current internal limits are 64 channels, 128 chips/channel, 8 dies/chip, and 8 planes/die. The analytic Figure 9 timing path and summaries still use the active CSV values, but controller traces are emitted only when the first configured platform is inside those checked limits.

## NPU And System CSV

File: `configs/default_system.csv`

Rows:

| Key | Meaning |
|---|---|
| `context_tokens` | Decode context length used for attention-cache traffic. |
| `npu_frequency_MHz` | NPU clock. |
| `npu_ops_per_cycle` | NPU operations per cycle. |
| `npu_peak_TOPS` | Direct peak throughput override. |
| `dram_bandwidth_GBps` | DRAM bandwidth for attention-cache reads. |

If `npu_peak_TOPS` is positive, it is used directly. Otherwise the simulator derives:

```text
npu_peak_ops_per_s = npu_frequency_hz * npu_ops_per_cycle
```

The effective system profile is written to `results/system_profile.csv`.

## Model CSV

File: `configs/default_models.csv`

Columns:

| Column | Meaning |
|---|---|
| `parameters_billion` | Weight size in billions of parameters. |
| `layers` | Transformer layer count. |
| `hidden_size` | Hidden width. |
| `attention_heads` | Attention query heads. |
| `cache_heads` | Cache heads. This supports dense MHA, MQA, or GQA-style models. |
| `head_dim` | Per-head dimension. |

The simulator uses these values to compute:

```text
weight_bytes = parameters_billion * 1e9
attention_cache_bytes = layers * 2 * cache_heads * head_dim * context_tokens
attention_ops = 2 * layers * attention_heads * head_dim * context_tokens
```

## Reference CSV

File: `configs/default_references.csv`

Reference values are only needed for paper-vs-simulator error reporting. For design-space exploration, a custom reference file should be supplied if measured or trusted baseline token/s values are available. If a custom hardware/model profile is evaluated with the default references, the token/s output is still meaningful, but the relative-error metrics should be read as a placeholder comparison rather than a reproduction claim.

## Output Artifacts Affected By Configuration

All major artifacts use the active configuration:

- `figure9_reproduction.csv`
- `request_trace.csv`
- `controller_schedule.csv`
- `cycle_controller_trace.csv`
- `cycle_controller_stats.csv`
- `ssdsim_ifc_trace.csv`
- `ssdsim_ifc_stats.csv`
- `ssdsim_ifc_event_trace.csv`
- `ssdsim_ifc_event_stats.csv`
- `controller_timing_summary.csv`
- `npu_timing.csv`
- `platform_summary.csv`
- `model_summary.csv`
- `tile_profile.csv`
- `system_profile.csv`
- `reproduction_checks.csv`
- all SVG plots under `results/figures/`

The default command remains the canonical Figure 9 reproduction. Configured runs are design-space experiments unless their reference CSV corresponds to the configured hardware and model setup.

`make hw-cycle` and `make systemc-cycle` also accept the active platform CSV through:

```bash
bin/ifc_hw_cycle_model --platforms-csv configs/default_platforms.csv
bin/ifc_hw_cycle_systemc --platforms-csv configs/default_platforms.csv
```

The hardware-cycle models currently use the first platform row. The dependency-free C++ checker emits `hw_cycle_trace.csv`, `hw_cycle_stats.csv`, and `hw_cycle_compare.csv`. The SystemC checker emits `systemc_cycle_trace.csv`, `systemc_cycle_stats.csv`, and `systemc_cycle_compare.csv`.

The SystemC target uses `SYSTEMC_HOME=../.ifc_systemc/systemc_sysroot/usr` by default. Use `SYSTEMC_HOME=/usr` for a system package installation, or run `tools/setup_systemc_local.sh` to create the default local sysroot without root privileges.
