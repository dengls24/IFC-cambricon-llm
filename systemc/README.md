# Hardware Cycle Model

This directory contains the hardware-cycle modeling path for the IFC controller.

The current implementation is `ifc_hw_cycle_model.cpp`. It is a C++17 hardware-cycle model that mirrors the SSDsim-derived event loop and is intentionally written so it can be migrated to SystemC modules when a SystemC library is available.

The local build does not require SystemC:

```bash
make hw-cycle
```

The target writes:

- `results/hw_cycle_trace.csv`
- `results/hw_cycle_stats.csv`
- `results/hw_cycle_compare.csv`

`hw_cycle_compare.csv` checks the hardware-cycle model against the C SSDsim-derived event backend for:

- total event count;
- completed command count;
- last event cycle.

## SystemC Migration Point

The C++ model is structured around the same concepts a SystemC model would expose as modules and processes:

| Current C++ concept | SystemC mapping |
|---|---|
| command vector | input command FIFO |
| `ResourceState` | channel/chip/plane/IFC resource modules |
| event loop | `SC_METHOD` or `SC_THREAD` driven by `sc_clock` |
| stage issue/complete | valid/ready handshake or event notification |
| CSV trace | CSV plus optional VCD trace |

When SystemC is installed, the next step is to add `ifc_hw_cycle_systemc.cpp` using `sc_module`, `sc_clock`, and optional VCD tracing while keeping the CSV schema compatible with `ifc_hw_cycle_model.cpp`.

## Boundary

This model is a hardware-cycle audit path. It strengthens the simulator by providing an independently compiled cycle model that cross-checks the C SSDsim-derived backend. It is not a replacement for the Figure 9 timing path and does not claim equivalence with the private Cambricon-LLM simulator.
