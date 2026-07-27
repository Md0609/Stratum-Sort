# DRS v9 — Especificación técnica

**Estado: especificación para implementar. Pendiente de revisión y
aprobación.** No contiene código. Cada decisión lleva su justificación;
las decisiones que no están cerradas están marcadas como **[ABIERTO]** y
se cierran con el barrido de §8, no por criterio del autor.

---

## 1. Respuestas directas a las ocho preguntas

| Pregunta | Respuesta |
|---|---|
| ¿Debe desaparecer `MAX_SUBDIVISION_DEPTH`? | **Sí, como semántica.** Sobrevive únicamente como aserción de depuración sobre un invariante demostrable (`profundidad ≤ w+1`). Nunca como salida alternativa. |
| ¿Qué ocurre cuando una hoja sigue siendo grande? | **Deja de existir esa categoría.** Una hoja es (a) de tamaño `≤ t`, o (b) monovaluada. Una hoja monovaluada de 200.000 elementos está ordenada y cuesta cero. |
| ¿Debe existir todavía `localSort()`? | **Sí, pero deja de ser un algoritmo de ordenación y pasa a ser un caso base.** Solo Insertion Sort. QuickSort, Introsort y HeapSort se eliminan porque son inalcanzables. |
| ¿Puede el refinamiento completarlo todo por sí solo? | **Sí** (con `t = 1`), pero **no debe**. El caso base no es una concesión: por debajo de `t` elementos, seguir refinando cuesta más pasadas que ordenar directamente. |
| ¿Cómo queda el flujo? | Un único recorrido en profundidad que **emite salida terminada**. Desaparecen las fases `distribute`, `localSort` y `merge` como etapas separadas. |
| ¿Qué estructuras nuevas hacen falta? | Una arena de desplazamientos por nivel. Y **una menos**: el árbol de refinamiento desaparece, y uno de los dos buffers también. |
| ¿Qué se puede eliminar? | ~40 % del algoritmo: 6 funciones de ordenación, la estructura de árbol, dos recorridos recursivos, un buffer de tamaño `n`, un umbral, un miembro muerto y el recorte de índice. |
| ¿Qué hay que reescribir de cero? | `refine()`, `countAndPlace()`, `computeRangeParameters()` (que se fusiona con `refine`) y la etapa de salida. |

---

## 2. Premisas aceptadas y prerrequisito de corrección

Se aceptan las tres premisas del encargo. Todas se apoyan en el
Teorema 4 de `RESEARCH_refinamiento_terminal.md`, que **sobrevivió a la
revisión adversaria** (`REVIEW_refinamiento_terminal.md`, Parte 3): el
refinamiento sin corte de profundidad termina, y toda hoja cumple
`m ≤ t` o es monovaluada.

**Ese teorema presupone aritmética exacta sobre el rango, y el código
actual no la tiene.** El prerrequisito no es opcional: sin él, la
premisa «`MAX_SUBDIVISION_DEPTH` no forma parte de la esencia» es falsa,
porque el corte está tapando un caso degenerado.

### 2.1 Prerrequisito: aritmética de *span*

El código actual representa el rango como `R = max − min + 1`, cantidad
que necesita `w+1` bits y **desborda a 0** cuando `R = 2^64` (verificado:
`min = INT64_MIN`, `max = INT64_MAX` ⟹ `binCount = 1`,
`intervalSize = 1`, todo colapsa en el último bucket).

v9 representa el rango por su **span**:

```
  span = (uint64) max − (uint64) min                     ∈ [0, 2^w − 1]
```

`span` nunca desborda. Todas las fórmulas se reescriben sobre él:

```
  monovaluado   ⟺  span == 0
  tope de abanico:  si span < s   entonces   s ← span + 1      (*)
  anchura       W  =  span / s + 1                    (división entera)
  índice        i(v) = ((uint64) v − (uint64) min) / W
```

**(\*)** es seguro: la rama solo se toma cuando `span < s ≤ S_max`, luego
`span + 1` es un número pequeño.

