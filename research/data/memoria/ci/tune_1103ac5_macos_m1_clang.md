<!-- CI run 36684344253, job "tune / macos-latest", commit 1103ac5 (partnerSpread). Times relative to the partner buffer (unlimited budget). Virtual M1: noisy between configurations (the partner column itself moves 226-292 ms at 1e7 random). -->

### L = 10, block = 512 B (block buffers 513 KB) - Apple M1 (Virtual), L1d 128 KiB, L2 12 MiB, Clang

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 14.81 | +18% | +90% | -1% | 29.20 | 0.60x |
| 1000000 | nearly_sorted | 7.81 | +49% | +122% | +19% | 11.25 | 1.04x |
| 1000000 | adversarial | 55.41 | -13% | +20% | -9% | 27.56 | 1.74x |
| 1000000 | few_outliers | 7.97 | +33% | +116% | +4% | 19.26 | 0.55x |
| 1000000 | low_entropy | 39.55 | +16% | +96% | -0% | 30.93 | 1.48x |
| 10000000 | random | 292.10 | -19% | +58% | -12% | 357.94 | 0.66x |
| 10000000 | nearly_sorted | 108.39 | +17% | +94% | +61% | 137.41 | 0.92x |
| 10000000 | adversarial | 264.70 | +7% | +131% | +2% | 327.45 | 0.87x |
| 10000000 | few_outliers | 68.57 | +46% | +237% | +104% | 223.47 | 0.45x |
| 10000000 | low_entropy | 369.55 | +18% | +84% | +13% | 251.28 | 1.74x |

### L = 9, block = 512 B (block buffers 257 KB) - Apple M1 (Virtual), L1d 128 KiB, L2 12 MiB, Clang

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 16.89 | +20% | +82% | -11% | 28.01 | 0.73x |
| 1000000 | nearly_sorted | 10.36 | +37% | +109% | +5% | 14.30 | 0.99x |
| 1000000 | adversarial | 50.28 | +8% | +58% | +1% | 32.33 | 1.68x |
| 1000000 | few_outliers | 7.17 | +62% | +133% | -6% | 16.24 | 0.71x |
| 1000000 | low_entropy | 35.82 | +44% | +99% | +13% | 26.30 | 1.97x |
| 10000000 | random | 280.33 | -7% | +67% | -18% | 346.76 | 0.75x |
| 10000000 | nearly_sorted | 86.56 | +61% | +218% | +37% | 133.89 | 1.04x |
| 10000000 | adversarial | 209.23 | +8% | +156% | +2% | 333.09 | 0.68x |
| 10000000 | few_outliers | 70.87 | +72% | +313% | +63% | 238.35 | 0.51x |
| 10000000 | low_entropy | 347.02 | +31% | +112% | +11% | 234.74 | 1.93x |

### L = 8, block = 512 B (block buffers 129 KB) - Apple M1 (Virtual), L1d 128 KiB, L2 12 MiB, Clang

| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |
|---|---|---|---|---|---|---|---|
| 1000000 | random | 16.50 | +5% | +60% | -18% | 26.06 | 0.67x |
| 1000000 | nearly_sorted | 7.07 | +37% | +127% | +0% | 9.10 | 1.06x |
| 1000000 | adversarial | 51.16 | +11% | +37% | -5% | 31.13 | 1.83x |
| 1000000 | few_outliers | 6.53 | +36% | +131% | +2% | 16.27 | 0.55x |
| 1000000 | low_entropy | 38.13 | +38% | +92% | -3% | 24.06 | 2.19x |
| 10000000 | random | 225.86 | +5% | +96% | -9% | 308.25 | 0.77x |
| 10000000 | nearly_sorted | 93.09 | +38% | +194% | +33% | 117.10 | 1.10x |
| 10000000 | adversarial | 347.63 | -14% | +109% | -23% | 394.35 | 0.76x |
| 10000000 | few_outliers | 99.91 | +58% | +280% | +33% | 293.85 | 0.54x |
| 10000000 | low_entropy | 325.81 | +45% | +120% | +39% | 240.33 | 1.97x |
