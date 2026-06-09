# Hardware Cycle And Replay Models

This directory contains three audit paths for the IFC controller command stream.

## Dependency-free C++ checker

```bash
make hw-cycle
```

This target builds `ifc_hw_cycle_model.cpp`. It mirrors the SSDsim-derived C event loop and writes:

- `results/hw_cycle_trace.csv`
- `results/hw_cycle_stats.csv`
- `results/hw_cycle_compare.csv`

## SystemC replay checker

```bash
make systemc-cycle
```

This target builds `ifc_hw_cycle_systemc.cpp` against `libsystemc`. The model uses an `sc_module` with an `SC_THREAD`; each stage transition advances SystemC time with `wait(sc_time(delta_cycles * cycle_ns, SC_NS))`.

This is a replay/equivalence checker. It intentionally shares the command generation, stage durations, and resource-conflict rules with `ifc_hw_cycle_model.cpp`; the SystemC kernel is used to replay those same event transitions in simulation time. Therefore, exact agreement with the C event backend is expected by construction. It should not be interpreted as a more detailed hardware model or as evidence that the timing is closer to RTL.

It writes:

- `results/systemc_cycle_trace.csv`
- `results/systemc_cycle_stats.csv`
- `results/systemc_cycle_compare.csv`

## SystemC component model

```bash
make systemc-component
```

This target builds `ifc_component_systemc.cpp` against `libsystemc`. It is not a replay of the C event loop. It separates the simulation into:

- `IfcComponentController`: command state, resource reservation, stage issue, completion handling, and trace emission;
- `IfcExecutionFabric`: timed SystemC stage execution with dynamic processes;
- ONFI-bus, plane-array, and IFC-compute module classes in the execution fabric;
- FIFO channels between controller and execution fabric;
- VCD signals for active commands, completed commands, and event count.

It writes:

- `results/systemc_component_trace.csv`
- `results/systemc_component_stats.csv`
- `results/systemc_component_compare.csv`
- `results/systemc_component_modules.csv`
- `results/systemc_component.vcd`

The current component model is still command-cycle architecture modeling rather than RTL. It validates module-level timing, resource-busy checks, and completion ordering, but it does not model bit-level ONFI signaling, internal NAND analog behavior, firmware queues, ECC, or FTL.

The local default assumes:

```bash
SYSTEMC_HOME=../.ifc_systemc/systemc_sysroot/usr
```

Use `SYSTEMC_HOME=/usr` when `libsystemc-dev` is installed system-wide. Without root access, install a local copy with:

```bash
tools/setup_systemc_local.sh
```

## What Is Compared

All audit paths compare against `results/ssdsim_ifc_event_stats.csv` for:

- total event count;
- completed command count;
- last event cycle.

For the checked default run, the SystemC replay path currently reports:

```text
events: 1536
completed_commands: 256
last_event_cycle: 316207
```

and `results/systemc_cycle_compare.csv` reports `PASS` for all three metrics.

For the checked default run, the SystemC component path currently reports:

```text
events: 1536
completed_commands: 256
last_event_cycle: 316293
last_event_ns: 316292.500000
fabric_busy_violations: 0
controller_timing_violations: 0
```

The corresponding C backend result is `last_event_cycle=316207` and `last_event_ns=316207.000000`. Therefore the component model reports a bounded non-zero timing delta of `+86` rounded cycles, or `+85.500000 ns` actual SystemC time, which is `0.027039%` of the C backend final time. `results/systemc_component_compare.csv` reports exact agreement for event count and completed command count, and bounded `PASS` for final timing.

## Modeling Boundary

The SystemC replay checker proves that the same IFC command stream can be executed through a SystemC kernel without changing event ordering or resource accounting. The component model goes further by splitting controller and execution fabric behavior into SystemC modules, applying a 2.5 ns module clock, limiting each dispatch round to eight stage issues, using a finite eight-entry issue FIFO, and quantizing stage service time to module-clock boundaries. Those hardware-module boundaries explain why the component model is not bit-identical to the C event backend.

It is still a command-cycle model. A more hardware-realistic SystemC model would need lower-level channel arbiters, ONFI bus signal timing, chip/die/plane state machines, IFC compute datapaths, firmware queues, valid/ready handshakes on every path, and broader VCD signal coverage. This repository does not claim RTL equivalence, full SSD firmware behavior, or line-by-line equivalence with the authors' private SSDsim fork.
