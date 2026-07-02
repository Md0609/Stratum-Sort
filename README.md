# Dynamic Range Sort (DRS) — v2

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

Este repositorio contiene una implementación en C++17 de DRS, junto con un
sistema de métricas internas y una batería de pruebas que compara su
rendimiento contra `std::sort` y `std::stable_sort` sobre distintos tipos
de datos y tamaños.

## Cómo funciona el algoritmo

1. **Análisis (una pasada O(n)):** se recorre el array una vez para
   obtener el valor mínimo, el máximo, la longitud y si el array ya está
   ordenado.
2. **Construcción de intervalos iniciales:** a partir del rango de valores
   observado y de un tamaño objetivo de elementos por intervalo (16 por
   defecto), se calcula cuántos intervalos crear y qué ancho debe tener
   cada uno. El número de intervalos queda acotado por el rango de valores
   real, para no crear más intervalos de los que ese rango puede llegar a
   ocupar.
3. **Primera pasada:** cada elemento actualiza el conteo, el mínimo y el
   máximo observados de su intervalo, y su índice de intervalo queda
   guardado para reutilizarlo más adelante sin recalcularlo.
4. **Refinamiento recursivo:** un intervalo que reúne más elementos de los
   deseados se subdivide usando únicamente el rango de valores que
   realmente contiene (no el rango original del array completo). Si
   después de subdividirse un sub-intervalo sigue siendo demasiado grande,
   vuelve a subdividirse de la misma forma, hasta un límite de
   profundidad. Un intervalo en el que todos los elementos son idénticos
   se reconoce como ya ordenado y no se subdivide ni se compara.
5. **Ordenación local:** cada intervalo final (hoja del árbol de
   refinamiento) se ordena con el algoritmo más adecuado a su tamaño:
   Insertion Sort para intervalos pequeños, QuickSort para intervalos
   medianos e Introsort para los más grandes. Antes de aplicar cualquier
   algoritmo de comparación, se comprueba en una sola pasada si el
   intervalo ya está ordenado ascendente o descendentemente, para
   resolverlo sin comparaciones adicionales en esos casos.
6. **Unión final:** los intervalos, ya ordenados y colocados en orden
   ascendente por construcción, se concatenan en el array de salida.

## Estructura del proyecto

```
drs_project/
├── include/
│   ├── Config.hpp             # DEBUG_METRICS y constantes globales
│   ├── Bin.hpp                # Bin<T>: estadísticas de los intervalos iniciales
│   ├── DRSMetrics.hpp         # Sistema de métricas internas
│   ├── DynamicRangeSort.hpp   # Declaración de la clase DynamicRangeSort<T>
│   └── DynamicRangeSort.tpp   # Implementación, organizada por fases
├── tests/
│   ├── DatasetGenerator.hpp   # Generador de distintos tipos de dataset
│   └── main.cpp               # Batería de pruebas y comparación de rendimiento
├── Makefile
├── README.md
└── ANALYSIS.md                # Análisis experimental con datos reales
```

## Compilación y ejecución

Requiere un compilador con soporte para C++17 (probado con `g++`,
`-std=c++17 -O2 -Wall -Wextra`).

```bash
make            # compila tests/main.cpp -> build/drs_tests
make test       # compila (si hace falta) y ejecuta la batería de pruebas
make clean      # elimina el directorio build/
```

## Uso de la clase

```cpp
#include "DynamicRangeSort.hpp"

std::vector<int64_t> data = /* ... */;

drs::DynamicRangeSort<int64_t> sorter; // targetElementsPerBin = 16 por defecto
sorter.sort(data);                     // ordena in-place

const drs::DRSMetrics& m = sorter.metrics();
m.print(std::cout);                    // vuelca todas las estadísticas recogidas
```

`DynamicRangeSort<T>` es una plantilla restringida a tipos enteros
(verificado con `static_assert`), porque las fórmulas del algoritmo
(rango, ancho de intervalo, índice de intervalo) están definidas en
términos de aritmética entera.

## Sistema de métricas (DRSMetrics)

El sistema de métricas internas se activa o desactiva con una única
constante, en `include/Config.hpp`:

```cpp
#define DEBUG_METRICS 1   // 1 = recoger métricas, 0 = desactivarlas por completo
```

Cuando `DEBUG_METRICS` vale `0`, cada método de `DRSMetrics` queda con el
cuerpo vacío, así que no añade coste en la ruta de ordenación. Esto se
verificó de forma experimental: con `DEBUG_METRICS=0` todas las
estadísticas devuelven cero y los tiempos de ejecución quedan dentro del
margen de ruido respecto a `DEBUG_METRICS=1` (los números se documentan en
`ANALYSIS.md`).

Entre las métricas recogidas están: número de comparaciones, número de
subdivisiones y profundidad máxima de refinamiento alcanzada, número total
de intervalos y de intervalos vacíos, tamaño medio y máximo de intervalo,
tiempo de cada fase, y qué algoritmo local se usó en cada intervalo
(ya ordenado, invertido, Insertion Sort, QuickSort o Introsort).

## Estado del algoritmo

Esta es la segunda iteración del proyecto. La primera versión implementaba
la especificación original de forma directa (subdivisión en un único
nivel). Un análisis experimental sobre esa primera versión identificó
varios cuellos de botella — sobre todo con datos muy concentrados o con
muchos valores duplicados — y propuso una serie de mejoras sin
implementarlas todavía.

Esta versión incorpora esas mejoras generalizando el paso de "refinar
intervalos" a una recursión acotada por profundidad, en vez de un único
paso fijo, y añadiendo reconocimiento de intervalos ya ordenados o
duplicados antes de recurrir a un algoritmo de comparación. El detalle
completo de qué se implementó, por qué, y con qué efecto medido está en
`ANALYSIS.md`, junto con los cuellos de botella que persisten y las
mejoras que quedan abiertas para futuras iteraciones.
