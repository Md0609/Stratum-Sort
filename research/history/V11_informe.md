# 0.11.0 — Informe: diagnóstico de 0.10.0, limitaciones y lo que se hizo con cada una

Cuaderno de trabajo de la versión 0.11.0. Todo número aquí viene de un
programa del repositorio que se puede volver a ejecutar; cuando un número
salió de CI se dice de qué runner. Nada se ha retocado para que parezca
mejor de lo que fue: incluye una conclusión que un cuarto entorno de medida
obligó a corregir (§4.3).

Las herramientas:

| programa | qué mide |
|---|---|
| `research/perf/MemoryProfile.cpp` | pico de memoria auxiliar en el asignador, 0.10.0 congelada vs actual |
| `research/perf/VersionTimings.cpp` | tiempo, 0.10.0 vs actual vs `std::sort`, alternando en un proceso |
| `research/perf/BucketIndexStrategies.cpp` | guardar el índice de bucket vs recalcularlo |
| `research/perf/InPlaceDistribution.cpp` | distribución fuera de sitio vs in-place (American flag) |
| `research/perf/PresortedDetection.cpp` | coste de detectar entrada ordenada, tres estrategias |
| `research/perf/LambdaSweep.cpp` | barrido de λ y de t/λ |
| `research/perf/RecordStrategies.cpp` | registros clave/valor: directo, indirecto, `std` |
| `benchmarks/stratum_bench.cpp` | el benchmark multiplataforma que CI ejecuta en Linux, macOS y Windows |

`research/baselines/v0_10_0/` es 0.10.0 byte a byte con el namespace
renombrado, para compararla en el mismo proceso.

---

## 1. Diagnóstico de 0.10.0

### 1.1 De dónde salía cada byte de memoria auxiliar

Medido en el asignador (`MemoryProfile`), clave de 8 bytes, λ = 32,
n = 10⁶: **26.25 B/elemento = 3.28× la entrada**. Desglose, leído del
código y confirmado por la medida:

| componente | B/elem | origen |
|---|---|---|
| `bufferA_` | 8 | copia de la entrada tras la distribución de nivel superior |
| `bufferB_` | 8 | el otro buffer del ping-pong entre niveles |
| `bucketOfScratch_` | 8 | un `size_t` por elemento: el índice de bucket, para no dividir dos veces |
| árbol `RefinedRange` | 1.5 | 48 B por bin, un nodo por bin, vivo hasta el final |
| histogramas + cursores | 0.75 | `bucketStart`, `bucketSize` por nodo, `writeCursorScratch_` |

Peor caso medido: **31.1× para `uint32` con λ = 1** (124 B/elem), **16.6×
para `int64` con λ = 1**, **10× para `uint8` con cualquier λ** (el índice
`size_t` pesa 8 veces la clave). La memoria dependía de la forma de la
entrada: 4.08× en el adversario frente a 3.28× en uniforme.

### 1.2 Por qué perdía en entrada ordenada

Porque la ordenaba: análisis, distribución (dos pasadas), refinamiento,
`detectRun` hoja a hoja y unión. Nada miraba si la entrada ya estaba en
orden. Medido en el Xeon local: 2.1× más lento que `std::sort` a n = 10⁶
(6× en el M4 de la documentación de 0.10.0 — la diferencia entre máquinas
ya era una advertencia).

### 1.3 Por qué λ dependía de la caché

El coste por bin era ~64 B (nodo del árbol + entradas de histograma). El
fan-out del nivel superior es `n/λ` bins, así que `n·64/λ` tenía que caber
en L2. No era una propiedad del algoritmo sino de su representación.

### 1.4 Por qué no era estable ni aceptaba clave/valor

Leído del código, solo tres pasos pueden reordenar claves iguales:

1. `quickSort`/`introSort`/`heapSort` en hojas de más de 64 elementos;
2. la inversión de una hoja descendente (`detectRun` + `std::reverse`);
3. (en 0.11.0) la inversión de una entrada entera no creciente.

La distribución **ya era estable** (los cursores avanzan en orden de
entrada), el certificado de span 0 no mueve nada. Y los tipos: tres
`static_assert` (integral, no `bool`, ≤ 64 bits) sobre `T`, el elemento
*era* la clave.

