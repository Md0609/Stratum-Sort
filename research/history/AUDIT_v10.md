# Auditoría técnica de Dynamic Range Sort

**Revisor externo. Partida: únicamente el código en `algoritmo/`
(`Config.hpp`, `DynamicRangeSort.hpp`, `DynamicRangeSort.tpp`,
`DRSMetrics.hpp`). No se asume nada de la documentación previa; cuando se
cita, se cita para contrastarla con el código, no para apoyarse en ella.**

Convenciones: `n` = número de elementos, `λ` = `targetElementsPerBin_`
(32 por defecto), `t` = `leafThreshold_` (64), `D` =
`MAX_SUBDIVISION_DEPTH` (6), `w` = bits del tipo (64 para `int64_t`).

---

# 1. Arquitectura

El algoritmo ordena en cinco fases secuenciales dentro de `sort()`.

### 1.1 `analyze` — mínimo y máximo

- **Qué hace.** Una pasada sobre `data` calculando `minimumValue` y
  `maximumValue`.
- **Por qué existe.** Todas las fórmulas posteriores (`span`, anchura de
  intervalo, índice de bin) están definidas sobre el rango observado. Es
  la única fase que no puede fusionarse con ninguna otra: la anchura no
  se conoce hasta haber visto el último elemento.
- **Invariante que mantiene.** A la salida,
  `∀x ∈ data: min ≤ x ≤ max`, y ambos son alcanzados.
- **Complejidad.** Θ(n) tiempo, Θ(1) espacio.
- **Coste.** `n` lecturas, 2 comparaciones por elemento. Medido: 1–9 % del
  total según el dataset.
- **¿Necesaria?** Sí, e irreducible. Ningún esquema de particionado por
  rango puede empezar sin ella.

### 1.2 `computeRangeParameters` — parámetros del nivel 0

- **Qué hace.** Deriva `binCount` y `intervalSize` de `(min, max, n, λ)`.
- **Por qué existe.** Traduce «quiero ~λ elementos por bin» a los dos
  números que la fase de distribución necesita.
- **Invariante.** `1 ≤ binCount ≤ span+1` y `intervalSize ≥ 1`, con
  `binCount · intervalSize > span` (garantiza que todo valor cae dentro).
- **Complejidad.** Θ(1).
- **Coste.** Despreciable.
- **¿Necesaria?** Sí, pero es tres líneas de aritmética; su existencia
  como función separada es organizativa, no algorítmica.

### 1.3 `distribute` — colocación de nivel 0

- **Qué hace.** Reserva dos buffers de tamaño `n`, los rellena de ceros, y
  llama a `countAndPlace` para repartir `data` en `binCount` intervalos de
  igual anchura dentro de `bufferA_`.
- **Por qué existe.** Convierte un problema global en `binCount`
  subproblemas independientes y **ordenados entre sí**: todo elemento del
  bin `i` es ≤ todo elemento del bin `i+1`. Ésa es la propiedad que hace
  que la fase final sea una concatenación y no una fusión.
- **Invariante.** Los bins teselan `[0, n)` en orden creciente de valor.
- **Complejidad.** Θ(n + binCount).
- **Coste.** 2n escrituras de relleno **que nadie lee** (§5.8), n lecturas
  + n escrituras de índices, n lecturas + n escrituras dispersas.
  Medido: 14–78 % del total según el dataset.
- **¿Necesaria?** Sí. Es el corazón del algoritmo.

### 1.4 `refine` — refinamiento recursivo

- **Qué hace.** Para cada bin de nivel 0, si supera `t` elementos y no es
  monovaluado, recalcula **su propio** mínimo y máximo y lo vuelve a
  repartir en `splits` intervalos, escribiendo en el *otro* buffer.
  Recursivo hasta profundidad `D`.
- **Por qué existe.** El nivel 0 usa una anchura uniforme sobre el rango
  global; una distribución no uniforme deja bins muy cargados. Refinar
  usando el rango **observado del propio bin** adapta la resolución a la
  densidad local sin mirar los datos globales.
- **Invariantes.** (a) el rango observado decrece estrictamente en cada
  nivel; (b) los hijos teselan el rango del padre en orden;
  (c) `splits ≥ 2` siempre que se subdivide.
- **Complejidad.** Θ(count) por nodo; Θ(n · profundidad) agregado.
- **Coste.** 3 lecturas + 1 escritura por elemento y nivel. Medido:
  1,4–57 % según el dataset.
- **¿Necesaria?** Sí para distribuciones sesgadas. Con λ=32 y t=64 está
  prácticamente vacía en datos uniformes (2,4 % medido), pero es lo único
  que impide que una gaussiana o un cúmulo degeneren.

### 1.5 `sortRefined` / `sortLeaf` — resolución local

- **Qué hace.** Recorre el árbol; por cada hoja, si tiene certificado no
  hace nada; si no, detecta tramo ascendente/descendente y, en el caso
  restante, ordena con Insertion / Quick / Intro según tamaño.
