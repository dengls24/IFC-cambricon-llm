# SSDsim IFC Backend

This document describes the SSDsim-derived backend added for the IFC extended commands.

The backend is implemented in `src/ssdsim_ifc.c`. It is intentionally narrower than a full SSDsim distribution: it focuses on the flash command stages needed by the Cambricon-LLM decode path and emits a clean C trace that can be checked by tests. It does not include SSDsim's full FTL, garbage collection, wear, host trace reader, or statistics package.

## Integration Level

The backend follows the public SSDsim read-service transition pattern:

```text
C/A transfer -> array read busy -> data transfer -> complete
```

For Cambricon-LLM's extended commands, the path is specialized as:

```text
READ_COMPUTE:
  SSDSIM_CA_TRANSFER
  IFC_VECTOR_TRANSFER
  SSDSIM_ARRAY_READ
  IFC_COMPUTE

READ_SLICE:
  SSDSIM_CA_TRANSFER
  SSDSIM_DATA_TRANSFER
```

This is a real C command-stage backend, not a post-hoc arithmetic table. Each stage updates explicit channel, chip, plane, and IFC compute-resource ready times.

## Output Artifacts

The simulator writes:

- `results/ssdsim_ifc_trace.csv`
- `results/ssdsim_ifc_stats.csv`

The trace columns are:

```text
command_id
opcode
logical_id
slice_id
channel, chip, die, plane
stage
subrequest_state
channel_state
chip_state
plane_state
start_cycle
end_cycle
duration_cycles
```

The state names intentionally mirror SSDsim-style terminology:

| Field | Example values |
|---|---|
| `subrequest_state` | `SR_R_C_A_TRANSFER`, `SR_R_READ`, `SR_R_DATA_TRANSFER`, `SR_IFC_COMPUTE` |
| `channel_state` | `CHANNEL_C_A_TRANSFER`, `CHANNEL_DATA_TRANSFER`, `CHANNEL_IDLE` |
| `chip_state` | `CHIP_C_A_TRANSFER`, `CHIP_READ_BUSY`, `CHIP_DATA_TRANSFER`, `CHIP_IFC_COMPUTE` |
| `plane_state` | `PLANE_CMD_LATCH`, `PLANE_ARRAY_BUSY`, `PLANE_DATA_REGISTER`, `PLANE_IFC_COMPUTE` |

## Timing Sources

The backend derives cycle lengths from active platform parameters:

```text
cycle_ns = 1e9 / ifc_frequency_hz
onfi_cycle_ns = 1000 / onfi_rate_MTps
command_address_cycles = ceil(7 * onfi_cycle_ns / cycle_ns)
```

`READ_COMPUTE` uses:

```text
vector_cycles = ceil((tile_width / channels) / channel_bandwidth_Bps / cycle_ns)
array_cycles = ceil(array_read_us * 1000 / cycle_ns)
ifc_compute_cycles =
    ceil((tile_height * tile_width / channels)
         / (compute_cores_per_die * ifc_frequency_hz * ifc_ops_per_core_cycle)
         / cycle_ns)
```

`READ_SLICE` uses:

```text
data_cycles =
    ceil((page_bytes / IFC_READ_SLICES_PER_REQUEST)
         / channel_bandwidth_Bps
         / cycle_ns)
```

## What Tests Check

`make test` parses `ssdsim_ifc_trace.csv` and checks that:

- every stage has positive duration and `end_cycle - start_cycle == duration_cycles`;
- `READ_COMPUTE` includes C/A transfer, vector transfer, array read, and IFC compute stages;
- `READ_SLICE` includes C/A transfer and data transfer stages;
- stage names match the expected SSDsim-style channel/chip/subrequest states.

## Boundary

This backend closes an important gap between a pure formula model and a controller trace: the extended commands now pass through an SSDsim-derived state sequence with explicit channel/chip/plane resource updates.

It still should not be described as a full reproduction of the private Cambricon-LLM SSDsim fork. A full fork would need the original SSDsim event loop, FTL mapping, queue management, garbage collection, normal read/write compatibility tests, and validation against the authors' private traces.
