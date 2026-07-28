# Review de complejidad de DRS v9

**Estado del algoritmo tras cerrar v9 (etiqueta `v9`, commit `7093ab2`).**
Todo enunciado de este documento es analítico sobre el código actual o
**[MEDIDO]** en esta máquina. No hay ninguna afirmación heredada.

---

## 1. Resumen

**DRS v9 es Θ(n) en el peor caso, con una constante explícita y acotada,
para las constantes que efectivamente lleva compiladas.** Esto es más
fuerte de lo que el proyecto había afirmado nunca, y —esto es lo
importante— es cierto **gracias a** `MAX_SUBDIVISION_DEPTH = 6`, no a
pesar de él, que es lo contrario de lo que la especificación inicial de v9
sostenía.

Medición del exponente por regresión log-log, con intervalo de confianza
al 95 %, `n ≥ 10⁴`:

| Dataset | b (tiempo, producción) | b (comparaciones) | prof. máx |
|---|---|---|---|
| **RandomUniformScaled** | 0,953 [0,912 – 0,994] | **0,997 [0,993 – 1,000]** | 1 |
| RandomUniform | 1,058 [1,036 – 1,080] | 0,980 [0,970 – 0,991] | 2 |
| SortedAscending | 1,027 [1,016 – 1,039] | **1,000 [1,000 – 1,000]** | 0 |
| ManyRepeated | 0,980 [0,946 – 1,014] | — | 0 |
| NormalGaussian | 1,025 [0,967 – 1,084] | 0,876 [0,727 – 1,026] | 3 |
| Concentrated | 0,977 [0,952 – 1,002] | 1,017 [0,965 – 1,069] | 3 |
| HugeRangeFewEl | 1,064 [1,036 – 1,091] | 0,997 [0,994 – 1,001] | 1 |
| **AdversarialPeeling** | 1,047 [1,015 – 1,079] | **0,994 [0,993 – 0,995]** | 6 |

**Los contadores deterministas dan exponente 1,00 ± 0,01 en todos los
datasets informativos.** El trabajo algorítmico es lineal. Las
desviaciones del exponente de tiempo (1,02–1,06) son jerarquía de memoria,
no algoritmo: al crecer `n`, el conjunto de trabajo desborda las cachés y
el coste por elemento sube.

## 2. Correcciones metodológicas respecto del ajuste histórico

`analisis/main.cpp` (v5) daba «`T(n)=a·n`, R² > 0,997» con cuatro defectos
que `ANALYSIS_v9_propuesta.md` §3 identificó y que este review corrige:

| Defecto | Corrección aquí |
|---|---|
| Sólo tiempo de pared, teniendo contadores deterministas al lado | Se ajustan **ambos**, por separado |
| Compilado **con** `DRS_ENABLE_METRICS`, cuyo overhead decrece con `n` y sesga el exponente a la baja | Tiempo medido en **producción**; contadores en una pasada aparte de investigación |
| `randomUniform` tiene el **rango fijo** en 10⁶ mientras `n` va de 10² a 5·10⁶: su densidad varía 4 órdenes de magnitud | Se añade `randomUniformScaled`, con rango ∝ `n` y densidad constante. **Es el único dataset con el que tiene sentido estimar un exponente** |
| Elegía entre seis exponentes prefijados por R² | Se **estima** el exponente con su IC al 95 % |

Que `RandomUniform` dé 1,058 y `RandomUniformScaled` 0,953 es la
confirmación directa del tercer defecto: el mismo algoritmo, y el
exponente se mueve 0,1 según si el dataset mantiene la densidad.

*Salvedad honesta:* el IC de `RandomUniformScaled` en tiempo excluye 1,0
por abajo. Un algoritmo de ordenación no puede ser sublineal; lo que dice
ese ajuste es que el modelo `T = a·n^b` **sin término constante** está mal
especificado a estos tamaños — los costes fijos se amortizan al crecer `n`
y curvan el ajuste hacia abajo. Por eso el exponente que hay que creer es
el de los contadores (0,997), no el del reloj.

## 3. Complejidad estructural del código actual

Contabilidad por elemento, leída del `.tpp`:

| Fase | Trabajo | Coste |
|---|---|---|
| `analyze()` | 1 pasada min/max | `n` lecturas |
| `distribute()` | `assign` ×2 (relleno de ceros **muerto**) | `2n` escrituras |
| | conteo: lectura + escritura de `bucketOf` | `n` + `n` |
| | prefijos | `O(b)`, `b = min(⌈n/t⌉, span+1)` |
| | colocación: 2 lecturas + 1 escritura dispersa | `2n` + `n` |
| `refine()` (por nivel, sólo bins que entran) | escaneo min/max + conteo + colocación | `3` lect. + `1` escr. por elemento y nivel |
| `sortLeaf()` | `detectRun` `O(k)` salvo certificado; Insertion `O(k²)` | `O(n·t)` agregado |
| `mergeRefined()` | `memcpy` por hoja | `n` lect. + `n` escr. |