- **Por qué existe.** Por debajo de cierto tamaño, seguir refinando cuesta
  más pasadas que ordenar directamente.
- **Invariante.** A la salida, cada rango de hoja está ordenado
  ascendentemente **in situ** en su buffer.
- **Complejidad.** Θ(k) si hay certificado o tramo; Θ(k²) Insertion;
  Θ(k log k) Intro.
- **Coste.** 0–65 % según el dataset. Es la fase dominante en datos sin
  redundancia.
- **¿Necesaria?** Sí. Sin ella el refinamiento tendría que llegar hasta
  rango 1, lo que cuesta más (ver §3.6).

### 1.6 `mergeRefined` — unión

- **Qué hace.** Recorrido en orden del árbol; por cada hoja, un `memcpy`
  del buffer donde vive a `data`.
- **Por qué existe.** El resultado está repartido entre dos buffers según
  la paridad de la profundidad de cada hoja; hay que consolidarlo en el
  array del usuario.
- **Invariante (I-TESELADO).** `pos == node.start` en cada hoja: **cada
  hoja ya ocupa su posición final**. Verificado por aserción.
- **Complejidad.** Θ(n).
- **Coste.** 2–13 % del total.
- **¿Necesaria?** Sí mientras `data` no sea uno de los dos buffers.
  **El cursor `pos` es redundante** — es siempre `node.start`.

---

# 2. Revisión función por función

### `DynamicRangeSort(λ, t)`
Objetivo: fijar los dos parámetros. Entradas: dos `size_t`. `λ=0` → valor
por defecto; `t=0` → `λ`; en otro caso `t = max(t, λ)`.
Invariante que impone: **`t ≥ λ`**, correcto y necesario.
Complejidad Θ(1).
**Crítica:** el parámetro `t` tiene dos significados para el valor 0
(«igual que λ») y otro para el resto, mientras que el valor por defecto
del header es 64. Un lector tiene que mirar el `.tpp` para saber qué hace
`DynamicRangeSort(32, 0)`. **Es un sentinel innecesario.**

### `analyze`
Objetivo: min/max. Entrada `const vector<T>&`; salida `AnalysisResult`
(min, max, length). Θ(n). **`length` es redundante**: el llamante ya tiene
`data.size()`. La estructura existe para devolver tres valores, dos de
los cuales bastarían. Simplificable a `std::pair` o a dos salidas.
**¿Eliminable?** No, pero el `struct` sí.

### `computeRangeParameters`
Objetivo: `binCount`, `intervalSize`. Escrita con dos parámetros de
salida por referencia en lugar de devolver un `struct` o un `pair` — es
un estilo C en un archivo C++.
La rama `analysis.length == 0` **es inalcanzable**: `sort()` retorna antes
para `size() < 2` y `debugPartitionOnly` también. Código muerto.
La aritmética es correcta y está bien argumentada en el comentario
(propiedades 1 y 2). Θ(1).

### `computeBinIndex`
Objetivo: `(v - rangeStart) / intervalSize`. Θ(1). Correcta y mínima.
**El comentario afirma que el resultado siempre cae en `[0, numBuckets)`,
pero la función ya no recibe `numBuckets`**, así que el lector no puede
comprobar la afirmación en el sitio. La demostración vive en el
comentario de `computeRangeParameters`, 30 líneas más arriba.

### `countAndPlace`
Objetivo: contar y colocar. **Es la función más importante del algoritmo y
la peor documentada respecto de su contrato**: tiene 10 parámetros, dos de
ellos de salida, y depende de tres miembros scratch cuya validez descansa
en un argumento no local (la recursión es estrictamente en profundidad).
Complejidad Θ(count + numBuckets).
**Crítica seria:** 10 parámetros es demasiado para seguirla. Los tres
primeros (`src`, `srcStart`, `count`) y los dos siguientes
(`dst`, `dstStart`) piden a gritos un tipo «rango».
La rama `numBuckets == 1` copia elemento a elemento; medido, es el caso
**más rápido** de todos (§5.9), así que no es un problema de rendimiento,
pero sí de simetría: el resto del algoritmo usa `memcpy` para lo mismo.

### `distribute`
Objetivo: orquestar el nivel 0. Θ(n + binCount).
**Es una función de cuatro líneas que sólo existe para envolver
`countAndPlace`.** Dos de esas líneas (`assign` × 2) son el relleno de
ceros muerto. **Podría desaparecer** fusionándose en `sort()` sin pérdida
alguna de claridad — de hecho ganaría, porque hoy el lector tiene que
saltar aquí para descubrir dónde se reservan los buffers.

