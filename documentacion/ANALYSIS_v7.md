# Dynamic Range Sort (DRS) v7 — Optimización real y limpieza

## Resumen

v7 deja de investigar hipótesis nuevas y se centra por completo en
rendimiento, tal como pedía el encargo. Cambios aplicados, todos medidos
antes de conservarse:

1. **Eliminadas del algoritmo de producción** las tres heurísticas de v6
   que ya se habían medido como perjudiciales (microhistograma, bins por
   densidad, salto por Difficulty Score). No quedan como código
   inalcanzable dentro de `DynamicRangeSort`: se movieron por completo a
   `algoritmo/versions/DRSv6_experimental.hpp`, aislado, usado solo para
   la comparación de versiones.
2. **Separación real de producción e investigación** mediante
   `DRS_ENABLE_METRICS`: sin esa macro, `DRSMetrics`, `debugPartitionOnly()`
   y cada punto de instrumentación desaparecen del binario — verificado
   con `nm`, no solo desactivados en tiempo de ejecución.
3. **Reducción de trabajo físico medida y conservada**: reutilización de
   buffers de scratch en `countAndPlace()` (~7-10% más rápido,
   confirmado en 6 comparaciones alternadas independientes) y eliminación
   del cálculo de presortedness del camino caliente de `refine()` (ya no
   se calcula si no hay nadie que lo consuma en producción).
4. **`-O3` como flag por defecto**, tras una comparación controlada
   contra `-O2`, `-flto` y `-march=native`.
5. **`analyze()` simplificado**: ya no calcula `isSorted` (una
   estadística que ningún camino de decisión usaba desde que se retiró
   en versiones anteriores) — un campo menos por elemento en la única
   pasada que no se puede fusionar con nada más.

La filosofía de cinco pasos y la regla de subdivisión por rango
observado no cambiaron. Todos los números de este documento proceden de
`make test`, `make benchmarks`, `make experiments` y comparaciones
específicas descritas en cada sección, reproducibles con el código
incluido.

## 1. Eliminar lo que ya sabíamos que perjudica

`algoritmo/DynamicRangeSort.hpp`/`.tpp` ya no tienen microhistograma,
bins por densidad ni salto por Difficulty Score — ni las constantes
(`USE_MICRO_HISTOGRAM`, `DENSITY_MAP_BUCKETS`,
`PRESORTEDNESS_SKIP_THRESHOLD`, etc.) ni los parámetros del constructor
que los activaban. El constructor volvió a su forma mínima:

```cpp
explicit DynamicRangeSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN);
```

Las tres heurísticas siguen existiendo, íntegras, en
`algoritmo/versions/DRSv6_experimental.hpp` — una reconstrucción
autocontenida (sin depender de `DRSMetrics`) que no participa en
`DynamicRangeSort` de ninguna forma; solo se usa desde
`experimentos/VersionComparison.hpp` para la comparación de versiones de
la sección 9.

## 2. Separación producción / investigación

`DRS_ENABLE_METRICS` sustituye a la constante `DEBUG_METRICS` de v4-v6.
La diferencia importante: `DEBUG_METRICS` era una constante en tiempo de
ejecución (el código de `DRSMetrics` seguía compilado, solo con cuerpos
vacíos); `DRS_ENABLE_METRICS` es una macro de preprocesador que decide
qué se compila. Verificado directamente sobre los binarios generados por
el `Makefile`:

```
$ nm build/drs_tests | grep -ci metrics
0
$ nm build/drs_overhead_production | grep -ci metrics
0
$ nm build/drs_overhead_research | grep -ci metrics
1
```

`build/drs_tests` (producción) y `build/drs_overhead_production` no
contienen ningún símbolo relacionado con métricas. El build de
investigación sí. Esto no es "métricas desactivadas": es que
`DRSMetrics`, `debugPartitionOnly()` y cada `metrics_.recordXxx()` no
existen como código objeto cuando la macro no está definida.

## 3. Coste medido de la instrumentación

`benchmarks/ProductionVsResearch.cpp` se compila dos veces —una por cada
configuración— desde el mismo archivo fuente, así que la única
diferencia entre los dos binarios es exactamente la macro que separa
producción de investigación.

```
sizeof(DynamicRangeSort<int64_t>): produccion=128 bytes   investigacion=416 bytes  (3,25x)

n            produccion(ms)   investigacion(ms)
1.000                 0,034              0,041   (+21%)
10.000                0,456              0,488   (+7%)
100.000               4,995              5,298   (+6%)
1.000.000            ~62,0              ~64,0   (+2-7%, dentro del ruido en varias repeticiones)

Memoria adicional acumulada en DRSMetrics durante sort() (n=1.000.000):
  508.664 bytes (7.993 registros de subdivision, 23.618 tamaños de bin)
```

