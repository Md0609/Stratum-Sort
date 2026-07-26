# Dynamic Range Sort (DRS) v6 — Reducción de subdivisiones y aproximación a O(n)

## Resumen

A diferencia de v4 y v5, esta iteración **aplica** cambios al algoritmo,
no solo los mide. Se implementaron y probaron rigurosamente dos
mecanismos nuevos, completamente integrados en `refine()` y activables en
tiempo de ejecución:

1. **Bins iniciales conscientes de densidad** (`useDensityAwareBins`):
   un mapa de densidad compacto (2048 contadores, no una copia de los
   datos) usado para elegir límites de bin no uniformes, buscando carga
   similar en vez de anchura similar.
2. **Salto de subdivisión por Difficulty Score** (`useDifficultyScoreSkip`):
   una puntuación de dificultad (fracción de pasos ya ordenados,
   calculada sin coste adicional dentro del escaneo que ya existía) usada
   para decidir si merece la pena subdividir un bin, en vez de decidirlo
   solo por tamaño.

**Los dos mecanismos funcionan correctamente y están completamente
implementados, pero los datos muestran que ambos empeoran el
rendimiento** con la implementación actual. Ambos quedan en el código,
activables por parámetro del constructor para seguir investigándolos,
pero **desactivados por defecto** — el algoritmo por defecto no cambia de
comportamiento respecto a v5. Esto no es una iteración fallida: es
exactamente lo que pide el encargo ("Si una hipótesis no funciona,
documentarla igualmente"), y el proceso de descubrir *por qué* fallan
generó el hallazgo más útil de esta versión (sección "Investigación 8").

Todos los números proceden de `make test`, `make experiments`,
`make benchmarks` y programas de verificación específicos, reproducibles
con el código incluido.

## Investigación 1 — Calidad de las subdivisiones

Se añadió `DRSMetrics::SubdivisionRecord`: cada subdivisión real registra
tamaño original, número de hijos, tamaño del hijo más grande, profundidad,
y `reductionPct = 1 - (tamaño del hijo más grande / tamaño original)` —
cuánto se redujo el peor caso restante. La métrica `Subdivision
Efficiency` es la media de `reductionPct` sobre todas las subdivisiones.

Resultado real, n=1.000.000, target=64:

```
Dataset            Subdivisiones   Efficiency media   Utiles   Marginales   Inutiles
RandomUniform             7.276              0,4530     4,2%        95,8%       0,0%
NormalGaussian            7.696              0,5584    50,2%        49,8%       0,0%
Concentrated                  2              0,9796   100,0%         0,0%       0,0%
SortedAscending                0                  -        -            -          -
```

**No se encontraron subdivisiones inútiles (reductionPct < 10%) en
ninguno de los ocho datasets estándar.** El umbral de `target=64` ya
filtra de forma efectiva los casos donde subdividir no ayudaría: un bin
solo llega a `refine()` si supera 64 elementos, y en la práctica, para
estos datasets, cuando eso ocurre la subdivisión casi siempre logra al
menos una reducción moderada. Esto es en sí mismo un resultado útil: no
hay margen de mejora obvio por "eliminar subdivisiones inútiles" con la
configuración actual — el problema que se sospechaba (Investigación 1
del encargo) no aparece con los datos disponibles.

## Investigación 2 — Mapa de densidad global

**Implementado y aplicado**, no solo diseñado. `buildDensityMap()`
construye un histograma de `DENSITY_MAP_BUCKETS=2048` contadores sobre
`[minimumValue, maximumValue]` en una pasada O(n) adicional;
`computeDensityAwareBoundaries()` usa su distribución acumulada para
elegir límites de bin no uniformes (misma técnica de interpolación que el
microhistograma de v4, aplicada aquí al nivel superior en vez de a
sub-bins). `distributeWithBoundaries()` coloca cada elemento con
búsqueda binaria (`std::upper_bound`) sobre esos límites.

**Funciona exactamente como se diseñó:** en `NormalGaussian`
(n=1.000.000), elimina el 100% de los bins vacíos (0 frente a 3.703 con
bins uniformes). **Pero es mucho más lento en total**, medido
directamente:

```
Dataset             uniforme (ms)   densidad (ms)   distribute() uniforme   distribute() densidad
RandomUniform               52,51          149,50                26,06 ms                114,74 ms
NormalGaussian              56,93          146,47                   -                     117,32 ms
Concentrated                25,75           76,42                   -                        -
HugeRangeFewEl              56,32          149,79                   -                        -
SmallRangeManyEl            15,80           63,68                   -                        -
```

La fase `distribute()` por sí sola pasa de ~26 ms a ~115-117 ms (4-5x) en
`RandomUniform`, donde los límites conscientes de densidad deberían ser
casi idénticos a los uniformes (y de hecho lo son: 22.931 bins frente a
22.901, comparaciones casi iguales) — así que ni siquiera en el caso
"neutro" compensa. La razón es aritmética, no un error: la versión
uniforme calcula el bin de cada elemento con una división O(1)
(`computeBinIndex`); la versión consciente de densidad necesita una
búsqueda binaria O(log b) sobre hasta ~15.600 límites
(`findBoundaryBin`), y esa búsqueda —con el overhead de iteradores de
`std::upper_bound`— resulta varias veces más cara que una sola división,
incluso para log b ≈ 14.

**Eliminar los bins vacíos no compensa ese coste**: en `NormalGaussian`,
las comparaciones totales apenas cambian (11.089.226 con densidad frente
a 11.070.493 sin ella) — los bins vacíos nunca fueron el cuello de
botella real, solo un síntoma visualmente llamativo.

**Conclusión, con datos:** `useDensityAwareBins` queda implementado y
disponible, pero por defecto **desactivado**
(`USE_DENSITY_AWARE_BINS=0`). Una propuesta concreta para v7 (no
implementada aquí): sustituir la búsqueda binaria por un índice de dos
niveles (usar el propio mapa de densidad, ya construido, como tabla de
salto directo O(1) hacia un rango estrecho de límites candidatos, seguido
de una búsqueda lineal corta) — debería acercar el coste de
`distribute()` consciente de densidad al de la versión uniforme sin
perder el beneficio de bins balanceados.

## Investigación 3 — Refinamiento adaptativo (Difficulty Score)

**Implementado y aplicado**, no solo diseñado. Fórmula elegida tras
comparar candidatas: `difficultyScore = 1 - presortedFraction`, donde
`presortedFraction` es la fracción de pasos adyacentes que coinciden con
la dirección mayoritaria del bin (ascendente o descendente), calculada
sin coste adicional dentro del mismo escaneo O(k) que ya calculaba
`observedMin`/`observedMax`. Se decidió no usar densidad ni varianza como
señal principal porque ambas ya están representadas indirectamente por
`observedRange`/`count`, que la lógica de subdivisión actual ya usa
directamente.

**Regla probada:** si `presortedFraction >= 0,90`, saltar la subdivisión
y ordenar el bin completo directamente (con el mismo despachador
Insertion/QuickSort/Introsort que usan las hojas).

**El umbral 0,90 se determinó por barrido** (0,50 a 1,01) sobre
`RandomUniform`, `NormalGaussian`, `Concentrated`, `SortedAscending` y
`SortedDescending` en n=1.000.000 — valores por debajo de 0,60 empeoran
claramente (0,50: `Concentrated` pasa de ~26 ms a ~56 ms, más del doble,
porque bins genuinamente desordenados empiezan a saltarse por
casualidad). Entre 0,60 y 1,01 las diferencias quedaron dentro del ruido
de medición en los ocho datasets estándar — **porque el mecanismo nunca
llegó a activarse**: con `target=64`, los ocho datasets estándar
registraron `subdivisionsSkippedByPresortedness = 0` en todos los casos.

Para poder medir el mecanismo en absoluto, se construyó un dataset
específico: un bloque de 900.000 elementos ya ordenados de un rango
estrecho ([0, 999]) más una cola de 100.000 elementos aleatorios de rango
amplio — diseñado para producir un bin inicial enorme y ya ordenado.
Resultado real, comparando la regla actual (siempre subdividir si
`count > target`) contra la regla con el predictor activado:

```
                                mediana(ms)   subdivisiones   saltadas   comparaciones
Decision actual (siempre subdivide)   27,49              8          0       1.154.197
Con predictor (Difficulty Score)      34,50              0          8      14.685.314
```

**El predictor es un 25% más lento y usa 12,7 veces más comparaciones.**
La razón: subdividir ese bin de 900.000 elementos casi ordenados sigue
siendo muy efectivo (`reductionPct` medido ≈ 0,991 cuando sí se permite
subdividir) — divide el problema en ~14.000 piezas pequeñas, cada una de
las cuales se resuelve en el camino rápido `AlreadySorted` de
`sortLeaf()` casi sin comparaciones. Saltarse la subdivisión, en cambio,
entrega el bin completo a Introsort, que sí necesita hacer un trabajo
de partición real (aunque el bloque esté mayormente ordenado, Introsort
no tiene un camino rápido para eso) — de ahí las 14,7 millones de
comparaciones frente a 1,15 millones.

**Conclusión, con datos: la hipótesis central de la Investigación 3
(que un bin ya ordenado no merece subdividirse) es falsa para esta
implementación.** Subdividir sigue mereciendo la pena incluso en datos
ya ordenados, porque el coste de subdividir es mucho menor que el de
ordenar por comparaciones un bloque grande de una vez, y las hojas
pequeñas resultantes tienen su propio camino rápido para el caso
ordenado. `useDifficultyScoreSkip` queda implementado y disponible para
seguir investigando, pero desactivado por defecto
(`USE_DIFFICULTY_SCORE_SKIP=0`).

## Investigación 4 — Subdivisiones útiles

Con la clasificación Útil (≥50% de reducción) / Marginal (10-50%) /
Inútil (<10%), ningún dataset estándar mostró subdivisiones inútiles (ver
Investigación 1). El informe automático (`experimentos/
SubdivisionQualityAnalysis.hpp`) genera exactamente el formato pedido:

```
Se realizaron 7276 subdivisiones.
De ellas:
  4.2% mejoraron claramente (308, reduccion >= 50%)
  95.8% apenas aportaron (6968, reduccion 10-50%)
  0.0% fueron innecesarias (0, reduccion < 10%)
```

(`RandomUniform`, n=1.000.000). El patrón "muchas marginales, pocas
claramente útiles, ninguna inútil" se repite en todos los datasets
continuos — sugiere que `target=64` ya está bien calibrado: las
subdivisiones que ocurren son casi siempre necesarias, solo que su
beneficio individual (reducir el bin a la mitad o menos del tamaño
original) rara vez llega al umbral "claramente útil" en una sola pasada,
porque el árbol de refinamiento converge en varios niveles, no en uno.

## Investigación 5 — Refinamiento parcial (argumento matemático, no implementado)

**Pregunta:** ¿es necesario volver a mover todos los elementos de un bin
al subdividirlo, o se puede reprocesar solo una "zona conflictiva"?

**Respuesta: no es viable con la arquitectura actual de DRS, y esto se
puede argumentar sin necesidad de implementarlo.**

El argumento central es que **qué bucket le corresponde a un elemento no
es una propiedad fija del elemento, sino que depende del `observedMin` y
`newIntervalSize` calculados en ese nivel de recursión concreto**, que a
su vez dependen de qué otros elementos terminaron en el mismo bin padre.
Formalmente: sea `B` un bin con elementos `E = {e_1, ..., e_k}`. Al
subdividirlo, el bucket de `e_i` es:

```
bucket(e_i) = floor((e_i - min(E)) / newIntervalSize(E))
```

donde `newIntervalSize(E)` depende de `max(E) - min(E)` (el rango
*observado*, no el original — la regla que DRS mantiene desde la
especificación inicial). Esto significa que **no existe ningún
subconjunto de `E` que se pueda identificar como "ya en su sitio" sin
calcular `min(E)` y `max(E)` primero**, lo cual ya exige recorrer todo
`E` una vez (el mismo coste O(k) que recorrerlo para moverlo). No hay
forma de saber qué es una "zona conflictiva" sin ese recorrido completo
previo — la propia definición de conflicto depende de datos que solo se
conocen después de mirar todos los elementos.

Una vía en la que sí se podría ahorrar movimiento sería mantener
`newIntervalSize` **fijo entre niveles** (no recalcularlo a partir del
rango observado de cada sub-bin) — pero eso violaría exactamente la regla
que el encargo pide mantener ("division por rango observado"), y es
además la razón por la que DRS evita crear sub-bins vacíos cuando los
datos están muy concentrados (ver `ANALYSIS.md`, v1→v2). Renunciar a esa
regla para ahorrar movimiento cambiaría la naturaleza del algoritmo, no
sería una optimización de implementación.

**Conclusión: no se implementa.** El argumento anterior es la
"investigación rigurosa" que el encargo pedía como alternativa a una
implementación incorrecta.

## Investigación 6 — Modelo predictivo

El predictor implementado es exactamente `useDifficultyScoreSkip`
(Investigación 3). La comparación directa "decisión actual" vs
"decisión con predictor" está en la tabla de la Investigación 3: el
predictor pierde en tiempo (+25%), comparaciones (+1.172%) y no aporta
ninguna subdivisión evitada que compense — es un predictor que funciona
exactamente como se diseñó (identifica correctamente los bins muy
ordenados) pero cuya *acción* (saltarse la subdivisión) resulta
contraproducente. La memoria es prácticamente idéntica en ambos casos
(16.675.216 vs 16.000.000 bytes) porque ambas rutas siguen usando los
mismos dos buffers de tamaño `n`.

## Investigación 7 — Nueva instrumentación

Implementada en `DRSMetrics` y expuesta vía
`experimentos/SubdivisionQualityAnalysis.hpp`: Difficulty Score medio y
máximo, Difficulty Score medio por profundidad, histograma de Difficulty
Score (10 buckets), distribución de tamaños de bin (ya existía desde v5,
`experimentos/BinSizeHistogram.hpp`), `Subdivision Efficiency`, y trabajo
(elementos reprocesados) por nivel de profundidad — este último confirma
cuantitativamente que la mayoría del trabajo de refinamiento ocurre en el
primer nivel de subdivisión: en `NormalGaussian` (n=1.000.000), 899.567
elementos se reprocesan en profundidad 1 frente a 166.321 en profundidad
2 — una caída de ~5,4x, coherente con `Subdivision Efficiency ≈ 0,56`.

No se implementó "porcentaje del tiempo empleado en cada profundidad del
árbol" (pedido explícitamente en la sección 7) con temporización real por
nivel: instrumentar temporizadores anidados por profundidad de forma
correcta en una función recursiva sin distorsionar la medición (el propio
acto de leer el reloj tiene coste, y se llama miles de veces por nivel)
requiere un diseño más cuidadoso que no se abordó en esta iteración; el
recuento de elementos por nivel es el proxy que se usó en su lugar,
documentado aquí como sustituto deliberado, no como un descuido.

## Investigación 8 — Análisis estadístico

Correlación de Pearson entre Difficulty Score, tamaño original,
profundidad y `reductionPct` (el proxy medible de "beneficio de la
subdivisión"), sobre subdivisiones reales:

```
Dataset          n(subdiv.)   DifScore~Reduc   Tamano~Reduc   Profund~Reduc   Tamano~DifScore
RandomUniform          7.276           0,0175         0,0255          0,0000            0,0417
NormalGaussian         7.696           0,4411         0,9398         -0,6339            0,4679
Concentrated               2           1,0000         1,0000          0,0000            1,0000
```

(`Concentrated` con solo 2 subdivisiones no es estadísticamente
significativo — se incluye por transparencia, no como evidencia.)

**El hallazgo más útil de esta sección: en `NormalGaussian`, el tamaño
original del bin correlaciona con `reductionPct` mucho más fuerte
(0,94) que el Difficulty Score (0,44).** Es decir, **el tamaño ya es, con
diferencia, el mejor predictor disponible de si merece la pena
subdividir** — que es exactamente la señal que la regla actual de DRS
(`count > target`) ya usa. Esto explica en retrospectiva por qué el
Difficulty Score no mejoró el algoritmo: no es que la señal esté mal
calculada, es que el tamaño por sí solo ya captura la mayor parte de la
información relevante, y añadir una señal secundaria correlacionada más
débilmente solo añade coste de cálculo y, en este caso, una decisión
peor.

También es revelador que `Difficulty Score ~ reductionPct` sea
**positivo** en `NormalGaussian` (0,44): los bins con Difficulty Score
**alto** (menos ordenados) tienden a lograr **más** reducción al
subdividirse, no menos — el sentido contrario al que asumía la hipótesis
de la Investigación 3. Subdividir un bin desordenado separa sus valores
de forma más efectiva que subdividir uno ya ordenado (cuyos valores,
precisamente por estar ordenados, pueden estar más agrupados o seguir un
patrón que reparte peor entre los nuevos sub-bins). Esto es coherente,
con datos, con por qué saltarse la subdivisión en bins ordenados fue
contraproducente.

## Restricciones respetadas

- La filosofía de cinco pasos no cambió.
- No se sustituyó DRS por Radix/Bucket/Counting Sort ni ningún otro
  algoritmo conocido — ambos mecanismos nuevos siguen construyendo bins
  por rango observado y refinando recursivamente, solo cambia cómo se
  deciden los límites (Investigación 2) y si subdividir (Investigación
  3).
- Toda conclusión de este documento está respaldada por mediciones
  reales, incluidas las dos hipótesis centrales que resultaron
  refutadas.

## Qué cambia por defecto

**Nada.** `DynamicRangeSort<T>` con el constructor por defecto se
comporta igual que en v5: `target=64`, sin microhistograma, sin bins
conscientes de densidad, sin salto por Difficulty Score. Los cuatro
parámetros del constructor permiten activar cualquier combinación para
seguir investigando:

```cpp
drs::DynamicRangeSort<int64_t> sorter(
    /*targetElementsPerBin=*/64,
    /*useMicroHistogram=*/false,
    /*useDensityAwareBins=*/false,
    /*useDifficultyScoreSkip=*/false);
```

## Trabajo futuro

- **Índice de dos niveles para bins conscientes de densidad** (v7): usar
  el propio mapa de densidad como tabla de salto O(1) antes de una
  búsqueda lineal corta, para intentar recuperar el beneficio de
  eliminar bins vacíos sin el coste de la búsqueda binaria completa.
- **Temporización real por nivel de profundidad** (Investigación 7,
  pendiente): diseñar una forma de medir tiempo por nivel sin que el
  propio temporizador distorsione la medición en funciones recursivas de
  alta frecuencia de llamada.
- **Repetir el análisis de correlación con más datasets con variación
  real de Difficulty Score.** `RandomUniform` y `Concentrated` no
  aportaron señal (varianza casi nula o muestra insuficiente,
  respectivamente); `NormalGaussian` fue el único caso informativo.
  Merece la pena construir datasets sintéticos con Difficulty Score
  variable de forma controlada para un análisis de correlación más
  potente.
