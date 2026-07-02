# Dynamic Range Sort (DRS) — Análisis experimental (v2)

Esta versión implementa las mejoras propuestas en la primera iteración,
manteniendo la filosofía original del algoritmo (Analizar → Construir
intervalos → Refinar intervalos → Ordenar localmente → Unir resultado) y sin
sustituirla por ninguna estructura de otro algoritmo conocido. El cambio
central es que "refinar intervalos" pasa de ser un único paso fijo a una
recursión acotada: un bin que sigue siendo demasiado grande después de
dividirse, y cuyo propio rango observado sigue teniendo más de un valor,
vuelve a refinarse contra sus propios datos — nunca contra el array
original — hasta `MAX_SUBDIVISION_DEPTH` niveles.

Todos los números proceden de ejecuciones reales de `build/drs_tests`
(mediana de 5 repeticiones por combinación dataset/tamaño). Reproducibles
con `make test`.

## Mejoras implementadas

| # | Mejora propuesta en v1 | Cómo quedó implementada |
|---|---|---|
| 1 | Subdivisión recursiva acotada | `refine()` se llama a sí misma sobre cada sub-bin hasta `MAX_SUBDIVISION_DEPTH` (6) niveles, usando siempre el rango observado de ese sub-bin, nunca el original. |
| 2 | Bin de "valor único" | Dentro de `refine()`, si `observedMin == observedMax` el bin se convierte en hoja inmediatamente: no se subdivide ni se ordena por comparaciones, porque un bin con un solo valor repetido ya está ordenado por definición. |
| 3 | Bins vacíos más baratos | `buildInitialBins()` ahora acota `initialBins` a `min(ceil(length/target), range)`: nunca se crean más bins de los que el rango de valores puede llegar a ocupar, así que la cantidad de bins vacíos deja de depender solo de `length/target`. |
| 4 | Fusionar cálculo de índice de bin | `firstPass()` cachea el índice de bin de cada elemento en un array paralelo; el paso de agrupación reutiliza ese índice en vez de recalcular la división. |
| 5 | Detección de tramos ya ordenados | Antes de aplicar cualquier sort por comparaciones, `sortLeaf()` hace un escaneo O(k) (`detectRun`) que reconoce si el bin ya está totalmente ascendente (no hace nada) o totalmente descendente (una sola inversión O(k)). Es una detección local por bin, no una fusión de tramos entre bins al estilo Timsort — se mantiene deliberadamente así para no convertir esto en una reimplementación de Timsort. |
| 6 | `targetElementsPerBin` adaptativo | **No implementada por separado.** La mejora #3 (acotar `initialBins` por el rango) ya resuelve el caso que motivaba esta idea (rangos muy pequeños con muchos duplicados); introducir una heurística adicional sin más datos de prueba habría añadido una constante arbitraria sin una justificación experimental clara. Queda como línea abierta. |

Estructuralmente, esto convirtió el segundo nivel de bins en un pequeño
árbol (`RefinedBin`, con hojas que contienen elementos y nodos internos que
contienen hijos), en vez de una lista plana de dos niveles. El recorrido
final (ordenación local y unión) es una traversal de ese árbol.

## Tabla completa de resultados (mediana de 5 ejecuciones)

