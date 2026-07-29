# Limpieza de v10 para publicación

**Objetivo: corrección y calidad de código, ignorando el rendimiento.**
Todo lo que sigue responde a un hallazgo concreto de `AUDIT_v10.md`.

---

## 1. Defectos funcionales corregidos

### 1.1 El acoplamiento del despachador local (defecto 5.3)

**Era.** `INSERTION_SORT_THRESHOLD` y `QUICKSORT_THRESHOLD` se derivaban de
`DEFAULT_LEAF_THRESHOLD`. Dos conceptos sin relación compartían un número:
cambiar cuándo para el particionado cambiaba en silencio **qué algoritmo
de ordenación** corría sobre las hojas.

**Ahora.** Tres constantes independientes que describen los sorts locales
y nada más: `LOCAL_INSERTION_MAX_ELEMENTS`, `LOCAL_QUICKSORT_MAX_ELEMENTS`,
`LOCAL_PARTITION_CUTOFF`.

**Precisión honesta:** para un llamante que pase `t = 128`, el
comportamiento observable **no cambia** — las hojas de 65 a 128 siguen
yendo a QuickSort. Lo que se ha corregido es que ahora eso es
**intencionado, documentado y estable** frente a cambios de
configuración, en lugar de un efecto colateral. El valor 64 en sí sigue
**sin medir**, y `Config.hpp` lo dice.

**Regresión impedida por:** `tests/api_contract.cpp` §7, que construye una
hoja de tamaño fijo bajo cinco parametrizaciones distintas y exige que el
despachador elija siempre lo mismo. Si alguien vuelve a acoplar las
constantes, ese test falla.

### 1.2 `width == 0` escapando de `computeRangeParameters` (defecto nuevo)

**Encontrado por una aserción añadida durante esta limpieza, no por la
auditoría.** Con `binCount == 1` y `span == 2^64 − 1` —entrada que
contiene `INT64_MIN` e `INT64_MAX`— la anchura exacta es `span + 1 = 2^64`,
que da la vuelta a **0**.

El código anterior pasaba ese 0 a `countAndPlace`, y era correcto **sólo
porque** esa función cortocircuita `numBuckets == 1` antes de dividir.
Funcionaba por suerte, no por contrato. Un cambio futuro en el orden de
esas comprobaciones habría producido una división por cero.

**Ahora.** Con un solo bucket la anchura no transporta información —todo
valor mapea al bucket 0— así que se normaliza a 1. La postcondición
`width >= 1` se cumple **incondicionalmente** y ningún valor desbordado
sale de la función. Hay `assert` de la postcondición.

**Cubierto por:** `api_contract.cpp` §3, cinco casos con `span = 2^64−1`.

### 1.3 La garantía fuerte no se cumplía en el build de investigación

**Encontrado por el test de excepciones nuevo.** El header prometía
garantía fuerte sin condiciones. Es cierta en release, pero **falsa** con
`DRS_ENABLE_METRICS`: `endPhase("merge")` inserta en un `unordered_map`
—y por tanto asigna memoria— **después** de que la salida ya se ha
escrito. Un fallo de asignación ahí deja la entrada modificada.

**Ahora.** El header declara la garantía **para la configuración de
release** y explica por qué el build de investigación no la ofrece. No se
"arregla" el build de investigación: es un instrumento de medida, no un
producto. La asimetría queda fijada por un test que exige que exista, de
modo que nadie la deshaga y deje el comentario obsoleto.

### 1.4 «Producción» no era una configuración de release

**Encontrado al medir la reescritura.** El `Makefile` nunca definió
`NDEBUG`, así que la configuración llamada «producción» llevaba todas las
aserciones activas. Con la aserción nueva de `Property 2` dentro del bucle
más caliente, eso costaba **~5 % en los datasets dominados por la
distribución** — un coste que se habría publicado como si fuera el
algoritmo.

**Ahora** hay tres configuraciones explícitas:

| | métricas | aserciones | para qué |
|---|---|---|---|
| `PROD_CXXFLAGS` | no | **no** (`-DNDEBUG`) | lo que recibe un usuario; la única cuyos tiempos significan algo |
| `TEST_CXXFLAGS` | no | **sí** | corrección; ~5 % más lenta, nunca para medir |
| `RESEARCH_CXXFLAGS` | sí | sí | laboratorio; varias veces más lenta |

`make test` compila con `TEST_CXXFLAGS`, así que **todos los invariantes
se siguen comprobando** en cada ejecución de la batería.

---

## 2. La anomalía de baja cardinalidad: explicada y disuelta

`AUDIT_v10.md` §5.9 registró una variación de **2,6x** entre entradas con
distinto número de valores distintos, con trabajo algorítmico idéntico, e
hipótesis de conflicto de caché **sin verificar**.

### 2.1 La hipótesis de aliasing queda REFUTADA

Con `k = 2`, la separación entre los dos flujos de escritura se varió de
0,38 MB a 7,25 MB (19x) cambiando la proporción de ceros. Si la causa
fuese conflicto de conjuntos de caché, el coste tendría que depender de
esa separación:

```
separacion   0,38  0,76  1,53  1,91  2,29  3,05  3,81  4,58  5,34  6,10  7,25 MB
tiempo       5,69  4,28  4,00  3,92  3,95  4,02  3,97  3,93  4,02  3,95  3,93 ms
```

**Plano.** La hipótesis del audit era incorrecta.

### 2.2 Dónde está realmente, medido por fase

```
k    total  distribute  refine  localSort  merge   ruta de countAndPlace
1     1,43        0,52    0,32       0,00   0,21   memcpy (numBuckets==1)
2     3,78        3,04    0,20       0,00   0,20   dos pasadas generales
3     2,28        1,77    0,17       0,00   0,18   dos pasadas generales
5     2,22        1,68    0,16       0,00   0,17   dos pasadas generales
17    1,89        1,48    0,14       0,00   0,12   dos pasadas generales
65    1,63        1,25    0,15       0,00   0,10   dos pasadas generales
```

**Toda la variación está en `distribute`.** `refine`, `localSort` y
`merge` son constantes.

### 2.3 Conclusión

**La mayor parte de la «anomalía» está explicada y no es una anomalía:**
el salto entre `k = 1` (1,43 ms) y `k ≥ 2` (1,63–3,78 ms) es la diferencia
entre la ruta degenerada de `countAndPlace`, que es un `memcpy`, y la ruta
general de dos pasadas. Es comportamiento diseñado, no un defecto.

**Queda un residuo sin explicar:** `k = 2` (3,04 ms de `distribute`) frente
a `k = 3` (1,77 ms), un factor 1,7. Está localizado en la dispersión con
muy pocos buckets, **la causa no está identificada**, y la única hipótesis
que se ha probado ha sido refutada. Se deja registrado como
**comportamiento observado, no como conclusión**, tal y como corresponde.

---

## 3. Limpieza de la API y del código

