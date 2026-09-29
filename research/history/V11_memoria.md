# 0.11.0 — Memoria auxiliar: ¿es Θ(n) inevitable?

**Estado: investigación previa a la release. 0.11.0 no está publicada**:
sin tag, sin GitHub Release; la rama no es una release final.

Pregunta de partida: Stratum Sort necesitaba un buffer de `n` elementos más
`2⌈n/λ⌉+2` contadores (1.06× la entrada) donde `std::sort` usa
`O(log n)`, y la documentación lo daba por **estructural**. ¿Lo es?

**Respuesta corta: no.** La partición de Stratum Sort — la que prueba el
`Θ(n)` — se puede producir con memoria auxiliar fijada por el ancho de la
clave e independiente de `n` (43 856 B para claves de 64 bits), en tiempo
`Θ(n)` peor caso, y en entradas grandes **más rápido** que con el buffer.
Lo único que sigue siendo Θ(n) es el orden **estable de registros**.

Código: `include/stratum/detail/InPlace.hpp` (motor) y el driver de
`detail/Engine.hpp`. Teoría y pruebas: `research/ALGORITHM.md` §14.
Herramientas: `research/perf/MemoryAudit.cpp` (bytes) y
`research/perf/MemoryModes.cpp` (tiempo). Datos crudos:
`research/data/memoria/`. Commits: auditoría (3654251), candidato A
(7967b88), candidato B (3af7c01), selección final (este commit).

Todas las cifras son de un Xeon a 2.8 GHz (4 vCPU, 15 GB, THP en modo
`madvise`), GCC 13 `-O3`, salvo §13 (CI en Linux, macOS y Windows).

---

## 0. Las doce respuestas

| # | pregunta | respuesta |
|---|---|---|
| 1 | ¿De dónde viene la memoria auxiliar? | Del **horario** del motor, no de la partición: buffer compañero `n·sizeof(E)` (el 94–99%) + arena `2⌈n/λ⌉+2` contadores (0.5 B/elem). Pila < 3 KB. Nada depende de la forma de la entrada (§2). |
| 2 | ¿Qué es realmente necesario? | Para la partición: `A(w) = 3·2^L + ⌊(w+2D+1)·max(3L, 2^L+1)/L⌋` contadores — 43 856 B (w = 64), 30 736 B (w = 32), 20 896 B (w = 8) — y `O(w + D)` marcos de pila. Para el horario de **una pasada**: Ω(s·log λ) bits, probado (§3). Para el orden estable de registros, en esta implementación: `n` registros. |
| 3 | ¿Qué era accidental? | El buffer de `n` elementos para los órdenes inestables y para `stable_sort` de números; la arena Θ(n/λ). Ambos son del horario "una pasada, estable, fuera de sitio". |
| 4 | ¿Se puede eliminar Θ(n)? | **Sí**, para `sort`, `sort_by_key`, `StratumSort<T>` y `stable_sort` de tipos que son su propia clave: memoria `O(1)` en `n`. **No** (en esta implementación) para `stable_sort_by_key` y `sorted_indices`. |
| 5 | Mínimo práctico | **44 KB** (el suelo; pases American flag): a 10⁷ u64, +8…+100% frente al buffer compañero (sin contar las formas de prefijo ordenado) y ≤ 1.04× `std::sort`. **600 KiB** (con bloques): más rápido que el buffer a 10⁷ en la mayoría de formas. |
| 6 | Coste en tiempo | Política por defecto a 10⁷ u64: aleatorio −31%, casi ordenado −26%, adversarial −43%, peor forma +12% (sorted_tail). A 10⁸: −58%. Registros de 72 B a 10⁷: −46%; 264 B: −74…−94%. Pierde: floats con pocos valores (+21…+24%), organ_pipe a 10⁸ (+237%, sigue en 0.15× `std::sort`). Tablas en §7–§10. |
| 7 | Estabilidad | `stable_sort` de números: gratis (claves iguales = bits iguales). Registros: Θ(n); con presupuesto explícito menor, `std::length_error` antes de tocar nada; con el automático, sin cota. El prototipo C (§10.3) no lo mejora lo bastante. |
| 8 | Clave/valor | `sort_by_key` in-place, moviendo bloques de registros: **a 10⁷, 72 B: 1.22× → 0.66× `std::sort`; 264 B: 3.69× → 0.94×**. El buffer compañero era el cuello de botella de los registros grandes. |
| 9 | uint8 vs uint64 | u8: rejilla de anchura 1 → solo contadores (1 KiB) con cualquier política; suelo 20 896 B. u64: suelo 43 856 B; 16 MiB por defecto desde 1.9·10⁶ elementos. |
| 10 | Escalado a 10⁸ | Memoria: `unl` 8.25 B/elem constante (Θ(n)); por defecto 16 MiB = 0.168 B/elem → decrece como 1/n; suelo 0.00044 B/elem. Tiempo: por defecto 0.33× `std::sort` a 10⁷ y 0.31× a 10⁸; el buffer compañero se degrada de 0.47× a 0.75×. |
| 11 | Mejor arquitectura | Dos horarios para una misma partición (`Engine` de una pasada con buffer; `InPlaceEngine` por pases de ≤ 1 024 grupos), elegidos por un **presupuesto** en el `Workspace`; defecto automático 16 MiB; suelo por ancho de clave; fusión por trozos del prefijo ordenado; rechazo explícito para registros estables (§6). |
| 12 | Limitación que queda | El orden **estable de registros** sigue siendo Θ(n) (`n` registros o `n` pares). Y por política, no por necesidad: por debajo de 16 MiB el defecto sigue usando el buffer compañero (≤ 16 MiB). |

---

## 1. Cómo se mide sin engañarse

`research/perf/MemoryAudit.cpp`. Cada fila (variante × tipo × forma × n)
corre en **su propio proceso** (`fork`): un proceso que ya ordenó conserva
páginas liberadas y sube el umbral de `mmap` de glibc, y la medida
siguiente saldría baja. Cuatro medidas independientes:

| medida | qué ve | qué no ve |
|---|---|---|
| **heap** | bytes pedidos a `operator new` vivos, pico durante la llamada menos lo vivo antes | la pila; el redondeo del asignador |
| **usable** | los mismos bloques según el asignador (`malloc_usable_size`) | la pila |
| **RSS** | `VmHWM` tras `/proc/self/clear_refs`, menos `VmRSS` antes: todo lo **tocado** | páginas reservadas y no tocadas; resolución ~1 MB (el kernel actualiza el contador por lotes) |
| **pila** | la llamada corre en un hilo con la pila pintada; el byte más profundo sobrescrito | — |

Antes de abrir la ventana la variante ordena un prefijo de 4 096 elementos
(calienta las páginas de código, que cuentan en la RSS) y libera su
workspace. **Auxiliar = heap** en todas las tablas; la RSS lo confirma y
además revela lo reservado pero no tocado (§2.3).

`research/perf/MemoryModes.cpp` mide el tiempo de cada política sobre las
mismas entradas: modos alternados en orden rotatorio, **workspace nuevo
en cada llamada** (asignación y primer toque incluidos), mediana de 3–21
repeticiones. Es un equipo virtual compartido: diferencias de ±10% en una
sola fila son ruido; las tendencias se repiten en tres campañas
independientes (candidatos A, B y final).

Nombres de variante: `v010` = 0.10.0 congelada, `v011` = 0.11 antes del
estudio congelada (`research/baselines/v0_11_pre`), `unl` =
`Workspace::kUnlimited` (buffer compañero, el comportamiento anterior),
`cur` = el defecto (`kAutomatic`), `b600k` = presupuesto de 600 KiB,
`b0` = presupuesto 0 (el suelo), `std`/`stdstable` = la biblioteca
estándar; sufijo `s` = orden estable.

