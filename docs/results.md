# Results

Run:

```bash
python scripts/run_reproduction.py
```

The simulator writes a row for each Figure 9 model/platform point. The key quality target is:

```text
max absolute relative error <= 15%
mean absolute relative error <= 9%
```

These thresholds are enforced by `tests/test_simulator.py`.

Current checked output:

| Metric | Value |
|---|---:|
| Rows | 21 |
| Mean absolute relative error | 8.341% |
| Max absolute relative error | 14.618% |
| Mean relative error | -0.812% |
| Worst case | LLaMA2-70B on Cambricon-LLM-L |

The report in `results/report.md` includes:

- paper decode speed;
- simulated decode speed;
- relative error;
- TPOT;
- derived tile shape;
- read-compute workload fraction.

The CSV in `results/figure9_reproduction.csv` additionally includes the no-read-slicing and no-tiling ablation outputs from the same model path.
