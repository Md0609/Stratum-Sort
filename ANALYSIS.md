# Dynamic Range Sort (DRS) — Análisis experimental

Todos los números de este documento proceden de ejecuciones reales de
`build/drs_tests` sobre esta máquina (5 repeticiones por combinación
dataset/tamaño, valor mediano reportado para atenuar el ruido del entorno
compartido). Puedes reproducirlos con `make test`.

Tipo de dato usado en las pruebas: `int64_t`. `targetElementsPerBin = 16`
(valor por defecto de la especificación). Umbral Insertion Sort ≤16,
QuickSort 17–100, Introsort >100.

## Tabla completa de resultados (mediana de 5 ejecuciones)

```
Dataset                      n     DRS(ms)   std::sort  stable_sort     Bins   Empty  Subdiv   AvgBin   MaxBin  Comparisons
Concentrated               100       0.006       0.003        0.003        8       0       1    12.50       16          403
HugeRangeFewEl             100       0.006       0.003        0.003        9       0       2    11.11       16          342
ManyRepeated               100       0.006       0.002        0.003       11       6       4     9.09       23          179
NormalGaussian             100       0.007       0.003        0.003       10       0       3    10.00       20          308
RandomUniform               100       0.014       0.003        0.004       12       0       5     8.33       13          288
SmallRangeManyEl           100       0.007       0.003        0.003        9       0       2    11.11       14          318
SortedAscending             100       0.005       0.001        0.001        7       0       0    14.29       15           93
SortedDescending            100       0.005       0.001        0.001        7       0       0    14.29       15          675
Concentrated              1000       0.052       0.033        0.039      124      64       1     8.06       30         2651
HugeRangeFewEl             1000       0.059       0.037        0.042       92       0      29    10.87       16         3411
ManyRepeated               1000       0.033       0.016        0.026      123     118       5     8.13      227         4950
NormalGaussian             1000       0.058       0.037        0.043      102       7      26     9.80       21         3566
RandomUniform              1000       0.067       0.036        0.042       91       0      28    10.99       17         3501
SmallRangeManyEl           1000       0.049       0.033        0.039      102      13      39     9.80       18         1228
SortedAscending            1000       0.021       0.007        0.006       63       0       0    15.87       16          937
SortedDescending           1000       0.031       0.005        0.009       63       0       0    15.87       16         7468
Concentrated              10000       0.323       0.329        0.424     1244    1059       1     8.04      129        39443
HugeRangeFewEl            10000       0.615       0.466        0.571      900       0     275    11.11       19        34392
ManyRepeated              10000       0.296       0.171        0.256     1246    1241       5     8.03     2032        88170
NormalGaussian            10000       0.587       0.480        0.560     1049     150     234     9.53       25        35733
RandomUniform             10000       0.685       0.498        0.555      898       0     273    11.14       17        34482
SmallRangeManyEl          10000       0.285       0.328        0.434     1202    1102     100     8.32      121        40010
SortedAscending           10000       0.185       0.093        0.055      625       0       0    16.00       16         9375
SortedDescending          10000       0.299       0.061        0.092      625       0       0    16.00       16        75000
Concentrated             100000       2.947       3.255        4.454    12437   11408       2     8.04     1069       765187
HugeRangeFewEl           100000       6.964       6.112        7.091     8959       0    2708    11.16       23       345110
ManyRepeated             100000       4.028       1.744        2.563    12497   12492       5     8.00    20199      1184404
NormalGaussian           100000       6.750       5.954        7.225    10636    1845    2257     9.40       27       346396
RandomUniform            100000       8.272       6.041        7.352     8978       0    2728    11.14       20       342716
SmallRangeManyEl         100000       2.913       3.141        4.443    12448   12348     100     8.03     1080       780761
SortedAscending          100000       2.003       1.192        0.854     6250       0       0    16.00       16        93750
SortedDescending         100000       3.210       0.760        1.193     6250       0       0    16.00       16       750000
Concentrated            1000000      42.062      33.672       50.290   124365  114931       7     8.04    10021     10723360
HugeRangeFewEl          1000000     109.070      73.550       88.910    89635       0   27132    11.16       25      3450445
ManyRepeated            1000000      44.668      19.469       30.639   124997  124992       5     8.00   200304     14874618
NormalGaussian          1000000      97.879      69.880       87.738   108578   26724   20485     9.21       32      2607051
RandomUniform           1000000     115.101      72.677       90.004    89639       0   27127    11.16       23      3244667
SmallRangeManyEl        1000000      38.967      32.685       49.005   124948  124848     100     8.00    10269     10844300
SortedAscending         1000000      23.396      15.681       14.337    62500       0       0    16.00       16       937500
SortedDescending        1000000      35.423      10.829       17.845    62500       0       0    16.00       16      7500000
```