## 2. Auditoría: de dónde sale cada byte

### 2.1 Motor de una pasada (0.11-pre; hoy `kUnlimited`)

| estructura | bytes | con `n` | con `λ` | fase | reutilizable | clase |
|---|---|---|---|---|---|---|
| buffer compañero | `n·sizeof(E)` | Θ(n) | — | toda la distribución; hojas estables | sí (Workspace) | **necesario solo para el horario estable, fuera de sitio, de una pasada** |
| arena de contadores | `(2⌈n/λ⌉+2)·4` (8 si `n ≥ 2³²`) | Θ(n/λ) | `1/λ` | histograma de cada split del camino DFS y fin de cada hijo | sí | **necesario solo para el horario de una pasada** (§3) |
| ↳ vivos a la vez | `Σ sᵢ` del camino `≤ 2⌈n/λ⌉` | | | | | lo que exige el fan-out |
| buffer de cola (prefijo ordenado ≥ n/2) | `(n−k)·sizeof(E)` | ≤ n/2 | — | ordenar la cola y fusionar | el mismo | atajo |
| contadores de conteo (anchura 1) | `(σ+1)·4 ≤ ⌈n/λ⌉·4` | | | countingFill | sí | sustituyen al buffer |
| rejillas, cursores, `Analysis` | O(1) palabras | — | — | — | pila | necesario |
| pila | ≤ `D+1` marcos + `O(log t)` introsort + ≤ `log₂ n` (cadena de prefijos) | O(log n) | — | — | — | medido 0.03–2.9 KiB |
| metadatos del Workspace | 7 palabras | — | — | — | — | — |
| sobrecoste del asignador | usable − pedido: ≤ 16 B por bloque, 2 bloques | — | — | — | — | medido +0.02–0.4% |
| reservado y no tocado | nada: el primer nivel copia la entrada entera al buffer | | | | | RSS ≈ heap (§2.3) |

`sorted_indices`: pares (clave, índice) de 16 B + su buffer compañero de
16 B = 32 B/elem auxiliares (la salida de 8 B/elem es el resultado).

**Qué parte del fan-out exige memoria simultánea.** Un split de `s`
buckets en una pasada necesita a la vez los `s` contadores (histograma →
sumas prefijas → cursores, sobre el mismo array) mientras escribe, y
después los `s` finales de hijo mientras los visita (o los suelta y galopa,
ALGORITHM.md §13.3). En el camino DFS viven los splits de todos sus
niveles: eso es lo que acota `2⌈n/λ⌉+2`.

### 2.2 Motor in-place (este estudio)

| estructura | bytes | con `n` | fase | reutilizable |
|---|---|---|---|---|
| arena | `A(w)·4`: 43 856 B (w = 64), 30 736 B (w = 32), 20 896 B (w = 8) | O(1) | temporales de pase + fronteras de los pases del camino | sí |
| bloques | `(2^L+3)·B·sizeof(E)` = 525 824 B con bloques de 512 B | O(1) | pase por bloques | sí; opcionales |
| buffer compañero parcial | `P·sizeof(E) + (2⌈P/λ⌉+2)·4`, lo que deje el presupuesto | — | nodos de ≤ P elementos; fusión por trozos | sí |
| pila | ≤ `w + 2D + 1 + 2(D+1)` marcos | O(w) | recursión de pases y niveles | medido 1–7.4 KiB |

### 2.3 Medido: la memoria no depende de la forma (u64, n = 10⁷)

`research/data/memoria/final_audit_shapes.csv`, 17 formas:

| política | heap en las 11 formas que distribuyen | sorted_tail | organ_pipe | duplicates / binary | sorted / reversed | RSS aleatorio | pila máx. |
|---|---|---|---|---|---|---|---|
| `v011` = `unl` | 82 500 008 B | 825 008 | 39 999 992 | 20 / 8 | 0 | 82.0 MB | 2.9 KiB (low_entropy) |
| `cur` (defecto) | 16 777 216 B | 825 008 | 16 777 216 | 20 / 8 | 0 | **0.96 MB** | 4.9 KiB |
| `b600k` | 614 400 B | 614 400 | 614 400 | 20 / 8 | 0 | 0.92 MB | 6.4 KiB |
| `b0` | 43 856 B | 43 856 | 43 856 | 20 / 8 | 0 | 0.78 MB | 7.4 KiB |

**Reservado pero no tocado.** Con el defecto a 10⁷ el heap es 16 MiB pero
la RSS apenas 1 MB en aleatorio: el buffer compañero parcial solo termina
nodos de ≤ P elementos, y en una entrada aleatoria casi no hay nodos
grandes que refinar. Se toca entero en organ_pipe (fusión por trozos:
16.9 MB de RSS) y en parte en low_entropy (10 MB).

## 3. Teoría: qué estado necesita cada fase

Resumen de `research/ALGORITHM.md` §14, donde están las pruebas.

| fase | estado necesario | por qué |
|---|---|---|
| análisis (min, max, prefijo ordenado) | O(1) | un recorrido |
| split de `s` buckets **en una pasada** | **Ω(s·log λ) bits** | tras leer un prefijo, dos historias con distinto recuento en algún bucket exigen estados distintos (el adversario continúa con un elemento de ese bucket): `log₂ C(p+s−1, s−1)` bits (§14.3) |
| split de `s` buckets en pases de ≤ 2^L grupos | O(2^L) por pase vivo | histograma + cursores del pase |
| todos los pases de un camino | ≤ `A(w)` contadores | **Lema M1**: los bits de índice de un camino suman ≤ `w + 2D + 1` (el producto de los `sᵢ` telescopa: `Π sᵢ < 2^{w+1+k}`); **Lema M2**: cada bit cuesta ≤ `max(3, (2^L+1)/L)` contadores |
| mover elementos | O(1) elementos (flag) u O(2^L·B) (bloques) | permutación in-place |
| hojas | O(log t) pila | introsort in-place |
| estabilidad de registros | Θ(n) en esta implementación | §10 y ALGORITHM.md §14.9 |

- **Tiempo (Lema M3):** cada pase consume ≥ 1 bit del camino → ≤ `w + 2D + 1`
  pases por elemento (≤ 14 con el reparto equilibrado; 1–2 en aleatorio),
  cada pase `O(c + r)` con `r ≤ c` → **Θ(n) peor caso** con H1–H4.
- **Una corrección honesta:** el primer borrador usaba `w + D + 2` bits
  (olvidaba el redondeo de `log₂ sᵢ` en cada nivel). La arena de entonces
  cubría la cota correcta por holgura; ahora se dimensiona con ella.
  Validado: la escalera adversaria diseñada para gastar 10 bits por nivel
  llega a 9 238 contadores (84% de `A(64)`); las 18 formas, 3 075–5 249.
- **Offsets bajo demanda** (§14.6): recalcular las fronteras galopando en
  vez de guardarlas bajaría la arena de 44 KB a 16 KB por `O(r log(c/r))`
  cálculos de dígito por pase. No se hizo: ambas son constantes y los
  bloques (526 KB) dominan la configuración rápida.
- **El extremo teórico:** con `L = 1` bastan O(w + D) palabras (~1 KB) y
  hasta 77 pases por elemento: sigue siendo Θ(n), con una constante que no
  compensa. `L = 10` da 44 KB y ≤ 14 pases.
- **§13.3 de ALGORITHM.md queda corregido**: decía que el buffer solo podía
  irse "con una distribución in-place 3.5–14× más lenta que cambia la
  partición". La medida de entonces usaba un fan-out de `⌈n/λ⌉` en una
  pasada American flag; con pases de ≤ 1 024 grupos por bloques, la
  partición no cambia y el tiempo mejora.

