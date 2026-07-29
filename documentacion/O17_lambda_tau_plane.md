# O17 — Barrido del plano (λ, t)

**CERRADA. (λ=32, t=64) es un óptimo práctico. El algoritmo no se toca.**

---

## 1. Rangos, justificados antes de medir

### λ — cota inferior: la L2

La fase `distribute` escribe en `bins = n/λ` flujos simultáneos; cada flujo
mantiene viva una línea de caché, luego la huella es `bins × 64 B`. La L2
del M4 es de 4 MiB.

| λ | bins | huella | vs L2 | penalización medida (paso 4b de v9) |
|---|---|---|---|---|
| 8 | 125.000 | 7,63 MB | 1,91x | **+43,1 %** |
| 12 | 83.333 | 5,09 MB | 1,27x | +15,8 % |
| **16** | 62.500 | 3,81 MB | **0,95x** | +12,9 % |
| 24 | 41.666 | 2,54 MB | 0,64x | −0,2 % |
| 32 | 31.250 | 1,91 MB | 0,48x | −0,5 % |
| 64 | 15.625 | 0,95 MB | 0,24x | 0 % |

**λ ≥ 16.** No es una suposición: por debajo, la huella desborda la L2 y v9
ya midió la penalización.

### λ — cota superior y t — cotas

- **λ ≤ 64**: el barrido fusionado de v9 midió +14,7 % en 96 y +30,6 % en
  128. Además λ ≤ t.
- **t ≥ λ**: un bin de tamaño λ tiene que poder ser hoja.
- **t ≤ 128**: subir t sólo sirve si aún quedan bins por encima de t. Con
  ocupación Poisson(λ):

| λ | P(X>32) | P(X>48) | P(X>64) | P(X>96) | P(X>128) |
|---|---|---|---|---|---|
| 16 | 1,3e−04 | 2,7e−11 | ~0 | ~0 | ~0 |
| 24 | 4,7e−02 | 5,0e−06 | 3,7e−12 | ~0 | ~0 |
| 32 | 4,5e−01 | 3,1e−03 | 2,0e−07 | ~0 | ~0 |
| 48 | — | 4,6e−01 | 1,1e−02 | 3,5e−10 | ~0 |
| 64 | — | — | 4,7e−01 | 7,4e−05 | 6,3e−13 |

Con λ=32, `P(X>64) ≈ 2e−7`: subir t de 64 a 96 **no puede** vaciar más
`refine()` en datos uniformes. Se prueba igualmente hasta 128 porque en
datos **no** uniformes los bins superan con mucho la predicción de Poisson.

**Rejilla: 21 configuraciones** con λ ∈ {16,24,32,48,64}, t ∈ {λ…128}.

## 2. Protocolo

Idéntico al de todos los pasos: build de **producción**, las 21
configuraciones recorridas **dentro de cada repetición** para que la deriva
afecte por igual, 3 rondas independientes × 5 repeticiones, corrección
verificada en cada ordenación.

## 3. Dos controles antes de leer los resultados

### 3.1 Dos datasets no pueden responder

Contadores en las 21 configuraciones:

| Dataset | bins / comparaciones |
|---|---|
| `ManyRepeated` | **5 / 0 en las 21** |
| `SmallRangeManyEl` | **100 / 0 en las 21** |

Sus bins están acotados por el span (4 y 99), así que λ no los toca y su
trabajo es bit a bit idéntico. **Sus columnas son ruido puro y se
excluyen.** Es la confirmación cuantitativa de O5.

### 3.2 El barrido contiene réplicas internas — y miden el ruido

Agrupando las configuraciones por su firma de contadores **por dataset**,
aparecen grupos que ejecutan trabajo idéntico. Su desacuerdo en tiempo es
el suelo de ruido, medido sin suposiciones:

| Dataset | grupos | peor desacuerdo entre réplicas | ejemplo |
|---|---|---|---|
| AdversarialPeeling | 4 | 0,9 % | (24,64) vs (24,24) |
| RandomUniform | 4 | 1,9 % | (48,96) vs (48,128) |
| FullRangeExtremes | 4 | 2,1 % | (16,64) vs (16,48) |
| HugeRangeFewEl | 5 | 2,3 % | (16,96) vs (16,48) |
| SortedAscending | 5 | 2,5 % | (16,48) vs (16,64) |
| SortedDescending | 5 | 2,7 % | (16,32) vs (16,64) |
| **Concentrated** | 5 | **12,0 %** | (16,96) vs (16,48) |

**`Concentrated` tiene un suelo de ruido del 12 %**: dos configuraciones
que hacen exactamente el mismo trabajo difieren un 12 % en el reloj.
Cualquier lectura de esa columna por debajo del 12 % es inservible — y
explica por qué (16,48) marcaba +12,5 % y (16,96) +0,4 % siendo idénticas.

Umbral de decisión por dataset: **max(6 %, suelo de ruido medido)**.

