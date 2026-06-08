# Hardware Cycle Models

This directory contains two hardware-cycle audit paths for the IFC controller command stream.

## Dependency-free C++ checker

```bash
make hw-cycle
```

This target builds `ifc_hw_cycle_model.cpp`. It mirrors the SSDsim-derived C event loop and writes:

- `results/hw_cycle_trace.csv`
- `results/hw_cycle_stats.csv`
- `results/hw_cycle_compare.csv`

## SystemC kernel checker

```bash
make systemc-cycle
```

This target builds `ifc_hw_cycle_systemc.cpp` against `libsystemc`. The model uses an `sc_module` with an `SC_THREAD`; each stage transition advances SystemC time with `wait(sc_time(delta_cycles * cycle_ns, SC_NS))`.

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

Both hardware-cycle paths compare against `results/ssdsim_ifc_event_stats.csv` for:

- total event count;
- completed command count;
- last event cycle.

For the checked default run, the SystemC path currently reports:

```text
events: 1536
completed_commands: 256
last_event_cycle: 316207
```

and `results/systemc_cycle_compare.csv` reports `PASS` for all three metrics.

## Modeling Boundary

The SystemC model strengthens the artifact by running the IFC command stream through a true SystemC simulation kernel. It is still a command-level cycle model. It does not claim RTL equivalence, full SSD firmware behavior, or line-by-line equivalence with the authors' private SSDsim fork.