## 4. Candidato A: la misma partición, in-place (commit 7967b88)

**Qué es.** `detail/InPlace.hpp`. El split de un nodo en `s` buckets —
los mismos buckets, el mismo `bucketOf` exacto — se hace en pases sobre
los bits del índice de bucket, ≤ 10 bits (1024 grupos) por pase, cada pase
in-place: **por bloques** (esquema de IPS⁴o: clasificar en 1024 bloques de
512 B, volcar bloques llenos detrás del puntero de lectura, permutar
bloques enteros, limpiar cabeza y cola de cada grupo) o, sin memoria para
los bloques, **American flag** (ciclos). Radio nunca mayor que el
subrango; ≤ 32 elementos, inserción por índice. Mismas hojas, mismas
ordenaciones locales.

**Historia corta del candidato** (lo que no llegó al commit):

| versión | resultado a 10⁶ u64 aleatorio | por qué |
|---|---|---|
| American flag con fan-out ⌈n/λ⌉ (0.11-pre, `InPlaceDistribution.cpp`) | +251…+1303% | cada paso del ciclo depende del anterior: acceso aleatorio encadenado sobre toda la entrada |
| American flag con radio ≤ 2¹⁰ | +56…+86% | ~14 ms por pase de 10⁶: cargas dependientes |
| pase por bloques, radio ≤ 2¹⁰ | ~2.7–3.4 ms por pase (el pase del buffer compañero: 5.5 ms) | escritura en streaming |

(Las dos últimas filas se midieron con prototipos durante el estudio; no
están en `research/data`.)

**Constantes** (`research/data/memoria/tuning_radix_block.txt`; media de 5
formas u64, tiempo in-place / buffer compañero):

| L, bloque | 10⁶ | 10⁷ | bloques |
|---|---|---|---|
| 8, 512 B | 1.167 | 1.044 | 142 KB |
| 9, 512 B | 1.109 | 1.083 | 280 KB |
| **10, 512 B** | **1.105** | **0.920** | **556 KB** |
| 11, 512 B | 1.057 | 0.923 | 1 105 KB |
| 10, 256 B | 1.116 | 1.051 | 299 KB |
| 10, 1024 B | 1.071 | 0.937 | 1 069 KB |
| 10, 2048 B | 1.166 | 1.050 | 2 096 KB |

L = 10 con 512 B es la configuración más pequeña a ≤ 5% de la más rápida.

**Medido** (`candidateA_*.csv`): véase §11 para la comparación completa.
Lo esencial: a 10⁷ el in-place con 0.057 B/elem es igual o más rápido que
el buffer compañero (8.25 B/elem) en 12 de 17 formas (aleatorio −35%);
pierde donde desaparece el atajo prefijo+fusión (sorted_tail +316%,
organ_pipe +204%) y en local_disorder (+22%) y low_entropy (+14%). Con
44 KB (sin bloques) nunca pasa de 1.02× `std::sort` a 10⁷.

## 5. Candidato B: gastar el resto del presupuesto (commit 3af7c01)

Dos usos del buffer compañero parcial de `P` elementos que cabe en lo que
sobra:

1. **Híbrido**: nodos de ≤ P elementos los termina `Engine` (una pasada
   por nivel). **Resultado: casi no compra nada.** A 10⁷ aleatorio:
   16 MiB −29%, 4 MiB −30%, 1 MiB −34%, 600 KB −31% frente al buffer
   completo. Por encima de los bloques, más memoria no da más velocidad.
2. **Fusión por trozos** para el prefijo ordenado cuando la cola no cabe:
   `O(n + q·cola)` movimientos con `q = ⌈cola/M⌉ ≤ 4` trozos. **Esto sí
   importa**: organ_pipe a 10⁷ con 16 MiB pasa de +175% a −12%;
   sorted_tail a 10⁷ con 600 KB, de +309% a +34%.

Además, todas las reservas cuentan lo que el workspace ya guarda
(`WorkspaceAccess::fit`): con presupuesto, el workspace nunca supera
`max(presupuesto, suelo)`, también cuando se reutiliza entre llamadas.

## 6. Selección: la arquitectura final

| política (`Workspace`) | memoria auxiliar | cuándo |
|---|---|---|
| `kAutomatic` (**defecto**) | `min(n·sizeof(E) + contadores, 16 MiB)` | todas las funciones sin workspace y `StratumSort<T>` |
| `kUnlimited` | `n·sizeof(E) + (2⌈n/λ⌉+2)·4` | el comportamiento de 0.11-pre |
| `Workspace(bytes)` | `max(bytes, A(w)·4)` | control explícito; `0` = el suelo |
| `stable_sort_by_key`, `sorted_indices` | `n` registros / `n` pares | presupuesto explícito menor → `std::length_error` antes de tocar nada |

**Por qué 16 MiB** (`Config.hpp`): por debajo, el buffer compañero es más
rápido en las formas que más lo necesitan (casi ordenado, pocos outliers,
quicksort killer: 13–45% a 10⁶) y cuesta como mucho 16 MiB; por encima, la
estrategia acotada es igual o más rápida casi siempre; lo que 16 MiB
compran frente a 600 KB es la fusión del prefijo ordenado.

**Por qué no el suelo por defecto**: 44 KB son posibles, pero sin bloques
cada pase es American flag: +8…+100% frente al buffer a 10⁷ (aunque
≤ 1.04× `std::sort`). Es una opción explícita (`Workspace(0)`), no el
defecto.

**Por qué error y no degradación** para registros estables: la única
alternativa acotada conocida con constantes usables es `O(n log n)`
(merge in-place); aceptarla en silencio rompería la garantía Θ(n) peor
caso que el usuario tiene. Con `kAutomatic` no se acota: la función
estable toma lo que necesita, como antes.

## 7. Escalado: de 10⁴ a 10⁸

### 7.1 Bytes auxiliares (heap pico; `research/data/memoria/final_audit_*.csv`)

#### u64, aleatorio: heap auxiliar pico (B/elem entre paréntesis)

| n | v010 | v011 | unl | cur | b600k | b0 | std | stdstable | pila (b0 / std) |
|---|---|---|---|---|---|---|---|---|---|
| 1e4 | 256 KiB (26.3) | 83 KiB (8.5) | 83 KiB (8.5) | 83 KiB (8.5) | 83 KiB (8.5) | 42.8 KiB (4.39) | 0 B (0) | 39.1 KiB (4) | 1.09 KiB / 335 B |
| 1e5 | 2.5 MiB (26.2) | 830 KiB (8.5) | 830 KiB (8.5) | 830 KiB (8.5) | 600 KiB (6.14) | 42.8 KiB (0.439) | 0 B (0) | 391 KiB (4) | 2.19 KiB / 623 B |
| 1e6 | 25 MiB (26.2) | 8.11 MiB (8.5) | 8.11 MiB (8.5) | 8.11 MiB (8.5) | 600 KiB (0.614) | 42.8 KiB (0.0439) | 0 B (0) | 3.81 MiB (4) | 2.19 KiB / 719 B |
| 1e7 | 250 MiB (26.2) | 78.7 MiB (8.25) | 78.7 MiB (8.25) | 16 MiB (1.68) | 600 KiB (0.0614) | 42.8 KiB (0.00439) | 0 B (0) | 38.1 MiB (4) | 1.39 KiB / 959 B |
| 1e8 | - | 787 MiB (8.25) | 787 MiB (8.25) | 16 MiB (0.168) | 600 KiB (0.00614) | 42.8 KiB (0.000439) | 0 B (0) | 381 MiB (4) | 1.69 KiB / 1.12 KiB |