### `refine`
Objetivo: subdividir recursivamente. Θ(count) por nodo.
Es la función más larga (≈95 líneas con comentarios) y la que concentra
toda la lógica de decisión: cuatro salidas distintas (vacío, hoja por
tamaño/profundidad, hoja por rango degenerado, subdivisión).
**Crítica:** las cuatro salidas están intercaladas con bloques
`#ifdef DRS_ENABLE_METRICS`, lo que rompe la lectura. Un lector que quiera
entender la lógica tiene que filtrar mentalmente seis bloques de
preprocesador.
**Devuelve por valor un `RefinedRange` que contiene un `vector`**, y el
llamante hace `push_back` — un movimiento por nodo. Funciona, pero la
estructura de árbol no aporta nada que las fases posteriores usen: sólo
necesitan la lista de hojas en orden.

### `detectRun`
Objetivo: reconocer tramo ascendente/descendente. Θ(k) con salida
temprana. Correcta y clara.
**Defecto de instrumentación:** el bucle hace **dos** comparaciones por
iteración y registra **una**. El contador de comparaciones subestima
sistemáticamente esta función en 2x.

### `sortLeaf`
Objetivo: ordenar una hoja. Θ(k)…Θ(k²).
**Defecto real (§5.3):** el despachador usa `INSERTION_SORT_THRESHOLD`,
una constante de **compilación**, mientras el umbral de hoja es un
parámetro de **ejecución**. Con `t` distinto de 64 los dos divergen.