**Propiedad 1 (equivalencia).** Donde la fórmula antigua no desbordaba,
`span/s + 1 = ⌈(span+1)/s⌉`. *Demostración:* con `span = qs + r`,
`0 ≤ r < s`, se tiene `⌈(qs+r+1)/s⌉ = q + ⌈(r+1)/s⌉ = q + 1` porque
`1 ≤ r+1 ≤ s`. ∎ (Verificado exhaustivamente para `span < 5000`.)

**Propiedad 2 (el índice está siempre en rango).** `i(v) ≤ s − 1` para
todo `v ∈ [min, max]`. *Demostración:* `i(v) ≤ span/W` con `W = q+1`,
`q = ⌊span/s⌋`. Como `span = qs + r < qs + s = s(q+1) = sW`, se sigue
`span/W < s`, luego `⌊span/W⌋ ≤ s−1`. ∎ (Verificado sobre 27.135 casos,
incluidos `span = 2^64−1` y barrido exhaustivo de monotonía.)

**Consecuencia inmediata: el recorte defensivo
`if (index >= binCount) index = binCount − 1` se elimina.** No es una
optimización: es código que sólo podía activarse por el desbordamiento
que acabamos de suprimir, y mantenerlo enmascararía cualquier regresión
futura de esta aritmética.

---

## 3. Cambios conceptuales

Ocho cambios. Cada uno altera **qué es** el algoritmo, no cuánto tarda.
Los cambios que sólo reducen constantes están fuera de esta
especificación y se tratarán, si procede, después de medir v9.

### CC1 — El refinamiento es terminal

`MAX_SUBDIVISION_DEPTH` desaparece como condición de salida. Las únicas
condiciones de hoja son:

```
  m ≤ t          → caso base (Insertion Sort)
  span == 0      → monovaluado, ya ordenado, coste cero
```

**Por qué mejora el algoritmo.** Elimina el único término superlineal:
hoy, un bin que agota la profundidad se entrega a Introsort con tamaño
arbitrario, lo que produce el peor caso `Θ(n log n)`. Sin esa salida,
el peor caso pasa a `O(n·(w + t))`, es decir **`O(n)` para palabra fija,
sin supuestos sobre la distribución de entrada**.