## 4. Resultado del barrido

Con esos umbrales y sin las dos columnas invariantes, sobreviven cuatro
configuraciones: **(16,32), (16,96), (24,96), (32,96)**.

Todo lo demás falla claramente. En particular, las configuraciones con
λ = t (16,16), (24,24), (32,32), (48,48), (64,64) —es decir, la forma de
v9— empeoran entre un 13 % y un 29 % en los cuatro datasets sin
redundancia, lo que **reconfirma el paso 8 por una vía independiente**.

## 5. Falsación: tres de los cuatro supervivientes son sobreajuste

(24,96), (32,96) y (32,128) ganan **únicamente** en `AdversarialPeeling`.
Ese dataset lo construí yo contra `target = 64`, produciendo grupos de
~107 elementos. Subir t por encima de 96 hace que esos grupos sean hoja
antes. Para comprobar si es una propiedad del algoritmo o del generador,
**se reconstruye el adversario contra cada t**:

| adversario | (32,64) actual | (24,96) | (32,96) | (32,128) | (16,96) |
|---|---|---|---|---|---|
| core=64 | 37,5 | 25,6 | 25,7 | **14,8** | 26,2 |
| core=96 | 38,1 | 39,4 | 37,9 | 27,2 | 39,4 |
| core=128 | 39,3 | 40,2 | 39,2 | **39,3** | 41,1 |
| core=160 | 40,1 | 40,3 | 40,1 | 39,9 | 41,9 |

**La ganancia desaparece por completo en cuanto el adversario se
reconstruye.** (32,128) pasa de −60 % a exactamente empatado. No es una
propiedad del algoritmo: es sobreajuste al parámetro con el que construí
el generador. Los tres candidatos quedan descartados.

## 6. Decisión final sobre el único candidato real

Eliminado el sobreajuste, sólo λ=16 tiene un efecto real. Como t es inerte
para λ=16 (`P(Poisson(16)>48) = 2,7e−11`), (16,48), (16,64) y (16,96) son
la misma configuración. Medición focalizada, **6 rondas alternadas × 15
repeticiones**:

| Dataset | (32,64) | (16,64) | Δ | |
|---|---|---|---|---|
| **RandomUniform** | 10,59 | **9,92** | **−6,3 %** | separados |
| HugeRangeFewEl | 10,47 | 9,86 | −5,8 % | separados |
| FullRangeExtremes | 10,58 | 9,97 | −5,7 % | separados |
| NormalGaussian | 11,75 | 11,41 | −2,9 % | separados |
| Concentrated | 4,82 | 4,86 | +0,9 % | se solapan |
| SortedDescending | 4,78 | 4,99 | +4,5 % | separados |
| **SortedAscending** | 4,25 | **4,52** | **+6,5 %** | separados |

**Mejora ≥6 %: `RandomUniform` (−6,3 %). Empeora ≥6 %:
`SortedAscending` (+6,5 %).** El criterio exige mejorar sin empeorar por
encima del umbral. **No se cumple.**

Y el resto de mejoras (−5,7 % a −5,8 %) quedan por debajo del umbral: λ=16
compra menos comparaciones (4,98 M frente a 8,99 M en `RandomUniform`) a
cambio de duplicar los bins, y la huella de dispersión sube a 0,95x de la
L2. Los dos efectos casi se cancelan.

## 7. Conclusión

**Ninguna configuración del plano supera claramente a (32, 64).** Se cierra
O17 y se mantiene la configuración actual, ahora con respaldo sistemático:
21 puntos, umbrales derivados de réplicas internas, y el único candidato
serio descartado por medición focalizada.

### 7.1 Hallazgo estructural: t no es un parámetro que haya que ajustar

Con λ=32, los contadores de (32,64), (32,96) y (32,128) son **idénticos en
todos los datasets reales**. `t` no tiene ningún efecto una vez está por
encima de la cola de Poisson.

**El parámetro que importa es λ. `t` sólo necesita una cota inferior
segura, no un valor ajustado.** `t = 64` con `λ = 32` está a ~5,7σ de la
media de ocupación, y cualquier `t ≥ 64` es equivalente. Eso simplifica el
espacio de diseño de dos parámetros a uno y medio, y conviene dejarlo
escrito: futuras iteraciones no deben perder tiempo barriendo t.

### 7.2 Limitaciones declaradas

- Todo es a **n = 10⁶** y en esta máquina. La cota inferior de λ depende de
  la L2 (4 MiB), luego **λ óptimo escala con n y con el tamaño de caché**:
  la condición es `n·64/λ ≲ L2`. Para n = 10⁷ el λ mínimo sería ~160.
  **La configuración (32,64) es óptima para este tamaño y esta máquina, no
  universalmente.**
- `Concentrated` no es decidible por reloj a este número de repeticiones
  (12 % de suelo de ruido). Si alguna decisión futura depende de ese
  dataset, hará falta otro instrumento.
