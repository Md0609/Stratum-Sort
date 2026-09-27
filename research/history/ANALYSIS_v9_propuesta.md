# DRS v9 — Propuesta: análisis de complejidad y vías hacia O(n)

> **Estado: PROPUESTA. No se ha modificado ni una línea del algoritmo.**
> Este documento es únicamente análisis. Cada cambio propuesto está
> pendiente de aprobación explícita y, después, de medición cabeza a
> cabeza contra v8 antes de conservarse, siguiendo el mismo protocolo que
> v6/v7/v8.

## 0. Alcance y restricciones asumidas

Restricciones que **toda** propuesta de este documento respeta:

- La filosofía de cinco pasos (`Analizar → Construir intervalos →
  Refinar → Ordenar localmente → Unir`) no se toca.
- La regla de subdividir **solo por rango observado** del propio bin no
  se toca.
- No se sustituye DRS por Radix / Bucket / Counting / Flash / Spread
  Sort ni por ningún otro algoritmo conocido.
- No se cambia el origen ni la naturaleza del algoritmo: todo lo que
  sigue es (a) reducción de trabajo físico sobre exactamente el mismo
  algoritmo, (b) recalibración de parámetros que el algoritmo ya expone,
  o (c) corrección de la **metodología de medición**.

Y una restricción metodológica que este documento añade por su cuenta:
donde el proyecto ya tiene una métrica determinista que responde a una
pregunta, **la pregunta se responde con esa métrica y no con tiempo de
pared**. La sección 3 muestra que esto no se ha hecho en varios de los
análisis previos, y que algunas de sus conclusiones dependen de ello.

---

## 1. Modelo de coste exacto de v7/v8

Contabilidad por elemento, leída directamente del código
(`algoritmo/DynamicRangeSort.tpp`), no estimada:

| Fase | Pasadas sobre los datos | Coste |
|---|---|---|
| `analyze()` | 1 lectura de `n` | `n` lecturas |
| `distribute()` → `bufferA_.assign` + `bufferB_.assign` | — | **`2n` escrituras de ceros** (líneas 152–153) |
| `distribute()` → pasada de conteo | 1 lectura de `n` + 1 escritura a `bucketOfScratch_` | `n` lect. + `n` escr. de `size_t` (8 bytes) |
| `distribute()` → prefijos | — | `O(b)`, `b = ⌈n/64⌉` |
| `distribute()` → pasada de colocación | 1 lectura de `bucketOfScratch_` + 1 lectura de `src` + 1 escritura dispersa a `dst` | `2n` lect. + `n` escr. |
| `refine()` (por nivel, solo sobre los bins que entran) | escaneo min/max + conteo + colocación | `3` lect. + `1` escr. por elemento y nivel |
| `sortLeaf()` → `detectRun()` | 1 lectura de la hoja | `n` lecturas agregadas |
| `sortLeaf()` → Insertion Sort | — | `≈ k²/4` por hoja de tamaño `k` |
| `mergeRefined()` | 1 lect. + 1 escr. | `n` lect. + `n` escr. |

Sumando el camino común (`RandomUniform`, donde el 51,6 % de `n` entra
en `refine()` según `ANALYSIS_v8.md`), un elemento medio atraviesa
**≈ 8 lecturas y ≈ 5 escrituras** antes de estar en su sitio, de las
cuales **2 escrituras son puro relleno de ceros que nadie lee jamás**
(ver §4, P2).

### 1.1 El coste dominante es cuadrático en el tamaño de hoja

Insertion Sort sobre una hoja de tamaño `k` cuesta ≈ `k²/4` operaciones.
Con `n/λ` hojas de tamaño medio `λ`:

```
coste_localSort ≈ (n/λ) · λ²/4 = n·λ/4
```

Es lineal en `n` — pero con un factor `λ/4` que con `λ = 64` vale **16**.
Contraste con los contadores deterministas ya medidos en el proyecto
(`ANALYSIS_v5.md` §2.1, `RandomUniform`, n=1.000.000):

| target | Predicción `n·λ/4` | Comparaciones medidas |
|---|---|---|
| 64 | 16,0 M | **12,15 M** |
| 19 | 4,75 M | **4,00 M** |

El modelo `n·λ/4` reproduce los contadores reales con un error del
15–25 % (la diferencia la explica el corte temprano de `detectRun()`).
**Esto valida el modelo de coste, y valida que la constante de DRS está
gobernada por `λ`, no por la estructura del algoritmo.**

---

## 2. ¿Es DRS realmente O(n)?

### 2.1 El refinamiento hace progreso estricto (demostrable)