#### u8, aleatorio: heap auxiliar pico (B/elem entre paréntesis)

| n | v010 | v011 | unl | cur | b600k | b0 | std | stdstable | pila (b0 / std) |
|---|---|---|---|---|---|---|---|---|---|
| 1e4 | 116 KiB (11.8) | 1 KiB (0.102) | 1 KiB (0.102) | 1 KiB (0.102) | 1 KiB (0.102) | 1 KiB (0.102) | 0 B (0) | 4.88 KiB (0.5) | 1.01 KiB / 327 B |
| 1e5 | 995 KiB (10.2) | 1 KiB (0.0102) | 1 KiB (0.0102) | 1 KiB (0.0102) | 1 KiB (0.0102) | 1 KiB (0.0102) | 0 B (0) | 48.8 KiB (0.5) | 1.01 KiB / 519 B |
| 1e6 | 9.55 MiB (10) | 1 KiB (0.00102) | 1 KiB (0.00102) | 1 KiB (0.00102) | 1 KiB (0.00102) | 1 KiB (0.00102) | 0 B (0) | 488 KiB (0.5) | 1.01 KiB / 567 B |
| 1e7 | 95.4 MiB (10) | 1 KiB (0.000102) | 1 KiB (0.000102) | 1 KiB (0.000102) | 1 KiB (0.000102) | 1 KiB (0.000102) | 0 B (0) | 4.77 MiB (0.5) | 1.01 KiB / 807 B |
| 1e8 | - | 1 KiB (1.02e-05) | 1 KiB (1.02e-05) | 1 KiB (1.02e-05) | 1 KiB (1.02e-05) | 1 KiB (1.02e-05) | 0 B (0) | 47.7 MiB (0.5) | 1.01 KiB / 951 B |

#### rec72, aleatorio: heap auxiliar pico (B/elem entre paréntesis)

| n | v011 | unl | cur | b600k | b0 | std | stdstable | pila (b0 / std) |
|---|---|---|---|---|---|---|---|---|
| 1e4 | 708 KiB (72.5) | 708 KiB (72.5) | 708 KiB (72.5) | 600 KiB (61.4) | 42.8 KiB (4.39) | 0 B (0) | 352 KiB (36) | 1.52 KiB / 1.3 KiB |
| 1e5 | 6.91 MiB (72.5) | 6.91 MiB (72.5) | 6.91 MiB (72.5) | 600 KiB (6.14) | 42.8 KiB (0.439) | 0 B (0) | 3.43 MiB (36) | 2.93 KiB / 2.05 KiB |
| 1e6 | 69.1 MiB (72.5) | 69.1 MiB (72.5) | 16 MiB (16.8) | 600 KiB (0.614) | 42.8 KiB (0.0439) | 0 B (0) | 34.3 MiB (36) | 2.93 KiB / 2.3 KiB |
| 1e7 | 689 MiB (72.3) | 689 MiB (72.3) | 16 MiB (1.68) | 600 KiB (0.0614) | 42.8 KiB (0.00439) | 0 B (0) | 343 MiB (36) | 2.08 KiB / 2.92 KiB |

#### rec264, aleatorio: heap auxiliar pico (B/elem entre paréntesis)

| n | v011 | unl | cur | b600k | b0 | std | stdstable | pila (b0 / std) |
|---|---|---|---|---|---|---|---|---|
| 1e4 | 2.52 MiB (265) | 2.52 MiB (265) | 2.52 MiB (265) | 600 KiB (61.4) | 42.8 KiB (4.39) | 0 B (0) | 1.26 MiB (132) | 2.96 KiB / 1.15 KiB |
| 1e5 | 25.2 MiB (265) | 25.2 MiB (265) | 16 MiB (168) | 600 KiB (6.14) | 42.8 KiB (0.439) | 0 B (0) | 12.6 MiB (132) | 5.77 KiB / 1.62 KiB |
| 1e6 | 252 MiB (265) | 252 MiB (265) | 16 MiB (16.8) | 600 KiB (0.614) | 42.8 KiB (0.0439) | 0 B (0) | 126 MiB (132) | 5.77 KiB / 1.77 KiB |
| 1e7 | 2.46 GiB (264) | 2.46 GiB (264) | 16 MiB (1.68) | 600 KiB (0.0614) | 42.8 KiB (0.00439) | 0 B (0) | 1.23 GiB (132) | 3.9 KiB / 2.16 KiB |

(f32, f64, u32 y rec16 en los CSV: mismo patrón.)

**Cómo escala `aux/n`:**

| política | `aux/n` cuando `n → ∞` | ¿constante, `1/λ`, o menos? |
|---|---|---|
| `v010` | 26.25 B/elem (u64) | constante: Θ(n) |
| `v011` / `unl` | 8.25–8.5 B/elem (u64) | constante: Θ(n) (el término `1/λ` es 0.25–0.5) |
| **`cur` (defecto)** | `min(8.5, 16 MiB/n)`: 1.68 a 10⁷, 0.168 a 10⁸ | **decrece como 1/n**: memoria O(1) desde 16 MiB |
| `b600k` | 600 KiB/n | 1/n |
| `b0` | 43 856/n | 1/n: memoria O(1) = `A(w)` |
| `std::sort` | 0 (pila O(log n)) | — |
| `std::stable_sort` | 4 B/elem (u64) | constante: `n/2` elementos |

### 7.2 Tiempo (u64, `MemoryModes --reps 3`, `research/data/memoria/final_modes_scaling.csv`)

| type | n | shape | unl ms | unl B/e | cur vs unl | b600k vs unl | b0 vs unl | cur B/e | b600k B/e | b0 B/e | std ms | unl vs std | cur vs std | b600k vs std | b0 vs std |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| u64 | 1e5 | random | 2.29 | 8.500 | -5% | +41% | +76% | 8.500 | 6.144 | 0.439 | 7.45 | 0.31x | 0.29x | 0.43x | 0.54x |
| u64 | 1e5 | nearly_sorted | 1.35 | 8.500 | -12% | +64% | +61% | 8.500 | 6.144 | 0.439 | 1.91 | 0.71x | 0.62x | 1.17x | 1.14x |
| u64 | 1e5 | adversarial | 5.24 | 8.500 | -3% | +31% | +33% | 8.500 | 6.144 | 0.439 | 6.07 | 0.86x | 0.84x | 1.13x | 1.15x |
| u64 | 1e5 | organ_pipe | 0.25 | 4.000 | -20% | -22% | +993% | 4.000 | 4.000 | 0.439 | 7.18 | 0.04x | 0.03x | 0.03x | 0.39x |
| u64 | 1e6 | random | 27.39 | 8.500 | +18% | +1% | +63% | 8.500 | 0.614 | 0.044 | 73.37 | 0.37x | 0.44x | 0.38x | 0.61x |
| u64 | 1e6 | nearly_sorted | 11.81 | 8.500 | +1% | +29% | +78% | 8.500 | 0.614 | 0.044 | 15.15 | 0.78x | 0.79x | 1.01x | 1.39x |
| u64 | 1e6 | adversarial | 58.92 | 8.500 | +3% | +3% | +43% | 8.500 | 0.614 | 0.044 | 72.57 | 0.81x | 0.84x | 0.83x | 1.16x |
| u64 | 1e6 | organ_pipe | 4.45 | 4.000 | -33% | +255% | +581% | 4.000 | 0.614 | 0.044 | 88.18 | 0.05x | 0.03x | 0.18x | 0.34x |
| u64 | 1e7 | random | 433.38 | 8.250 | -30% | -32% | +27% | 1.678 | 0.061 | 0.004 | 923.99 | 0.47x | 0.33x | 0.32x | 0.60x |
| u64 | 1e7 | nearly_sorted | 162.49 | 8.250 | -12% | -13% | +29% | 1.678 | 0.061 | 0.004 | 213.36 | 0.76x | 0.67x | 0.66x | 0.98x |
| u64 | 1e7 | adversarial | 456.42 | 8.250 | -32% | -33% | +24% | 1.678 | 0.061 | 0.004 | 880.41 | 0.52x | 0.35x | 0.35x | 0.64x |
| u64 | 1e7 | organ_pipe | 67.68 | 4.000 | -7% | +203% | +480% | 1.678 | 0.061 | 0.004 | 1242.75 | 0.05x | 0.05x | 0.16x | 0.32x |
| u64 | 1e8 | random | 7750.69 | 8.250 | -58% | -58% | -2% | 0.168 | 0.006 | 0.000 | 10330.55 | 0.75x | 0.31x | 0.32x | 0.74x |
| u64 | 1e8 | nearly_sorted | 2229.93 | 8.250 | -11% | -13% | +47% | 0.168 | 0.006 | 0.000 | 3104.41 | 0.72x | 0.64x | 0.63x | 1.05x |
| u64 | 1e8 | adversarial | 8406.22 | 8.250 | -59% | -59% | -6% | 0.168 | 0.006 | 0.000 | 10581.21 | 0.79x | 0.32x | 0.32x | 0.75x |
| u64 | 1e8 | organ_pipe | 676.50 | 4.000 | +237% | +234% | +561% | 0.168 | 0.006 | 0.000 | 15593.98 | 0.04x | 0.15x | 0.15x | 0.29x |