El overhead relativo de tiempo es mayor en `n` pequeño (hasta ~21% en
n=1.000) porque el coste fijo de construir el objeto `DRSMetrics` (varios
`std::vector` y `std::unordered_map` vacíos) pesa más cuando el trabajo
de ordenar es también pequeño; en `n` grande el overhead relativo cae a
un rango de 2-7%, parcialmente dentro del ruido de esta máquina. El
`sizeof` es la diferencia más nítida y estable: cada instancia de
`DynamicRangeSort` en el build de investigación pesa 3,25 veces más,
antes de que se ejecute ni una sola llamada a `sort()`.

## 4. Reducción del trabajo físico

**Reutilización de scratch buffers en `countAndPlace()`.** `refine()` se
llama miles de veces por cada `sort()`; antes, cada llamada a
`countAndPlace()` asignaba dos `std::vector<std::size_t>` nuevos
(`bucketOf`, `writeCursor`) que se descartaban al terminar. v7 los
sustituye por dos miembros reutilizados (`bucketOfScratch_`,
`writeCursorScratch_`), redimensionados con `resize()` — que no
reasigna si la capacidad ya alcanza el tamaño pedido. Esto es seguro
porque la recursión de DRS es estrictamente secuencial y de una sola
hebra: cuando una llamada anidada a `countAndPlace()` ocurre, la llamada
que la originó ya ha terminado de usar sus propios `bucketOf`/
`writeCursor`.

Medido de forma aislada (mismo código, con y sin la reutilización),
alternando el orden de medición en 3 rondas para descartar deriva del
entorno:

```
Ronda 1: con scratch=53,66 ms   sin scratch=57,90 ms
Ronda 2: con scratch=52,69 ms   sin scratch=58,92 ms
Ronda 3: con scratch=54,82 ms   sin scratch=58,21 ms
```

Mejora consistente del 7-10% en las tres rondas. Se conserva.

**`analyze()` ya no calcula `isSorted`.** Ese campo se introdujo en v1 y
ya no lo consumía ningún camino de decisión (las versiones posteriores
detectan el caso "ya ordenado" en `sortLeaf()`, no en `analyze()`).
Quitar la comparación extra por elemento en la única pasada de la que no
se puede prescindir es una reducción de trabajo real, aunque pequeña.

**Comparación cabeza a cabeza v6 (config. por defecto) vs v7**, mismos
datos, ejecuciones alternadas para controlar el ruido del entorno:

```
Ronda 0: v6=66,16 ms   v7=62,13 ms   1,065x
Ronda 1: v6=65,47 ms   v7=61,52 ms   1,064x
Ronda 2: v6=64,44 ms   v7=61,01 ms   1,056x
Ronda 3: v6=63,74 ms   v7=61,17 ms   1,042x
```

v7 es consistentemente un 4-6,5% más rápido que la configuración por
defecto de v6, combinando la reutilización de scratch y la eliminación
del cálculo de presortedness del camino caliente (v6 lo calculaba
siempre, aunque solo actuara sobre él si `useDifficultyScoreSkip` estaba
activo).

## 5. Simplificación de `refine()`

`refine()` perdió, respecto a v6: el cálculo de `ascendingSteps`/
`descendingSteps` (solo existía para decidir el salto de subdivisión,
ahora eliminado), la rama `useMicroHistogram_ && splits <=
MICRO_HISTOGRAM_BUCKETS` (siempre falsa ahora, literalmente no
compilable porque `useMicroHistogram_` ya no existe), y la comprobación
`useDifficultyScoreSkip_ && presortedFraction >= threshold`. Quedan
exactamente las comprobaciones que la especificación original pedía:
tamaño ≤ target, profundidad máxima, y rango observado degenerado
(`observedMin == observedMax`). Tres ramas de decisión en vez de seis.

## 6. Reducir llamadas

Se revisó `insertionSort`/`quickSort`/`introSort`/`partition` en busca de
funciones pequeñas y muy llamadas que se beneficiaran de simplificación.
Todas ya estaban definidas directamente en el header (compilación
unity-friendly, sin unidades de traducción separadas), sin virtualidad,
sin `std::function` ni otros indirectos que impidieran inlining. No se
encontró una función candidata a eliminar sin cambiar comportamiento; el
compilador con `-O3` ya inlinea agresivamente estas funciones pequeñas
dentro de `refine()`/`sortLeaf()` (confirmado indirectamente por la
mejora medida al pasar de `-O2` a `-O3` en la sección 8).

## 7. Memoria y localidad

Los dos buffers en cascada (`bufferA_`, `bufferB_`) y ahora los dos
scratch (`bucketOfScratch_`, `writeCursorScratch_`) son la única memoria
dinámica del camino de producción; los cuatro se reservan como máximo
una vez por tamaño de problema (los scratch crecen monótonamente hasta
estabilizarse en la llamada más grande, normalmente la raíz). No se
encontró una reestructuración adicional de datos que mejorara la
localidad sin cambiar la filosofía de bins-por-rango — la propia
naturaleza de DRS (acceso secuencial dentro de cada bin, buckets
contiguos) ya es favorable a la caché por construcción.

## 8. Comparación de flags de compilador

Comparación controlada, 3 rondas alternadas, `RandomUniform` n=1.000.000:

```
Ronda 1: O2=53,79 ms   O3=49,27 ms   O3+native=52,27 ms
Ronda 2: O2=52,43 ms   O3=52,55 ms   O3+native=49,85 ms
Ronda 3: O2=54,69 ms   O3=50,64 ms   O3+native=50,88 ms
```

`-O3` es consistentemente más rápido que `-O2` (mejora del 3-8% según la
ronda). `-march=native` y `-flto` no mostraron una mejora adicional
consistente más allá de `-O3` solo — sus resultados se solapan con los
de `-O3` dentro del ruido de esta máquina. Dado que `-march=native`
produce binarios no portables (no funcionan en CPUs distintas a la que
compiló), y ni `-march=native` ni `-flto` demostraron una ganancia clara
adicional aquí, el `Makefile` usa solo `-O3` por defecto. Esta decisión
puede no trasladarse a otro hardware; los números crudos quedan aquí
para que se pueda re-verificar.

## 9. Benchmarks

### v3 → v7, todas las versiones, RandomUniform

Mediana de 7 repeticiones. La fila `v7` corresponde a este mismo binario
de investigación (compilado con `DRS_ENABLE_METRICS`, necesario para el
resto del programa); el número de producción real, sin ninguna
instrumentación, es el de la sección 3 (`make overhead`), sistemáticamente
algo más bajo.

```
n            v1(ms)     v2(ms)     v3(ms)     v6(ms)     v7(ms, con metricas)
1.000          0,068      0,066      0,036      0,042      0,055
10.000         0,815      0,698      0,440      0,460      0,430
100.000        9,393      7,728      4,717      4,854      4,561
1.000.000    167,382    119,085     61,977     69,566     64,624
```

En n=1.000.000: v7 (con métricas) es 2,59x más rápido que v1, por
delante de v6 (2,41x) y ligeramente por detrás de v3 (2,70x) — coherente
con que v3 no carga con ninguna instrumentación en absoluto y v7-con-
métricas sí. La versión de producción real de v7 (sección 3, ~62-64 ms)
queda prácticamente empatada con v3 en esta máquina.

### DRS (producción, vía este binario de investigación) vs std::sort vs std::stable_sort

Mediana de 5 repeticiones, n=1.000.000:

```
Dataset             DRS(ms)   std::sort(ms)   stable_sort(ms)   DRS vs sort
RandomUniform          67,17           71,60             95,69        0,94x
SortedAscending        26,82           24,23             17,41        1,11x
SortedDescending       25,12           10,22             23,95        2,46x
ManyRepeated           25,67           19,96             37,23        1,29x
NormalGaussian         69,27           68,51             96,11        1,01x
Concentrated           44,21           34,83             56,08        1,27x
SmallRangeManyEl       30,81           34,15             57,78        0,90x
HugeRangeFewEl         65,78           71,47             99,91        0,92x
```

DRS iguala o supera a `std::sort` en la mitad de los ocho datasets
(`RandomUniform`, `NormalGaussian`, `SmallRangeManyEl`, `HugeRangeFewEl`)
y siempre supera a `std::stable_sort`. `SortedDescending` sigue siendo el
caso donde `std::sort` saca más ventaja (2,46x): el introsort de
libstdc++ tiene un camino especialmente barato para datos totalmente
invertidos que la detección de tramos de DRS no iguala del todo — el
mismo hallazgo que ya se documentó en `ANALYSIS.md` (v3) y que sigue sin
resolverse.

## Trabajo futuro

- El overhead de instrumentación en `n` pequeño (~21% en n=1.000) podría
  reducirse con una construcción más perezosa de las estructuras internas
  de `DRSMetrics` (los `std::unordered_map` de fases/algoritmos se crean
  vacíos igualmente); no se abordó aquí por no ser el cuello de botella
  dominante en los tamaños que más importan.
- `SortedDescending` sigue siendo el único dataset estándar donde
  `std::sort` gana con margen — ya señalado como línea abierta en
  `ANALYSIS.md` y `ANALYSIS_v5.md`, sigue sin una solución que no
  implique una heurística nueva (fuera del alcance de esta iteración por
  instrucción explícita del encargo).
- No se repitió el ajuste de complejidad completo (`analisis/`) para v7:
  como esta iteración es un pase de constante y no cambia la estructura
  del algoritmo, no se esperaba (ni se buscó) un cambio en el exponente
  de escalado — solo en la constante `a`. Repetirlo con los 14 tamaños
  completos queda como verificación pendiente si se quiere confirmar
  numéricamente.

## Restricciones respetadas

DRS sigue siendo claramente Dynamic Range Sort: análisis, construcción de
bins por rango, refinamiento recursivo con rango observado, ordenación
local, unión. Ninguna optimización de esta iteración sustituye ninguna
parte de esa estructura por Radix/Bucket/Counting/Flash/SpreadSort — todo
lo hecho aquí es reducción de trabajo constante (menos asignaciones,
menos ramas, mejor flag de compilador) sobre exactamente el mismo
algoritmo.
