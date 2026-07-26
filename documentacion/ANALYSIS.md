# Dynamic Range Sort (DRS) — Análisis experimental (v4)

Esta iteración no optimiza el algoritmo: convierte el proyecto en una
plataforma de investigación. La filosofía de cinco pasos y la regla de
subdividir siempre por rango observado no cambian. Lo que cambia es:

1. Un sistema de métricas mucho más completo (sección 1 del encargo).
2. Ajuste de complejidad por mínimos cuadrados contra varios modelos
   matemáticos, en vez de una lectura visual de `tiempo/n` (sección 2).
3. Una variante experimental con microhistograma por bin, opcional y sin
   sustituir el algoritmo base (sección 3).
4. Un estudio de `targetElementsPerBin` dinámico frente a varias funciones
   candidatas (sección 4).
5. Una validación automática que compara las tres variantes con datos
   reales (sección 5).

Además, se implementó la mejora de "buffers en cascada" que había quedado
documentada como abierta en la v3.

Todos los números de este documento proceden de ejecuciones reales de
`build/drs_tests` en esta máquina. Reproducibles con `make test`. El
propio binario imprime la información de sistema/compilador al principio
de cada ejecución (ver sección "Información del sistema" más abajo).

## 0. Buffers en cascada (mejora pendiente de la v3, implementada aquí)

`refine()` en v3 seguía construyendo un `std::vector<T>` por cada bucket
en cada nivel de recursión (con conteo previo para reservar la capacidad
exacta, pero un `vector` nuevo por bucket de todas formas). En v4 esto se
sustituyó por dos buffers compartidos (`bufferA_`, `bufferB_`) de tamaño
`n`, asignados una sola vez por llamada a `sort()`. Cada nodo del árbol de
refinamiento es ahora solo un rango `[start, start+count)` dentro de uno
de los dos buffers; al subdividir, un nodo escribe sus hijos en el
**otro** buffer, en las mismas posiciones absolutas — la misma técnica
que usa la fase de distribución de un MSD radix sort, pero con los
límites de bucket calculados con las fórmulas de rango observado propias
de DRS, no con dígitos. Esto elimina prácticamente toda asignación de
memoria dinámica dentro de `refine()`.

Efecto medido (RandomUniform, mediana de 7 repeticiones, mismo `target=64`
en ambas versiones):

```
n            v3 (count-then-place)   v4 (buffers en cascada)
1.000                     0,052 ms                  0,046 ms
10.000                    0,493 ms                  0,497 ms
100.000                   4,942 ms                  4,690 ms
1.000.000                56,466 ms                 60,434 ms
```

El tiempo de pared queda prácticamente empatado (dentro del ruido de la
máquina para n≤100.000, y una diferencia de ~7% a favor de v3 en
n=1.000.000). Esto es un resultado honesto que vale la pena señalar: la
mejora de v4 **no** es de velocidad — es de previsibilidad de memoria. Con
buffers en cascada, el uso de memoria total es exactamente
`2 · n · sizeof(T)` más una estructura de árbol pequeña (columna
`MemApprox` de las tablas de abajo: 16.706.368 bytes para n=1.000.000 con
`int64_t`, que son casi exactamente `2 · 1.000.000 · 8`), en vez de
depender de cuántos `vector` intermedios haya creado y liberado el
recolector de memoria durante la recursión, que v3 nunca llegó a medir de
forma exacta. Queda documentado como un cambio arquitectónico con un
objetivo distinto al de rendimiento puro — coherente con el encargo de
esta iteración ("no quiero optimizar todavía").

## 1. Sistema de métricas ampliado

`DRSMetrics` ahora incluye, además de lo ya existente en v3: memoria
aproximada (instrumentada explícitamente en cada asignación de buffer, no
inferida — ver `addApproxMemory()`), y sigue reportando el tiempo de cada
fase con los nombres exactos pedidos: `analyze`, `distribute`, `refine`,
`localSort`, `merge`.

Se añadió `SystemInfo` (compilador, versión, flags de compilación
exactos — inyectados desde el `Makefile`, no adivinados —, arquitectura,
sistema operativo y modelo de CPU vía `/proc/cpuinfo`) y `BenchmarkRunner`,
que ejecuta N repeticiones, verifica que el array quedó bien ordenado en
cada una, y calcula mínimo, máximo, media, mediana y desviación típica del
tiempo. Los contadores deterministas (comparaciones, subdivisiones,
bins, memoria) se toman de una sola ejecución representativa, porque no
varían entre repeticiones para la misma entrada — solo el tiempo de pared
está sujeto a repetirse y agregarse.

Ejemplo real de cabecera de sistema impresa por el binario:

```
Compiler:        GCC 13.3.0
Compile flags:   -std=c++17 -O2 -Wall -Wextra -Iinclude
Architecture:    x86_64
Operating system:Linux
CPU:             Intel(R) Xeon(R) Processor @ 2.80GHz
```