A 10⁸ el buffer compañero de 825 MB se degrada (0.47× → 0.75× `std::sort`
de 10⁷ a 10⁸ en aleatorio) y la política por defecto se mantiene (0.33× →
0.31×): **−58%** con 16 MiB. La excepción es organ_pipe a 10⁸: la cola
(5·10⁷ elementos) pide 25 trozos de 2·10⁶ y la fusión por trozos se declina
(> 4): se ordena todo in-place, +237%, 0.15× `std::sort`.

## 8. Comparación con `std::sort` y `std::stable_sort`

Heap + pila, aleatorio. `std::sort` no usa heap; su memoria es la pila de
introsort, `O(log n)`. La comparación no es simétrica: `std::sort` es
`O(n log n)` comparaciones, Stratum `Θ(n)`; lo que se compara es lo que
cuesta en memoria esa linealidad.

**u64:**

| n | Stratum por defecto | Stratum suelo (`b0`) | `std::sort` | defecto / `std::sort` | suelo / `std::sort` | `std::stable_sort` | Stratum estable por defecto |
|---|---|---|---|---|---|---|---|
| 10^4 | 83.4 KiB | 43.9 KiB | 335 B | 255× | 134× | 39.1 KiB | 83.4 KiB |
| 10^5 | 831 KiB | 45 KiB | 623 B | 1366× | 74× | 391 KiB | 831 KiB |
| 10^6 | 8.11 MiB | 45 KiB | 719 B | 11823× | 64× | 3.81 MiB | 8.11 MiB |
| 10^7 | 16 MiB | 44.2 KiB | 959 B | 17496× | 47× | 38.1 MiB | 16 MiB |
| 10^8 | 16 MiB | 44.5 KiB | 1.12 KiB | 14578× | 40× | 381 MiB | - |

**u8** (rejilla de anchura 1: solo contadores con cualquier política):

| n | Stratum por defecto | Stratum suelo (`b0`) | `std::sort` | defecto / `std::sort` | suelo / `std::sort` | `std::stable_sort` | Stratum estable por defecto |
|---|---|---|---|---|---|---|---|
| 10^4 | 1.41 KiB | 2.01 KiB | 327 B | 4× | 6× | 4.96 KiB | 1.41 KiB |
| 10^5 | 1.41 KiB | 2.01 KiB | 519 B | 3× | 4× | 48.9 KiB | 1.41 KiB |
| 10^6 | 1.41 KiB | 2.01 KiB | 567 B | 3× | 4× | 488 KiB | 1.41 KiB |
| 10^7 | 1.41 KiB | 2.01 KiB | 807 B | 2× | 3× | 4.77 MiB | 1.41 KiB |
| 10^8 | 1.41 KiB | 2.01 KiB | 951 B | 2× | 2× | 47.7 MiB | - |

**Registros de 72 B** (inestable; la última columna es `stable_sort_by_key`,
que no se acota):

| n | Stratum por defecto | Stratum suelo (`b0`) | `std::sort` | defecto / `std::sort` | suelo / `std::sort` | `std::stable_sort` | Stratum estable por defecto |
|---|---|---|---|---|---|---|---|
| 10^4 | 709 KiB | 44.4 KiB | 1.3 KiB | 547× | 34× | 352 KiB | 708 KiB |
| 10^5 | 6.92 MiB | 45.8 KiB | 2.05 KiB | 3461× | 22× | 3.43 MiB | 6.92 MiB |
| 10^6 | 16 MiB | 45.8 KiB | 2.3 KiB | 7137× | 20× | 34.3 MiB | 69.1 MiB |
| 10^7 | 16 MiB | 44.9 KiB | 2.92 KiB | 5610× | 15× | 343 MiB | 689 MiB |

Lectura: el suelo de Stratum es **15–134× la memoria de `std::sort`, pero
constante** (44 KB frente a 0.3–3 KB); `std::stable_sort` usa `n/2`
elementos. El defecto es 16 MiB por encima de ~2·10⁶ elementos de 8 B.

## 9. Todas las formas (u64, 10⁶ y 10⁷)

`research/data/memoria/final_modes_u64.csv`. Relativo a `unl` (buffer
compañero) en la misma campaña alternada:

