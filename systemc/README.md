# Hardware Cycle And Replay Models

This directory contains two audit paths for the IFC controller command stream.

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

The local default assumes:

```bash
SYSTEMC_HOME=../.ifc_systemc/systemc_sysroot/usr
```

Use `SYSTEMC_HOME=/usr` when `libsystemc-dev` is installed system-wide. Without root access, install a local copy with:

```bash
tools/setup_systemc_local.sh
```

## What Is Compared

Both audit paths compare against `results/ssdsim_ifc_event_stats.csv` for:

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

## Modeling Boundary

The SystemC replay checker strengthens the artifact by proving that the same IFC command stream can be executed through a SystemC kernel without changing event ordering or resource accounting. It is still a command-level replay model. A more hardware-realistic SystemC model would need separate channel arbiters, ONFI bus modules, chip/die/plane state machines, IFC compute units, queues, valid/ready handshakes, and VCD-level signal traces. This repository does not claim RTL equivalence, full SSD firmware behavior, or line-by-line equivalence with the authors' private SSDsim fork.
