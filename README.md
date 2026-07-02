# Dynamic Range Sort (DRS) — v1

Implementación en C++17 de **Dynamic Range Sort**, un algoritmo de ordenación
propio basado en la filosofía:

```
Analizar -> Construir intervalos -> Refinar intervalos -> Ordenar localmente -> Unir resultado
```

Esta es la primera versión funcional, de mi propio algoritmo intentando llegar a O(n) mejorando el ordenamiento local.

## Estructura del proyecto

```
drs_project/
├── include/
│   ├── Config.hpp             # DEBUG_METRICS y constantes globales
│   ├── Bin.hpp                # Bin<T>: estructura de estadísticas (Fase 1)
│   ├── DRSMetrics.hpp         # Sistema de métricas internas
│   ├── DynamicRangeSort.hpp   # Declaración de la clase DynamicRangeSort<T>
│   └── DynamicRangeSort.tpp   # Implementación (una función por fase)
├── tests/
│   ├── DatasetGenerator.hpp   # Generador de los 8 tipos de dataset pedidos
│   └── main.cpp               # Harness de pruebas + benchmarking vs std::sort
├── Makefile
├── README.md
└── ANALYSIS.md                # Análisis experimental con datos reales
```

## Compilar y ejecutar

```bash
make            # compila tests/main.cpp -> build/drs_tests
make test       # compila (si hace falta) y ejecuta la batería de pruebas
make clean      # elimina build/
```

Requiere un compilador con soporte C++17 (probado con `g++`, flags
`-std=c++17 -O2 -Wall -Wextra`).

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
(`static_assert(std::is_integral<T>::value)`), porque las fórmulas de la
especificación (`range`, `intervalSize`, `binIndex`, `observedRange`) están
definidas en términos de aritmética entera.

## Sistema de métricas (DRSMetrics)

Todo el sistema de métricas se activa/desactiva con **una única constante**
en `include/Config.hpp`:

```cpp
#define DEBUG_METRICS 1   // 1 = recoger métricas, 0 = desactivar por completo
```

Cuando `DEBUG_METRICS` es `0`, cada método de `DRSMetrics` (`recordComparison`,
`recordSubdivision`, `recordBin`, `startPhase`/`endPhase`, etc.) queda con el
cuerpo vacío, así que no queda coste real en la ruta de ordenación. Se
verificó experimentalmente: con `DEBUG_METRICS=0` todas las estadísticas
devuelven cero y los tiempos de ejecución quedan dentro del margen de ruido
respecto a `DEBUG_METRICS=1` (ver `ANALYSIS.md`).

Métricas recogidas:

- Número de comparaciones (en las fases de ordenación local)
- Número de subdivisiones realizadas
- Profundidad máxima de subdivisión
- Número total de bins, bins vacíos, tamaño medio y máximo de bin
- Tiempo (ms) de cada fase: `analysis`, `buildInitialBins`, `firstPass`,
  `subdivision`, `secondPass`, `localSort`, `merge`
- Qué algoritmo local se usó en cada bin (Insertion Sort / QuickSort / Introsort)

## Fidelidad a la especificación

| Sección del documento     | Dónde está implementada                              |
|---------------------------|-------------------------------------------------------|
| FASE 1 — Análisis          | `DynamicRangeSort<T>::analyze()`                      |
| Fórmulas                  | `DynamicRangeSort<T>::buildInitialBins()`, `computeBinIndex()` |
| Estructura de cada bin    | `Bin<T>` (`include/Bin.hpp`)                          |
| Primera pasada            | `DynamicRangeSort<T>::firstPass()`                    |
| Subdivisión                | `DynamicRangeSort<T>::subdivideBins()`                |
| Segunda pasada             | `DynamicRangeSort<T>::secondPass()`                   |
| Ordenación local           | `sortBinLocally()` + `insertionSort()` / `quickSort()` / `introSort()` |
| Unión final                 | `DynamicRangeSort<T>::mergeResults()`                 |
| Tests                      | `tests/DatasetGenerator.hpp`, `tests/main.cpp`         |

La subdivisión usa exclusivamente el **rango observado** (`observedMin`,
`observedMax`) del bin, nunca el rango original, tal como pide el documento.

## Próximos pasos

Las posibles mejoras identificadas durante el análisis experimental están
documentadas al final de `ANALYSIS.md`, sin implementar, para que decidas
cuáles incorporar.