**Memoria auxiliar:** `2n·sizeof(T)` (dos buffers) `+ n·sizeof(size_t)`
(`bucketOf`) `= 24n` bytes para `int64_t`. **[MEDIDO]** 22,89 MB exactos
para `n = 10⁶`, con la métrica ya reparada en el paso 4.

### 3.1 Cota del peor caso, con constante explícita

Sea `w` el ancho de palabra (64), `D = MAX_SUBDIVISION_DEPTH` (6), `t` el
target (64).

**Presupuesto de bits.** Cada nivel de refinamiento consume `log₂(s)` bits
del span observado, con `s = ⌈m/t⌉` para un bin de tamaño `m`. Como el
span tiene a lo sumo `w` bits, un bin sólo puede sobrevivir `D` niveles
degenerados si `D · log₂(m/t) ≤ w`, es decir

```
  m  ≤  m_max = t · 2^(w/D) = 64 · 2^(64/6) ≈ 104.032
```

**[MEDIDO en O8]**, y **`m_max` no depende de `n`**.

**Consecuencia.** El residuo que el tope de profundidad puede entregar a
Introsort está acotado por `min(n, m_max)`. Su coste agregado es

```
  (n / m_max) · m_max · log₂(m_max)  =  n · log₂(m_max)  ≈  17n
```

y el refinamiento cuesta `≤ D·n = 6n`. Sumando la ordenación local
(`≈ t/4 = 16` operaciones por elemento) y las pasadas fijas:

```
  Coste_peor(v9)  =  O( n · (D + log₂ min(n, m_max) + t/4) )  ≈  O(39n)
```

**Es lineal, con constante acotada e independiente de `n`.**

**Verificación experimental:** el exponente de comparaciones de
`AdversarialPeeling` —el dataset construido precisamente para agotar el
tope— es **0,994 [0,993 – 0,995]** sobre tres órdenes de magnitud de `n`.
Si el tope produjese un término `n log n`, el exponente crecería con `n`.
No lo hace. **El argumento de O8 queda confirmado no en un punto, sino en
todo el barrido.**

### 3.2 Límite de validez

La cota depende de que `m_max` sea pequeño frente a `n`. Con `w = 64` y
`D = 6`:

| `target` | `m_max` | ¿acotado frente a n=10⁶? |
|---|---|---|
| 16 | 26.008 | sí |
| **64 (actual)** | **104.032** | **sí** |
| 256 | 416.128 | sí |
| **1024** | **1.664.511** | **NO — reaparece `n log n`** |

**`target ≥ 1024` rompe la linealidad del peor caso.** Está anotado en
`Config.hpp` y es la única restricción dura del algoritmo.

## 4. Dónde se va la constante [MEDIDO]

Reparto por fase del código cerrado, `n = 10⁶`, producción-equivalente:

| Dataset | analyze | distribute | refine | **localSort** | merge |
|---|---|---|---|---|---|
| RandomUniform | 1,1 % | 15,5 % | 16,3 % | **63,0 %** | 3,2 % |
| NormalGaussian | 1,0 % | 13,9 % | 21,0 % | **58,2 %** | 3,5 % |
| HugeRangeFewEl | 1,0 % | 15,8 % | 15,0 % | **64,5 %** | 2,6 % |
| FullRangeExtremes | 1,0 % | 15,8 % | 15,3 % | **64,8 %** | 2,7 % |
| SortedAscending | 3,1 % | **77,7 %** | 2,0 % | 11,3 % | 5,6 % |
| SortedDescending | 2,8 % | **75,1 %** | 1,8 % | 14,6 % | 5,5 % |
| ManyRepeated | 6,7 % | **77,6 %** | 7,2 % | **0,0 %** | 7,3 % |
| SmallRangeManyEl | 8,8 % | **77,1 %** | 8,2 % | **0,1 %** | 5,7 % |
| Concentrated | 2,9 % | **59,4 %** | 32,9 % | 1,4 % | 3,9 % |
| AdversarialPeeling | 0,4 % | 5,3 % | **56,3 %** | 34,7 % | 2,0 % |

Tres regímenes claramente distintos:

1. **Sin redundancia** (`RandomUniform`, `NormalGaussian`, `HugeRangeFewEl`,
   `FullRangeExtremes`): `localSort` domina con **58–65 %**. El coste es
   `n·t/4` de Insertion Sort. Es el régimen del caso medio y **v9 no lo ha
   mejorado en absoluto**.
2. **Con redundancia** (`ManyRepeated`, `SmallRangeManyEl`,
   `SortedAscending/Descending`, `Concentrated`): `distribute` domina con
   **59–78 %**, y `localSort` cae a **0,0–14 %**. Aquí es donde v9
   concentró todas sus mejoras (−23 % a −42 %), y por eso `localSort` en
   `ManyRepeated` marca ahora **0,0 %**: el certificado de ordenado lo
   eliminó por completo.