Sea un bin `B` con `count = m > target` y `observedMin ≠ observedMax`.
Entonces `splits = ⌈m/target⌉ ≥ 2` y
`newIntervalSize = ⌈observedRange/splits⌉`.

- El elemento igual a `observedMin` cae en el bucket `0`.
- El elemento igual a `observedMax` cae en el bucket
  `⌊(observedRange−1)/newIntervalSize⌋ ≥ 1`.

Luego **cada subdivisión produce siempre al menos dos buckets no
vacíos**, y por tanto `maxChildSize ≤ m − 1`: el refinamiento no puede
estancarse ni ciclar. Además, el rango observado de cada hijo es
**≤ observedRange / splits**, es decir, el rango se divide por un factor
`≥ splits ≥ 2` en cada nivel.

**Corolario (cota de profundidad).** Para un bin de tamaño `m` y rango
observado `R`, la profundidad necesaria para llegar a hojas es

```
d(m, R) ≤ log_{splits}(R) = log2(R) / log2(⌈m/target⌉)
```

Para enteros de `w` bits, `log2(R) ≤ w`. Con `m = 1.000.000` y
`target = 64`, `splits = 15.625` → el rango se divide por `2^13,9` por
nivel → `d ≤ 64/13,9 ≈ 5`. **Ese es el motivo real por el que
`MAX_SUBDIVISION_DEPTH = 6` funciona**, y hasta donde alcanza esta
lectura del proyecto nunca se había escrito: no es un número empírico
afortunado, es la cota que se deriva del colapso geométrico del rango
observado.

Con la profundidad acotada por una constante, el coste total del
refinamiento es `O(n · d) = O(n)`.

### 2.2 Dónde DRS deja de ser O(n)

El bloqueo no está en el refinamiento, está en las **hojas**:

1. **Hojas por tamaño (`count ≤ target`)** → Insertion Sort → `O(λ²)`
   por hoja, `O(n·λ/4)` total. Lineal, pero con constante grande.
2. **Hojas por profundidad agotada (`depth ≥ MAX_SUBDIVISION_DEPTH`)** →
   la hoja puede tener tamaño arbitrario → Introsort → `O(m log m)`.

El caso (2) es el único término genuinamente superlineal. Es alcanzable:
tómese un bin con `m−1` valores en `[0, m−2]` más un único valor
`10^18`. `newIntervalSize ≈ 10^18/splits`, así que todos los `m−1`
primeros caen en el bucket 0 y la reducción es ≈ 0. Repetido, agota los
6 niveles con `d·m` trabajo desperdiciado y entrega ≈ `m` elementos a
Introsort. Con `m = n`, **DRS es Θ(n log n) en el peor caso**.

Nótese que este peor caso **no aparece en ninguno de los ocho datasets
del proyecto** — por eso nunca se ha visto. `Concentrated` es la forma
suave del mismo fenómeno (99 % de `n` entra en `refine()`, `refine()` es
el 39,6 % del tiempo).

### 2.3 Qué significa "alcanzar O(n)" de forma alcanzable

Conclusión honesta, para fijar el objetivo antes de optimizar:

