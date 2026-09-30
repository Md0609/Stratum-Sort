<!-- CI run 36684344253, job "tune / ubuntu-latest", commit 1103ac5 (partnerSpread). Times relative to the partner buffer (unlimited budget). -->

### L = 10, block = 512 B (block buffers 513 KB) - AMD EPYC 7763 64-Core Processor, L1d 32K, L2 512K, L3 32768K, GCC

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 16.84 | +76% | +54% | -0% | 64.12 | 0.46x |
| 1000000 | nearly_sorted | 7.70 | +161% | +89% | +7% | 11.19 | 1.80x |
| 1000000 | adversarial | 45.42 | +37% | +35% | +1% | 63.26 | 0.98x |
| 1000000 | few_outliers | 7.03 | +171% | +81% | +9% | 28.68 | 0.66x |
| 1000000 | low_entropy | 38.82 | +80% | +54% | +1% | 60.82 | 1.15x |
| 10000000 | random | 204.65 | +59% | +46% | +32% | 743.02 | 0.44x |
| 10000000 | nearly_sorted | 89.91 | +130% | +69% | +90% | 134.77 | 1.54x |
| 10000000 | adversarial | 222.42 | +55% | +45% | +30% | 741.08 | 0.46x |
| 10000000 | few_outliers | 73.01 | +155% | +171% | +103% | 210.46 | 0.89x |
| 10000000 | low_entropy | 396.66 | +87% | +56% | +32% | 651.35 | 1.14x |

### L = 9, block = 512 B (block buffers 257 KB) - AMD EPYC 7763 64-Core Processor, L1d 32K, L2 512K, L3 32768K, GCC

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 16.06 | +84% | +62% | -0% | 64.08 | 0.46x |
| 1000000 | nearly_sorted | 7.62 | +170% | +90% | -0% | 11.13 | 1.85x |
| 1000000 | adversarial | 45.69 | +37% | +34% | -0% | 63.21 | 0.99x |
| 1000000 | few_outliers | 7.08 | +179% | +79% | -1% | 28.70 | 0.69x |
| 1000000 | low_entropy | 38.83 | +86% | +57% | -0% | 60.79 | 1.19x |
| 10000000 | random | 205.02 | +97% | +74% | +33% | 743.38 | 0.54x |
| 10000000 | nearly_sorted | 91.06 | +203% | +143% | +70% | 135.50 | 2.04x |
| 10000000 | adversarial | 222.15 | +91% | +70% | +32% | 740.09 | 0.57x |
| 10000000 | few_outliers | 72.90 | +255% | +244% | +94% | 211.07 | 1.23x |
| 10000000 | low_entropy | 404.72 | +109% | +64% | +44% | 651.51 | 1.30x |

### L = 8, block = 512 B (block buffers 129 KB) - AMD EPYC 7763 64-Core Processor, L1d 32K, L2 512K, L3 32768K, GCC

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 16.74 | +78% | +58% | -2% | 64.16 | 0.46x |
| 1000000 | nearly_sorted | 7.70 | +168% | +88% | +0% | 11.24 | 1.84x |
| 1000000 | adversarial | 46.16 | +37% | +36% | -1% | 63.20 | 1.00x |
| 1000000 | few_outliers | 7.11 | +177% | +79% | -1% | 28.92 | 0.68x |
| 1000000 | low_entropy | 38.82 | +91% | +59% | -0% | 60.78 | 1.22x |
| 10000000 | random | 206.83 | +96% | +72% | +32% | 756.59 | 0.54x |
| 10000000 | nearly_sorted | 90.59 | +205% | +144% | +72% | 135.13 | 2.04x |
| 10000000 | adversarial | 221.60 | +92% | +70% | +34% | 739.83 | 0.57x |
| 10000000 | few_outliers | 73.17 | +252% | +242% | +88% | 210.71 | 1.22x |
| 10000000 | low_entropy | 395.94 | +122% | +72% | +52% | 651.15 | 1.35x |