### 1.5 Por qué una instancia no era thread-safe

La configuración (λ, t) y el estado mutable (los buffers) vivían en el
mismo objeto. No había estado global: el problema era solo de diseño.

---

## 2. Clasificación de las limitaciones

| limitación de 0.10.0 | clase | resultado en 0.11.0 |
|---|---|---|
| Solo claves integrales ≤ 64 bits | **eliminable en parte** | enteros de todo ancho (incl. `char`, `char16_t`, `char32_t`, `wchar_t`), enums, `float`, `double` (totalOrder IEEE-754, demostrado exhaustivamente para `float`), registros por clave extraída. **Estructural:** claves > 64 bits y cadenas (la hipótesis H1 acota la profundidad por el ancho de la clave) |
| No estable, no extensible a clave/valor | **eliminable** | `stable_sort`, `stable_sort_by_key`, `sorted_indices`; la variante inestable sigue siendo la rápida por defecto |
| Instancia no thread-safe | **eliminable** | `Workspace` separado; `sort(data, workspace)` es `const`; una instancia sirve a N hilos |
| Θ(n) memoria extra | **reducible**, con una parte **estructural** | 3.28× → 1.06× (int64); 10× → ~0 (uint8); 0 bytes en entrada ordenada. Estructural: `O(n/λ)` contadores (el fan-out es el algoritmo). Reducible pero no en 0.11: el buffer compañero de `n` elementos (§3.1) |
| Pierde en entrada ordenada | **eliminable** | 0.07× `std::sort` (era 2.1×); inversa 0.16×; prefijo ordenado + cola 0.05× |
| λ ajustado a una caché | **eliminable** | la dependencia venía de la representación; λ automático por `n`, determinista (§4.3) |
| Tiempos de una sola máquina | **eliminable** | benchmark en CI en Linux x86_64, macOS arm64 y Windows x86_64 |

---

## 3. Paso 1 — memoria

### 3.1 Qué se hizo y por qué cada cosa

- **El array del llamador es uno de los dos buffers.** El nivel superior
  copia la entrada al workspace *mientras cuenta* y la coloca de vuelta en
  el array del llamador; desde ahí los niveles alternan como antes. Las
  hojas de nivel superior —las de toda entrada no adversarial— terminan
  donde deben sin copia final.
- **Sin índice de bucket cacheado.** Se recalcula `floor(offset/width)` con
  una división exacta por recíproco (Granlund–Montgomery). Medido sobre la
  distribución sola (`BucketIndexStrategies`, n = 10⁶, λ = 32):

  | estrategia | ms | vs 0.10.0 |
  |---|---|---|
  | guardar `size_t` (0.10.0) | 10.80 | — |
  | guardar `uint32` | 10.23 | −5% |
  | dividir dos veces (hardware) | 8.29 | −23% |
  | **recíproco dos veces** | **7.01** | **−35%** |

  El caché costaba tiempo además de memoria. La exactitud no se argumenta,
  se comprueba: `tests/fast_division.cpp`, 12.2 millones de comprobaciones
  (todo divisor < 2¹⁶, potencias de dos ± 2, los anchos que produce el
  algoritmo, 4·10⁶ pares aleatorios, y las rutas portables contra las
  nativas), 0 fallos, también bajo ASan/UBSan.
- **Sin árbol.** DFS fusionado: cada hoja se ordena y coloca al
  encontrarla. Los hijos se localizan por el histograma del nodo; si
  guardarlo pudiera dejar sin contadores a un descendiente, se libera y se
  usa búsqueda galopante (los buckets son no decrecientes a lo largo del
  nodo). La arena de `2⌈n/λ⌉+2` contadores basta siempre: prueba por
  inducción junto a `visitChildren()`.

**La partición es idéntica a la de 0.10.0**, hoja por hoja y en el mismo
orden interno (toda distribución es estable y parte del mismo orden). La
evidencia más fuerte: los contadores del build de investigación coinciden
exactamente —7 + 1569 ejecuciones de heapSort, 14.2476 y 14.3409
comparaciones por elemento en las pruebas de techo de parámetros—.

### 3.2 Resultado (pico medido, n = 10⁶)

