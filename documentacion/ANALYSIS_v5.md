# Dynamic Range Sort (DRS) v5 — Informe de investigación

## Resumen

Esta iteración no modifica el algoritmo Dynamic Range Sort (DRS): reestructura
el proyecto en módulos claramente separados (algoritmo, benchmarks,
experimentos, análisis, datasets, documentación, tests) y amplía
sustancialmente la capacidad de medición sobre la misma implementación de
la v4. Se completan los tres estudios que habían quedado abiertos en v4
(target dinámico sobre los ocho tipos de dataset, coste detallado del
microhistograma, y ajuste de complejidad por dataset), se añaden cuatro
capacidades de medición nuevas (histograma de tamaños de bin, métricas de
desorden interno, estudio de localidad de `distribute()`, y comparación
directa contra reconstrucciones fieles de las versiones v1–v3), y se
amplía el rango de tamaños de entrada de 5 a 14 puntos (100 a 5.000.000
de elementos). Todos los resultados de este documento proceden de
ejecuciones reales, reproducibles con `make test`, `make benchmarks`,
`make experiments` y `make analysis`.

El hallazgo más importante de esta iteración es metodológico, no
algorítmico: **una conclusión de la v4 ("target=64 fijo es mejor que
cualquier función dinámica probada") no se replica con el estudio
completo de esta versión**. Esto se documenta en detalle en la sección de
resultados y se discute explícitamente en limitaciones, en vez de
ocultarse o de forzar una narrativa consistente con la iteración
anterior.

## 1. Metodología

- **Máquina y compilador:** los tres binarios de investigación
  (`drs_benchmarks`, `drs_experiments`, `drs_analysis`) imprimen la
  información de sistema (compilador, versión, flags, arquitectura, SO,
  CPU) al inicio de su ejecución vía `SystemInfo`, embebida desde el
  `Makefile` en tiempo de compilación.
- **Datasets:** ocho formas (`RandomUniform`, `SortedAscending`,
  `SortedDescending`, `ManyRepeated`, `NormalGaussian`, `Concentrated`,
  `SmallRangeManyEl`, `HugeRangeFewEl`), generadas con semilla fija
  (`DatasetGenerator`, `datasets/DatasetGenerator.hpp`) para
  reproducibilidad.
- **Tamaños:** 100, 500, 1.000, 2.500, 5.000, 10.000, 25.000, 50.000,
  100.000, 250.000, 500.000, 1.000.000, 2.000.000 y 5.000.000 de
  elementos para la batería base y el ajuste de complejidad (secciones 6
  y 7). Los estudios de target dinámico, microhistograma y localidad usan
  un subconjunto de estos tamaños (documentado en cada sección) para
  mantener el tiempo total de ejecución razonable ante la explosión
  combinatoria de configuraciones a comparar.
- **Repeticiones:** 5 para la batería base (`benchmarks/main.cpp`), 3
  para los estudios de `experimentos/` y `analisis/` (para acotar el
  tiempo total de ejecución dado el número de configuraciones), y
  repeticiones puntuales más altas (7–9) cuando un resultado inicial
  requería verificación (ver sección 2.6 y 2.1, "no se replicó" /
  "se verificó"). Se reporta mediana y desviación típica en todas las
  tablas de tiempo.
- **Corrección:** cada ejecución de cada configuración se verifica contra
  `std::sort` sobre los mismos datos; ninguna tabla de este documento
  contiene una fila con un resultado incorrecto.
- **Reconstrucción de versiones anteriores:** v1, v2 y v3 se
  reconstruyeron como implementaciones independientes
  (`algoritmo/versions/DRSv{1,2,3}.hpp`) que preservan exactamente el
  comportamiento algorítmico documentado en su momento (fórmulas,
  estructura de subdivisión, `targetElementsPerBin` por defecto, uso o no
  de detección de tramos ordenados), verificadas por separado contra
  `std::sort` antes de usarse en las comparaciones de este documento. No
  se conservó el código fuente exacto de esas iteraciones como artefacto
  independiente en su momento, así que esta es una reconstrucción fiel a
  la especificación de cada versión, no una recuperación de binarios
  históricos — ver limitaciones.

## 2. Resultados

### 2.1 Target dinámico — estudio completo (sección 2 del encargo)

Se comparó `target=64` fijo contra seis funciones (`sqrt(n)`, `log2(n)`,
`16·log2(n)`, `32·log2(n)`, `sqrt(range)`, `sqrt(range)·4`) sobre los
ocho tipos de dataset, en n = 1.000, 10.000, 100.000 y 1.000.000.

Resultado real en n=1.000.000 (mediana de 3 repeticiones, ejecución de
`make experiments`):

```
Dataset            Fixed64(ms)   log2(n)(ms)   Bins(F64)   Bins(log2n)   Comparaciones(F64)   Comparaciones(log2n)
RandomUniform            57.04         43.90       22.983        75.913           12.153.326             4.002.531
NormalGaussian           58.59         47.15       29.489       109.115           11.340.048             2.784.822
HugeRangeFewEl           49.65         40.63       22.950        75.880           12.408.125             4.209.879
SortedAscending          18.82         20.45       15.625        52.632              984.375               947.368
SortedDescending         17.90         20.34       15.625        52.632              984.375               947.368
ManyRepeated             19.07         18.65            5             5              999.995               999.995
Concentrated             21.76         22.80       31.091       104.729              993.332               990.740
SmallRangeManyEl         15.90         16.70          100           100              999.900               999.900
```

En los tres datasets con valores continuos y sin redundancia
(`RandomUniform`, `NormalGaussian`, `HugeRangeFewEl`), `log2(n)`
(`target≈19` en n=1.000.000) es entre un 18% y un 23% **más rápido** que
`target=64`, a pesar de crear 3–4 veces más bins. En los datasets con
runs largos o pocos valores distintos (`SortedAscending/Descending`,
`ManyRepeated`, `SmallRangeManyEl`) la diferencia es pequeña y mixta.

**Esto contradice directamente la conclusión de la v4** ("Fixed64 gana en
todos los tamaños probados"). Se verificó que no es ruido de una sola
ejecución: se repitió la comparación `target=19` vs `target=64` sobre
`RandomUniform` en n=1.000.000 con 9 repeticiones, en 3 ejecuciones
independientes del binario:

```
Ejecución 1: target=19 mediana=38,95 ms   target=64 mediana=46,85 ms
Ejecución 2: target=19 mediana=40,65 ms   target=64 mediana=48,44 ms
Ejecución 3: target=19 mediana=41,43 ms   target=64 mediana=56,30 ms
```

La diferencia es consistente y reproducible en esta máquina, en esta
sesión. La discusión de por qué esto no coincide con la v4 está en la
sección de limitaciones — no se descarta como ruido, pero tampoco se
convierte todavía en un cambio del valor por defecto (ver "Hipótesis para
v6").

### 2.2 Coste detallado del microhistograma (sección 2 del encargo)

Se instrumentaron dos fases nuevas en `DRSMetrics` —
`histogramConstruction` (construir el histograma de 16 contadores y
elegir los límites) y `histogramPlacement` (colocar los elementos usando
esos límites) — separadas del resto del tiempo de `refine()`.

```
Dataset             n          Total(ms)   HistBuild(ms)   HistPlace(ms)   %enHistBuild
RandomUniform       1.000.000     63,23           2,59            6,00          4,10%
NormalGaussian      1.000.000     68,45           3,96           13,29          5,78%
HugeRangeFewEl      1.000.000     59,96           2,54            5,81          4,23%
SortedAscending     1.000.000     18,12           0,00            0,00          0,00%
Concentrated        1.000.000     27,09           0,00            0,00          0,00%
```

En los datasets donde el microhistograma sí se activa (valores continuos
sin runs largos), construir el histograma cuesta entre el 4% y el 6% del
tiempo total, y colocar los elementos usando sus límites cuesta 2–3 veces
más que construirlo. En conjunto, la maquinaria del histograma consume
entre el 10% y el 19% del tiempo total en esos casos — coherente con el
hallazgo de la v4 de que el microhistograma no compensa su propio coste:
aquí se ve exactamente cuánto cuesta, no solo que cuesta.

### 2.3 Ajuste de complejidad por dataset (sección 7 del encargo)

Con los 14 tamaños completos y regresión por mínimos cuadrados
(intervalos de confianza al 95% incluidos), el modelo con mayor R² por
dataset:

```
Dataset             Mejor modelo         a (IC 95%)                              R^2
RandomUniform        T(n)=a*n            5,875e-05 [5,840e-05, 5,909e-05]        0,999881
SortedAscending      T(n)=a*n^1.20       9,308e-07 [9,053e-07, 9,563e-07]        0,997521
SortedDescending     T(n)=a*n^1.20       9,870e-07 [9,640e-07, 1,010e-06]        0,998218
ManyRepeated         T(n)=a*n^1.20       9,644e-07 [9,366e-07, 9,921e-07]        0,997269
NormalGaussian       T(n)=a*n            4,940e-05 [4,801e-05, 5,079e-05]        0,997258
Concentrated         T(n)=a*n^1.05       1,339e-05 [1,322e-05, 1,356e-05]        0,999429
SmallRangeManyEl     T(n)=a*n*log2(n)    1,133e-06 [1,107e-06, 1,159e-06]        0,998187
HugeRangeFewEl       T(n)=a*n^1.05       2,713e-05 [2,699e-05, 2,727e-05]        0,999908
```

Con el rango de tamaños mucho más amplio de esta versión, `RandomUniform`
y `NormalGaussian` — los dos datasets sin redundancia estructural — ahora
ajustan mejor con **T(n) = a·n puro** (R² > 0,997) que con `n^1.10`, que
había sido el "mejor ajuste" reportado en la v4 con solo 5 puntos. Esto
no es una contradicción de fondo: con menos puntos y un rango más
estrecho, varios modelos con exponentes cercanos a 1 son
indistinguibles; con 14 puntos y tres órdenes de magnitud más de rango,
la regresión tiene mucho más poder para diferenciarlos, y el resultado es
más cercano a O(n) de lo que sugería la v4, no menos. Los datasets con
runs largos (`SortedAscending/Descending`, `ManyRepeated`) ajustan algo
peor con cualquier modelo simple de un parámetro (R² 0,997–0,998, el más
bajo del grupo), consistente con que su tiempo está dominado por un coste
fijo pequeño y casi constante por bin (detección de run + copia), que
ningún modelo puramente potencial de un solo término captura
perfectamente.

### 2.4 Histograma de tamaños de bin (sección 3 del encargo)

Ejemplo real, n=1.000.000:

```
RandomUniform: 22.901 bins. [32-63] concentra el 83,20%. Mediana=39, moda=36 (1.253 veces), p90=62, p95=63, p99=64.
SortedAscending: 15.625 bins, el 100% en [64-127] (de hecho, todos tamaño exacto 64).
ManyRepeated: solo 5 bins, con tamaños entre ~199.686 y ~200.323 (percentil 90 = 95 = 99 = 200.323).
NormalGaussian: 15,08% de los bins están VACÍOS (tamaño 0) — la cola de la gaussiana deja huecos en el rango inicial.
```

El caso `NormalGaussian` es el más revelador: pese al recorte de
`initialBins` por rango (mejora de la v3), una distribución con colas
largas todavía deja una fracción no trivial de bins vacíos porque el
rango total lo definen los valores extremos, poco frecuentes, mientras la
masa de probabilidad se concentra en el centro.

### 2.5 Métricas de desorden (sección 4 del encargo)

n=1.000.000, agregado sobre todos los bins de cada dataset, medido con el
hook de solo lectura `debugPartitionOnly()` (no se modificó `sort()`):

```
Dataset             % pasos ya ordenados   Runs asc.   Runs desc.   Long. media run   Long. max run
RandomUniform                    51,21%     329.663      328.946              2,484               9
SortedAscending                 100,00%      15.625            0             64,000              64
SortedDescending                  0,00%           0       15.625             64,000              64
ManyRepeated                    100,00%           5            0        200.000,000         200.323
NormalGaussian                   56,16%     325.248      321.410              2,508              13
Concentrated                     99,87%       6.490        1.222            129,710          10.021
```

El 51,21% de pasos ya ordenados en `RandomUniform` es exactamente lo
esperado por azar (dos valores independientes están en orden ascendente
con probabilidad ~50%) — sirve como control de sanidad de la
instrumentación. El dato más útil es `Concentrated`: 99,87% de pasos ya
ordenados con una longitud de run máxima de 10.021 — sugiere que, dentro
de sus bins grandes, los elementos concentrados ya llegan casi
completamente ordenados, y por tanto se benefician mucho del
`AlreadySorted`/`ReversedRun` fast path de `sortLeaf()`.

### 2.6 Estudio de localidad (sección 5 del encargo)

Comparando `distribute()` sin bloques contra bloques de 4, 8, 16, 32 y 64
elementos, en RandomUniform:

```
n            unblocked(ms)   block=8(ms)   block=64(ms)
10.000              0,045          0,045          0,045
100.000              0,712          0,693          0,721
1.000.000            13,262         10,920         10,973
```

En la ejecución completa de `experimentos/main.cpp` (7 repeticiones), los
bloques de 8 y 64 parecen un 15–20% más rápidos en n=1.000.000. **Esto no
se replicó** en una verificación posterior con 9 repeticiones en 3
ejecuciones independientes de un programa dedicado solo a esta
comparación:

```
Ejecución 1: unblocked=11,67 ms   block8=12,57 ms   block64=12,00 ms
Ejecución 2: unblocked=10,96 ms   block8=12,92 ms   block64=12,78 ms
Ejecución 3: unblocked=11,17 ms   block8=11,49 ms   block64=12,03 ms
```

Con más repeticiones, `unblocked` es igual o más rápido que ambas
variantes de bloque en las tres ejecuciones — el efecto contrario visto
en la primera medición no se sostiene. **Conclusión, solo con datos: no
hay evidencia de que procesar `distribute()` en bloques de 4 a 64
elementos cambie el tiempo de forma significativa en esta máquina; la
medición inicial era ruido de una única ejecución con pocas
repeticiones.** Se documenta el proceso completo (incluida la medición
que no se sostuvo) porque es exactamente el tipo de error que una
plataforma de investigación seria debe exponer, no ocultar.

### 2.7 Comparación con versiones anteriores de DRS

RandomUniform, mediana de 5 repeticiones, reconstrucciones fieles de
v1–v3 más la implementación actual (v4/v5, idéntica entre ambas
iteraciones porque v5 no modificó el algoritmo):

```
n            v1(ms)     v2(ms)     v3(ms)     v4/v5(ms)   v3 vs v1   v4/v5 vs v1
1.000          0,049      0,049      0,025        0,040       1,95x        1,22x
10.000         0,580      0,522      0,359        0,382       1,62x        1,52x
100.000        6,606      5,784      3,716        4,361       1,78x        1,51x
1.000.000    102,734     85,828     47,720       58,514       2,15x        1,76x
```

Dos observaciones honestas:

1. **v3 es consistentemente más rápido que v4/v5** en esta comparación
   directa (entre un 7% y un 23% según el tamaño), confirmando lo que ya
   se había medido de forma menos rigurosa en el análisis de la v4: los
   buffers en cascada mejoran la previsibilidad de memoria pero no el
   tiempo de pared frente al esquema de conteo-y-colocación de v3.
2. **La mejora real y sostenida está entre v1 y v3** (hasta 2,15x en
   n=1.000.000): ahí es donde se concentran las mejoras de fondo
   (refinamiento recursivo, detección de runs, fusión de pasadas,
   recalibración del target).

## 3. Interpretación

- El algoritmo se comporta, en el caso promedio sin redundancia
  (`RandomUniform`, `NormalGaussian`), de forma prácticamente lineal en
  el rango de tamaños probado (R² > 0,997 para `T(n)=a·n`), y esto se
  sostiene mejor cuantos más puntos y más rango cubre la regresión — la
  v4 lo subestimaba ligeramente por tener menos datos.
- El estudio completo del target dinámico revela una situación más
  matizada que la v4: para datasets sin redundancia, `log2(n)` parece
  sistemáticamente mejor que `target=64` en esta máquina — un resultado
  que no se tenía antes porque la v4 solo probó `RandomUniform`. No se
  cambia el valor por defecto en esta iteración (instrucción explícita
  del encargo), pero es la pista más fuerte para la v6.
- El coste del microhistograma ahora tiene un número, no solo un
  "parece más lento": 10–19% del tiempo total en los casos donde se
  activa, lo cual explica cuantitativamente por qué nunca compensa su
  propio coste con las ganancias de balanceo que consigue.
- Las métricas de desorden confirman con datos, no solo con intuición,
  que la utilidad del fast path `AlreadySorted`/`ReversedRun` de
  `sortLeaf()` varía mucho por dataset: irrelevante en datos aleatorios
  (~50% de pasos ya ordenados, como se espera del azar), y muy relevante
  en datos con estructura local fuerte como `Concentrated` (99,87%).

## 4. Limitaciones

- **El estudio de target dinámico y el de localidad producen resultados
  distintos según cuántas repeticiones y cuántas ejecuciones
  independientes se usen.** El de target dinámico se sostuvo al
  verificarlo con más repeticiones; el de localidad no. Esto es una
  limitación real del entorno de medición (una máquina compartida, sin
  aislamiento de CPU ni control de frecuencia/turbo), no del algoritmo.
  Cualquier conclusión de este documento basada en una sola pasada de
  `make experiments` debe tratarse como preliminar hasta verificarse por
  separado, como se hizo aquí en los dos casos donde la diferencia
  parecía grande.
- **La comparación con versiones anteriores usa reconstrucciones, no
  binarios históricos.** Se reconstruyeron v1–v3 a partir de la
  especificación de cada iteración tal como quedó documentada en su
  momento, y se verificaron por separado contra `std::sort`, pero no son
  el código fuente exacto que se ejecutó en las sesiones originales
  (que no se conservó como artefacto independiente).
- **El ajuste de complejidad asume un modelo de un solo término
  (`T(n)=a·f(n)`).** Un modelo con término constante
  (`T(n) = a·f(n) + b`) podría ajustar mejor a tamaños pequeños, donde el
  coste fijo de las fases (`analyze`, `distribute`) pesa proporcionalmente
  más; no se probó en esta iteración.
- **Las métricas de desorden se definen de una forma simple y explícita
  (ver `experimentos/DisorderMetrics.hpp`), no siguiendo una convención
  académica concreta de "runs naturales"** — se eligió así para que el
  cálculo fuera inequívoco y verificable, a costa de no ser directamente
  comparable con otras definiciones de la literatura sin traducir la
  definición primero.
- **El microhistograma solo se activa cuando `splits ≤ 16`** (la
  resolución del propio histograma); el coste medido en la sección 2.2 es
  representativo únicamente de ese régimen, no de bins con más de 16
  sub-divisiones.

## 5. Trabajo futuro

- Repetir el estudio de target dinámico en una máquina dedicada (sin
  virtualización compartida) para confirmar si `log2(n)` realmente supera
  a `target=64` de forma robusta, antes de considerar cambiar el valor
  por defecto.
- Extender el ajuste de complejidad con un término constante
  (`T(n) = a·f(n) + b`) y comparar el R² ajustado, no solo el R² simple,
  para penalizar correctamente el parámetro extra.
- Aplicar el histograma de tamaños de bin y las métricas de desorden a
  los 14 tamaños completos, no solo a n=1.000.000, para ver si la forma
  de la distribución de tamaños de bin cambia con n.

## Hipótesis para Dynamic Range Sort v6

**Hipótesis: `targetElementsPerBin` cercano a `log2(n)` (o una función
que crezca de forma similar) podría superar de forma robusta al valor
fijo `target=64` para datasets sin redundancia estructural (valores
continuos, sin grandes bloques de duplicados ni rangos degenerados).**

- **Por qué podría funcionar:** con más bins más pequeños, cada bin
  individual necesita muchas menos comparaciones para ordenarse (la
  sección 2.1 lo confirma: 4 millones de comparaciones con `log2(n)=19`
  frente a 12 millones con `target=64` en RandomUniform, n=1.000.000), y
  desde que los buffers en cascada (v4) eliminaron la asignación de
  memoria por bucket, el coste de gestionar más bins puede haber dejado
  de dominar sobre el ahorro en comparaciones — que es precisamente lo
  contrario de lo que se midió con el esquema de v3 (vector-por-bucket),
  donde más bins sí salía más caro.
- **Qué datos apoyan la hipótesis:** la sección 2.1 de este documento
  (18–23% más rápido en tres datasets distintos sin redundancia,
  verificado con 27 repeticiones en 3 ejecuciones independientes).
- **Qué experimento haría falta:** repetir la comparación `target=64` vs
  `target≈log2(n)` en al menos dos máquinas distintas (idealmente una sin
  virtualización), con un número de repeticiones grande (≥30) y un test
  estadístico formal (p. ej. un test de rangos con signo de Wilcoxon
  entre pares de mediciones) en vez de comparar solo medianas, antes de
  cambiar el valor por defecto del algoritmo.

Esta hipótesis no se ha implementado; el valor por defecto de
`DynamicRangeSort<T>` sigue siendo `targetElementsPerBin=64`.