## 2. Ajuste de complejidad (regresión por mínimos cuadrados)

Se comparan los seis modelos pedidos, cada uno de la forma `T(n) = a·f(n)`,
ajustando `a` por mínimos cuadrados (fórmula cerrada de una sola variable,
sin necesidad de solver iterativo) contra los tiempos medianos de
`RandomUniform` en n = 100, 1.000, 10.000, 100.000 y 1.000.000. El
resultado real (no interpretación subjetiva, tabla completa):

```
Model                                a               SSE         R^2
--------------------------------------------------------------------
T(n) = a*n                  7.0687e-05        3.2596e+00    0.999158
T(n) = a*n*log2(n)          3.5530e-06        3.9071e-01    0.999899
T(n) = a*n^1.05             3.5472e-05        1.0636e+00    0.999725
T(n) = a*n^1.10             1.7794e-05        1.2739e-01    0.999967  <- mejor ajuste (R^2 mas alto)
T(n) = a*n^1.20             4.4747e-06        7.1532e-01    0.999815
T(n) = a*sqrt(n)            6.5334e-02        3.0769e+02    0.920555
```

El modelo con mayor R² (criterio objetivo estándar para "qué modelo
explica mejor los datos" entre ajustes por mínimos cuadrados) es
**T(n) = a·n^1.10**, con R² = 0,999967 y el SSE más bajo de los seis. El
modelo puramente lineal (`a·n`) también ajusta muy bien (R² = 0,999158)
pero peor que `n^1.10` y que `n·log2(n)`; `sqrt(n)` queda claramente
descartado (R² = 0,92, muy por debajo del resto). Esto es coherente con
lo que ya se observaba en la v3 de forma menos formal (el cociente
`tiempo/(n·log2 n)` decrecía en vez de mantenerse constante): el
comportamiento medido está entre O(n) y O(n log n), y la regresión lo
sitúa de forma concreta muy cerca de n^1,1 — más cerca de O(n) que de
O(n log n), sin llegar a ser lineal puro.

## 3. Microhistograma experimental

Cada llamada a `refine()` que necesita subdividir, y cuyo número de
sub-bins (`splits`) no supera `MICRO_HISTOGRAM_BUCKETS` (16), puede
construir un histograma de 16 contadores sobre el rango observado del bin
y usar su distribución acumulada para elegir límites de sub-bin no
uniformes (en vez del ancho uniforme del algoritmo base), buscando hijos
más equilibrados en tamaño. Se activa con el parámetro `useMicroHistogram`
del constructor (por defecto sigue la constante de compilación
`USE_MICRO_HISTOGRAM`, `0` por defecto) — el algoritmo base no se ha
tocado ni sustituido.

Resultado real (mediana de 5 repeticiones, mismo `target=64` en ambas
variantes, n=1.000.000):

```
Dataset             baseline (ms)   microhist (ms)   Diferencia
RandomUniform               67,20            72,58   +8,0% (peor)
SortedAscending             26,36            26,65   +1,1% (peor)
SortedDescending            28,25            28,57   +1,1% (peor)
ManyRepeated                26,45            27,70   +4,7% (peor)
NormalGaussian              65,46            77,29   +18,1% (peor)
Concentrated                38,97            39,61   +1,6% (peor)
SmallRangeManyEl            22,17            22,76   +2,7% (peor)
HugeRangeFewEl              58,90            66,64   +13,1% (peor)
```

**El microhistograma no ayuda con esta implementación: es consistentemente
igual o más lento que el algoritmo base, en los ocho tipos de dataset
probados, sin una sola excepción.** La razón más probable, visible en las
columnas `Bins`/`Comparisons` de las tablas completas (sección
"Datos completos"): el número de bins y comparaciones apenas cambia
respecto al baseline (por ejemplo, `RandomUniform` en n=1.000.000 tiene
22.983 bins en ambas variantes), lo que indica que el split uniforme ya
producía una distribución de tamaños razonablemente equilibrada para
estos datasets — el histograma no encuentra un reparto mejor que valga la
pena, y el coste de construirlo (una pasada O(k) adicional por cada nodo
subdividido) se paga sin compensación. El caso donde más penaliza
(`NormalGaussian`, +18%) es precisamente el que tiene una distribución con
más curvatura dentro de cada bin — y aun así el histograma de 16
contadores no consigue aprovechar esa forma lo suficiente como para
compensar su propio coste.

Conclusión, según los datos: **no se recomienda activar
`USE_MICRO_HISTOGRAM`** con la implementación actual. Se mantiene en el
código como característica experimental opcional, tal como pedía el
encargo, no como mejora recomendada.

## 4. Target dinámico experimental

Se comparó `target=64` fijo contra `sqrt(n)`, `log2(n)`, `16·log2(n)`,
`32·log2(n)`, `sqrt(range)` y `sqrt(range)·4`, sobre `RandomUniform` en
n = 1.000, 10.000, 100.000 y 1.000.000 (una sola distribución representativa
para esta comparación de estrategias entre sí; la sección de validación
vuelve a comprobar la estrategia ganadora contra los ocho datasets).

Resultado real en n=1.000.000 (mediana de 5 repeticiones):

```
Strategy                    target      Median(ms)     Bins    Comparisons
Fixed64                          64          59,22    22.983     12.147.177
sqrt(n)                        1000          67,24     1.486      9.655.118
log2(n)                          19          61,06    75.924      4.007.858
16*log2(n)                      318          64,41     4.672      7.716.162
32*log2(n)                      637          68,43     2.355      8.877.983
sqrt(range)                     999          68,70     1.494      9.655.423
sqrt(range)*4                  3999          74,83       358     12.076.140
```

`Fixed64` gana en tiempo total sumado sobre los cuatro tamaños probados
(64,27 ms de mediana sumada, frente a >64,4 ms de todas las demás
estrategias). El patrón es el mismo que ya reveló el barrido de la v3:
`log2(n)` reduce mucho las comparaciones (4 millones frente a 12 millones
con `target=64`) pero dispara el número de bins (75.924 frente a 22.983),
y ese overhead de gestión por bin vuelve a pesar más que el ahorro en
comparaciones. En el extremo opuesto, `sqrt(range)*4` reduce los bins a
solo 358 pero cada uno es tan grande que las comparaciones casi igualan a
`target=64` sin ganar nada en overhead de gestión. `Fixed64` resulta ser,
con los datos disponibles, un punto cercano al óptimo entre ambos
extremos — no por asunción, sino porque ninguna de las seis funciones
alternativas lo superó en ningún tamaño probado.

## 5. Validación

Comparando las tres variantes (`DRS actual` con `target=64`,
`DRS + microhistograma`, `DRS + target dinámico` con la estrategia
ganadora de la sección 4, `Fixed64`) sobre los ocho tipos de dataset a
n=1.000.000:

```
Dataset             actual (ms)   microhist (ms)   dynTarget (ms)
RandomUniform              67,28            72,58            70,72
SortedAscending            19,23            19,24            19,71
SortedDescending           20,45            20,73            19,86
ManyRepeated               19,36            20,45            19,36
NormalGaussian             67,43            78,32            66,77
Concentrated               37,08            38,90            39,14
SmallRangeManyEl           25,08            24,29            23,92
HugeRangeFewEl             61,15            69,18            60,38
```

Como `Fixed64` fue también la estrategia ganadora del estudio de target
dinámico, `[dynTarget]` y `[actual]` usan aquí el mismo valor de
`target` — las pequeñas diferencias entre columnas son ruido de
repetición, no un efecto real, y sirven como control de sanidad: cuando
dos configuraciones son idénticas, sus tiempos deberían solaparse dentro
del margen de ruido, y así ocurre. `[microhist]` sigue siendo igual o
peor en 6 de los 8 datasets, confirmando la sección 3.

**Conclusión de esta iteración, solo con datos:** ni el microhistograma
ni ninguna función de `target` dinámica probada mejoran sobre la
configuración base (`target=64` fijo, sin microhistograma) para los
datasets y tamaños de este proyecto. La configuración por defecto de DRS
no cambia como resultado de esta investigación.

## Datos completos

La ejecución completa de `make test` imprime, para cada combinación de
dataset y tamaño: mediana, desviación típica, corrección, número de bins,
comparaciones y memoria aproximada — para las secciones 1 (batería base),
3 (microhistograma) y 5 (validación). Se omiten aquí por espacio; están
disponibles reproduciendo `make test` (tarda unos minutos por la cantidad
de configuraciones y repeticiones).

## Trabajo abierto

- **Paralelizar el estudio de target dinámico a los ocho datasets**
  directamente, en vez de solo `RandomUniform` seguido de una validación
  puntual — ahora mismo es una decisión razonable por tiempo de ejecución,
  no una limitación técnica.
- **Investigar por qué el microhistograma no ayuda:** con más tiempo,
  merecería la pena medir específicamente el coste de construir el
  histograma frente al coste del split uniforme que sustituye, en vez de
  solo el tiempo total — la respuesta actual (sección 3) es una hipótesis
  razonable a partir de los datos de bins/comparaciones, no una medición
  directa de ese sub-coste.
- **Ajustar modelos de complejidad también para los datasets no
  uniformes** (`NormalGaussian`, `Concentrated`, etc.), no solo
  `RandomUniform` — quedó fuera de esta iteración por alcance.

Ninguna de estas queda implementada en `include/`; se documentan aquí para
una futura iteración.