`std::sort` = Introsort (libstdc++). `stable_sort` = variante de la familia
Merge Sort (libstdc++ usa un merge sort adaptativo). En todas las
combinaciones y para los cinco tamaños probados (100 / 1.000 / 10.000 /
100.000 / 1.000.000), **DRS produjo el resultado correcto** (verificado
elemento a elemento contra `std::sort`).

## Escalado empírico (RandomUniform)

```
n            time(ms)     time/n         time/(n·log2 n)
100          0.014        1.40e-04       2.11e-05
1000         0.067        6.70e-05       6.72e-06
10000        0.685        6.85e-05       5.15e-06
100000       8.272        8.27e-05       4.98e-06
1000000      115.101      1.15e-04       5.77e-06
```

---

## 1–6. Complejidad observada

**Caso favorable (datos ya ordenados o casi):** con `SortedAscending`, DRS
no subdivide ningún bin (`Subdiv = 0` en todos los tamaños) y el número de
comparaciones es casi lineal en `n` (937.500 comparaciones para 1.000.000
de elementos ≈ 0,94 por elemento). Esto se debe a que Insertion Sort es
adaptativo: sobre un bin ya ordenado hace una sola comparación por elemento
y termina. El tiempo de DRS en este caso (23,4 ms para n=1.000.000) es
consistente con **comportamiento cercano a O(n)**.

**Caso promedio (RandomUniform, NormalGaussian):** el cociente
`tiempo / (n·log2 n)` se mantiene en una banda estrecha (4,98e-06 a
6,72e-06) para n ≥ 1.000, mientras que `tiempo/n` crece de forma moderada
(de 6,7e-05 a 1,15e-04 al pasar de n=1.000 a n=1.000.000, un incremento de
~1,7×, frente a las ~2× que predeciría un crecimiento logarítmico puro de
`log2(n)` en ese rango). Esto sitúa el comportamiento observado **entre
O(n) y O(n log n)**, coherente con el diseño: la mayoría de los bins acaban
con ~11 elementos (ver columna `AvgBin`) y se resuelven con Insertion Sort
en tiempo ~O(1) por bin; solo una fracción (`Subdiv`, ~27.000 de ~90.000
bins en n=1.000.000) requiere una subdivisión adicional y ordenación local
más cara.

**Caso peor observado (SortedDescending, ManyRepeated, Concentrated):**
`SortedDescending` es el caso donde el coste de Insertion Sort dentro de
cada bin es máximo (7.500.000 comparaciones para n=1.000.000, frente a
937.500 en el caso ascendente — 8× más), porque cada bin de 16 elementos
llega en orden totalmente inverso, el peor caso local de Insertion Sort. Aun
así, como el tamaño del bin está acotado por `targetElementsPerBin`, el
coste por bin es O(k²) con k≈16 (constante), así que el total sigue siendo
O(n) en número de bins × O(1) por bin — el algoritmo no degenera a O(n²)
global, solo empeora la constante.

El verdadero peor caso observado es **`ManyRepeated`**: con solo 5 valores
distintos, casi todos los bins iniciales quedan vacíos (124.992 de 124.997
bins vacíos en n=1.000.000) y prácticamente todos los elementos terminan en
un único bin gigantesco (`MaxBin = 200.304`), porque la subdivisión se basa
en el **rango observado**, y cuando ese rango observado es 1 (valores
idénticos), `newIntervalSize` no puede separar valores iguales en bins
distintos por más subdivisiones que se pidan. Ese mega-bin se ordena con
Introsort — de ahí que DRS (44,7 ms) sea ~2,3× más lento que `std::sort`
(19,5 ms) en este dataset, con 14.874.618 comparaciones, un orden de
magnitud más que en el resto de datasets del mismo tamaño.

## 7. Comparación con Quicksort, Merge Sort, Timsort e Introsort

- **Introsort (`std::sort` de libstdc++):** comparación directa medida.
  DRS es entre 0,7× y 3,3× más lento según el dataset. Es más rápido que
  DRS en casi todos los casos salvo picos puntuales de ruido del entorno;
  la única situación donde DRS se acerca o iguala a `std::sort` es en
  `Concentrated`/`SmallRangeManyEl` a partir de n=10.000, donde muchos bins
  vacíos se recorren en O(1) y el trabajo útil se concentra en pocos bins
  medianos.
- **Merge Sort (`std::stable_sort`, proxy medido):** consistentemente más
  lento que `std::sort` (como es esperado, al reservar memoria auxiliar y
  garantizar estabilidad) pero también consistentemente más rápido que DRS
  en la mayoría de los datasets a partir de n=10.000.
- **Quicksort clásico (no medido directamente, análisis teórico):** DRS usa
  quicksort con pivote de mediana de tres únicamente para bins de 17–100
  elementos, por lo que nunca hereda el O(n²) de un quicksort ingenuo sobre
  el array completo; el coste de un quicksort "plano" sobre todo el array
  sería comparable en el caso medio a `std::sort`, pero sin la protección de
  Introsort ante distribuciones adversarias.
