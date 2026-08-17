# Línea base de DRS v8 en la máquina actual

**SPEC_v9.md, paso 0. Completado.** Reproducible con `make baseline`.
No se modificó el algoritmo: sólo se midió.

---

## 1. Máquina y compilador

```
Compiler:         Clang 21.0.0 (clang-2100.1.1.101)
Compile flags:    -std=c++17 -O3 -Wall -Wextra -DDRS_ENABLE_METRICS
Architecture:     arm64
Operating system: macOS
CPU:              Apple M4        (L1d 64 KB, L2 4 MB, RAM 16 GB)
```

**Ninguna comparación con los números de `ANALYSIS.md` … `ANALYSIS_v8.md`
es válida:** aquéllos proceden de un Xeon x86_64 con GCC 13.3 sobre Linux
y libstdc++. Ésta es arm64 con Apple clang y libc++. Ver §5.

## 2. Reproducibilidad

Tres ejecuciones independientes del binario, 7 repeticiones por punto:

- **Contadores deterministas idénticos byte a byte en las tres**
  (comparaciones, bins, subdivisiones, profundidad, uso de algoritmo).
  Confirma lo que `BenchmarkRunner.hpp` afirma: DRS no tiene
  aleatoriedad propia.
- **Dispersión del tiempo mediano entre ejecuciones: 1,5 % – 5,9 %.**

| Dataset | run A | run B | run C | mediana | dispersión |
|---|---|---|---|---|---|
| RandomUniform | 13,96 | 13,65 | 14,02 | **13,96** | 2,7 % |
| SortedAscending | 4,39 | 4,28 | 4,42 | **4,39** | 3,2 % |
| SortedDescending | 4,86 | 4,74 | 4,81 | **4,81** | 2,5 % |
| ManyRepeated | 4,22 | 4,40 | 4,34 | **4,34** | 4,1 % |
| NormalGaussian | 14,79 | 14,07 | 14,49 | **14,49** | 5,0 % |
| Concentrated | 6,66 | 6,75 | 6,89 | **6,75** | 3,4 % |
| SmallRangeManyEl | 3,37 | 3,50 | 3,30 | **3,37** | 5,9 % |
| HugeRangeFewEl | 14,07 | 13,69 | 14,09 | **14,07** | 2,8 % |
| FullRangeExtremes | 20,68 | 20,18 | 20,29 | **20,29** | 2,5 % |
| AdversarialPeeling | 38,72 | 38,15 | 38,55 | **38,55** | 1,5 % |

**Umbral de significación adoptado para los pasos 1–6: una diferencia de
tiempo cuenta como real sólo si supera el 6 %.** Por debajo, hay que
decidir con contadores deterministas, no con el reloj.

## 3. Línea base, n = 1.000.000, target = 64

| Dataset | DRS(ms) | sd | std::sort(ms) | ratio | prof | maxHoja | Intro | Quick | Comparaciones |
|---|---|---|---|---|---|---|---|---|---|
| RandomUniform | 13,96 | 0,54 | 15,29 | **0,91x** | 1 | 64 | 0 | 0 | 11.995.911 |
| SortedAscending | 4,39 | 0,38 | 0,73 | **6,02x** | 0 | 64 | 0 | 0 | 984.375 |
| SortedDescending | 4,86 | 0,13 | 1,23 | **3,96x** | 0 | 64 | 0 | 0 | 984.375 |
| ManyRepeated | 4,22 | 0,35 | 2,40 | 1,76x | 0 | 200.812 | 0 | 0 | 999.995 |
| NormalGaussian | 14,79 | 0,29 | 14,84 | 1,00x | 2 | 64 | 0 | 0 | 11.070.493 |
| Concentrated | 6,66 | 0,22 | 5,51 | 1,21x | 1 | 10.028 | 0 | 0 | 993.445 |
| SmallRangeManyEl | 3,37 | 0,11 | 5,19 | **0,65x** | 0 | 10.205 | 0 | 0 | 999.900 |
| HugeRangeFewEl | 14,07 | 0,16 | 15,62 | **0,90x** | 1 | 64 | 0 | 0 | 12.398.687 |
| *FullRangeExtremes* | *20,68* | *0,40* | *15,69* | *1,32x* | *3* | *64* | *0* | *0* | *12.418.718* |
| *AdversarialPeeling* | *38,72* | *0,55* | *15,55* | ***2,49x*** | *6* | *100* | *0* | ***9.346*** | *5.719.963* |