- **O(n) estricto para entrada entera arbitraria es inalcanzable sin
  romper la filosofía**, porque el paso 4 ("ordenar cada intervalo con un
  algoritmo de comparación convencional") es, por definición, comparativo
  — y la única forma de acotar su coste linealmente es acotar el tamaño
  de hoja por una constante, cosa que el cap de profundidad no garantiza
  para distribuciones adversarias.
- **O(n) para entradas cuyo rango observado colapsa geométricamente
  (todas las distribuciones "razonables", incluidas las ocho del
  proyecto) sí es alcanzable y de hecho ya se cumple** — es exactamente
  lo que mide `ANALYSIS_v5.md` §2.3 con `T(n)=a·n`, R² > 0,997.
- Por lo tanto **el objetivo operativo correcto no es "cambiar el
  exponente", es "minimizar la constante `a` del O(n) que ya se tiene"**,
  más una salvaguarda para que el caso adversario degrade a `O(n log n)`
  de forma controlada en vez de accidental.

Con el modelo de §1.1, la constante es:

```
T(n) / n  ≈  c_dist  +  c_leaf · λ/4  +  c_bin / λ  +  c_refine · P(λ, target)
                        ↑ crece con λ    ↑ decrece con λ
```

Y tiene un mínimo en `λ* = 2·√(c_bin / c_leaf)`. **Ese mínimo nunca se
ha buscado**, porque `λ` y `target` son hoy el mismo número (§4, P1).

---

## 3. Lo que las métricas dicen y los análisis no usaron

Ésta es la parte del encargo sobre "las métricas no se han tenido en
cuenta en algunos de los análisis". Hay cinco casos concretos.

### 3.1 El ajuste de complejidad usa **solo tiempo de pared**

`analisis/main.cpp:60`:

```cpp
points.push_back({static_cast<double>(n), report.timing.medianMs});
```

`report.metrics` está ahí mismo, con `comparisons()`, `totalBins()`,
`subdivisions()`, `workByDepth()` — **todos deterministas para una
entrada dada** (lo dice el propio comentario de `BenchmarkRunner.hpp`,
líneas 27–31) — y ninguno entra en la regresión. Se está estimando el
exponente de complejidad con la señal más ruidosa disponible, teniendo
al lado señales con **ruido cero**.

Consecuencia práctica: `ANALYSIS_v5.md` documenta en sus propias
limitaciones que el estudio de localidad "no se replicó" y que el de
target dinámico contradijo a v4. Ambos problemas son de ruido de
medición. El ajuste de complejidad tiene exactamente la misma
vulnerabilidad y **no se verificó de la misma manera**.

### 3.2 El ajuste se hace sobre el **build de investigación**

`build/drs_analysis` se compila con `RESEARCH_CXXFLAGS`
(`Makefile:64-66`), es decir, con `DRS_ENABLE_METRICS`. Y
`ANALYSIS_v7.md` §3 mide que ese build tiene un overhead de **+21 % en
n=1.000 y +2–7 % en n=1.000.000**.

Un overhead que **decrece con `n`** infla los puntos pequeños respecto a
los grandes, lo que **sesga el exponente ajustado hacia abajo**. Es
decir: el resultado "`T(n)=a·n` es el mejor ajuste para `RandomUniform`"
está sesgado precisamente en la dirección que lo hace parecer más lineal
de lo que es. El proyecto ya tiene el número exacto de ese sesgo
(`make overhead`) y no lo ha aplicado como corrección.

### 3.3 `RandomUniform` no escala el rango con `n`

`datasets/DatasetGenerator.hpp:25`:

```cpp
DataVector randomUniform(std::size_t n, int64_t minV = 0, int64_t maxV = 1'000'000)
```

El rango está **fijo en 10^6** mientras `n` recorre 100 → 5.000.000. La
densidad `n/rango` pasa de 10^-4 a 5. En n=5.000.000 cada valor distinto
aparece ~5 veces de media, así que el atajo
`observedMin == observedMax` de `refine()` se dispara cada vez más a
menudo **por construcción del dataset, no por mérito del algoritmo**.

Esto significa que la curva `T(n)` de `RandomUniform` mezcla dos efectos
—escalado con `n` y aumento de redundancia— y el segundo empuja hacia
abajo justo en los puntos grandes, que son los que dominan el ajuste.
**La conclusión "DRS es O(n) en el caso medio" está medida sobre un
dataset que no mantiene constante la única variable que debería
mantenerse constante.**

Lo mismo afecta a `SmallRangeManyEl` (rango 100 fijo), `ManyRepeated`
(5 valores) y `Concentrated` (rango fijo). Solo `HugeRangeFewEl` mantiene
densidad ≈ 0 en todo el barrido, y es también el único cuyo mejor ajuste
sale `n^1.05` en vez de `n` — coherente con esta explicación.

### 3.4 R² sobre modelos de un parámetro no discrimina exponentes

`analisis/Statistics.hpp` ajusta seis modelos `T(n)=a·f(n)` por el origen
y elige por R². Con puntos que abarcan cinco órdenes de magnitud, `SST`
está dominado por el punto mayor: cualquier modelo que acierte
`n=5.000.000` obtiene R² > 0,99. Por eso `n`, `n·log n`, `n^1.05`,
`n^1.10` y `n^1.20` salen todos con R² entre 0,997 y 0,9999 y el
"ganador" cambia de v4 a v5 según el número de puntos.

Lo correcto para responder *"¿es O(n)?"* es **estimar el exponente, no
elegir entre exponentes prefijados**: regresión sobre
`log T = log a + b·log n`, reportando `b` con su intervalo de confianza.
Si el IC de `b` contiene 1,0, la afirmación "es lineal" tiene respaldo
estadístico; si no, no lo tiene. Además, en escala log los residuos son
relativos, que es como se comporta realmente el ruido de temporización
(multiplicativo, no aditivo). Ese modelo **no está implementado** en
`Statistics.hpp`.

### 3.5 `approxMemoryBytes` subestima el consumo real en ~50 %

`addApproxMemory()` se llama en `distribute()` (los dos buffers) y en
`refine()` (los nodos del árbol). **No se llama en `countAndPlace()`**,
que es donde vive `bucketOfScratch_`: `n · sizeof(size_t)` = **8 MB
adicionales para n=1.000.000**.

`ANALYSIS.md` (v4) afirma "16.706.368 bytes … casi exactamente
2·1.000.000·8". El consumo real de v7/v8 es ≈ **24,7 MB**, un 48 % más.
La métrica de memoria era correcta en v4 y dejó de serlo en v7, cuando se
introdujo el scratch, sin que se actualizara la instrumentación. Cualquier
comparación de memoria entre versiones que use esta métrica está mal.

### 3.6 Métricas que existen y ningún análisis consume

- `workByDepth()` — cuántos elementos se reprocesan en cada nivel. Es la
  medida directa del término `n·d` de §2.1 y solo aparece citada de
  pasada en `ANALYSIS_v6.md` (Investigación 7).
- `algorithmUsage()` — cuántas hojas van a `AlreadySorted` /
  `ReversedRun` / `InsertionSort` / `QuickSort` / `Introsort`. Con la
  hipótesis de v8 ("`localSort()` es el 34 % del tiempo"), este contador
  dice exactamente **a qué despachador** hay que atacar, y no se ha
  tabulado nunca.
- `binSizes()` — está la distribución completa; el histograma de v5 la
  resume, pero nadie ha comprobado si `Σ k²/4` sobre esa distribución
  predice el tiempo de `localSort()`. Si lo predice (§1.1 sugiere que
  sí), se tiene un **modelo de coste sin ejecutar nada**, con el que
  barrer parámetros analíticamente en vez de por fuerza bruta cronometrada.

---

## 4. Propuestas, ordenadas por impacto esperado

Cada una indica: qué cambia, por qué debería funcionar, qué predice el
modelo, y con qué métrica se valida (no solo con el reloj).

### P1 — Desacoplar la ocupación objetivo del umbral de hoja ★ prioridad máxima

**Qué.** Hoy `targetElementsPerBin_` hace dos trabajos distintos con el
mismo número:

```cpp
initialBins = ceil(length / targetElementsPerBin_);   // ocupación media λ
if (count <= targetElementsPerBin_) → hoja;           // umbral de hoja
splits      = ceil(count / targetElementsPerBin_);    // ocupación en refine
```

Propuesta: dos parámetros, `binOccupancy` (λ) y `leafThreshold` (t), con
λ ≤ t. **No cambia ninguna fórmula ni ninguna fase** — solo deja de
obligar a que dos constantes distintas sean iguales.

**Por qué debería funcionar.** Con λ = t = 64 y datos uniformes, la
ocupación de bin es Poisson(64) y `P(X > 64) ≈ 0,5`: **la mitad de los
elementos entra en `refine()` por construcción**. Esto no es una
propiedad del dataset, es una consecuencia aritmética de haber igualado
λ y t. Y coincide con el 51,6 % medido en `ANALYSIS_v8.md`. Con λ = 32 y
t = 64, `P(Poisson(32) > 64) ≈ 10^-6`: **`refine()` deja de ejecutarse
casi por completo** en datos sin redundancia.

**Predicción cuantitativa** (n=1.000.000, `RandomUniform`, usando el
reparto por fase de `ANALYSIS_v8.md` y el modelo `n·λ/4` de §1.1):

| | v8 (λ=t=64) | Propuesta (λ=32, t=64) |
|---|---|---|
| % de `n` que entra en `refine()` | 51,6 % | ≈ 0 % |
| `refine()` (13,7 % del tiempo) | 13,7 % | ≈ 0,5 % |
| comparaciones de `localSort()` | 12,15 M | ≈ 6,5 M |
| `localSort()` (34,1 % del tiempo) | 34,1 % | ≈ 19 % |
| bins | 15.625 → 22.983 | ≈ 31.250 |
| **tiempo total previsto** | 100 % | **≈ 72 %** |

Ese ≈ 28 % es del mismo orden que el **18–23 % medido y verificado con
27 repeticiones** en `ANALYSIS_v5.md` §2.1 para `log2(n)` — que este
modelo ahora **explica**: `log2(n)=19` ganaba no por ser logarítmico,
sino porque bajaba λ. Es también la explicación de por qué v4 concluyó
lo contrario: v4 midió con el esquema vector-por-bucket, donde `c_bin`
era enorme y `λ*` mucho mayor.

**Experimento.** Barrido **bidimensional** λ × t sobre
{8,16,24,32,48,64} × {16,32,64,128}, los ocho datasets, midiendo
`comparisons()`, `totalBins()`, `workByDepth()` **además** del tiempo.
Con el modelo `c_leaf·λ/4 + c_bin/λ` ajustado a los contadores se puede
localizar `λ*` analíticamente y verificar solo ese punto con el reloj.
Ningún barrido anterior fue bidimensional: v4 y v5 movían un único
número que arrastraba los dos efectos a la vez, con signos opuestos.

**Riesgo.** Ninguno estructural. λ menor ⇒ más bins ⇒ más memoria de
bookkeeping (`bucketStart`/`bucketSize` de tamaño `b`) y peor localidad
en la pasada de colocación. `SmallRangeManyEl` y `ManyRepeated` están
limitados por rango, no por λ, y no deberían moverse.

---

### P2 — Eliminar el relleno de ceros de `bufferA_` / `bufferB_`

**Qué.** `DynamicRangeSort.tpp:152-153`:

```cpp
bufferA_.assign(data.size(), T{});
bufferB_.assign(data.size(), T{});
```

`bufferA_` se sobrescribe íntegramente por `countAndPlace()` acto
seguido. `bufferB_` solo se lee en posiciones que `refine()` ha escrito
antes (`refine` lee `cur[start..start+count)`, siempre escrito;
`mergeRefined` y `sortLeaf` solo tocan rangos de hoja, siempre
escritos). **Los `2n` ceros no los lee nadie jamás.**

**Impacto.** `2n` escrituras = 16 MB de tráfico de escritura para
n=1.000.000 con `int64_t`, dentro de `distribute()`, que es el **46,8 %
del tiempo total** en `RandomUniform`. Es la reducción de trabajo físico
más barata que queda en el algoritmo.

**Cómo.** `std::vector::assign`/`resize` siempre inicializan. Hay que
sustituir el almacenamiento por memoria con *default-init* (por ejemplo
`std::unique_ptr<T[]>` con `new T[n]`, que para `T` trivial no
inicializa, más un `size_`), manteniendo idéntica la interfaz interna.
Es un cambio de contenedor, no de algoritmo.

**Validación.** `approxMemoryBytes()` no debe cambiar; el tiempo de la
fase `distribute` (ya instrumentada) sí. Comparación alternada, ≥ 6
rondas, como en v7 §4.

---

### P3 — Eliminar las dos asignaciones de heap por llamada a `refine()`

**Qué.** `DynamicRangeSort.tpp:231-232`:

```cpp
std::vector<std::size_t> bucketStart;
std::vector<std::size_t> bucketSize;
```

Locales ⇒ dos `malloc` + dos `free` **por cada subdivisión**. Para
`RandomUniform` n=1.000.000 son 7.276 subdivisiones = **14.552
asignaciones por `sort()`**.

v7 ya resolvió esto para `bucketOfScratch_`/`writeCursorScratch_` y ganó
un 7–10 %; el comentario del header (líneas 111–114) explica por qué
`bucketStart`/`bucketSize` **no** podían ser el mismo tipo de scratch: su
contenido debe sobrevivir a la recursión en los hijos.

**Pero sí pueden ser un *pool por profundidad*.** La recursión es
estrictamente en profundidad y `depth ≤ MAX_SUBDIVISION_DEPTH = 6`, así
que **como máximo hay un marco de `refine()` vivo por nivel**. Un
`std::array<std::vector<std::size_t>, MAX_SUBDIVISION_DEPTH+1>` indexado
por `depth` da exactamente la misma garantía de validez con cero
asignaciones tras las primeras llamadas. Es la extensión natural del
razonamiento de v7, no una técnica nueva.

**Impacto esperado.** Del mismo orden que la ganancia de v7 (7–10 %)
escalado por la fracción de asignaciones que quedan; conservador: 2–4 %.

---

### P4 — Aplanar el árbol `RefinedRange` a una lista de hojas

**Qué.** `refine()` construye y devuelve **por valor** un árbol de nodos,
cada nodo con su propio `std::vector<RefinedRange> children`
(`reserve(splits)` = una asignación más por nodo interno). Después
`sortRefined()` y `mergeRefined()` lo recorren recursivamente. Para
`RandomUniform` n=1.000.000: ≈ 23.000 hojas + 7.276 nodos internos.

El árbol **no se usa para nada más**. Lo único que consumen las fases
posteriores es *"la lista de hojas en orden ascendente"*. Sustituirlo por
un `std::vector<LeafView>` al que `refine()` va anexando en su recorrido
en profundidad da el mismo resultado, elimina ~7.000 asignaciones más y
convierte dos recorridos recursivos de árbol en dos bucles secuenciales
sobre un vector contiguo.

**Nota:** `debugPartitionOnly()` ya hace exactamente esto
(`flattenLeaves`), así que la estructura de destino ya existe y ya está
validada.

**Impacto.** Asignaciones: −7.276 por `sort()`. Localidad: los recorridos
de `sortRefined`/`mergeRefined` pasan de saltar por nodos dispersos en el
heap a recorrer un vector contiguo. Combinable con P3 y P8.

---

### P5 — Estrechar (o eliminar) `bucketOfScratch_`

**Qué.** `bucketOfScratch_` es `std::vector<std::size_t>` = 8 bytes por
elemento. Para `int64_t` eso significa que **la pasada de conteo escribe
tantos bytes como ocupan los propios datos**, y la de colocación los
vuelve a leer: 16 MB de tráfico extra por `sort()` en n=1.000.000, dentro
de la fase más cara.

Dos variantes, ambas a medir:

- **(a) Estrechar a `uint32_t`.** `numBuckets` cabe en 32 bits para
  cualquier `n < 2^32`. Mitad de tráfico, cambio trivial. Se puede
  estrechar aún más (`uint16_t`) en las llamadas de `refine()`, donde
  `splits` es pequeño.
- **(b) No almacenar: recalcular.** La pasada de colocación recalcula
  `computeBinIndex()` en vez de leer el índice guardado. Cambia 8n bytes
  de escritura + 8n de lectura por `n` divisiones. Cuál gana depende de
  la máquina, y **combinado con P6 (división por multiplicación) la
  variante (b) pasa a ser claramente favorable**.

**Validación.** Además del tiempo, `approxMemoryBytes()` — **arreglando
antes la omisión de §3.5**, para que la métrica de memoria por fin mida
lo que dice medir.

---

### P6 — Sustituir la división de `computeBinIndex()` por multiplicación

**Qué.** `computeBinIndex()` hace `offset / intervalSize` **una vez por
elemento y por nivel**. `intervalSize` es constante durante toda una
llamada a `countAndPlace()`, así que puede precomputarse una vez el par
(magic, shift) y sustituir la división por `mulhi + shift`, técnica
estándar de división por invariante. **El cociente resultante es
idéntico bit a bit**: la fórmula
`binIndex = ⌊(value − rangeStart)/intervalSize⌋` no cambia, solo cómo se
calcula.

**Impacto.** La división entera de 64 bits es la operación aritmética más
cara del camino caliente (decenas de ciclos, unidad no completamente
segmentada). Ejecutada ≥ `n` veces por `sort()`, con `distribute()` al
46,8 % del tiempo, es un candidato de primer orden.

**Aviso de honestidad.** El beneficio depende mucho de la CPU: en el
Xeon x86 donde se tomaron todas las medidas históricas debería ser
notable; en el arm64 de esta máquina (que tiene divisor entero
comparativamente rápido) puede ser menor. Debe medirse aquí, no
asumirse. Ver §6.

**Variante alternativa (más agresiva, decisión del autor).** Elegir
`intervalSize` como la mayor potencia de dos ≤ `⌈range/initialBins⌉`
convierte la división en un desplazamiento. Los bins siguen siendo
intervalos de anchura uniforme sobre el rango observado —la regla de DRS
se mantiene— pero cambia el redondeo de la anchura y puede hasta duplicar
el número de bins. Es más invasivo que P6 y lo dejo señalado, no
recomendado, hasta que P6 se haya medido.

---

### P7 — Propagar "bin ya ordenado" de `refine()` a `sortLeaf()`

**Qué.** Cuando `refine()` detecta `observedMin == observedMax`
(`.tpp:207`) **ya sabe** que el bin está ordenado: todos sus elementos
son idénticos. Pero devuelve un nodo sin marca, y después `sortLeaf()`
llama a `detectRun()`, que **vuelve a recorrer el bin completo** para
redescubrirlo.

Basta un `bool sorted` en el nodo (o en el `LeafView` de P4) puesto por
`refine()`.

**Impacto.** En `ManyRepeated` (5 bins de ~200.000 elementos) esto
elimina una pasada completa sobre `n`. Es el dataset donde `std::sort` le
saca un 1,29x a DRS (`ANALYSIS_v7.md` §9). En `SmallRangeManyEl`
(100 valores distintos) el efecto es del mismo tipo. En `RandomUniform`,
nulo — no perjudica.

**Validación.** `algorithmUsage()["AlreadySorted"]` debe mantenerse
constante y las comparaciones registradas por `detectRun()` deben caer;
es decir, la métrica **ya existente** demuestra el ahorro sin cronómetro.

---

### P8 — `merge` por `memcpy` coalescido, usando un invariante no documentado

**Qué.** `mergeRefined()` lleva un cursor `pos` y copia elemento a
elemento. Pero se cumple, siempre, que **`pos == node.start`**:

- `distribute()` coloca los datos en `bufferA_[0, n)`.
- `refine(start, count)` escribe sus hijos en el **otro** buffer en las
  **mismas posiciones absolutas** `[start, start+count)`.
- Los hijos teselan el rango del padre en orden ascendente, y se visitan
  en ese orden.

Por inducción, las hojas teselan exactamente `[0, n)` en orden. Cada hoja
**ya está en su posición final**; lo único que hace falta es saber en qué
buffer vive.

Por tanto `merge` se reduce a: recorrer las hojas y, para cada **racha
maximal de hojas consecutivas que viven en el mismo buffer**, un único
`std::memcpy` de todo el bloque. En `RandomUniform`, cerca de la mitad de
los bins nunca se refinan y quedan en `bufferA_`, así que las rachas son
largas.

**Impacto.** `merge()` es el 4,1 % del tiempo (`RandomUniform`) y 6,3 %
(`Concentrated`); sustituir copia elemento-a-elemento con recursión de
árbol por `memcpy` vectorizado debería llevárselo casi entero. Trivial
sobre P4.

**Beneficio adicional:** el invariante `pos == start` es una **propiedad
de corrección verificable** (`assert(pos == node.start)`) que hoy no está
escrita en ninguna parte.

---

### P9 — Revisar el despachador de `localSort()` con `algorithmUsage()`

`ANALYSIS_v8.md` deja `localSort()` (34,1 %) como una de las dos
hipótesis abiertas para v9, y `algorithmUsage()` —el contador que dice a
qué algoritmo va cada hoja— **nunca se ha tabulado**. Antes de tocar
nada:

1. Tabular `algorithmUsage()` por dataset y tamaño. Con `target=64`,
   `INSERTION_SORT_THRESHOLD = 64` y hojas de tamaño ≤ 64, la expectativa
   es que **el 100 % de las hojas vaya a Insertion Sort** y que
   `QuickSort`/`Introsort` sean código muerto en la práctica salvo en las
   hojas por profundidad agotada. Si es así, hay dos ramas por hoja que
   nunca se toman.
2. Contrastar `Σ k²/4` sobre `binSizes()` con el tiempo medido de la
   fase `localSort`. Si correlacionan (§1.1 sugiere que sí), se obtiene
   un **predictor del coste de `localSort` sin ejecutar el sort**, con el
   que barrer P1 analíticamente.
3. Solo entonces, y con `λ` ya elegido por P1, evaluar si para `λ`
   pequeño (≤ 16–32) conviene una red de ordenación sin ramas en lugar de
   Insertion Sort. Esto es sustituir un algoritmo de comparación
   convencional por otro dentro del paso 4 — permitido por la filosofía,
   que solo dice "un algoritmo de comparación convencional" — pero es el
   cambio más discutible de la lista y va deliberadamente el último.

---

### P10 — Limpieza: miembro muerto

`DynamicRangeSort.hpp:158` declara `std::vector<std::size_t>
bucketSizeScratch_`, que **no se usa en ninguna parte** (verificado por
búsqueda en todo el proyecto: una única aparición, la declaración).
Residuo de v7. Coste: 24 bytes por instancia y confusión al leer.

---

## 5. Qué NO propongo, y por qué

- **Tocar la regla de rango observado.** `ANALYSIS_v6.md`
  (Investigación 5) ya demuestra que fijar `intervalSize` entre niveles
  ahorraría movimiento pero cambia la naturaleza del algoritmo. De
  acuerdo; no se propone.
- **Reintentar bins conscientes de densidad, microhistograma o salto por
  Difficulty Score.** Medidos y refutados en v4/v6, con causa
  identificada en cada caso. No hay información nueva que los reabra.
- **Reintentar el min/max como subproducto de la pasada del padre
  (v8).** Cinco variantes medidas. Además, con P1 aplicado `refine()`
  pasa a ser ≈ 0 % del tiempo en los datasets comunes, lo que **elimina
  del todo el motivo** para volver a intentarlo.
- **Paralelizar.** Fuera del alcance: cambiaría el modelo de ejecución,
  no la complejidad, y haría incomparables todas las medidas históricas.
- **`-march=native` / `-flto`.** Medidos en v7 §8 sin ganancia clara.
  P6 obtiene el mismo tipo de beneficio de forma portable y explícita.

---

## 6. Plan de medición (sin esto, ninguna conclusión es válida)

Antes de aceptar cualquier propuesta:

1. **Arreglar la metodología primero (§3), medir después.** En concreto:
   - `addApproxMemory()` en `countAndPlace()` (§3.5).
   - Regresión log-log con IC sobre el exponente en `Statistics.hpp`
     (§3.4).
   - Un `randomUniformScaled(n)` cuyo rango crezca con `n` (p. ej.
     `[0, 16n]`), para medir el exponente con densidad constante (§3.3).
     **Se añade, no se sustituye:** los ocho datasets actuales siguen
     siendo la base de comparación con todas las versiones anteriores.
   - Ajustar los modelos también sobre `comparisons()` y
     `workByDepth()`, que son deterministas (§3.1). El exponente sobre
     contadores es la medida limpia de la complejidad; el exponente sobre
     tiempo es la medida de la implementación. **Deberían separarse.**
2. **Re-establecer la línea base en esta máquina.** Todos los números
   históricos vienen de un Xeon x86_64 con GCC 13.3 sobre Linux; esta
   máquina es arm64 con Apple clang. Ninguna comparación cruzada entre
   este documento y los anteriores es válida hasta re-medir v8 aquí.
   (`benchmarks/SystemInfo.hpp` además solo lee el modelo de CPU en
   Linux, así que en macOS ese campo saldrá vacío.)
3. **Protocolo por propuesta**: ≥ 6 rondas alternadas
   (v8-propuesta-v8-propuesta…) en el mismo binario, mediana, sobre los
   ocho datasets; corrección verificada contra `std::sort` en cada
   ejecución.
4. **Criterio de aceptación** (el mismo que el encargo ha usado desde
   v6): se conserva únicamente si mejora `RandomUniform` **y no empeora
   ningún otro dataset**. Si empeora alguno, se elimina por completo del
   código, no se desactiva.
5. **Orden de aplicación sugerido**, de menor a mayor riesgo, midiendo
   entre cada paso: P10 → P2 → P3 → P4 → P8 → P7 → P5 → P6 → **P1** → P9.
   P1 va casi al final a propósito: es el de mayor impacto pero el que
   cambia el reparto de tiempo entre fases, así que conviene tener las
   reducciones de constante ya dentro para que su barrido bidimensional
   encuentre el `λ*` de la implementación definitiva y no el de la actual.

---

## 7. Resumen ejecutivo

| # | Propuesta | Impacto previsto | Riesgo | Cómo se valida sin reloj |
|---|---|---|---|---|
| P1 | Desacoplar λ del umbral de hoja | **≈ −28 %** | bajo | `comparisons()`, `workByDepth()`, `totalBins()` |
| P2 | Quitar el relleno de ceros `2n` | −3…−6 % | muy bajo | fase `distribute` + `approxMemoryBytes()` |
| P3 | Pool por profundidad en `refine()` | −2…−4 % | muy bajo | nº de asignaciones (instrumentable) |
| P4 | Aplanar el árbol a lista de hojas | −1…−3 % | bajo | nº de asignaciones |
| P5 | Estrechar/eliminar `bucketOfScratch_` | −2…−5 % | bajo | `approxMemoryBytes()` corregido |
| P6 | División → multiplicación mágica | −3…−10 % (según CPU) | bajo | índices idénticos bit a bit |
| P7 | Propagar "ya ordenado" | −0…−15 % (según dataset) | muy bajo | comparaciones de `detectRun()` |
| P8 | `merge` por `memcpy` coalescido | −2…−4 % | bajo | fase `merge` |
| P9 | Revisar despachador de `localSort` | por determinar | medio | `algorithmUsage()`, `binSizes()` |
| P10 | Borrar `bucketSizeScratch_` | 0 | ninguno | — |

**Las tres frases que resumen el análisis:**

1. **DRS ya es O(n)** para entradas cuyo rango observado colapsa
   geométricamente (§2.1 lo demuestra, y explica por qué
   `MAX_SUBDIVISION_DEPTH=6` basta); es Θ(n log n) solo en el caso
   adversario descrito en §2.2. El objetivo útil no es cambiar el
   exponente sino **minimizar la constante**, que hoy está dominada por
   `λ/4 = 16` operaciones de Insertion Sort por elemento.
2. **La constante está atrapada por un acoplamiento accidental**: `λ` y
   el umbral de hoja son el mismo número, lo que fuerza a que ≈ 50 % de
   los elementos pasen por `refine()` **por aritmética, no por los
   datos**. Separarlos es la propuesta de mayor impacto y no toca la
   filosofía en absoluto.
3. **Varias conclusiones previas están medidas con la señal equivocada**:
   el exponente de complejidad se ajusta con tiempo de pared del build
   instrumentado, sobre un dataset cuyo rango no escala con `n`, eligiendo
   entre exponentes prefijados por R². Los contadores deterministas que
   responderían limpiamente a esas preguntas ya existen y no se usan
   (§3). **Antes de optimizar, hay que arreglar cómo se mide.**