| type | n | shape | unl ms | unl B/e | cur vs unl | b600k vs unl | b0 vs unl | cur B/e | b600k B/e | b0 B/e | std ms | unl vs std | cur vs std | b600k vs std | b0 vs std |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| u64 | 1e6 | random | 23.55 | 8.500 | +8% | +14% | +106% | 8.500 | 0.614 | 0.044 | 76.95 | 0.31x | 0.33x | 0.35x | 0.63x |
| u64 | 1e6 | sorted | 0.63 | 0.000 | -21% | -39% | -35% | 0.000 | 0.000 | 0.000 | 13.23 | 0.05x | 0.04x | 0.03x | 0.03x |
| u64 | 1e6 | reversed | 0.97 | 0.000 | -2% | -8% | -29% | 0.000 | 0.000 | 0.000 | 11.71 | 0.08x | 0.08x | 0.08x | 0.06x |
| u64 | 1e6 | nearly_sorted | 16.74 | 8.500 | -3% | +4% | +21% | 8.500 | 0.614 | 0.044 | 23.34 | 0.72x | 0.69x | 0.75x | 0.86x |
| u64 | 1e6 | local_disorder | 19.67 | 8.500 | +1% | +18% | +36% | 8.500 | 0.614 | 0.044 | 28.46 | 0.69x | 0.70x | 0.82x | 0.94x |
| u64 | 1e6 | few_outliers | 10.82 | 8.500 | +1% | +35% | +62% | 8.500 | 0.614 | 0.044 | 41.84 | 0.26x | 0.26x | 0.35x | 0.42x |
| u64 | 1e6 | sorted_tail | 2.40 | 0.085 | -12% | -19% | +1353% | 0.085 | 0.085 | 0.044 | 72.32 | 0.03x | 0.03x | 0.03x | 0.48x |
| u64 | 1e6 | organ_pipe | 3.44 | 4.000 | -22% | +391% | +817% | 4.000 | 0.614 | 0.044 | 86.17 | 0.04x | 0.03x | 0.20x | 0.37x |
| u64 | 1e6 | sawtooth | 22.22 | 8.500 | +10% | +5% | +72% | 8.500 | 0.614 | 0.044 | 39.29 | 0.57x | 0.62x | 0.59x | 0.97x |
| u64 | 1e6 | duplicates | 2.26 | 0.000 | -10% | -13% | -6% | 0.000 | 0.000 | 0.000 | 21.33 | 0.11x | 0.10x | 0.09x | 0.10x |
| u64 | 1e6 | binary | 2.68 | 0.000 | -9% | -10% | -6% | 0.000 | 0.000 | 0.000 | 14.89 | 0.18x | 0.16x | 0.16x | 0.17x |
| u64 | 1e6 | low_entropy | 58.37 | 8.500 | -12% | +10% | +62% | 8.500 | 0.614 | 0.044 | 75.26 | 0.78x | 0.68x | 0.85x | 1.26x |
| u64 | 1e6 | normal | 30.67 | 8.500 | -1% | -12% | +62% | 8.500 | 0.614 | 0.044 | 75.51 | 0.41x | 0.40x | 0.36x | 0.66x |
| u64 | 1e6 | clustered | 13.98 | 8.500 | -3% | +8% | +69% | 8.500 | 0.614 | 0.044 | 40.30 | 0.35x | 0.34x | 0.37x | 0.59x |
| u64 | 1e6 | whole_universe | 29.50 | 8.500 | -11% | -19% | +35% | 8.500 | 0.614 | 0.044 | 72.93 | 0.40x | 0.36x | 0.33x | 0.55x |
| u64 | 1e6 | adversarial | 57.48 | 8.500 | +0% | +2% | +44% | 8.500 | 0.614 | 0.044 | 74.09 | 0.78x | 0.78x | 0.79x | 1.12x |
| u64 | 1e6 | worst_case | 34.55 | 8.500 | +0% | +8% | +52% | 8.500 | 0.614 | 0.044 | 72.16 | 0.48x | 0.48x | 0.52x | 0.73x |
| u64 | 1e6 | qsort_killer | 9.62 | 8.500 | -7% | +45% | +132% | 8.500 | 0.614 | 0.044 | 89.71 | 0.11x | 0.10x | 0.16x | 0.25x |
| u64 | 1e7 | random | 435.96 | 8.250 | -31% | -36% | +23% | 1.678 | 0.061 | 0.004 | 923.79 | 0.47x | 0.32x | 0.30x | 0.58x |
| u64 | 1e7 | sorted | 12.63 | 0.000 | +1% | -2% | +4% | 0.000 | 0.000 | 0.000 | 284.54 | 0.04x | 0.04x | 0.04x | 0.05x |
| u64 | 1e7 | reversed | 19.96 | 0.000 | +4% | +20% | -4% | 0.000 | 0.000 | 0.000 | 173.28 | 0.12x | 0.12x | 0.14x | 0.11x |
| u64 | 1e7 | nearly_sorted | 185.99 | 8.250 | -26% | -24% | +8% | 1.678 | 0.061 | 0.004 | 251.86 | 0.74x | 0.54x | 0.56x | 0.80x |
| u64 | 1e7 | local_disorder | 220.96 | 8.250 | +2% | +5% | +14% | 1.678 | 0.061 | 0.004 | 328.45 | 0.67x | 0.68x | 0.70x | 0.77x |
| u64 | 1e7 | few_outliers | 138.41 | 8.250 | +2% | -1% | +100% | 1.678 | 0.061 | 0.004 | 344.31 | 0.40x | 0.41x | 0.40x | 0.80x |
| u64 | 1e7 | sorted_tail | 30.03 | 0.083 | +12% | +36% | +1409% | 0.083 | 0.061 | 0.004 | 555.52 | 0.05x | 0.06x | 0.07x | 0.82x |
| u64 | 1e7 | organ_pipe | 74.57 | 4.000 | -3% | +153% | +489% | 1.678 | 0.061 | 0.004 | 1301.65 | 0.06x | 0.06x | 0.15x | 0.34x |
| u64 | 1e7 | sawtooth | 360.26 | 8.250 | -27% | -23% | +36% | 1.678 | 0.061 | 0.004 | 528.22 | 0.68x | 0.50x | 0.53x | 0.93x |
| u64 | 1e7 | duplicates | 45.30 | 0.000 | -4% | -9% | -1% | 0.000 | 0.000 | 0.000 | 262.74 | 0.17x | 0.16x | 0.16x | 0.17x |
| u64 | 1e7 | binary | 46.59 | 0.000 | -6% | -3% | -3% | 0.000 | 0.000 | 0.000 | 235.90 | 0.20x | 0.19x | 0.19x | 0.19x |
| u64 | 1e7 | low_entropy | 588.28 | 8.250 | -15% | -4% | +42% | 1.678 | 0.061 | 0.004 | 802.39 | 0.73x | 0.62x | 0.71x | 1.04x |
| u64 | 1e7 | normal | 488.95 | 8.250 | -38% | -29% | +39% | 1.678 | 0.061 | 0.004 | 902.04 | 0.54x | 0.34x | 0.38x | 0.76x |
| u64 | 1e7 | clustered | 272.31 | 8.250 | -4% | -11% | +40% | 1.678 | 0.061 | 0.004 | 423.24 | 0.64x | 0.62x | 0.57x | 0.90x |
| u64 | 1e7 | whole_universe | 448.32 | 8.250 | -39% | -33% | +25% | 1.678 | 0.061 | 0.004 | 933.33 | 0.48x | 0.29x | 0.32x | 0.60x |
| u64 | 1e7 | adversarial | 533.14 | 8.250 | -43% | -45% | +10% | 1.678 | 0.061 | 0.004 | 921.09 | 0.58x | 0.33x | 0.32x | 0.64x |
| u64 | 1e7 | worst_case | 524.82 | 8.250 | -13% | -8% | +49% | 1.678 | 0.061 | 0.004 | 903.30 | 0.58x | 0.51x | 0.53x | 0.87x |

Lectura:

- **10⁶** (8.5 MB, por debajo de 16 MiB): el defecto *es* el buffer
  compañero; las diferencias de ±10–20% entre `unl` y `cur` son ruido de
  la misma ruta. 600 KiB cuesta +35…+45% en few_outliers y qsort_killer y
  +391% en organ_pipe (atajo de prefijo sin buffer); por eso el defecto no
  acota por debajo de 16 MiB.
- **10⁷**: el defecto (1.68 B/elem) gana (≤ −3%) en 12 de 17 formas,
  empata (±2%) en 3 y pierde en reversed (+4%, sobre 20 ms) y sorted_tail
  (+12%).
- **El suelo** (44 KB) nunca pasa de 1.04× `std::sort` a 10⁷; a 10⁶
  llega a 1.39× en casi ordenado (medición de escalado) y 1.26× en
  low_entropy.
- **Correctitud:** ninguna fila `WRONG` en ninguna campaña (cada
  repetición se verifica).

## 10. Tipos, registros y estabilidad

### 10.1 Tipos a 10⁷ (`final_modes_types.csv`)