3. **Adversario**: `refine` domina con 56 %, que es el coste de los seis
   niveles de pelado.

## 5. Qué limita seguir mejorando

| Fase | Peso | Por qué es difícil de reducir |
|---|---|---|
| `localSort` | 58–65 % en el caso medio | El coste es `n·λ/4` y λ = target. El barrido del paso 4b demostró que `target = 64` está a **<2 % del óptimo** de una curva plana entre 24 y 64: por debajo, la huella de dispersión (`bins × 64 B`) desborda la L2 de 4 MiB; por encima, Insertion Sort crece con λ². **La única vía sin explorar es separar λ del umbral de hoja** (paso 8, no ejecutado). |
| `distribute` | 59–78 % en datos redundantes | De sus tres partes, el **relleno de ceros (2n escrituras que nadie lee)** es la única eliminable: techo medido y estable de **9,5–10,1 %** en `ManyRepeated` y `SmallRangeManyEl`. Exige sustituir `std::vector` por almacenamiento con *default-init*. Es O14. |
| `refine` | 15–21 % en el caso medio | Las asignaciones por llamada son 3 por subdivisión: techo medido **5,8–6,2 %**, en el umbral de significación. Es el paso 6 original, descartado. |
| `merge` | 2,6–7,3 % | Ya es `memcpy`. Lo que queda es tráfico de memoria irreducible. |
| `analyze` | 1–9 % | Una pasada obligatoria: toda fórmula posterior depende de min/max. |

## 6. Comparación con `std::sort` y qué significa

`std::sort` de libc++ en M4 es un competidor muy distinto del de la
máquina histórica (`BASELINE_v8.md` §5). El punto débil de DRS no es
algorítmico:

- **`SortedAscending`: ~6x más lento.** libc++ resuelve un millón de
  enteros ya ordenados en 0,73 ms; DRS hace como mínimo tres pasadas
  (contar, colocar, copiar) y **no puede bajar de ahí sin una heurística
  de detección previa** — precisamente el tipo de mecanismo que v6 y v7
  eliminaron por perjudicial. Es una limitación declarada, no un defecto
  por descubrir.
- En datos sin redundancia DRS está entre **0,90x y 1,00x**: gana o
  empata.
- En datos con redundancia v9 mejoró mucho, pero sigue por detrás en los
  casos donde libc++ tiene camino rápido.

## 7. Lo que está demostrado y lo que no

**Demostrado (analítico + medido):**
- Terminación, y profundidad acotada por el presupuesto de bits.
- El índice de bin siempre cae en `[0, s−1]` — por eso v9 pudo eliminar el
  recorte defensivo (Propiedad 2, verificada bajo ASan en 19 casos límite).
- **I-TESELADO**: las hojas cubren `[0,n)` en orden y cada una está ya en
  su posición final. Verificado por aserción activa en los diez datasets.
  Es lo que hace válido el `memcpy` del paso 6.
- Peor caso lineal con constante ≈ 39, para `target < 1024`.

**No demostrado:**
- **El peor caso absoluto.** `AdversarialPeeling` es el peor adversario
  que he sabido construir, y el barrido del tamaño de núcleo (7 puntos)
  no encontró uno peor. Eso no prueba que no exista.
- **El comportamiento para `w > 64`** (enteros de 128 bits) o para
  `target ≥ 1024`. La cota de §3.1 dice qué pasaría; nadie lo ha medido.
- **La transferibilidad a otro hardware.** Todo este documento es de un
  Apple M4 con libc++. El proyecto ya ha aprendido tres veces que los
  resultados no transfieren: `target ≈ 19` ganaba en el Xeon y pierde
  aquí; `distribute` era el 46,8 % allí y es el 15,5 % aquí.

## 8. Puerta de entrada a v10

Por techo medido, en este orden:

1. **Separar λ (ocupación) de `t` (umbral de hoja)** — paso 8 nunca
   ejecutado. Ataca `localSort`, el 58–65 % del caso medio, que v9 no ha
   tocado. Objetivo cuantificado: los **21,4 %** que `refine` consume con
   λ=32, que con λ=32/t=64 deberían desaparecer (`P(Poisson(32) > 64) ≈
   10⁻⁶`). **Criterio ya pre-registrado** en `STEP4b_target_sweep.md` §7,
   con las dos cláusulas.
2. **O14 — eliminar el relleno de ceros.** `2n` escrituras muertas, techo
   **9,5–10,1 %** en dos datasets.
3. **O16 — CC-E**, ya sólo por memoria: 24 MB → 16 MB. Se llevaría por
   delante la mitad de O14 como efecto colateral.

Y una advertencia heredada de v9, la más cara de aprender: **medir el
terreno antes de escribir el criterio, y escribir el criterio contra la
hipótesis, no contra el instrumento disponible.** Ese error se cometió en
cuatro pasos de seis.