- **Timsort (no disponible en la librería estándar de C++, no medido):**
  conceptualmente Timsort explota tramos ya ordenados ("runs") de forma
  global; DRS solo explota el orden local dentro de cada bin de 16
  elementos vía Insertion Sort adaptativo, así que en arrays con largos
  tramos ya ordenados Timsort debería escalar mejor que DRS, que no
  detecta ni fusiona runs más largos que un bin.

En resumen: en esta implementación de referencia, **DRS no supera a
Introsort/Merge Sort en tiempo total** para los tamaños y distribuciones
probados; su interés está en el enfoque (particionado por rango en vez de
comparaciones globales) y en las métricas internas que expone para seguir
investigando.

## 8. Cuellos de botella identificados

1. **Duplicados masivos con rango observado degenerado:** cuando muchos
   elementos comparten exactamente el mismo valor, `observedRange = 1` y la
   subdivisión no puede repartirlos en sub-bins distintos (todos caen en el
   sub-bin 0), generando un bin enorme y miles de sub-bins vacíos. Este es
   el cuello de botella más severo medido (`ManyRepeated`, `Concentrated`,
   `SmallRangeManyEl`).
2. **Bins vacíos:** en distribuciones muy concentradas, la mayoría de los
   bins iniciales quedan vacíos (hasta 124.992 de 124.997 en el peor caso
   medido). Recorrerlos en la unión final es O(1) cada uno, pero siguen
   ocupando memoria (`Bin` con sus campos) y aumentan el número de
   iteraciones del bucle de fusión.
3. **Doble recorrido completo del array** (primera y segunda pasada) más un
   tercer recorrido para el análisis inicial: son tres pasadas O(n) antes
   de tocar la ordenación local, frente a un único recorrido inicial en
   quicksort/introsort. Esto añade una constante fija que penaliza más en
   arrays pequeños (ver `n=100`, donde DRS es 2–5× más lento que
   `std::sort` en términos absolutos, aunque las diferencias sean de
   microsegundos).
4. **Sensibilidad de `intervalSize` al rango total:** con `HugeRangeFewEl`
   (rango cercano a 2⁶³), `intervalSize` se vuelve enorme y casi todos los
   elementos comparten pocos bins iniciales por azar, forzando
   subdivisiones (`Subdiv = 27.132` en n=1.000.000) que no siempre reducen
   el tamaño de los bins de forma efectiva si los valores dentro de un bin
   siguen dispersos en un sub-rango amplio.
5. **Insertion Sort en el peor caso local:** los bins ≤16 elementos usan
   Insertion Sort sin importar su orden interno; en datos descendentes esto
   multiplica por ~8 el número de comparaciones frente al caso ascendente
   para el mismo tamaño de bin.

## 9. Mejoras propuestas (NO implementadas)

Se listan aquí, tal como pediste, para que decidas cuáles incorporar en la
siguiente iteración:

- **Subdivisión recursiva acotada:** si tras subdividir un bin sigue habiendo
  un sub-bin con `count > targetElementsPerBin` y `observedRange > 1`,
  repetir la subdivisión sobre ese sub-bin (con un límite de profundidad)
  en vez de dejarlo para Introsort. `DRSMetrics::maxSubdivisionDepth` ya
  está preparado para reportar más de un nivel si se implementa esto.
- **Bin especial para "valor único" (run-length):** cuando `observedMin ==
  observedMax` (todos los elementos de un bin son idénticos), evitar la
  subdivisión y el ordenamiento local por completo: un bin de valor
  constante ya está ordenado por definición. Esto eliminaría de raíz el
  cuello de botella de `ManyRepeated`/`Concentrated`.
- **Omitir bins vacíos de forma más barata:** en vez de crear objetos `Bin`
  para todos los `initialBins` (incluso los que nunca reciben elementos),
  usar una estructura dispersa (mapa o lista de bins no vacíos) cuando se
  detecta que `initialBins` es mucho mayor que el número de valores
  distintos observados.
- **Fusionar primera y segunda pasada cuando no hay subdivisión:** si tras
  la primera pasada ningún bin necesita subdivisión, se podría insertar
  directamente en la segunda pasada sin repetir el cálculo de `binIndex`
  (cachear el índice calculado en la primera pasada, a costa de memoria
  adicional `O(n)`).
- **Detección de runs largos entre bins consecutivos (estilo Timsort):**
  aprovechar tramos ya ordenados que crucen los límites de un bin, no solo
  dentro de un bin, para reducir el trabajo de Insertion Sort en datasets
  con grandes secciones ya ordenadas.
- **`targetElementsPerBin` adaptativo:** actualmente es una constante fija
  (16); se podría estimar en función de `n` y de la dispersión observada
  en la primera pasada antes de fijar `initialBins`.

Ninguna de estas mejoras se ha aplicado a `include/`; quedan aquí como
propuestas para que evolucione el diseño según decidas.