### `sortRefined`
Objetivo: recorrer el árbol ordenando hojas. Θ(#nodos + trabajo).
Clara. El atajo por certificado está bien colocado.

### `insertionSort`
Θ(k²) peor caso, Θ(k) si ya está ordenado. Implementación estándar y
correcta. `long` como tipo de índice es una elección desafortunada
(§6.5), pero funcional.

### `partition`
Mediana de tres + partición de Hoare. Θ(k).
**Precondición no comprobada:** «asume `right - left >= 2`». Se cumple
porque los dos llamantes filtran por `> 12`, pero **no hay `assert`**.
Es la función más frágil del archivo: los dos bucles `do/while` sin
comprobación de límites son correctos **sólo** porque la mediana de tres
coloca centinelas en ambos extremos. Ese razonamiento no está escrito.

### `quickSort`, `introSort`, `introSortImpl`, `heapSort`, `siftDown`
Implementaciones de libro, correctas. `quickSort` e `introSortImpl`
recursan sobre la partición menor e iteran sobre la mayor: profundidad de
pila Θ(log k). Correcto.
**`introSort` calcula `2·log2(n)` en coma flotante** dentro de un
algoritmo que por lo demás es puramente entero. Innecesario:
`std::bit_width` o un bucle de desplazamientos daría lo mismo sin tocar la
FPU.
**¿Necesarias?** `quickSort` e `introSort` **sí son alcanzables** (§3.5) —
por hojas que agotan la profundidad. `heapSort` sólo si además Introsort
agota su propio límite. Son la red de seguridad del peor caso.

### `mergeRefined`
Θ(n). El `memcpy` está bien justificado en el comentario (tres objetos
distintos, `T` trivialmente copiable, rango exacto por I-TESELADO).
**El parámetro `pos` es demostrablemente redundante** — la propia
aserción de la función lo prueba. Se podría eliminar pasando sólo `out`.

### `sort`
Orquestación. Θ del total.
**Crítica:** 50 líneas de las cuales ~20 son `#ifdef`. La estructura real
del algoritmo (cinco llamadas) está enterrada.

### `flattenLeaves`, `debugPartitionOnly`
Sólo en build de investigación. `debugPartitionOnly` **duplica** la
secuencia analyze → computeRangeParameters → distribute → refine de
`sort()`. Si alguien cambia el orden en `sort()` y no aquí, la
introspección mentiría en silencio. **Es duplicación peligrosa.**

---

# 3. Revisión matemática

Todo lo que sigue se deriva del código actual, con `λ=32`, `t=64`, `D=6`,
`w=64`.

### 3.1 Terminación

**Lema 1.** Si un bin se subdivide, `observedSpan ≥ 1` y `splits ≥ 2`.
*Demostración.* Se llega a la subdivisión sólo si `observedMin ≠
observedMax`, luego `observedSpan ≥ 1`. `splits = ⌈count/λ⌉` con
`count > t ≥ λ`, luego `splits ≥ 2`; y si el tope por span actúa,
`splits = observedSpan + 1 ≥ 2`. ∎

**Lema 2 (colapso del rango).** Con anchura `W = ⌊span/s⌋ + 1`, todo hijo
tiene span `≤ W − 1 ≤ ⌊span/s⌋ < span` para `s ≥ 2`, `span ≥ 1`.
*Demostración.* Un hijo ocupa un intervalo de `W` valores consecutivos,
luego su span es `≤ W−1`. Y `⌊span/s⌋ < span` cuando `s ≥ 2` y `span ≥ 1`.
∎

**Corolario.** El span decrece estrictamente por nivel. Como es un entero
`≥ 0` y la recursión se detiene en `span = 0`, **la recursión termina**
incluso sin el tope de profundidad.

### 3.2 Cobertura (I-TESELADO)

**Lema 3.** `countAndPlace` coloca los hijos en desplazamientos
consecutivos desde `dstStart` (el bucle de prefijos), y en orden creciente
de índice de bucket. Como `computeBinIndex` es monótona no decreciente en
el valor, los hijos teselan el rango del padre **en orden de valor**.
Por inducción sobre la profundidad, las hojas teselan `[0,n)` en orden.
**Verificado en ejecución** por dos aserciones (`pos == node.start` por
hoja, `pos == n` al final).

**Corolario (corrección).** Concatenar las hojas ordenadas produce el
array ordenado. La ordenación local ordena cada hoja *in situ*; la unión
las copia en orden. ∎

### 3.3 Profundidad máxima

Dos cotas independientes.

**(a) Presupuesto de bits.** Por el Lema 2, `span_{d+1} ≤ ⌊span_d/s_d⌋`.
Iterando, `∏ s_i ≤ span_0 ≤ 2^w − 1`, luego
`Σ log₂ s_i ≤ w`. Con `s_i ≥ 2`, **la profundidad es ≤ w = 64** aunque no
hubiera tope.

**(b) Tope explícito.** El código corta en `depth ≥ D = 6`.

**Profundidad efectiva = min(64, 6) = 6.** La recursión de pila está
acotada por 6 marcos en `refine`, `sortRefined` y `mergeRefined`.

### 3.4 Tamaño máximo de una hoja que llega al sort por comparación

Una hoja que alcanza `sortLeaf` con `count > t` sólo puede provenir de
agotar la profundidad. Para que un bin de tamaño `m` sobreviva `D` niveles
**sin** que su span se agote hace falta, por (a):

```
   D · log₂(splits)  ≤  w,    splits = ⌈m/λ⌉
   ⟹  m  ≤  λ · 2^(w/D)  =  32 · 2^(64/6)  ≈  52.016
```

**Éste es el resultado clave de todo el análisis:** el residuo entregado a
Quick/Introsort está acotado por una **constante independiente de `n`**.

### 3.5 Coste por nivel y coste total

Sea `n_d` el número de elementos vivos (en bins que se subdividen) en el
nivel `d`. Cada nivel cuesta:
- escaneo min/max: `n_d` lecturas
- conteo: `n_d` lecturas + `n_d` escrituras de índice
- prefijos: `Σ splits` ≤ `n_d/λ + #bins`
- colocación: `2n_d` lecturas + `n_d` escrituras

⟹ **Θ(n_d) por nivel**, con constante ≈ 4 accesos por elemento.

**Coste total**, sumando las cinco fases con las constantes del código:

| Fase | coste por elemento | notas |
|---|---|---|
| `analyze` | 1 lectura | |
| relleno de ceros | **2 escrituras** | trabajo muerto |
| `distribute` | ~4 accesos | |
| `refine` | ~4 accesos × profundidad | profundidad ≤ 6 |
| `detectRun` | ≤ 1 lectura | |
| ordenación local | ver abajo | |
| `merge` | 1 lectura + 1 escritura | `memcpy` |

### 3.6 Mejor caso, caso medio, peor caso

**Mejor caso: Θ(n).** Todos los elementos iguales. `span = 0` ⟹
`binCount = 1` ⟹ un bin, refinado una vez, certificado de ordenado, cero
comparaciones. Coste ≈ 6n accesos. **Medido: 1,27 ms para n=10⁶**, el
mínimo de toda la batería.

**Caso medio (uniforme, densidad no degenerada): Θ(n).**
Con `λ=32`, la ocupación es Poisson(32) y
`P(X > 64) ≈ 2·10⁻⁷`: prácticamente ningún bin se refina. Las hojas
tienen tamaño `k ~ Poisson(32)` y se ordenan con Insertion Sort:

```
  E[comparaciones/elemento] = E[k²]/(4·E[k]) = (λ²+λ)/(4λ) = (λ+1)/4 = 8,25
```

**Predicción 8,25n; medido 8,99n** (`RandomUniform`, n=10⁶). El error del
9 % lo explica `detectRun`, que el modelo no cuenta. Coste total ≈ 20n.

**Peor caso: Θ(n), constante ≈ 44.**
El adversario maximiza (i) la profundidad y (ii) el coste de la
ordenación local. Cotas por elemento:

```
  analyze            1
  relleno de ceros   2
  distribute         4
  refine           ≤ 4·D = 24
  detectRun          1
  local sort       ≤ t/2 = 32   (Insertion, peor caso k(k−1)/2)
                   ó ≤ log₂(52.016) ≈ 15,7  (Intro, hoja por profundidad)
  merge              2
```

Una hoja es de un tipo **o** del otro, luego el peor caso es
`1+2+4+24+1+32+2 ≈ 66n` si domina Insertion, y `≈ 50n` si domina
Introsort. **Θ(n) con constante acotada, para `w` y `λ` fijos.**

> **Advertencia sobre el sentido de «Θ(n)».** La constante contiene
> `w/log₂(λ)` a través de la profundidad y `log₂(λ·2^{w/D})` a través del
> residuo. Es lineal **tratando `w` como constante**, exactamente en el
> mismo sentido en que radix sort es lineal. No es una cota en el modelo
> de comparación, ni pretende serlo: el algoritmo hace aritmética sobre
> las claves, así que la cota Ω(n log n) no le aplica.

### 3.7 Memoria

```
  bufferA_ + bufferB_      2n · sizeof(T)          = 16n bytes (int64)
  bucketOfScratch_         n · sizeof(size_t)      =  8n bytes
  writeCursorScratch_      maxBuckets · 8          ≤ 8n/λ
  bucketStart/bucketSize   2 · Σ splits por camino ≤ 2·8·(n/λ)·(1+1/λ+…)
  árbol RefinedRange       #nodos · 48 bytes       ≈ 48n/λ
```

**Total ≈ 25n bytes** para `int64_t`, es decir **≈ 3,1 veces el tamaño de
la entrada**. Espacio auxiliar Θ(n).

---

# 4. Revisión de constantes

| Constante | Valor | Significado matemático |
|---|---|---|
| `DEFAULT_TARGET_ELEMENTS_PER_BIN` (λ) | 32 | Ocupación media objetivo. Fija `binCount = ⌈n/λ⌉` y `splits = ⌈count/λ⌉`. **Es el único parámetro con efecto de primer orden.** |
| `DEFAULT_LEAF_THRESHOLD` (t) | 64 | Umbral del caso base. Sólo importa su posición relativa a la cola de Poisson(λ). |
| `MAX_SUBDIVISION_DEPTH` (D) | 6 | Corte de la recursión. |
| `INSERTION_SORT_THRESHOLD` | 64 | Frontera Insertion/Quick en `sortLeaf`. |
| `QUICKSORT_THRESHOLD` | 384 | Frontera Quick/Intro. |
| `12` (literal, en `quickSort`/`introSortImpl`) | 12 | Corte a Insertion dentro de QuickSort. **No tiene nombre.** |
| `2.0` (literal, en `introSort`) | 2 | Factor del límite de profundidad de Introsort. **No tiene nombre.** |

### Qué ocurre si cambian

**λ.** Es el parámetro delicado.
- **Bajarlo** multiplica los bins por `64/λ`. La fase de dispersión
  escribe en `n/λ` flujos simultáneos, con huella `(n/λ)·64` bytes; cuando
  esa huella se acerca a la L2 el rendimiento cae bruscamente
  (medido: +12,9 % en λ=16, +43,1 % en λ=8, con n=10⁶ y L2 de 4 MiB).
  **La cota inferior útil de λ depende de `n` y del tamaño de caché**, no
  del algoritmo: la condición es `n·64/λ ≲ L2`. Para n=10⁷ el mínimo
  sería ~160. **λ=32 es óptimo para n≈10⁶ en esta máquina, no
  universalmente.** Esto no está expuesto en la API.
- **Subirlo** aumenta el coste de Insertion Sort, que es `(λ+1)/4`
  comparaciones por elemento, y **empeora la cota del peor caso**:
  `m_max = λ·2^(w/D)`. Con λ ≥ 1024, `m_max ≥ 1,7·10⁶`, comparable a `n`
  realista, y **reaparece un término Θ(n log n)**. El comentario lo
  documenta como restricción dura.

**t.** Inerte por encima de la cola de Poisson(λ). Con λ=32, `t=64`,
`t=96` y `t=128` producen contadores idénticos. Bajarlo hacia λ
reintroduce el problema que la separación resolvió: `P(X > t) → 0,5`.
**t no necesita ajuste, sólo una cota inferior segura.**

**D.** Bajarlo aumenta el residuo entregado a Introsort
(`m_max = λ·2^(w/D)`): con D=3, `m_max = 32·2^21 ≈ 6,7·10⁷`, y el peor
caso pasa a ser efectivamente `n log n`. **Subirlo** reduce `m_max` pero
añade niveles de refinamiento a coste `n` cada uno. D=6 con λ=32 da
`m_max ≈ 52.016`; es un compromiso razonable, pero **no hay ninguna
evidencia en el código de que 6 sea óptimo** — sólo de que funciona.

**`INSERTION_SORT_THRESHOLD` / `QUICKSORT_THRESHOLD`.** Fronteras del
despachador. Ver el defecto de §5.3.

**El literal `12`.** Aparece dos veces sin nombre ni justificación. Es el
punto en que QuickSort deja de partir y llama a Insertion. Cambiarlo mueve
el equilibrio entre coste de partición y coste cuadrático. **Nadie lo ha
medido.**

---

# 5. Intento de refutación

## 5.1 Recursión infinita — **NO**

Por el Lema 2 el span decrece estrictamente y está acotado inferiormente.
Además el tope `D` corta incondicionalmente. `quickSort` e `introSortImpl`
recursan siempre sobre un subrango estrictamente menor. **No hay ciclo
posible.**

## 5.2 Desbordamiento — **NO en el camino principal**

- `span = (uint64)max − (uint64)min`: la resta en `uint64` es modular y
  exacta para cualquier par de `int64`, **no desborda**.
- `W = span/s + 1`: con `s ≥ 2`, `span/s ≤ 2^63−1`, y `+1` es seguro.
  Con `s = 1` (sólo posible en `computeRangeParameters` cuando
  `binCount = 1`) y `span = 2^64−1`, `W` desbordaría a 0 — pero
  `countAndPlace` **aísla `numBuckets == 1`** y no calcula ningún índice
  en ese caso. El agujero está tapado.
- `(length + λ − 1)/λ` y `(count + λ − 1)/λ`: desbordarían con
  `length > SIZE_MAX − λ`. Inalcanzable en la práctica (requeriría
  2⁶⁴ elementos), pero **no hay comprobación ni comentario**.
- `computeBinIndex` devuelve `size_t`; el cociente cabe siempre por la
  propiedad `s·W > span`.

**Prueba adicional:** los 19 casos límite de `tests/edge_sanitizers.cpp`
incluyen `{INT64_MIN, INT64_MAX}`, arrays con span `2^64−1` y escalones
sobre todo el universo, todos bajo ASan+UBSan sin diagnóstico.

## 5.3 **DEFECTO REAL: el despachador local no sigue al umbral de ejecución**

`sortLeaf` decide con `INSERTION_SORT_THRESHOLD`, que es
`constexpr = DEFAULT_LEAF_THRESHOLD = 64`. Pero `leafThreshold_` es un
**parámetro de constructor**.

Construyendo `DynamicRangeSort<int64_t>(32, 128)`:
- `refine` produce hojas de hasta 128 elementos;
- `sortLeaf` manda las de 65–128 a **QuickSort**, no a Insertion Sort.

El usuario no lo ha pedido y no hay forma de saberlo leyendo la API.
QuickSort sobre 100 elementos hace menos comparaciones que Insertion pero
es más lento a esos tamaños. **El contador de comparaciones bajaría
mientras el tiempo sube** — el peor tipo de defecto, porque engaña a la
métrica.

La configuración por defecto no lo sufre (`t = 64` coincide con la
constante). **Es un defecto latente que sólo aparece al usar la API
completa.** Severidad: media. Es un fallo de diseño, no de corrección.

## 5.4 Entradas adversarias — acotadas, no eliminadas

El peor caso construible es el «pelado»: un núcleo denso más un valor
lejano colocado de modo que la anchura de intervalo deje todo el núcleo en
el bucket 0. Repetido, agota los `D` niveles. El residuo está acotado por
`m_max ≈ 52.016` (§3.4), luego el coste agregado sigue siendo Θ(n).
**No he encontrado ninguna entrada que rompa la linealidad con λ=32.**
Tampoco puedo demostrar que no exista: sólo que la construcción obvia está
acotada por el presupuesto de bits.

## 5.5 Estabilidad — **no aplica, pero no está dicho**

DRS **no es estable**: `detectRun` + `std::reverse` invierte elementos
iguales, y la colocación en `countAndPlace` sí preserva el orden relativo
dentro de un bucket. Como `T` está restringido a tipos **enteros** por el
`static_assert`, dos elementos iguales son indistinguibles y la
estabilidad es inobservable. **Correcto, pero el header no lo dice**, y un
lector que quisiera generalizar a pares clave/valor tropezaría aquí.

## 5.6 Seguridad frente a excepciones — **buena, por accidente**

`T` es entero, luego copiar no lanza. Las únicas excepciones posibles son
`bad_alloc` en `bufferA_`, `bufferB_`, los scratch, `roots` o los vectores
de `refine`. **Todas ocurren antes de que se escriba una sola posición de
`data`** (`data` sólo se modifica en `mergeRefined`, la última fase).
⟹ **garantía fuerte: si `sort()` lanza, `data` queda intacta.**
Es una propiedad valiosa y **no está documentada ni testada**.

## 5.7 Concurrencia — **no soportada, no documentada**

Los buffers y los scratch son miembros. Una instancia **no** puede usarse
desde dos hilos, y dos instancias sí. El header no dice ni una palabra.

## 5.8 Trabajo muerto: `2n` escrituras que nadie lee

`distribute` hace `bufferA_.assign(n, T{})` y `bufferB_.assign(n, T{})`.
`bufferA_` se sobrescribe íntegramente acto seguido; `bufferB_` sólo se
lee en posiciones que `refine` escribió antes. **Los 2n ceros no los lee
nadie.** Medido de forma estable: 0,16–0,22 ms para n=10⁶, es decir
**~10 % del total en entradas de baja cardinalidad** y ~1,6 % en las
grandes.

## 5.9 **ANOMALÍA NO EXPLICADA: cardinalidad baja y patrón de escritura**

Midiendo `v[i] = i % k` para distintos `k`, con **cero comparaciones y
cero subdivisiones en todos los casos** (o sea, trabajo algorítmico
idéntico):

| valores distintos | bins | tiempo (ms) |
|---|---|---|
| 1 | 1 | **1,27** |
| **2** | **2** | **3,27** |
| 3 | 3 | 2,26 |
| 5 | 5 | 2,16 |
| 17 | 17 | 1,66 |
| 65 | 65 | 1,57 |
| 1000 | 1000 | 1,64 |

**`k=2` es 2,6 veces más lento que `k=1` y 2,1 veces más lento que
`k=65`, haciendo exactamente el mismo trabajo.** La hipótesis más
plausible es conflicto de conjuntos de caché entre los flujos de escritura
de la dispersión (dos flujos separados por 4 MB), pero **no la he
verificado**. Es una variación de 2,6x dependiente de la entrada, real,
reproducible y no documentada. **Un revisor no puede firmar esto sin una
explicación.**

## 5.10 Hipótesis ocultas que he encontrado

1. **`w` es constante.** Toda la afirmación de linealidad lo asume. Para
   `T` de 128 bits la cota `m_max = λ·2^(w/D)` explota.
2. **`n < 2^64/λ`.** No comprobado; irrelevante en la práctica.
3. **`sizeof(size_t) == 8`.** `bucketOfScratch_` gasta 8 bytes por
   elemento; en una plataforma de 32 bits el consumo cambia y ningún
   comentario lo contempla.
4. **La L2 es de 4 MiB.** El valor por defecto de λ está ajustado a esta
   máquina y a n≈10⁶. **No hay nada en la API que lo advierta.**
5. **La recursión es de un solo hilo y en profundidad.** De ello depende
   la reutilización de los tres scratch. Está comentado, pero no impuesto
   por ningún mecanismo.

---

# 6. Legibilidad (ignorando el rendimiento)

**6.1 Comentarios obsoletos, en cabecera.** `Config.hpp` y
`DynamicRangeSort.hpp` se anuncian como **«DRS - v7»** y describen los
cambios de v7 (retirada del microhistograma, etc.). El código es tres
versiones posterior. **Lo primero que lee un tercero es falso.**

**6.2 Comentarios que contradicen el código.** El comentario de `refine`
dice «*A bin at or below targetElementsPerBin also becomes a leaf*»: es
`leafThreshold_`. El de `RefinedRange::sorted` dice «*las hojas que
llegaron a refine() con count > targetElementsPerBin_*»: igual.

**6.3 Miembro muerto.** `bucketSizeScratch_` está declarado y **no se usa
en ninguna parte**. Un lector pierde tiempo buscando dónde se consume.

**6.4 Idiomas mezclados.** Comentarios en inglés y en español en el mismo
archivo, a veces en la misma función. Para publicación es inaceptable.

**6.5 Tipos de índice inconsistentes.** `std::size_t` en el particionado,
`long` en los sorts locales, `uint64_t` en las anchuras, con `static_cast`
de ida y vuelta. `long` es de 32 bits en Windows: **los sorts locales
serían incorrectos para rangos > 2³¹ en esa plataforma.** Hoy inalcanzable
(hojas ≤ 52.016), pero es una bomba de relojería.

**6.6 Densidad de `#ifdef`.** `refine` tiene 6 bloques y `sort` unos 10.
La lógica real de `sort` son cinco llamadas enterradas en preprocesador.

**6.7 Comentarios más largos que el código.** `Config.hpp`: 100 líneas
para 6 constantes. Buena parte es historia del proyecto («v3 lo retuneó»,
«el paso 8 de v10»), no información necesaria para usar el archivo.
**La historia pertenece al repositorio, no al header.**

**6.8 `countAndPlace` con 10 parámetros.** Es la función crítica y la más
difícil de invocar correctamente.

**6.9 Duplicación peligrosa.** `debugPartitionOnly` reimplementa la
secuencia de `sort()`. Divergirán.

**6.10 Números mágicos sin nombre.** `12` (dos veces) y `2.0`.

**6.11 Contrato de `partition` no comprobado.** Precondición en un
comentario, sin `assert`, y la corrección de los bucles `do/while` depende
de un argumento de centinelas que no está escrito.

---

# 7. Deuda técnica

### Imprescindible (bloquea publicación)
1. Cabeceras que dicen «v7» y describen una versión que ya no existe (6.1).
2. Comentarios que contradicen el código (6.2).
3. `INSERTION_SORT_THRESHOLD` no sigue al umbral de ejecución (5.3).
4. `bucketSizeScratch_` muerto (6.3).
5. Idiomas mezclados (6.4).
6. Ausencia de documentación de: no ser estable, no ser seguro entre
   hilos, garantía fuerte ante excepciones, y que λ está ajustado a un
   tamaño de caché y a un `n` concretos (5.5, 5.7, 5.10).

### Recomendable
7. Explicar o eliminar la anomalía de cardinalidad baja (5.9).
8. Eliminar el relleno de ceros muerto (5.8), ~10 % en algunos datasets.
9. Unificar tipos de índice; erradicar `long` (6.5).
10. `assert` de la precondición de `partition` y escribir el argumento de
    los centinelas (6.11).
11. Eliminar el cursor `pos` redundante de `mergeRefined`.
12. Sacar la historia del proyecto de `Config.hpp` (6.7).
13. Nombrar los literales `12` y `2.0` (6.10).
14. Eliminar la rama inalcanzable `length == 0`.

### Opcional
15. Aplanar el árbol `RefinedRange` a una lista de hojas.
16. Reducir los parámetros de `countAndPlace` con un tipo «rango».
17. Fusionar `distribute` en `sort()`.
18. `bucketOfScratch_` a `uint32_t` (mitad de memoria auxiliar).
19. Sustituir `std::log2` por aritmética entera en `introSort`.
20. Reescribir `debugPartitionOnly` sobre las mismas primitivas que
    `sort()` para evitar divergencia.

---

# 8. Preparación para publicación

**Para leer el código:** falta un README del algoritmo (no del proyecto)
con el pseudocódigo de las cinco fases en una página. Hoy hay que leer 700
líneas de plantilla para entender cinco pasos. Y hay que unificar el
idioma.

**Para entenderlo:** falta un diagrama de la relación entre `λ`, `t`, `D`,
la ocupación Poisson y la cota `m_max`. Los tres parámetros interactúan y
esa interacción sólo existe hoy dispersa en comentarios.

**Para implementarlo en otro lenguaje:** falta una especificación
independiente del código: las cuatro fórmulas (`span`, `binCount`,
`W`, índice), las tres condiciones de hoja, y los invariantes. Debe poder
implementarse sin leer C++.

**Para verificar la demostración:** falta enunciar los lemas **en el
código o junto a él** (hoy están repartidos entre comentarios y
documentos), y falta declarar explícitamente que la linealidad es en el
sentido «`w` constante», no en el modelo de comparación. Sin eso, un
lector con formación teórica lo leerá como una afirmación falsa.

**Para reproducir los benchmarks:** falta (a) que `make baseline` y
`make profile` estén documentados en el README; (b) declarar que los
resultados son de un Apple M4 con libc++ y **no transfieren** — hay
precedente medido de inversión de signo entre máquinas; (c) el generador
adversario debe documentar que **se construye contra un `λ` dado** y que
compararlo entre configuraciones sin reconstruirlo produce conclusiones
falsas.

---

# 9. Veredicto

**¿El algoritmo está terminado?**
Sí como algoritmo, no como pieza de ingeniería. La estructura es coherente,
los invariantes se sostienen y hay dos aserciones que los verifican en
ejecución. Pero queda un defecto de diseño en la API (5.3), una anomalía
de rendimiento sin explicar (5.9) y trabajo muerto medible (5.8).

**¿Está preparada para hacerse pública?**
**No.** Los seis puntos «imprescindible» de §7 son bloqueantes, y cuatro
de ellos son de documentación, no de código: hoy el primer comentario que
lee un tercero dice que está leyendo v7.

**¿Queda alguna optimización importante (>5 %) sin explorar?**
Sí, dos, ambas medidas y ninguna aplicada:
- El relleno de ceros muerto: **~10 %** en entradas de baja cardinalidad,
  ~1,6 % en las grandes. Exige sustituir `std::vector` por almacenamiento
  sin inicializar.
- La anomalía de `k=2`: **2,6x** frente al mejor caso con trabajo
  idéntico. Si la causa es conflicto de caché, puede haber ahí una mejora
  general de la dispersión, no sólo de ese caso.
Y una tercera **no medida**: el literal `12` de QuickSort no lo ha
evaluado nadie.

**¿Qué haría antes de publicar?**
1. Arreglar los seis puntos imprescindibles.
2. Explicar o cerrar la anomalía de §5.9 — no se firma lo que no se
   entiende.
3. Escribir la especificación independiente del código (§8).
4. Añadir tests de: no estabilidad (documentada), garantía fuerte ante
   excepciones, y configuraciones no por defecto del constructor —
   **hoy la API completa no está testada**, que es justamente donde vive
   el defecto 5.3.

**¿Qué haría después de publicar?**
1. Reproducir la batería en al menos dos máquinas más, incluida una x86
   con libstdc++, y publicar las tres. El proyecto ya tiene evidencia de
   que las conclusiones se invierten entre plataformas.
2. Exponer λ como algo que depende del tamaño de caché, o calcularlo en
   tiempo de ejecución a partir de `n` y de la caché detectada.
3. Atacar el relleno de ceros y volver a medir.
4. Barrer `D` y el literal `12`, los dos únicos parámetros que nadie ha
   tocado nunca.