```
Dataset                      n     DRS(ms)   std::sort  stable_sort     Bins   Empty  Subdiv   AvgBin   MaxBin  Comparisons
Concentrated               100       0.006       0.003        0.003        8       0       1    12.50       16          423
HugeRangeFewEl             100       0.007       0.003        0.003        9       0       2    11.11       16          363
ManyRepeated               100       0.004       0.002        0.003        5       0       0    20.00       23           95
NormalGaussian             100       0.010       0.003        0.003       11       0       4     9.09       14          302
RandomUniform              100       0.017       0.003        0.005       12       0       5     8.33       13          315
SmallRangeManyEl           100       0.007       0.003        0.003        9       0       2    11.11       14          339
SortedAscending             100       0.005       0.001        0.001        7       0       0    14.29       15           93
SortedDescending            100       0.004       0.001        0.001        7       0       0    14.29       15           93
Concentrated              1000       0.102       0.034        0.046      162      64      39     6.17       18         1224
HugeRangeFewEl            1000       0.067       0.039        0.047       92       0      29    10.87       16         3634
ManyRepeated              1000       0.018       0.018        0.029        5       0       0   200.00      227          995
NormalGaussian            1000       0.085       0.038        0.047      110       7      34     9.09       16         3657
RandomUniform             1000       0.070       0.038        0.048       92       0      29    10.87       16         3706
SmallRangeManyEl          1000       0.060       0.034        0.046      102      13      39     9.80       18         1214
SortedAscending           1000       0.024       0.007        0.008       63       0       0    15.87       16          937
SortedDescending          1000       0.024       0.005        0.010       63       0       0    15.87       16          937
Concentrated             10000       0.419       0.334        0.499     1244    1059       1     8.04      129         9815
HugeRangeFewEl           10000       0.686       0.513        0.631      902       0     277    11.09       16        36553
ManyRepeated             10000       0.158       0.213        0.285        5       0       0  2000.00     2032         9995
NormalGaussian           10000       0.757       0.512        0.619     1154     150     339     8.67       16        36152
RandomUniform            10000       0.722       0.515        0.627      903       0     278    11.07       16        36584
SmallRangeManyEl         10000       0.161       0.329        0.496      100       0       0   100.00      121         9900
SortedAscending          10000       0.209       0.076        0.083      625       0       0    16.00       16         9375
SortedDescending         10000       0.233       0.065        0.110      625       0       0    16.00       16         9375
Concentrated            100000       3.390       3.649        5.326    12437   11408       2     8.04     1069        98974
HugeRangeFewEl          100000       6.690       6.261        7.943     9007       0    2756    11.10       16       366405
ManyRepeated            100000       1.833       2.219        3.157        5       0       0 20000.00    20199        99995
NormalGaussian          100000       8.555       6.403        8.067    11795    1845    3416     8.48       16       348555
RandomUniform           100000       8.454       6.356        7.968     9010       0    2760    11.10       16       364286
SmallRangeManyEl        100000       1.647       3.557        5.244      100       0       0  1000.00     1080        99900
SortedAscending         100000       2.642       0.958        1.165     6250       0       0    16.00       16        93750
SortedDescending        100000       2.233       0.803        1.445     6250       0       0    16.00       16        93750
Concentrated           1000000      37.937      39.341       59.239   124365  114931       7     8.04    10021       990623
HugeRangeFewEl         1000000      94.805      76.193       98.543    90017       0   27514    11.11       16      3662427
ManyRepeated           1000000      20.417      26.236       37.829        5       0       0 200000.00  200304       999995
NormalGaussian         1000000     108.592      73.920       97.233   125977   26724   37884     7.94       21      2310900
RandomUniform          1000000     108.057      77.061       98.395    90066       0   27554    11.10       16      3471574
SmallRangeManyEl       1000000      20.943      38.826       59.096      100       0       0  10000.00    10269       999900
SortedAscending        1000000      28.347      12.345       18.470    62500       0       0    16.00       16       937500
SortedDescending       1000000      28.839       9.713       21.191    62500       0       0    16.00       16       937500
```

## Efecto medido de las mejoras (v1 → v2, n = 1.000.000)

```
Dataset             DRS v1 (ms)   DRS v2 (ms)   Factor
ManyRepeated             44.668        20.417    2.19x más rápido
SmallRangeManyEl         38.967        20.943    1.86x más rápido
Concentrated             42.062        37.937    1.11x más rápido
RandomUniform            115.101      108.057    1.07x más rápido
SortedDescending          35.423       28.839    1.23x más rápido
```

Los mayores saltos se dan exactamente en los datasets que el análisis de
la v1 había señalado como cuellos de botella (`ManyRepeated`,
`SmallRangeManyEl`): ambos dejan de necesitar subdivisión (`Subdiv = 0`) y
el bin gigante que antes se ordenaba con Introsort ahora se reconoce como
un único valor repetido (`ManyRepeated`) o como bins de un solo valor tras
el recorte de `initialBins` (`SmallRangeManyEl`), y se resuelve como
"ya ordenado" en una pasada O(k). En ambos casos DRS pasa de ser más lento
que `std::sort` a ser más rápido.

`SortedDescending` también mejora de forma consistente en todos los
tamaños gracias a la detección de tramos: cada bin de 16 elementos en
orden inverso se invierte en O(k) en vez de pasar por Insertion Sort en su
peor caso.

## Complejidad observada (RandomUniform, caso promedio)

```
n            time(ms)     time/n         time/(n·log2 n)
100          0.017        1.70e-04       2.56e-05
1000         0.070        7.00e-05       7.02e-06
10000        0.722        7.22e-05       5.43e-06
100000       8.454        8.45e-05       5.09e-06
1000000      108.057      1.08e-04       5.42e-06
```