| type | n | shape | unl ms | unl B/e | cur vs unl | b600k vs unl | cur B/e | b600k B/e | std ms | unl vs std | cur vs std | b600k vs std |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| u8 | 1e7 | random | 15.90 | 0.000 | +4% | -0% | 0.000 | 0.000 | 421.72 | 0.04x | 0.04x | 0.04x |
| u8 | 1e7 | nearly_sorted | 30.51 | 0.000 | +0% | -0% | 0.000 | 0.000 | 122.90 | 0.25x | 0.25x | 0.25x |
| u8 | 1e7 | duplicates | 17.10 | 0.000 | -4% | -4% | 0.000 | 0.000 | 244.58 | 0.07x | 0.07x | 0.07x |
| u32 | 1e7 | random | 363.80 | 4.250 | -30% | -30% | 1.678 | 0.061 | 879.26 | 0.41x | 0.29x | 0.29x |
| u32 | 1e7 | nearly_sorted | 129.19 | 4.250 | -5% | -5% | 1.678 | 0.061 | 197.23 | 0.66x | 0.62x | 0.62x |
| u32 | 1e7 | duplicates | 26.20 | 0.000 | +3% | +1% | 0.000 | 0.000 | 215.98 | 0.12x | 0.12x | 0.12x |
| f32 | 1e7 | random | 413.46 | 4.250 | -12% | -12% | 1.678 | 0.061 | 1144.40 | 0.36x | 0.32x | 0.32x |
| f32 | 1e7 | nearly_sorted | 233.10 | 4.250 | -5% | -10% | 1.678 | 0.061 | 355.26 | 0.66x | 0.63x | 0.59x |
| f32 | 1e7 | duplicates | 85.95 | 4.250 | +24% | +56% | 1.678 | 0.061 | 340.28 | 0.25x | 0.31x | 0.39x |
| f64 | 1e7 | random | 424.07 | 8.250 | +1% | -4% | 1.678 | 0.061 | 1161.45 | 0.37x | 0.37x | 0.35x |
| f64 | 1e7 | nearly_sorted | 246.92 | 8.250 | -9% | -4% | 1.678 | 0.061 | 344.54 | 0.72x | 0.65x | 0.69x |
| f64 | 1e7 | duplicates | 127.34 | 8.250 | +21% | +15% | 1.678 | 0.061 | 437.88 | 0.29x | 0.35x | 0.33x |
| rec16 | 1e7 | random | 565.98 | 16.250 | -36% | -34% | 1.678 | 0.061 | 1044.32 | 0.54x | 0.35x | 0.36x |
| rec16 | 1e7 | nearly_sorted | 296.19 | 16.250 | -5% | -7% | 1.678 | 0.061 | 344.27 | 0.86x | 0.82x | 0.80x |
| rec16 | 1e7 | duplicates | 190.16 | 16.000 | -38% | -38% | 1.678 | 0.061 | 339.90 | 0.56x | 0.35x | 0.35x |
| rec72 | 1e7 | random | 2018.70 | 72.250 | -46% | -52% | 1.678 | 0.061 | 1659.81 | 1.22x | 0.66x | 0.58x |
| rec72 | 1e7 | nearly_sorted | 1006.72 | 72.250 | -36% | -28% | 1.678 | 0.061 | 1013.43 | 0.99x | 0.63x | 0.72x |
| rec72 | 1e7 | duplicates | 813.42 | 72.000 | -53% | -53% | 1.678 | 0.061 | 1282.93 | 0.63x | 0.30x | 0.30x |
| rec264 | 1e7 | random | 22742.98 | 264.250 | -74% | -75% | 1.678 | 0.061 | 6161.78 | 3.69x | 0.94x | 0.91x |
| rec264 | 1e7 | nearly_sorted | 16613.30 | 264.250 | -90% | -90% | 1.678 | 0.061 | 1956.92 | 8.49x | 0.87x | 0.86x |
| rec264 | 1e7 | duplicates | 17914.18 | 264.000 | -94% | -94% | 1.678 | 0.061 | 5673.64 | 3.16x | 0.19x | 0.20x |

- **u8**: la rejilla de anchura 1 cuenta en vez de mover; 1 KiB con
  cualquier política, 0.04× `std::sort`.
- **u32, u64 y f32 aleatorios**: −12…−31% con 16 MiB; f64 +1%.
- **floats con pocos valores distintos**: +21…+24% con 16 MiB (+15…+56%
  con 600 KiB). Es la pérdida más clara del modo acotado fuera de los
  atajos de prefijo: los pases en profundidad sobre pocos valores
  repetidos cuestan más que la pasada única del buffer.
- **Registros**: el buffer compañero de una pasada era el cuello de botella
  de los registros grandes. 72 B: 1.22× → **0.66×** `std::sort`; 264 B:
  3.69× → **0.94×** (casi ordenado: 8.49× → 0.87×). Con registros de
  264 B el suelo (American flag, un movimiento por registro) llegó a ser
  más rápido que los bloques (dos movimientos) en la auditoría: 4.1 s
  frente a 5.2 s a 10⁷.

### 10.2 Órdenes estables (`final_modes_stable.csv`)

| type | n | shape | unl ms | unl B/e | cur vs unl | cur B/e | std ms | unl vs std | cur vs std |
|---|---|---|---|---|---|---|---|---|---|
| u64s | 1e6 | random | 21.86 | 8.500 | +5% | 8.500 | 89.66 | 0.24x | 0.26x |
| u64s | 1e6 | nearly_sorted | 9.10 | 8.500 | +16% | 8.500 | 17.87 | 0.51x | 0.59x |
| u64s | 1e6 | duplicates | 2.10 | 0.000 | -1% | 0.000 | 32.41 | 0.06x | 0.06x |
| rec72s | 1e6 | random | 123.60 | 72.500 | +1% | 72.500 | 305.69 | 0.40x | 0.41x |
| rec72s | 1e6 | nearly_sorted | 89.54 | 72.500 | +7% | 72.500 | 305.57 | 0.29x | 0.31x |
| rec72s | 1e6 | duplicates | 75.98 | 72.000 | +15% | 72.000 | 300.05 | 0.25x | 0.29x |
| u64s | 1e7 | random | 427.00 | 8.250 | -26% | 1.678 | 1181.50 | 0.36x | 0.27x |
| u64s | 1e7 | nearly_sorted | 171.88 | 8.250 | -3% | 1.678 | 411.82 | 0.42x | 0.40x |
| u64s | 1e7 | duplicates | 36.39 | 0.000 | +2% | 0.000 | 519.24 | 0.07x | 0.07x |
| rec72s | 1e7 | random | 1549.80 | 72.250 | -3% | 72.250 | 3193.60 | 0.49x | 0.47x |
| rec72s | 1e7 | nearly_sorted | 838.97 | 72.250 | -2% | 72.250 | 3108.92 | 0.27x | 0.26x |
| rec72s | 1e7 | duplicates | 708.35 | 72.000 | +5% | 72.000 | 3179.19 | 0.22x | 0.23x |

- `stable_sort` de u64 (su propia clave): acotado como el inestable,
  −26% a 10⁷ con 1.68 B/elem.
- `stable_sort_by_key` de 72 B: **sin cota** con el defecto (72.25
  B/elem), mismo tiempo que antes (0.23–0.47× `std::stable_sort`). Con un
  presupuesto explícito menor: `std::length_error` sin tocar la entrada
  (tests/api_contract.cpp §14).

### 10.3 Candidato C: orden estable de registros por pares (prototipo, no adoptado)

`research/perf/StableByIndex.cpp`, datos en
`candidateC_stable_by_index.txt`. Idea: pares (clave, índice) de 16 B,
ordenados **de forma inestable** e in-place (presupuesto 16 MiB) por clave;
cada racha de claves iguales se ordena por índice (los índices son
distintos: el orden dentro de la racha queda forzado y el resultado es el
estable); luego se aplica la permutación a los registros por ciclos.
Memoria: 16 B/elem **sea cual sea el registro** — sigue siendo Θ(n).

