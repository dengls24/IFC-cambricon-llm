# Figure 9 Reproduction Report

This report compares the standalone C IFC simulator against the Cambricon-LLM Figure 9 W8A8 decode-speed points.

## Summary

- Rows: 21
- Mean absolute relative error: 8.341%
- Max absolute relative error: 14.618%
- Worst case: llama2_70b on cam_llm_l

## Comparison

| Model | Platform | Paper token/s | Sim token/s | Error | TPOT ms | Tile HxW | Alpha |
|---|---|---:|---:|---:|---:|---:|---:|
| OPT-6.7B | Cambricon-LLM-S | 3.600 | 3.666 | +1.83% | 272.796 | 256x2048 | 0.355 |
| OPT-6.7B | Cambricon-LLM-M | 11.000 | 10.047 | -8.66% | 99.532 | 362x5793 | 0.356 |
| OPT-6.7B | Cambricon-LLM-L | 36.300 | 31.115 | -14.28% | 32.139 | 512x16384 | 0.357 |
| OPT-13B | Cambricon-LLM-S | 1.900 | 1.878 | -1.14% | 532.405 | 256x2048 | 0.355 |
| OPT-13B | Cambricon-LLM-M | 4.700 | 5.247 | +11.63% | 190.597 | 362x5793 | 0.356 |
| OPT-13B | Cambricon-LLM-L | 14.200 | 16.017 | +12.80% | 62.434 | 512x16384 | 0.357 |
| OPT-30B | Cambricon-LLM-S | 0.800 | 0.799 | -0.08% | 1250.980 | 256x2048 | 0.355 |
| OPT-30B | Cambricon-LLM-M | 2.500 | 2.308 | -7.68% | 433.282 | 362x5793 | 0.356 |
| OPT-30B | Cambricon-LLM-L | 7.600 | 6.731 | -11.43% | 148.562 | 512x16384 | 0.357 |
| OPT-66B | Cambricon-LLM-S | 0.400 | 0.350 | -12.39% | 2853.710 | 256x2048 | 0.355 |
| OPT-66B | Cambricon-LLM-M | 1.200 | 1.059 | -11.79% | 944.698 | 362x5793 | 0.356 |
| OPT-66B | Cambricon-LLM-L | 2.600 | 2.836 | +9.06% | 352.662 | 512x16384 | 0.357 |
| LLaMA2-7B | Cambricon-LLM-S | 3.600 | 3.644 | +1.23% | 274.405 | 256x2048 | 0.355 |
| LLaMA2-7B | Cambricon-LLM-M | 10.400 | 9.991 | -3.93% | 100.086 | 362x5793 | 0.356 |
| LLaMA2-7B | Cambricon-LLM-L | 34.000 | 30.959 | -8.95% | 32.301 | 512x16384 | 0.357 |
| LLaMA2-13B | Cambricon-LLM-S | 1.900 | 1.878 | -1.14% | 532.405 | 256x2048 | 0.355 |
| LLaMA2-13B | Cambricon-LLM-M | 4.700 | 5.247 | +11.63% | 190.597 | 362x5793 | 0.356 |
| LLaMA2-13B | Cambricon-LLM-L | 14.000 | 16.017 | +14.41% | 62.434 | 512x16384 | 0.357 |
| LLaMA2-70B | Cambricon-LLM-S | 0.300 | 0.337 | +12.41% | 2965.466 | 256x2048 | 0.355 |
| LLaMA2-70B | Cambricon-LLM-M | 1.000 | 1.041 | +4.06% | 960.942 | 362x5793 | 0.356 |
| LLaMA2-70B | Cambricon-LLM-L | 3.400 | 2.903 | -14.62% | 344.473 | 512x16384 | 0.357 |

## Sanity Checks

- Cambricon-LLM-S derives a 256x2048 tile, matching the paper's tile-size study.
- The read-compute workload fraction is about 0.355 across the three Table II platforms.
- No-read-slicing and no-tiling rows are produced as controlled ablations from the same C model path.