*(en cursiva, los dos datasets añadidos en este paso)*

Los datos de n = 100.000 están en la salida de `make baseline`; la forma
es la misma.

## 4. Los dos defectos predichos por SPEC_v9

### D1 — Desbordamiento de rango: **observado, pero NO como predije**

`SPEC_v9.md` §11 afirmaba que el dataset de rango completo «debe exhibir
el defecto», con la expectativa implícita de una hoja gigante cayendo a
Introsort. **Eso no ocurre, y mi propia revisión adversaria ya lo había
demostrado** (`REVIEW_refinamiento_terminal.md`, Parte 0.1: el colapso no
puede encadenarse). Escribí el criterio del paso 0 a partir del
planteamiento de la especificación en vez de a partir de la demostración.
La predicción era incorrecta; el defecto es real pero más leve.

Comparación pareada — dos entradas uniformes y dispersas sobre un rango
enorme, que sólo difieren en si el span desborda:

| | control (span ≈ 2⁶³) | test (span = 2⁶⁴−1) |
|---|---|---|
| tiempo mediano | 14,23 ms | **20,41 ms** |
| comparaciones | 12,40 M | 12,42 M |
| profundidad máxima | 1 | **3** |
| hoja más grande | 64 | 64 |
| `workByDepth` | `d1=520.559` | `d1=1.000.000  d2=999.999  d3=515.985` |

**`workByDepth` es la prueba directa.** En el control, la mitad de los
elementos se reprocesa una vez. En el test, **el array entero se
reprocesa dos veces** (`d1 = n`, `d2 = n−1`) antes de comportarse con
normalidad, y pierde exactamente un elemento entre d1 y d2: el
`INT64_MIN` plantado, único valor cuyo desplazamiento es 0. Es
literalmente el patrón de pelado que predice la teoría.

**Coste medido: +43 % de tiempo a igualdad de comparaciones (12,40 M vs
12,42 M).** Es decir, ~2 pasadas O(n) desperdiciadas — exactamente la
cota que la revisión demostró, ni más ni menos.

**Conclusión operativa sin cambios:** el prerrequisito de aritmética de
span (SPEC_v9 §2.1) sigue siendo necesario, y ahora está cuantificado.
Lo que cambia es la justificación: no es «evita un peor caso
catastrófico», es «elimina un 43 % de sobrecoste en entradas de rango
completo, y elimina la única violación conocida del Lema 1, de la que
depende que quitar el tope de profundidad sea seguro».

### D2 — Peor caso por pelado: **observado como se predijo**

```
dataset AdversarialPeeling, n=1.000.000, target=64
  bins totales:        71.701
  subdivisiones:       56.076
  hoja más grande:     100
  profundidad máxima:  6   (= MAX_SUBDIVISION_DEPTH)
  hojas a QuickSort:   9.346
  ordenado correcto:   sí
```

**El tope de profundidad se alcanza y 9.346 hojas caen a un algoritmo de
comparación** — bins que el refinamiento podría haber terminado él solo.
Es el dataset más lento del proyecto respecto de `std::sort` (2,49x), y
lo es **sin ser el que más comparaciones hace**: 5,72 M frente a los
12,00 M de `RandomUniform`, y aun así tarda 2,8 veces más. El coste no
está en comparar, está en las 6 pasadas de refinamiento sobre casi todo
el array.

