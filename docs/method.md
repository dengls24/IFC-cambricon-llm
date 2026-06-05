# Method Notes

This project implements a compact C simulator for Cambricon-LLM style decode speed. It is designed for method reproduction of Figure 9 rather than a full SSD firmware simulator.

## Hardware Profile

The hardware profile follows Table II:

| Platform | Channels | Chips/channel | Dies/chip | Planes/die | Page | tR | Channel bus |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cambricon-LLM-S | 8 | 2 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |
| Cambricon-LLM-M | 16 | 4 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |
| Cambricon-LLM-L | 32 | 8 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |

The NPU profile is the paper's 16x16 systolic array at 1 GHz, modeled as 2 TOPS INT8 peak with 40 GB/s DRAM bandwidth.

## Tile Shape

The C simulator uses the Section V transfer-minimizing tile:

```text
H_req = sqrt(cores_per_channel * page_size)
W_req = channel_count * H_req
```

For Cambricon-LLM-S:

```text
cores_per_channel = 2 chips/channel * 2 dies/chip * 1 core/die = 4
H_req = sqrt(4 * 16384) = 256
W_req = 8 * 256 = 2048
```

This matches the `256 x 2048` tile used as the optimal point in the paper's tile-size study.

## Request Timing

Read-compute request time:

```text
t_rc = t_R + W_req / (channels * channel_bandwidth)
```

Read-compute channel occupancy:

```text
rate_rc = (H_req + W_req / channels) / (t_R * channel_bandwidth)
```

Sliced read request time:

```text
t_read = page_size / ((1 - rate_rc) * channel_bandwidth)
```

Workload fraction assigned to read-compute:

```text
alpha = t_read / (t_read + t_rc)
```

The tiled weight stage uses the max of the read-compute and sliced-read paths, then applies one platform-level pipeline efficiency term. The efficiency term absorbs startup, imperfect command packing, and controller effects that are not visible in the high-level equations. It is calibrated per S/M/L platform, not per model.

## Token Time

The final per-token latency is:

```text
TPOT = tiled_weight_stage
     + DRAM_attention_cache_bytes / 40 GBps
     + attention_arithmetic_ops / 2 TOPS
```

This keeps the reproduction aligned with Cambricon-LLM's Figure 9 setup: flash-resident weights, in-flash read-compute, sliced read requests for NPU-side work, and DRAM-resident attention cache.

## Boundaries

This simulator does not model:

- full SSDsim queue internals;
- ECC area/power and bit-error behavior;
- prefill latency;
- FlexGen or MLC-LLM baselines;
- multi-batch scheduling.

Those features are outside the Figure 9 reproduction path.