| clave | λ | 0.10.0 | 0.11.0 |
|---|---|---|---|
| int64 | default (32 → 16) | 3.28× | **1.06×** |
| int64 | 1 | 16.57× | 2.00× |
| int64 adversarial | 16 | 5.81× | 1.06× |
| int64 ya ordenada | default | 3.28× | **0** |
| uint32 | 1 | 31.13× | 3.00× |
| uint8 | cualquiera | 10.0× | **~0** (≤ 1 KiB de contadores, §6) |

Además: 2 asignaciones por ordenación en vez de ~30, y la memoria ya no
depende de la forma de la entrada.

### 3.3 In-place: medido y rechazado

La pregunta era si el buffer de `n` elementos es inevitable. Se midió la
distribución in-place tipo American flag (permutación por ciclos,
`InPlaceDistribution`), misma partición, misma división:

| random, λ = 32 | fuera de sitio | in-place |
|---|---|---|
| n = 10⁵ | 0.31 ms | 1.10 ms (+251%) |
| n = 10⁶ | 9.7 ms | 61.5 ms (+533%) |
| n = 10⁷ | 158 ms | 2222 ms (+1303%) |

**Por qué es estructural para Stratum:** el fan-out es `⌈n/λ⌉` (31 250
buckets a 10⁶), y con ese fan-out cada paso de un ciclo es un fallo de
caché/TLB que depende del anterior. Un radix sort in-place usa 256 buckets
justo para evitarlo. Un Stratum in-place sería más lento que `std::sort`
*y* no usaría menos memoria que él: dominado. Las alternativas que lo
arreglarían (bloques tipo IPS⁴o, o un primer nivel de fan-out acotado)
cambian la partición y obligan a rehacer el Lema 4: **demasiado invasivo
para 0.11**.

Se esbozó una variante de cuatro ciclos en paralelo y se descartó antes de
medirla: dos ciclos pueden competir por el último hueco de un bucket.

### 3.4 Tiempo (misma sesión, instancia nueva, n = 10⁶)

−24% a −70% en las 18 formas, solo por el paso 1. La memoria bajó y el
tiempo también: no hubo compromiso.

### 3.5 Una trampa de medida

Reutilizar el mismo workspace en todas las repeticiones salió hasta un 30%
más lento que asignarlo de nuevo en la forma `clustered`, de forma
reproducible entre procesos, pero no cuando también se reutiliza el
array de datos. Es la colocación relativa de dos buffers de 8 MB, no el
algoritmo. Consecuencia metodológica: el benchmark usa asignaciones
frescas por defecto y publica la dispersión (p10–p90).

---

## 4. Pasos 2 y 3 — entrada ordenada y λ

### 4.1 Detección: tres estrategias medidas

`PresortedDetection`, ms de la pasada de detección, 10⁶ int64:

| estrategia | random | sorted | sorted + 1% cola |
|---|---|---|---|
| sin detección | 0.70 | 0.72 | 0.81 |
| fusionada (contar descensos en min/max) | 0.89 | 1.00 | 1.03 |
| prefijo (salida temprana) | 0.75 | 0.53 | 1.33 |

Ninguna gana en todo: la fusionada cuesta +27% en *toda* entrada; la de
prefijo, una pasada extra cuando el orden se rompe tarde. Implementada:
**prefijo con reanudación** — si el prefijo monótono se rompe en `k`, sus
extremos son sus dos puntas y el min/max continúa en `k`. Una pasada
siempre.

Caminos: ascendente → return sin asignar; no creciente → una inversión
(estable en las variantes estables); prefijo ascendente ≥ n/2 → ordenar
solo la cola y fusionar hacia atrás con un buffer de `n−k`. Lineal:
`T(n) = T(n−k) + O(n)` con `n−k ≤ n/2`.

Resultado (vs `std::sort`, n = 10⁶): sorted 2.24× → **0.08×**, reversed
3.08× → **0.16×**, sorted_tail 0.59× → **0.06×**, organ_pipe 0.40× →
**0.06×**. Sin coste medible en random.

**No implementado, con datos:** fusión de pocos runs (sawtooth de 16) y
extracción de elementos fuera de sitio. La fusión de runs de contenido
aleatorio sufre fallos de predicción de rama (~log₂ r pasadas de mezcla
con ramas impredecibles); el beneficio estimado era ~2× solo en entradas
de ≤ 16 runs largos, y sawtooth ya está en 0.62× `std::sort`.