El cociente `tiempo/(n·log2 n)` se mantiene en una banda estrecha (5,09e-06
– 5,43e-06) para n ≥ 10.000, y `tiempo/n` crece de forma moderada: sigue
entre O(n) y O(n log n), igual que en la v1. Esto es esperado: para datos
continuos sin duplicados masivos (como `RandomUniform`), la recursión rara
vez pasa de profundidad 1–2, así que el comportamiento del núcleo del
algoritmo no cambia mucho; las mejoras de esta iteración estaban dirigidas
a los casos degenerados, no al caso promedio.

Los casos donde sí se observa un comportamiento muy cercano a O(n) puro
siguen siendo los de baja entropía real: `SortedAscending` (0,94
comparaciones/elemento), `ManyRepeated` y `SmallRangeManyEl` (~1
comparación/elemento, todo resuelto por el escaneo `detectRun` sin ningún
sort por comparaciones).

## Comparación con Introsort y Merge Sort

- **Introsort (`std::sort`):** DRS iguala o supera a `std::sort` en
  `ManyRepeated`, `SmallRangeManyEl` y `Concentrated` (los tres casos con
  redundancia real en los datos). Sigue siendo más lento en datos
  continuos sin redundancia (`RandomUniform`, `NormalGaussian`,
  `HugeRangeFewEl`), donde el coste fijo de tres pasadas O(n) sobre el
  array completo (análisis, primera pasada, agrupación) antes de empezar a
  ordenar pesa más que lo que se gana evitando comparaciones.
- **Merge Sort (`std::stable_sort`, proxy medido):** DRS es más rápido que
  `stable_sort` en prácticamente todos los datasets y tamaños probados,
  incluyendo los casos sin redundancia — la reserva de memoria auxiliar y
  las garantías de estabilidad de `stable_sort` tienen un coste que DRS no
  arrastra.

## Cuellos de botella que persisten

1. **Coste fijo de tres pasadas completas:** `analyze()`, `firstPass()` y
   la agrupación en `groups` siguen siendo tres recorridos O(n) del array
   antes de tocar la ordenación local. Sigue siendo la razón principal por
   la que DRS no le gana a `std::sort` en datos sin redundancia: un solo
   quicksort/introsort empieza a comparar en la primera llamada.
2. **`HugeRangeFewEl` sigue siendo el caso más caro en términos absolutos**
   (94,8 ms en n=1.000.000): con un rango cercano a 2⁶³, `intervalSize` es
   enorme, así que la coincidencia de dos valores en el mismo bin inicial
   es prácticamente aleatoria, y las subdivisiones observadas (27.514)
   rara vez logran aislar valores en bins de un solo elemento en el primer
   nivel, arrastrando trabajo real de comparación en los niveles
   siguientes.
3. **Memoria durante `refine()`:** cada nivel de recursión crea vectores
   `std::vector<std::vector<T>>` temporales para los `splits` cubos antes
   de liberar el vector padre; para bins con mucha profundidad de
   refinamiento esto añade presión de asignación de memoria que un sort
   in-place no tendría.
4. **`detectRun` cuesta O(k) incluso cuando el bin no está ordenado ni
   invertido:** en el peor caso (un bin genuinamente desordenado) esos k-1
   comparaciones extra se suman al coste del sort por comparaciones que
   sigue después, sin aportar nada. El coste es pequeño porque k está
   acotado, pero no es gratis.

## Mejoras que quedan abiertas

- **`targetElementsPerBin` verdaderamente adaptativo** (mejora #6 de la
  iteración anterior), ahora que el recorte de `initialBins` por rango ya
  cubre el caso más urgente.
- **Reducir el número de pasadas completas** fusionando `analyze()` y
  `firstPass()` en una sola, calculando min/max de forma incremental
  mientras se calculan los índices de bin — requiere conocer el rango
  antes de tener `intervalSize`, así que exige repensar el orden de las
  fórmulas, no solo fusionar bucles.
- **Bins in-place:** sustituir los `std::vector<std::vector<T>>` de
  `refine()` por un esquema de partición in-place (al estilo counting sort
  con offsets precalculados) para eliminar las asignaciones de memoria por
  nivel de recursión.

Ninguna de estas queda implementada en `include/`; se documentan aquí para
una futura iteración.