| Hallazgo del audit | Estado |
|---|---|
| Cabeceras que dicen «v7» y describen una versión inexistente (6.1) | **Corregido.** Cero referencias de versión en `algoritmo/`. La historia vive en el repositorio |
| Comentarios que contradicen el código (6.2) | **Corregido.** Reescritos los tres archivos |
| Miembro muerto `bucketSizeScratch_` (6.3) | **Eliminado** |
| Idiomas mezclados (6.4) | **Corregido.** `algoritmo/` es íntegramente inglés; `documentacion/` sigue en español |
| `long` como índice, 32 bits en Windows (6.5) | **Corregido.** `using Index = std::ptrdiff_t` |
| Rama inalcanzable `length == 0` | **Eliminada**, sustituida por `assert(length > 0)` |
| Cursor `pos` redundante en la unión | **Eliminado.** `appendLeaves` escribe en `node.start` directamente |
| Precondición de `partition` sin comprobar (6.11) | **Corregida.** `assert` + el argumento de centinelas, escrito |
| Literales `12` y `2.0` sin nombre (6.10) | **Nombrados:** `LOCAL_PARTITION_CUTOFF`, `INTROSORT_DEPTH_FACTOR` |
| `std::log2` en un algoritmo entero | **Sustituido** por `floorLog2` entero. Cambia el límite de profundidad de Introsort de `2·log₂(n)` redondeado a `2·⌊log₂ n⌋`, que es la fórmula clásica; sin efecto sobre la salida |
| `AnalysisResult` con campo `length` redundante | **Sustituido** por `ValueRange{min, max}` |
| Dos parámetros de salida por referencia | **Sustituidos** por `IntervalGrid{binCount, width}` |
| Propiedades no documentadas (5.5, 5.7, 5.6) | **Documentadas** en el header: no estable, no seguro entre hilos, garantía fuerte, reutilización de memoria |
| λ depende del tamaño de caché y de `n` (5.10) | **Documentado** en `Config.hpp`, con la condición `n·64/λ ≲ L2` |
| `debugPartitionOnly` duplica `sort()` (6.9) | **No corregido.** Marcado con una advertencia explícita. Unificarlo exige refactorizar `sort()` en primitivas, que es un cambio de arquitectura |

### 3.1 Interfaz: lo que se propuso y no se hizo

Los dos parámetros del constructor son `std::size_t` adyacentes, así que
intercambiarlos compila: `DynamicRangeSort(64, 32)` se lee como `(64, 64)`.
Un `struct` de parámetros con nombre eliminaría el riesgo, **pero cambia
la API pública** y el encargo pedía no añadir características. Queda
propuesto en el header, junto al constructor, y el test §2 fija el
comportamiento actual para que el riesgo sea al menos visible.

---

## 4. Tests añadidos

`tests/api_contract.cpp`, compilado en **las dos** configuraciones
(124 comprobaciones en release, 133 en investigación):

1. **Espacio de parámetros:** 8 valores de λ × 10 de `t`, más 20
   combinaciones contra entradas construidas para agotar la profundidad —
   la única vía por la que QuickSort e Introsort son alcanzables. **La
   región donde vivía el defecto 5.3 no tenía ninguna cobertura.**
2. **Reglas de recorte del constructor**, incluido el riesgo de
   intercambio de argumentos.
3. **Aritmética de rango:** `span = 2^64−1` en cinco formas, todos
   iguales, vacío, un elemento, y `uint8_t`, `int16_t`, `uint64_t`
   abarcando su tipo entero. La implementación ya no se prueba sólo con
   `int64_t`.
4. **Reutilización de instancia** sobre tamaños de 50.000 a 0 en orden
   mezclado, que es lo que caza estado obsoleto en los buffers.
5. **Garantía fuerte ante excepciones**, forzando `bad_alloc` en cada una
   de las 40 primeras asignaciones y verificando que la entrada queda
   intacta.
6. **Determinismo.**
7. **Desacoplamiento del despachador** (guardia de regresión del defecto
   5.3).

---

## 5. Verificación

- `make test`: los tres binarios, **verde**.
- ASan + UBSan sobre los tres ficheros de test: limpio.
- Correctitud en los 10 datasets a n = 10⁶.
- **Rendimiento de release frente al código anterior**, 4 rondas
  alternadas: dentro del ruido en 9 de 10 datasets. El décimo es
  `ManyRepeated`, con +11,1 % — el dataset cuya dispersión del 18 % está
  documentada desde v9 y cuyos contadores se demostraron idénticos en las
  21 configuraciones de O17. No es resoluble por reloj a esta precisión.
- Sin `NDEBUG` la reescritura costaba +10 % en `SmallRangeManyEl`; con la
  separación release/test correcta, −0,5 %.
