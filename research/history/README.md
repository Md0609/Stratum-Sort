# Dynamic Range Sort (DRS) — v8

Dynamic Range Sort (DRS) es un algoritmo de ordenación que, en lugar de
comparar elementos entre sí de forma global, primero analiza cómo se
distribuyen los valores de entrada, construye intervalos ("bins") a partir
de esa distribución, refina esos intervalos cuando hace falta, y solo
entonces ordena cada intervalo por separado con un algoritmo de comparación
convencional. El resultado final se obtiene concatenando los intervalos ya
ordenados, que quedan colocados en orden ascendente por construcción.

La idea general se resume en cinco pasos:

```
Analizar -> Construir intervalos -> Refinar intervalos -> Ordenar localmente -> Unir resultado
```

**v8 investigó un cambio estructural en `refine()` (eliminar un escaneo
redundante de min/max) y lo revirtió por completo tras medir cinco
variantes distintas, ninguna de las cuales pasó la validación en el
benchmark más común.** El código de producción de v8 es idéntico al de
v7. La investigación reveló algo más útil que la optimización en sí:
para datos sin redundancia, `refine()` solo representa el 13,7% del
tiempo total — `distribute()` y `localSort()` dominan. El informe
completo, incluyendo las cinco variantes y por qué cada una falló, está
en `documentacion/ANALYSIS_v8.md`.

## Estructura del proyecto

```
drs_project/
├── algoritmo/
│   ├── Config.hpp               # Constantes de produccion (sin flags experimentales)
│   ├── DRSMetrics.hpp           # Sistema de metricas (solo se compila con DRS_ENABLE_METRICS)
│   ├── DynamicRangeSort.hpp     # Declaracion de DynamicRangeSort<T>
│   ├── DynamicRangeSort.tpp     # Implementacion (algoritmo limpio, sin heuristicas retiradas)
│   └── versions/                 # Reconstrucciones historicas v1, v2, v3, v6 para comparar
│       ├── DRSv1.hpp
│       ├── DRSv2.hpp
│       ├── DRSv3.hpp
│       └── DRSv6_experimental.hpp  # microhistograma + bins por densidad + skip, aislado
├── benchmarks/
│   ├── SystemInfo.hpp
│   ├── BenchmarkRunner.hpp       # Requiere DRS_ENABLE_METRICS (usa .metrics())
│   ├── ProductionVsResearch.cpp  # Se compila 2 veces: mide el coste real de instrumentar
│   └── main.cpp                  # Bateria de benchmarks + comparacion vs std::sort
├── experimentos/                 # (build de investigacion)
├── analisis/                     # (build de investigacion)
├── datasets/
│   └── DatasetGenerator.hpp
├── documentacion/
│   ├── README.md
│   ├── ANALYSIS.md               # Historial v1 -> v4
│   ├── ANALYSIS_v5.md
│   ├── ANALYSIS_v6.md            # Por que se retiraron las 3 heuristicas
│   └── ANALYSIS_v7.md            # Este informe
├── tests/
│   └── main.cpp                  # Build de PRODUCCION: valida el codigo real
└── Makefile
```

## Producción vs investigación

El proyecto compila en dos configuraciones completamente distintas,
controladas por una única macro:

```cpp
#ifdef DRS_ENABLE_METRICS
    // DRSMetrics, debugPartitionOnly(), y todo punto de instrumentación
#endif
```

Sin `DRS_ENABLE_METRICS`, ese código no es que esté "desactivado": no
existe en el binario. Verificado con `nm`:

```bash
$ nm build/drs_tests | grep -ci metrics
0
```

`make test` compila y ejecuta la configuración de producción (rápido,
sin instrumentación). `make benchmarks`, `make experiments` y
`make analysis` compilan con `DRS_ENABLE_METRICS` (necesitan leer
`.metrics()`). `make overhead` compila el mismo archivo fuente dos veces,
una por configuración, para medir exactamente cuánto cuesta la
instrumentación — ver `ANALYSIS_v7.md`, sección 3.

## Cómo funciona el algoritmo

1. **Análisis (una pasada O(n)):** mínimo, máximo y longitud del array.
2. **Cálculo de los parámetros de intervalo:** a partir del rango
   observado y `targetElementsPerBin` (64 por defecto), cuántos
   intervalos crear y qué ancho tiene cada uno, acotado por el rango real.