**Garantía de terminación** (Lemas 3 y 3'): mientras `span ≥ 1` y
`s ≥ 2`, la anchura es `W ≤ ⌈(span+1)/2⌉`, luego el span de todo hijo es
`≤ W − 1 < span`. Decrecimiento estricto de un entero positivo.

**Sustituto defensivo.** Se conserva una constante
`DEPTH_ASSERT_BOUND = 66` usada **solo en una aserción de compilación de
depuración**. Si se dispara, es un fallo de la aritmética de §2.1, no una
condición de los datos. **No existe camino alternativo**: en compilación
de producción no hay comprobación de profundidad en absoluto.

### CC2 — El abanico está acotado por el rango observado

```
  s = clamp( ⌈m / λ⌉ , 2 , min(span + 1, S_max) )
```

El tope `s ≤ span + 1` es el cambio conceptual. Cuando se activa,
`W = 1` y **cada bucket contiene exactamente un valor distinto: el bin
queda completamente ordenado en esa única pasada, sin recursión y sin
una sola comparación** (Lema 3).

**Por qué mejora el algoritmo.** Hoy `computeRangeParameters()` aplica
este tope en el nivel superior ([DynamicRangeSort.tpp:73](../algoritmo/DynamicRangeSort.tpp:73))
y `refine()` **no** lo aplica ([DynamicRangeSort.tpp:217](../algoritmo/DynamicRangeSort.tpp:217)).
Esa asimetría no está justificada en ninguna parte y hace que un
sub-bin de rango estrecho se subdivida durante varios niveles cuando
podía resolverse en uno. Unificar la regla no es limpieza: es la
condición que convierte los datos densos o con redundancia en un
problema de una sola pasada.

Efecto concreto sobre los datasets del proyecto (`n = 10⁶`):

| Dataset | span | `s` efectivo | Resultado |
|---|---|---|---|
| `ManyRepeated` | 4 | 5 | `W=1` → 5 hojas monovaluadas. **Una pasada.** |
| `SmallRangeManyEl` | 99 | 100 | `W=1` → 100 hojas monovaluadas. **Una pasada.** |
| `RandomUniform` | ~10⁶ | `⌈n/λ⌉` | tope no activo, comportamiento normal |

### CC3 — Ocupación objetivo y umbral de caso base son parámetros distintos

Hoy `targetElementsPerBin` desempeña tres papeles a la vez: ocupación
media de los bins iniciales, umbral de hoja, y divisor del abanico. v9
los separa en:

- **`λ`** — ocupación objetivo. Determina `s = ⌈m/λ⌉`.
- **`t`** — umbral de caso base. Determina cuándo se deja de refinar.

**Por qué mejora el algoritmo.** Con `λ = t`, la ocupación de un bin es
Poisson(`t`) y `P(X > t) ≈ 0,5`: **la mitad de los elementos entra en
refinamiento por aritmética, no por los datos**. (Coincide con el 51,6 %
medido en `ANALYSIS_v8.md`.) Con `λ = t/2`, `P(Poisson(t/2) > t)` cae
varios órdenes de magnitud y la mayoría de los bins son hoja al primer
nivel. Los dos parámetros responden a preguntas distintas y fusionarlos
sólo puede acertar por casualidad.

### CC4 — «Ordenado» es un certificado producido por el refinamiento

Cuando `refine()` determina `span == 0`, **ya sabe** que el bin está
ordenado. v9 propaga ese hecho al descriptor de la hoja.

**Por qué mejora el algoritmo (y por qué ahora es imprescindible).** En
v8 esto sería una micro-optimización. Bajo CC1, la hoja monovaluada pasa
a ser **la categoría principal de hoja grande**: sin el certificado, el
caso base volvería a recorrer 200.000 elementos con `detectRun()` para
redescubrir algo que el refinamiento acababa de calcular. El certificado
es carga estructural, no ahorro.

### CC5 — El árbol de refinamiento no se materializa

`RefinedRange` y su `std::vector<children>` desaparecen.

**Por qué mejora el algoritmo.** El árbol no transporta ninguna
información que las etapas posteriores consuman. Lo único que necesitan
es *la lista de hojas en orden ascendente*, y esa lista se obtiene del
propio recorrido.

**Invariante que lo justifica (I-TESELADO).** Las hojas teselan `[0, n)`
en orden ascendente, y **cada hoja ya ocupa su posición final**.
*Demostración:* `countAndPlace` coloca los hijos en desplazamientos
consecutivos a partir del `start` del padre, en orden de índice de
bucket; los hijos teselan el rango del padre; por inducción sobre la
profundidad, las hojas teselan `[0,n)`. Como los intervalos de valor son
crecientes (A1), el orden de teselado es el orden de valor. ∎

Este invariante ya se cumple en v8 pero **nadie lo había escrito**, y
`mergeRefined()` lleva un cursor `pos` redundante que siempre vale
`node.start`.

### CC6 — El array de entrada es uno de los dos buffers

v8 usa `bufferA_` y `bufferB_` (ambos de tamaño `n`) y después copia todo
a `data`. v9 hace ping-pong entre **`data` y un único buffer scratch**.

**Por qué mejora el algoritmo.** Memoria auxiliar de `2n` a `n`. Y por
I-TESELADO, una hoja que quede en `data` **ya está en su sitio: coste
cero**. Sólo las hojas de paridad impar se copian.

*Seguridad:* el nivel 0 lee `data` y escribe `scratch` (arrays
disjuntos); el nivel 1 lee `scratch` y escribe `data`, cuyo contenido en
ese rango ya fue consumido. Todos los bins ocupan rangos disjuntos, así
que escribir la salida de una hoja nunca pisa un bin pendiente.

### CC7 — `localSort()` colapsa a un único caso base

Se elimina el despachador de tres vías. Queda:

```
  si el certificado dice "ordenado"     → nada
  si detectRun() == descendente         → invertir  (O(t))
  en otro caso                          → Insertion Sort (O(t²), t acotado)
```

**Por qué mejora el algoritmo.** Por CC1, `QuickSort` e `Introsort` son
**inalcanzables**: ninguna hoja supera `t` elementos salvo las
monovaluadas, que no se ordenan. Mantener código inalcanzable en el
camino caliente es una responsabilidad de corrección sin
contrapartida: hoy nadie puede afirmar que esas ramas se ejecutan, y sin
embargo `QUICKSORT_THRESHOLD` sigue participando en decisiones.

`detectRun()` se conserva **con una justificación más estrecha que
antes**: para una hoja ya ascendente, Insertion Sort ya es `O(t)`, así
que `detectRun` no aporta; su único valor real es la hoja descendente,
donde convierte `O(t²)` en `O(t)`. Se conserva por eso, y su utilidad es
medible (§9).

### CC8 — No hay fase `distribute()`: el nivel 0 es el primer refinamiento

`computeRangeParameters()` + `distribute()` desaparecen como concepto
separado. El nivel 0 es `refine(origen = data, start = 0, count = n)`.

**Por qué mejora el algoritmo.** Hoy hay dos códigos que calculan
fronteras con reglas ligeramente distintas — y esa divergencia es
exactamente el defecto que CC2 corrige. Un solo camino de código hace
imposible que vuelvan a separarse. Es también la consecuencia natural de
aceptar que el refinamiento es el núcleo: si lo es, el primer nivel no
puede ser un caso especial.

La única asimetría que queda es la paridad del buffer, que es un
parámetro de la llamada.

---

## 4. Especificación del algoritmo

### 4.1 Parámetros

| Nombre | Significado | Restricción | Valor inicial |
|---|---|---|---|
| `λ` | ocupación objetivo | `1 ≤ λ ≤ t` | 16 **[ABIERTO]** |
| `t` | umbral de caso base | `t ≥ 1` | 32 **[ABIERTO]** |
| `S_max` | tope de abanico por nivel | `≥ 2` | 2¹⁶ **[ABIERTO]** |

`S_max` **no** es una constante de rendimiento arbitraria: acota la
memoria del array de desplazamientos de un nivel (`(s+1)` palabras) y
por tanto el consumo total. Su valor está abierto (§8).

### 4.2 Invariantes

- **I-TERM.** Mientras un bin se refina, `span` decrece estrictamente.
  ⟹ terminación; profundidad `≤ w + 1`.
- **I-RANGO.** `0 ≤ i(v) ≤ s−1` para todo `v` del bin (Propiedad 2).
  ⟹ no hace falta recorte.
- **I-TESELADO.** Las hojas teselan `[0,n)` en orden ascendente y cada
  hoja está ya en su posición final.
- **I-HOJA.** Toda hoja cumple `count ≤ t` **o** `span == 0`.
- **I-PROGRESO.** Toda subdivisión produce `≥ 2` buckets no vacíos
  (el elemento igual a `min` cae en el 0; el igual a `max`, en un índice
  `≥ 1`). ⟹ `max hijo ≤ m − 1`.

### 4.3 Flujo

```
ORDENAR(data[0..n)):
    si n < 2: retorno
    reservar scratch[0..n)                       # sin inicializar
    (min, max) ← ANALIZAR(data)                  # una pasada O(n)
    REFINAR(origen=data, destino=scratch, start=0, count=n,
            min, max, profundidad=0)

REFINAR(origen, destino, start, count, min, max, profundidad):
    span ← (u64)max − (u64)min

    # ---- condiciones de hoja (I-HOJA) --------------------------------
    si span == 0:                                # monovaluado
        EMITIR_HOJA(origen, start, count, ordenado=verdadero); retorno
    si count ≤ t:
        EMITIR_HOJA(origen, start, count, ordenado=falso);     retorno

    # ---- abanico (CC2) ------------------------------------------------
    s ← ⌈count / λ⌉
    s ← max(s, 2);  s ← min(s, S_max)
    si span < s:  s ← span + 1                   # ⟹ W = 1, hijos monovaluados
    W ← span / s + 1

    # ---- conteo y colocación -----------------------------------------
    offsets[0..s] ← arena.reservar(s + 1)
    CONTAR_Y_COLOCAR(origen, destino, start, count, min, W, s, offsets)

    # ---- descenso, en orden ascendente de bucket ----------------------
    para b en 0..s−1:
        c ← offsets[b+1] − offsets[b]
        si c == 0: continuar                     # los buckets vacíos se descartan
        si c ≤ t:
            EMITIR_HOJA(destino, offsets[b], c, ordenado=falso)
        si no:
            (mn, mx) ← MINMAX(destino, offsets[b], c)
            REFINAR(destino, origen, offsets[b], c, mn, mx, profundidad+1)
    arena.liberar(s + 1)

EMITIR_HOJA(buffer, start, count, ordenado):
    si no ordenado y count ≥ 2:
        segun DETECTAR_TRAMO(buffer, start, count):
            ascendente  → nada
            descendente → invertir en el sitio
            desordenado → INSERTION_SORT en el sitio
    si buffer ≠ data:
        copiar buffer[start..start+count) → data[start..start+count)
```

### 4.4 Notas sobre el flujo

- **La comprobación `c ≤ t` se hace antes de calcular `MINMAX`.** Un bin
  que va al caso base no necesita su rango observado. Esto ya ocurre en
  v8 y se conserva deliberadamente.
- **`EMITIR_HOJA` hace la ordenación y la escritura de salida en el
  mismo punto**, mientras los datos están calientes en caché tras la
  pasada de colocación que acaba de escribirlos. Desaparecen las fases
  `localSort` y `merge` como recorridos separados.
- **Los buckets vacíos se descartan por completo.** No generan hoja, no
  ocupan lista, no se recorren después. (En `NormalGaussian` el 15,08 %
  de los bins están vacíos, según `ANALYSIS_v5.md` §2.4.)
- **El recorrido es recursivo**, con profundidad demostrablemente
  `≤ w+1 = 65`. No hace falta pila explícita.

---

## 5. Estructuras de datos

### 5.1 Se elimina

| Estructura | Motivo |
|---|---|
| `RefinedRange` (nodo + `vector<children>`) | CC5. ~7.276 asignaciones por `sort()` en `RandomUniform` |
| `bufferB_` | CC6. `n·sizeof(T)` bytes |
| `bucketStart` / `bucketSize` locales de `refine()` | 2 asignaciones por subdivisión → arena |
| `bucketSizeScratch_` | Miembro muerto: declarado, nunca usado |

### 5.2 Se conserva o se transforma

| Estructura | Forma en v9 |
|---|---|
| `bufferA_` | pasa a ser el único `scratch_`, `n` elementos, **sin inicializar** |
| `bucketOfScratch_` | `uint32_t` en vez de `size_t` (mitad de tráfico). Requiere `n < 2³²`, comprobado con aserción estática/dinámica |
| `writeCursorScratch_` | se fusiona con `offsets` (el cursor final del bucket `b` es el inicio del `b+1`) |

### 5.3 Se añade

**Arena de desplazamientos.** Un único `vector<size_t>` con puntero de
tope. `REFINAR` reserva `s+1` posiciones al entrar y las libera al salir.
Sustituye a las dos asignaciones por llamada y a cualquier reserva por
profundidad.

*Dimensionado:* el pico es `Σ_d (s_d + 1)` a lo largo de una rama.
Como `s_d ≈ s_{d-1}/λ`, la suma converge a `s_0·(1 + 1/λ + 1/λ² + …) ≈
s_0·λ/(λ−1)`. Para `n = 10⁶`, `λ = 16`, `S_max = 2¹⁶`: `s_0 = 62.500`,
pico ≈ 66.700 palabras ≈ **534 KB**. Se reserva una vez por `sort()`.

**Descriptor de hoja** (solo en compilación de investigación,
`DRS_ENABLE_METRICS`): `{start, count, buffer, ordenado, profundidad}`.
En producción no se materializa ninguna lista de hojas.

### 5.4 Balance de memoria (`n = 10⁶`, `int64_t`)

| | v8 | v9 |
|---|---|---|
| buffers de datos | `2n·8` = 16,0 MB | `n·8` = 8,0 MB |
| `bucketOf` | `n·8` = 8,0 MB | `n·4` = 4,0 MB |
| desplazamientos / árbol | ~1,2 MB (árbol + vectores) | 0,53 MB (arena) |
| **total auxiliar** | **~25,2 MB** | **~12,5 MB** |

Reducción del 50 %. Además, la cifra de v8 **no es la que reporta el
propio proyecto**: `addApproxMemory()` no contabiliza `bucketOfScratch_`
(§9).

---

## 6. Qué se elimina del código actual

**Funciones que desaparecen por completo** (inalcanzables bajo CC1/CC7):

```
quickSort, introSort, introSortImpl, heapSort, siftDown, partition
```

≈ 150 líneas. Con ellas, la constante `QUICKSORT_THRESHOLD`.

**Funciones que desaparecen por reestructuración:**

```
computeRangeParameters   → absorbida por REFINAR (CC8)
distribute               → absorbida por REFINAR (CC8)
sortRefined              → absorbida por EMITIR_HOJA (CC5)
mergeRefined             → absorbida por EMITIR_HOJA (CC5, CC6)
flattenLeaves            → innecesaria: las hojas ya se emiten en orden
```

**Constantes que desaparecen:** `MAX_SUBDIVISION_DEPTH` (semántica),
`QUICKSORT_THRESHOLD`, `INSERTION_SORT_THRESHOLD` (se funde con `t`).

**Fragmento que desaparece:** el recorte
`if (index >= binCount) index = binCount − 1` (Propiedad 2).

## 6.1 Qué se reescribe desde cero

`refine()`, `countAndPlace()`, la orquestación de `sort()`, y la etapa de
salida. Es el 60 % de `DynamicRangeSort.tpp`.

## 6.2 Qué se conserva casi intacto

`analyze()`, `insertionSort()`, `detectRun()`, y todo lo externo
(`DatasetGenerator`, `SystemInfo`, `Statistics`, las reconstrucciones
históricas `versions/`).

---

## 7. Comportamiento previsto por dataset (`n = 10⁶`, `λ=16`, `t=32`, `S_max=2¹⁶`)

Predicciones **falsables**, para contrastar en el paso 6 del plan:

| Dataset | `s` nivel 0 | `W` | Profundidad | Observación |
|---|---|---|---|---|
| `ManyRepeated` | 5 | 1 | **1** | 5 hojas monovaluadas; hoy hace 2 escaneos completos de más |
| `SmallRangeManyEl` | 100 | 1 | **1** | 100 hojas monovaluadas |
| `SortedAscending` | 62.500 | 17 | **1** | hojas de ~16 ya ordenadas |
| `SortedDescending` | 62.500 | 17 | **1** | hojas de ~16 descendentes → `detectRun` + invertir |
| `RandomUniform` | 62.500 | 16 | **1** (≈99,99 %) | Poisson(16), `P(X>32) ≈ 10⁻⁴` |
| `NormalGaussian` | 62.500 | 16 | 1–2 | colas ⟹ bins vacíos, ahora descartados |
| `Concentrated` | 62.500 | 16 | 2–3 | el cúmulo estrecho concentra la masa; CC2 lo resuelve al bajar |
| `HugeRangeFewEl` | 62.500 | ~2·10¹³ | 1–2 | span enorme, tope inactivo |
| `FullRangeExtremes` | 62.500 | ~2·10¹⁴ | **1–2** | hoy 3 por el desbordamiento; con §2.1 debe igualar a `HugeRangeFewEl` |
| `AdversarialPeeling` | 62.500 | — | **≤ 66** | hoy tope 6 + 9.346 hojas a QuickSort; con CC1 debe dar **cero** hojas a sort por comparación |

**Comparaciones previstas en `RandomUniform`:** hojas de tamaño medio
`λ=16` ⟹ `n·λ/4 ≈ 4,0 M`, frente a los **12,15 M** medidos en v8. La
predicción del modelo para `target=19` era 4,75 M y lo medido fue 4,0 M
(`ANALYSIS_v5.md` §2.1), así que el modelo está calibrado en este
régimen.

---

## 8. Parámetros abiertos y cómo se cierran

Los tres parámetros están **abiertos a propósito**. La revisión adversaria
(`REVIEW_...`, C-A y H-9) estableció que:

- el cálculo teórico que fijaba `t ∈ [16,32]` era **incorrecto**, y
  corregido pide `t → 1`;
- la medición de v5 pidió `t ≈ 19`;
- las dos discrepan porque el modelo de coste teórico (`Θ(m+s)`) ignora
  caché y TLB, que es justo lo que decide `S_max`.

**Esta especificación no resuelve esa contradicción por decreto.** Fija
valores iniciales para poder implementar, y los cierra con un barrido.

**Barrido obligatorio (paso 6 del plan):** `λ × t × S_max` sobre
`{4,8,16,24,32} × {16,32,64} × {2¹¹, 2¹⁴, 2¹⁶, ∞}`, los ocho datasets,
midiendo **contadores deterministas además del tiempo**:
`comparisons`, `bins`, `workByDepth`, profundidad máxima, hojas por
categoría. `S_max = ∞` es una configuración válida y recupera el
comportamiento de un solo nivel.

**Criterio de cierre:** se elige el punto que minimiza el tiempo mediano
en `RandomUniform` sin empeorar ningún otro dataset respecto de v8. Si
no existe tal punto, se documenta y se decide explícitamente, no por
omisión.

---

## 9. Instrumentación necesaria

Sin esto no se puede validar la especificación:

1. **Corregir `addApproxMemory()`** para contabilizar `bucketOfScratch_`
   y la arena. Hoy subestima ~50 % y por tanto la tabla de §5.4 no es
   verificable con la métrica del proyecto.
2. **Contadores nuevos:** abanico `s` por nivel, bits consumidos
   (`Σ log₂ s` por camino), profundidad máxima alcanzada, hojas por
   categoría (monovaluada / caso base), buckets vacíos descartados.
3. **Aserciones de invariante** (compilación de depuración):
   `I-TESELADO` (`Σ count de hojas == n` y `start` estrictamente
   creciente), `I-RANGO`, `I-HOJA`, profundidad `≤ 66`.
4. **Predicción crítica de CC1/CC7:** `algorithmUsage()` **no debe
   registrar jamás** `QuickSort` ni `Introsort`. Si lo hace, el
   Teorema 4 está mal aplicado y hay que parar.
5. **Dataset adversario ausente.** Ninguno de los ocho datasets activa el
   peor caso (grupos que pierden un elemento por nivel; ver
   `REVIEW_...` Teorema 9'). Hay que construir el generador, o el peor
   caso de v9 seguirá sin verificarse — igual que el de v8.
6. **Caso límite de rango completo:** un dataset que contenga
   `INT64_MIN` y `INT64_MAX` con más de `t` elementos.
   **[AÑADIDO en el paso 0: `fullRangeExtremes`.]** Su justificación,
   corregida tras medirlo: el desbordamiento **no** produce una hoja
   gigante (la revisión ya había demostrado que no puede encadenarse);
   cuesta **2 pasadas O(n) desperdiciadas, +43 % de tiempo a igualdad de
   comparaciones**, y es **la única violación conocida del Lema 1** — de
   la cual depende que quitar el tope de profundidad (CC1) sea seguro.
   Ésa, y no el peor caso, es la razón por la que §2.1 es prerrequisito.

---

## 10. Riesgos

| Riesgo | Mitigación |
|---|---|
| Un fallo en la aritmética de span produce recursión profunda al no haber tope | Aserción `profundidad ≤ 66`; Propiedades 1 y 2 verificadas antes de implementar |
| `scratch` sin inicializar ⟹ lectura de memoria no inicializada si I-TESELADO falla | Aserción de teselado en depuración; ejecución bajo sanitizers en el paso 1 |
| `S_max` pequeño obliga a niveles extra en datos que se resolvían en uno | `S_max = ∞` es configuración válida; el barrido lo decide |
| Muchos `memcpy` pequeños (uno por hoja) en vez de pocos grandes | Alternativa especificada: coalescer hojas consecutivas del mismo buffer. Requiere lista de hojas ⟹ solo si el barrido lo justifica |
| `bucketOf` en `uint32_t` limita `n < 2³²` | Aserción explícita; el proyecto mide hasta `5·10⁶` |
| **`SortedAscending` es ahora el peor caso relativo (6,02x vs `std::sort`) y v9 no lo mejora por construcción** | Detectado en el paso 0 (`BASELINE_v8.md` §5): la `std::sort` de libc++ resuelve un millón de enteros ordenados en 0,73 ms; DRS hace como mínimo 3 pasadas. **Decidir explícitamente en el paso 6 si se acepta**, en vez de descubrirlo al final. Una detección de "ya ordenado" sería una heurística nueva, fuera del alcance de v9 |
| `λ` pequeño ⟹ más bins ⟹ más presión de TLB en la pasada de colocación | Es exactamente lo que mide el barrido de §8 |

---

## 11. Plan de implementación

Cada paso es independiente y **medible**; ninguno avanza sin que el
anterior pase. Todos contra la línea base v8 re-medida **en esta máquina**
(los números históricos son de un Xeon x86 con GCC; ésta es arm64 con
Apple clang).

| # | Paso | Criterio de aceptación |
|---|---|---|
| 0 | Re-medir v8 en esta máquina; añadir los dos datasets de §9.5–9.6 | **COMPLETADO** — ver `BASELINE_v8.md`. Criterio corregido: el dataset de rango completo debe mostrar profundidad estrictamente mayor y tiempo ≥ 15 % superior al de un control pareado, a igualdad de comparaciones (medido: 3 vs 1, +43 %) |
| 1 | Aritmética de span + eliminar el recorte (sin más cambios) | `make test` pasa; `FullRangeExtremes` baja de 20,3 ms a ≈ 14 ms y su profundidad de 3 a 1; **ningún otro dataset se mueve más del 6 %** (umbral de ruido medido) |
| 2 | CC2 (tope de abanico por span) en `refine()` | `ManyRepeated` y `SmallRangeManyEl` bajan a profundidad 1; comparaciones caen |
| 3 | CC1 + CC7 (quitar el tope de profundidad; borrar los 6 sorts) | `algorithmUsage()` sin `QuickSort`/`Introsort` en ningún dataset; profundidad máxima ≤ 66 |
| 4 | CC5 + CC4 (sin árbol; certificado de ordenado; hojas en orden) | Aserción de teselado activa y verde; asignaciones por `sort()` caen a O(1) |
| 5 | CC6 + CC8 (`data` como buffer; un solo camino de código) | Memoria auxiliar medida ≈ `n·12` bytes; `distribute` ya no existe como fase |
| 6 | CC3 + barrido `λ × t × S_max` (§8) | Parámetros cerrados con datos, no por criterio |
| 7 | Informe v9 | Solo si hay algo que justificar; no un documento por defecto |

**Los pasos 1–3 son los que aportan la mejora conceptual.** Los pasos
4–6 la hacen barata. Si el paso 3 falla —si aparece una hoja grande no
monovaluada— hay que **detenerse** y revisar el Teorema 4, no añadir un
tope de profundidad de vuelta.