### 4.2 λ: primer barrido (tres máquinas)

Con el motor nuevo el óptimo se movió: el coste por bin pasó de ~64 B a
4 B. Peor ralentización frente al mejor λ de cada fila (formas random,
normal, casi ordenada, universo completo, duplicados; claves de 64 y 32
bits; n de 10⁴ a 10⁷):

| λ | Xeon | runner Linux | Apple M1 |
|---|---|---|---|
| 32 (0.10.0) | 1.34 | 1.39 | 1.40 |
| **16** | **1.13** | **1.09** | **1.17** |
| 8 | 1.18 | 1.00 | 1.61 |

A 5·10⁷ y 10⁸ (Xeon) la curva es plana ±10% para todo λ entre 8 y 512:
domina la memoria principal. **t = 2λ** se re-midió frente a t/λ ∈ {1,
1.5, 3, 4, 8}: óptimo o a ≤ 4% en toda forma no adversarial. (La forma
adversarial está construida contra (λ, t) concretos y un t mayor la
esquiva: es el sobreajuste que ya documentó O17.)

La conclusión con tres máquinas fue "λ = 16 fijo, sin regla dependiente de
n", y se publicó así.

### 4.3 Corrección: el cuarto entorno

El runner Windows x86_64 (MSVC, AMD EPYC 7763, L2 de 512 KiB) la
contradijo: a n = 10⁷, λ = 16 cuesta **1.27×** su óptimo. Tabla completa:

| | n ≤ 10⁶: λ 16 / 32 | n = 10⁷: λ 16 / 32 |
|---|---|---|
| Xeon, Linux, GCC | 1.10 / 1.29 | 1.12 / 1.00 |
| runner x86_64, Linux, GCC | 1.09 / 1.39 | 1.09 / 1.17 |
| Apple M1, AppleClang | 1.17 / 1.40 | 1.17 / 1.10 |
| EPYC, Windows, MSVC | 1.05 / 1.15 | 1.27 / 1.04 |

Cruce localizado en el Xeon **y** en Windows entre 2²² (λ = 16 óptimo) y
2²³ (λ = 24–32 óptimo). Implementado: **λ = 16 si n ≤ 2²², 32 si no, t =
2λ**, función de `n` y nunca de la caché —consultar la caché haría que el
orden de claves iguales en los sorts inestables dependiera de la máquina—.
H2 se cumple: dos constantes bajo los techos.

Compatibilidad: `StratumSort<T>()` y `Parameters{}` son automáticos; los
argumentos explícitos conservan exactamente el contrato fijo de 0.10.0.

### 4.4 Una comparación injusta que casi se publica

Con el default en 16, la forma `adversarial` salió +34…56% más lenta que
0.10.0. El generador se construye contra el λ por defecto: 0.11.0 recibía
un adversario hecho para él y 0.10.0 (λ = 32) uno que no le apuntaba. Cada
versión contra su propio adversario: **0.11.0 es 26–37% más rápida**.
`VersionTimings` hace ahora siempre esa comparación y lo indica en la
fila.

---

## 5. Pasos 4, 5 y 7 — clave/valor, estabilidad, tipos

### 5.1 Arquitectura

El motor ve los elementos solo a través de un objeto *traits*:
`key(e) → uint64_t` que preserva el orden y `less(a, b)`, con
`less(a,b) ⇔ key(a) < key(b)`. Enteros con signo: extensión a 64 bits y
bit de signo invertido — la diferencia de claves es la diferencia de
valores, así que cada span, anchura e índice es el número que calculaba
0.10.0.

### 5.2 Clave/valor: alternativas medidas

`RecordStrategies`, Xeon, n = 10⁶, claves distintas:

| registro | `sort_by_key` vs `std::sort` | `stable_sort_by_key` vs `std::stable_sort` | indirecto vs `std::sort` |
|---|---|---|---|
| 16 B | 0.39× | 0.29× | 0.96× |
| 32 B | 0.64× | 0.45× | 0.98× |
| 64 B | 0.89× | 0.67× | 0.98× |
| 128 B | 1.18× | 0.41× | 1.04× |
| 256 B | 1.26× | 0.34× | 1.01× |