3. **Distribución (una pasada O(n)):** cada elemento se coloca
   directamente en su posición final dentro de un buffer compartido
   (conteo y colocación al estilo counting sort).
4. **Refinamiento recursivo con buffers en cascada:** un intervalo que
   reúne más elementos de los deseados se subdivide usando únicamente su
   propio rango observado, escribiendo los hijos en el otro buffer
   compartido. Un intervalo de un solo valor se reconoce como ya
   ordenado y ni se subdivide ni se mueve.
5. **Ordenación local:** cada intervalo final se ordena con Insertion
   Sort, QuickSort o Introsort según su tamaño, tras comprobar en una
   sola pasada si ya está ordenado ascendente o descendentemente.
6. **Unión final:** los intervalos, ya en orden ascendente por
   construcción, se copian al array de salida.

Tres características que existieron en v6 (microhistograma, bins por
densidad, salto de subdivisión por Difficulty Score) **ya no forman parte
de este algoritmo**: se midieron como perjudiciales y se retiraron por
completo (no solo desactivadas) — ver `ANALYSIS_v6.md` para el porqué y
`ANALYSIS_v7.md`, sección 1, para dónde quedó ese código.

## Compilación y ejecución

Requiere un compilador con soporte para C++17 (probado con `g++`,
`-std=c++17 -O3 -Wall -Wextra`; `-O3` sustituye a `-O2` desde v7 tras una
comparación medida — ver `ANALYSIS_v7.md`, sección 8).

```bash
make                # compila los seis binarios (produccion + investigacion)
make test             # correctitud, build de PRODUCCION (segundos)
make benchmarks       # bateria ampliada + comparacion vs std::sort (minutos)
make experiments       # target dinamico, desorden, localidad, version comparison (minutos)
make analysis          # ajuste de complejidad por dataset (minutos)
make overhead           # mide el coste exacto de la instrumentacion
make clean
```

## Uso de la clase

```cpp
#include "DynamicRangeSort.hpp"

std::vector<int64_t> data = /* ... */;

drs::DynamicRangeSort<int64_t> sorter; // target=64 por defecto
sorter.sort(data);                     // ordena in-place
```

Con `DRS_ENABLE_METRICS` definida:

```cpp
const drs::DRSMetrics& m = sorter.metrics();
m.print(std::cout);

drs::DynamicRangeSort<int64_t> inspector;
auto leaves = inspector.debugPartitionOnly(data); // introspeccion, no ordena
```

`DynamicRangeSort<T>` es una plantilla restringida a tipos enteros.

## Estado del algoritmo

Resumen de las siete iteraciones (detalle completo en cada `ANALYSIS*.md`):

- **v1**: especificación original, subdivisión de un único nivel.
- **v2**: refinamiento recursivo acotado por profundidad.
- **v3**: pasadas fusionadas, `target` recalibrado (16 → 64).
- **v4**: buffers en cascada, sistema de métricas, ajuste de complejidad.
- **v5**: reestructuración del proyecto, estudios completos sobre todos
  los datasets — sin cambios en el algoritmo.
- **v6**: tres mecanismos nuevos implementados y medidos
  (microhistograma, bins por densidad, salto por Difficulty Score) —
  los tres resultaron netamente negativos.
- **v7**: las tres heurísticas de v6 se **eliminan** de producción (no
  solo se desactivan). Separación real producción/investigación vía
  macro de compilación. Optimizaciones de coste constante medidas y
  conservadas: reutilización de scratch buffers (~7-10%),
  simplificación de `refine()`, `-O3` por defecto.
- **v8** (esta versión): investigación de un cambio estructural en
  `refine()` (min/max de un bin como subproducto gratuito de la pasada
  de colocación del nivel padre, en vez de recalculado con un escaneo
  propio). Cinco variantes implementadas y medidas rigurosamente
  (rastreo incondicional, condicional por bucket, sin rama, híbrido por
  nivel, condicionado por el principio del palomar) — **ninguna superó
  a v7 en el benchmark `RandomUniform`**, así que se revirtió por
  completo. El código de producción de v8 es idéntico al de v7. La
  investigación sí dejó un hallazgo real: el reparto de tiempo por fase
  muestra que `refine()` es solo el 13,7% del tiempo total en datos sin
  redundancia — `distribute()` (46,8%) y `localSort()` (34,1%) dominan,
  lo que apunta a dónde debería mirar la siguiente iteración.