| registro | n | aleatorio | casi ordenado | duplicados | cola ordenada | B/elem (partner → pares) |
|---|---|---|---|---|---|---|
| 16 B | 10⁶ | +455% | +90% | +1461% | +3716% | 16.5 → 32.5 |
| 72 B | 10⁶ | +75% | −49% | +127% | +814% | 72.5 → 32.5 |
| 264 B | 10⁶ | −44% | −82% | −8% | +291% | 264.5 → 32.5 |
| 16 B | 10⁷ | +360% | +44% | +1176% | +4276% | 16.3 → 17.7 |
| 72 B | 10⁷ | −26% | −85% | −25% | +979% | 72.3 → 17.7 |
| 264 B | 10⁷ | −80% | — | — | — | 264.3 → 17.7 |

(Tiempo relativo a `stable_sort_by_key` con buffer compañero, misma
campaña.) **Veredicto:** reduce la memoria de los registros estables en
un factor `sizeof(E)/16` y es más rápido para registros ≥ 264 B, pero es
mucho más lento para registros pequeños y pierde el atajo de entrada
casi ordenada. No cambia la asintótica. Queda como el siguiente paso más
útil para registros grandes (§14), no como parte de esta fase.

### 10.4 ¿Por qué el buffer compañero es tan lento con registros grandes?

No está establecido. La hipótesis del mensaje del commit 7967b88 (fallos
de TLB del reparto de una pasada sobre 720 MB) se probó y **no se
sostiene**: con páginas enormes (`GLIBC_TUNABLES=glibc.malloc.hugetlb=1`)
el buffer compañero con registros de 72 B a 10⁷ tardó **más**, 5.7 s frente
a 1.8 s (`final_thp_on.csv` / `final_thp_off.csv`). Su tiempo en esa fila
varía 1.8–5.7 s entre campañas (1.06×–3.25× `std::sort`); el del motor
acotado, 0.93–1.02 s (0.53×–0.66×). El entorno no expone contadores de
hardware (`perf` no disponible), así que la causa queda abierta; lo medido
es la diferencia, no su explicación.

## 11. Comparación de versiones (u64, 10⁷, aleatorio salvo indicación)

Cada fila se midió contra `unl` (el buffer compañero, = 0.11-pre) **en su
propia campaña alternada**; la fila de 0.10.0 sale de `VersionTimings`
(0.10.0 contra el header actual en el mismo proceso) y es relativa al
**final**, no a `unl`.

| versión | memoria aux. (10⁷ u64) | aleatorio | casi ordenado | organ_pipe | adversarial | registros 72 B |
|---|---|---|---|---|---|---|
| 0.10.0 | 262.5 MB (26.25 B/elem) | +89% (533 ms vs 283 ms del final) | +110% | +401% | +143% | — |
| 0.11-pre (`unl`) | 82.5 MB (8.25) | referencia | referencia | referencia | referencia | referencia |
| candidato A (600 KiB) | 0.61 MB (0.061) | −35% | −21% | +204% | −21% | −83% |
| candidato B (16 MiB, sin fusión) | 16 MiB (1.68) | −34% | −6% | +175% | −26% | −73% |
| candidato B + fusión por trozos | 16 MiB (1.68) | — | — | −12% | — | — |
| **final (defecto)** | **16 MiB (1.68)** | **−31%** | **−26%** | **−3%** | **−43%** | **−46…−84%** |

Y frente a `std::sort` a 10⁷ (`final_version_timings.txt`, 18 formas): el
final va de 0.04× (sorted_tail) a 0.68× (local_disorder); 0.10.0 iba de
0.21× a 1.47×.

## 12. Lo descartado, y por qué

| opción | resultado | decisión |
|---|---|---|
| American flag con el fan-out de una pasada (`⌈n/λ⌉` grupos) | +251…+1303% (0.11-pre) | descartado: el radio debe estar acotado |
| American flag con radio ≤ 2¹⁰ | +56…+86% a 10⁶ | solo como suelo, sin bloques |
| radio 2⁸ / 2⁹ / 2¹¹ | 4–18% peor o el doble de memoria | L = 10 |
| bloques de 256 / 1024 / 2048 B | 4–14% peor o el doble de memoria | 512 B |
| **offsets bajo demanda** (fronteras recalculadas galopando) | arena 44 KB → 16 KB, `O(r log(c/r))` por pase | no hecho: constante contra constante; los bloques dominan |
| **L = 1** (radio 2) | O(w + D) palabras, ≤ 77 pases | no hecho: Θ(n) con constante inútil |
| contadores de 16 bits por pase | ahorra la mitad de una arena de 44 KB | no hecho: constante |
| híbrido con más memoria (4–16 MiB) | ≈ igual que 600 KiB a 10⁷ | se mantiene solo porque la fusión por trozos usa el mismo buffer |
| fusión con `std::rotate` | ~ igual; con buffer (memmove) menos movimientos | rotación por el buffer cuando cabe |
| fusión por trozos con > 4 trozos | `O(q·cola)` crece: a q = 4 ya cuesta lo mismo que ordenar todo in-place | tope en 4 |
| suelo como defecto (44 KB) | +8…+100% frente al buffer a 10⁷ | opción explícita (`Workspace(0)`) |
| orden estable acotado por degradación a `O(n log n)` | rompería la garantía Θ(n) | **rechazado**: `std::length_error` |
| candidato C (pares clave-índice) | §10.3 | no adoptado en esta fase |
| explicación TLB del buffer compañero con registros | refutada por el experimento de páginas enormes | §10.4 |

## 13. Multiplataforma

`benchmarks/stratum_bench.cpp` gana una tabla de políticas de memoria
(`--suite memory`, y dentro de `ci` y `full`): int64 a 10⁶ y 10⁷ en varias
formas y registros de 72 B a 10⁷, con presupuesto ilimitado, automático,
600 KiB y 0, tiempo relativo y bytes medidos en el asignador. El workflow
`bench.yml` la ejecuta en Linux (GCC), macOS arm64 (AppleClang) y Windows
(MSVC) en cada push de esta rama. Resultados: sección añadida tras el
run de CI (ver el final de este documento).

## 14. Lo que queda

1. **El orden estable de registros sigue siendo Θ(n).** `stable_sort_by_key`
   necesita `n` registros; `sorted_indices`, `n` pares. El candidato C lo
   baja a 16 B/elem con ganancia de tiempo para registros ≥ 264 B, pero no
   lo elimina. Un orden estable lineal con memoria acotada existe en teoría
   (Franceschini–Muthukrishnan–Pătraşcu, ESA 2007) sin constantes usables.
2. **Por debajo de 16 MiB el defecto usa el buffer compañero** — por
   política, no por necesidad: ahí es más rápido en las formas casi
   ordenadas y con pocos outliers. `Workspace(bytes)` lo acota a cualquier
   nivel.
3. **Costes del modo acotado**: floats con pocos valores (+21…+24%),
   prefijo ordenado con una cola que necesita > 4 trozos (organ_pipe a
   10⁸: +237%, aunque 0.15× `std::sort`), y el suelo sin bloques
   (+8…+100% a 10⁷).
4. **El suelo es 15–134× la memoria de `std::sort`** (44 KB frente a
   0.3–3 KB de pila). Es constante, pero no es `O(log n)` en bytes
   absolutos; bajar a ~16 KB (offsets bajo demanda) o ~1 KB (L = 1) es
   posible a coste de tiempo.
5. **No verificado fuera de x86-64/Linux** hasta que el CI de esta rama
   termine (§13).