Esto es exactamente la ruta que CC1 y CC7 eliminan. **Es la primera
verificación experimental del peor caso de DRS en la historia del
proyecto**: ninguno de los ocho datasets anteriores lo activa, así que
todas las afirmaciones previas sobre el peor caso —incluidas las de
`ANALYSIS_v9_propuesta.md`— estaban sin comprobar.

## 5. Qué transfiere de la máquina histórica y qué no

Ratio DRS / `std::sort`, n = 1.000.000:

| Dataset | Xeon + GCC (v7 §9) | M4 + clang (aquí) | ¿transfiere? |
|---|---|---|---|
| RandomUniform | 0,94x | 0,91x | **sí** |
| NormalGaussian | 1,01x | 1,00x | **sí** |
| HugeRangeFewEl | 0,92x | 0,90x | **sí** |
| SmallRangeManyEl | 0,90x | 0,65x | mejora |
| Concentrated | 1,27x | 1,21x | aproximadamente |
| ManyRepeated | 1,29x | 1,76x | **empeora** |
| SortedDescending | 2,46x | 3,96x | **empeora** |
| SortedAscending | 1,11x | **6,02x** | **no transfiere en absoluto** |

En términos absolutos DRS es ~4,8x más rápido aquí (13,96 ms frente a
67,17 ms) — es el hardware. Lo que **no** transfiere es la posición
relativa en datos de baja entropía: la `std::sort` de libc++ en M4 ordena
un millón de enteros ya ordenados en **0,73 ms**, mientras la de
libstdc++ en el Xeon tardaba 24,23 ms. Esa diferencia de 33x en el
competidor, no en DRS, invierte por completo la lectura del dataset.

**Consecuencia para v9:** el punto débil documentado desde v3
(`SortedDescending`) ya no es el peor. **El peor es `SortedAscending`,
con 6,02x.** Y es un caso donde v9 tampoco puede ganar por diseño: DRS
hace como mínimo tres pasadas (contar, colocar, copiar) mientras libc++
resuelve el caso en una. Conviene decidir explícitamente si eso se
acepta, y no descubrirlo al final del paso 6.

## 6. Correcciones que este paso obliga a hacer en SPEC_v9.md

1. **§11, paso 0** — el criterio «el dataset de rango completo *debe*
   exhibir el defecto» estaba mal formulado. Sustituido por el predicado
   medible: profundidad estrictamente mayor y tiempo ≥ 15 % superior que
   el control pareado, a igualdad de comparaciones. **Cumplido.**
2. **§9.6** — la justificación del dataset de rango completo pasa de
   «rompe la aritmética» a «cuesta 2 pasadas O(n) y es la única violación
   conocida del Lema 1». Ambas son ciertas; sólo la segunda es la que
   importa para autorizar CC1.
3. **§7** — la tabla de predicciones por dataset debe incluir
   `AdversarialPeeling` y `FullRangeExtremes`, que no estaban.
4. **§10** — añadir el riesgo detectado en §5 de este documento:
   `SortedAscending` es ahora el peor caso relativo y v9 no lo mejora por
   construcción.

## 7. Estado del paso 0

| Criterio de aceptación (SPEC_v9 §11) | Estado |
|---|---|
| Línea base reproducible | **Cumplido.** Contadores idénticos en 3 ejecuciones; tiempo ±6 % |
| Datasets de §9.5 y §9.6 añadidos | **Cumplido.** `fullRangeExtremes`, `adversarialPeeling` |
| El dataset de rango completo exhibe el defecto | **Cumplido con predicado corregido** (§4, D1) |
| — | *Extra:* el peor caso de DRS queda verificado por primera vez (§4, D2) |

**Listo para el paso 1** (aritmética de span + eliminar el recorte de
índice), cuyo criterio de aceptación es ahora medible contra los números
de §3: `FullRangeExtremes` debe bajar de 20,3 ms a ≈ 14 ms y su
profundidad de 3 a 1, y ningún otro dataset debe moverse más de un 6 %.