Mover el registro entero (AoS) gana hasta ~64 B; desde 128–256 B y n
grande el indirecto (`sorted_indices` + un gather, cada registro se copia
una vez) es mejor, pero a 10⁵ el directo sigue ganando a 128 y 256 B. El
cruce depende de n, así que no se automatizó: se documenta. La variante
estable gana a `std::stable_sort` en todos los tamaños. SoA no se expuso
como API: es el indirecto aplicado a dos arrays.

Requisito: registro trivialmente copiable (se mueve con `memcpy`). Para
cualquier otro tipo, `sorted_indices`.

### 5.3 Estabilidad

La variante estable cambia exactamente los tres pasos de §1.4: inserción
hasta 64, merge sort por encima (cuyo buffer es el rango de la hoja en el
*otro* buffer, libre por construcción) y una inversión que devuelve los
empates a su orden. O(m log m) en hojas de m ≤ max(t, M): el papel de
introsort, dentro de la misma cota.

Tests con miles de duplicados y payloads identificables
(`tests/stability.cpp`, 3694 comprobaciones), comparados campo a campo con
`std::stable_sort`. **Las cuatro mutaciones que romperían la estabilidad
son detectadas**: hoja inestable (108 fallos), inversión de hoja (22),
inversión global (98), desempate de la fusión (44).

### 5.4 Floats

Transformación de bits: negativo → invertir todo; positivo → poner el bit
de signo. Da exactamente IEEE 754-2008 totalOrder: −NaN < −∞ < … < −0 <
+0 < … < +∞ < +NaN. **Demostrado antes de habilitarlo**:
`tests/float_keys.cpp` recorre los 2³² patrones `float` en orden de clave
contra un totalOrder de referencia escrito desde la definición del
estándar (signo, NaN, comparación numérica, −0/+0, payload), y comprueba
la biyección. Para `double`: especiales y vecinos, cadenas `nextafter` por
todas las regiones, 5·10⁶ pares aleatorios. Cadenas: fuera (no caben en
64 bits; H1).

### 5.5 Workspace y concurrencia

`Workspace<T>` contiene el buffer y los contadores; `sort(data,
workspace)` es `const`. `tests/concurrency.cpp`: 12 hilos, un sorter
compartido, 640 ordenaciones, limpio bajo ThreadSanitizer; el control
negativo (dos hilos compartiendo un workspace) sí lo detecta TSan.

---

## 6. Contar en vez de mover (hallazgo de Windows)

El benchmark de Windows mostró a Stratum perdiendo 1.4–2.7× frente al
`std::sort` de MSVC con pocos valores distintos. Causa: con rejilla de
anchura 1 cada bucket contiene una sola clave, pero el motor seguía
contando, copiando y dispersando. Si el elemento es su propia clave, basta
contar y escribir cada clave su número de veces en su posición final.
Misma partición (Lema 2), O(m + s).

n = 10⁶: int64 duplicates 5.6 → **2.85 ms (0.15× `std::sort`)**, binary
6.3 → 3.1 ms, clustered 15.5 → 10.6 ms, uint8 random 3.5 → **1.5 ms
(0.04×)**; memoria de uint8 y duplicados: unos bytes de contadores.

---

## 7. Lo que sigue existiendo, y por qué

- **`n·sizeof(T)` de memoria** para cualquier entrada que necesite
  distribución: el buffer compañero. In-place es 3.5–14× más lento (§3.3).
- **`O(n/λ)` contadores**: estructural.
- **Pierde frente a `std::sort` en algunas formas y plataformas**:
  - con libc++ en Apple M1: adversarial 1.3–1.6×, low_entropy 1.5–1.8× (el
    `std::sort` de libc++ tiene particionado sin ramas);
  - con MSVC: casi ordenada con intercambios dispersos (1.25× a 10⁶) y
    floats casi ordenados (1.6–1.9×).
  Stratum hace ahí el pipeline completo; la fusión de runs o la extracción
  de elementos fuera de sitio podrían ayudar y quedan como trabajo futuro.
- **Claves > 64 bits y cadenas**: H1.
- **Registros no trivialmente copiables**: solo vía `sorted_indices`.
- **Los tiempos de CI** son de máquinas virtuales compartidas: los ratios
  frente a `std::sort` viajan, los milisegundos no.
