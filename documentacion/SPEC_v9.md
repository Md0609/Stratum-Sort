# DRS v9 — Especificación técnica (reescritura tras O8)

**Estado: alineada con la evidencia experimental a fecha del paso 3
revertido.** Esta versión sustituye por completo a la anterior, que
prometía cosas que la medición ha descartado. Todo enunciado de este
documento es o bien **[MEDIDO]** en esta máquina, o bien **[ABIERTO]** y
pendiente de medir. No queda ningún enunciado derivado sólo de análisis
asintótico.

> **Qué ha cambiado respecto de la versión anterior de este documento.**
> Se eliminan **CC1** (refinamiento terminal), **CC7** (borrar los seis
> algoritmos de ordenación) y **CC8** (unificar los dos caminos de
> código). Los tres han perdido su justificación: los dos primeros por la
> medición de O8, el tercero por no tener criterio medible. Con ellos
> desaparece el objetivo declarado de v9 —«eliminar el término
> superlineal»—, que la evidencia dice que no existe a las constantes en
> uso. Ver §2 y `O8_resolucion_y_reversion_paso3.md`.

---

## 1. Qué es v9 ahora, honestamente

v9 **ya no es** un cambio de generación del algoritmo. Es:

1. Una corrección de aritmética con efecto grande y medido en una clase
   de entradas (§3, CC-A). **Hecho.**
2. La eliminación de trabajo demostrablemente muerto, sin efecto medible
   en tiempo (§3, CC-B). **Hecho.**
3. Un conjunto de cambios de coste constante y de memoria cuyo valor
   está **sin demostrar** y que este documento ordena de forma que se
   demuestre o se descarte (§5).

La filosofía de cinco pasos, la regla del rango observado, el tope de
profundidad y el despachador de ordenación local **siguen intactos**.

### 1.1 Respuestas a las ocho preguntas del encargo, corregidas

| Pregunta | Respuesta anterior | Respuesta actual |
|---|---|---|
| ¿Debe desaparecer `MAX_SUBDIVISION_DEPTH`? | Sí | **No.** [MEDIDO] Quitarlo cuesta hasta +109 % y no gana en ningún punto del barrido de peor caso. Además ahora tiene justificación cuantitativa (§2.2). |
| ¿Qué ocurre cuando una hoja sigue siendo grande? | Deja de existir esa categoría | **Sigue existiendo, y es inofensiva.** [MEDIDO] El presupuesto de bits acota el residuo a ~104.000 elementos con `target=64`, independientemente de `n`. |
| ¿Debe existir todavía `localSort()`? | Sí, colapsado a Insertion Sort | **Sí, con su despachador completo.** Con el tope puesto, QuickSort e Introsort **no** son inalcanzables: se ejecutan 9.346 veces en el adversario de núcleo 64. |
| ¿Puede el refinamiento completarlo todo por sí solo? | Sí pero no debe | **Sí, y demostrablemente no conviene.** [MEDIDO] |
| ¿Cómo queda el flujo? | Un solo recorrido en profundidad | **Igual que en v8**, salvo la aritmética de span y el tope de abanico. |
| ¿Qué estructuras nuevas hacen falta? | Arena; un buffer menos | **Ambas siguen vivas** [MEDIDO]: 3 asignaciones por subdivisión (22.000–24.500 por `sort()`) y `merge` pesa 31–37 % en dos datasets. |
| ¿Qué se puede eliminar? | ~40 % del algoritmo | **Nada más, por ahora.** Lo eliminado hasta hoy: el recorte de índice y los buckets inalcanzables. |
| ¿Qué hay que reescribir de cero? | 60 % del `.tpp` | **Nada.** Los cambios restantes son locales. |

---

## 2. Lo que la medición ha establecido

### 2.1 Línea base [MEDIDO]

Apple M4 / arm64 / Apple clang 21 / libc++. `n = 10⁶`, `target = 64`.
Contadores deterministas idénticos entre ejecuciones; dispersión temporal
1,5–5,9 %. **Umbral de significación: 6 %.** `ManyRepeated` es atípico
(18 % de dispersión) y no admite decisiones por reloj.

Detalle en `BASELINE_v8.md`. Puntos que condicionan el resto:

- `SortedAscending` es **6,02x** más lento que `std::sort` — el peor de
  la batería, y no lo era en la máquina histórica (1,11x). No es una
  regresión de DRS: la `std::sort` de libc++ resuelve un millón de
  enteros ya ordenados en 0,73 ms.
- Los ratios históricos **no transfieren** en datos de baja entropía.

### 2.2 El término superlineal no existe a las constantes en uso [MEDIDO]

Para que un bin de tamaño `m` sobreviva `D` niveles degenerados hace
falta `D · log₂(m/target) ≤ 64` bits de span. Con `D = 6`:

| `target` | residuo máximo entregable a Introsort |
|---|---|
| 16 | 26.008 |
| 32 | 52.016 |
| **64 (actual)** | **104.032** |
| 256 | 416.128 |
| **1024** | **1.664.511 ← reabre el problema** |

**Con `target = 64` el residuo está acotado por una constante
independiente de `n`, luego la caída a Introsort cuesta `O(17n) = O(n)`.**
El barrido del tamaño de núcleo del adversario (7 puntos, 3 rondas
alternadas) lo confirma: con núcleo ≥ 1.024 la versión capada entrega **un
solo bin**, y con núcleo ≥ 16.384, **ninguno**.

> **Límite de validez que hay que respetar:** `target ≥ 1024` reabre el
> término superlineal. Cualquier cambio del target debe comprobarlo. Es
> la primera justificación no empírica de `MAX_SUBDIVISION_DEPTH = 6` en
> todo el proyecto.

### 2.3 Cambios ya aplicados y aceptados

**CC-A — Aritmética de span** (paso 1, `STEP1_span.md`). [MEDIDO]
El rango se representa como `span = max − min`, nunca como `span + 1`.
`W = span/s + 1`; el recorte de índice se eliminó por ser demostrablemente
innecesario (Propiedad 2). `FullRangeExtremes`: 20,16 → 14,19 ms
(**−29,6 %**), profundidad 3 → 1. Contadores idénticos en los otros nueve
datasets.

**CC-B — Tope de abanico por rango observado** (paso 2,
`STEP2_fanout_cap.md`). [MEDIDO]
`splits ≤ observedSpan + 1` en `refine()`, la misma regla que el nivel
superior ya aplicaba. Elimina el 100 % de los buckets **inalcanzables**
(15.368 en `Concentrated`). **Sin efecto medible en tiempo.** Se conserva
por ser trabajo demostrablemente muerto y de riesgo nulo (el tope no puede
cambiar la partición: `W = 1` antes y después), no por rendimiento.

### 2.4 Cambio revertido

**CC1 + CC7 — Refinamiento terminal** (paso 3, revertido,
`O8_resolucion_y_reversion_paso3.md`). El enunciado es cierto —sin tope,
toda hoja es `≤ t` o monovaluada, verificado con aserciones activas— pero
el problema que resolvía no se materializa y su coste sí: hasta **+109 %**.
No se reintenta.

---

## 3. Invariantes vigentes

Los que se han demostrado y siguen en pie tras O8:

- **I-RANGO.** `0 ≤ i(v) ≤ s−1` sin necesidad de recorte (Propiedad 2 de
  `STEP1_span.md`). Verificado bajo ASan sobre 19 casos límite.
- **I-TESELADO.** Las hojas teselan `[0, n)` en orden ascendente y cada
  hoja está ya en su posición final. **[MEDIDO]** Verificado por aserción
  activa (`pos == node.start` por hoja, `pos == n` al final) en los diez
  datasets a n = 10⁵ y 10⁶; ninguna se disparó. Precondición del paso 7.
- **I-PROGRESO.** Toda subdivisión produce ≥ 2 buckets no vacíos, luego
  `max hijo ≤ m − 1`.
- **I-BITS.** Cada nivel de refinamiento consume ≥ 1 bit del span, luego
  ningún camino raíz-hoja supera los 64 niveles. Es lo que acota el
  residuo de §2.2.

---

## 4. Paso 4 — Medición [HECHO]

Detalle en `STEP4_profile.md`. Los cuatro entregables existen y son
reproducibles (`make profile`). El algoritmo no cambió de comportamiento:
contadores deterministas idénticos a los del paso 2.

### 4.1 Reparto de tiempo por fase [MEDIDO]

`n = 10⁶`, `target = 64`, mediana de 7, rango sobre 3 ejecuciones. Las
fases suman 97–101 % del total.

| Dataset | total | analyze | distribute | refine | **localSort** | merge |
|---|---|---|---|---|---|---|
| RandomUniform | 13,73 | 1 % | 14–16 % | 16 % | **60–62 %** | 4 % |
| SortedAscending | 4,39 | 3 % | **74–75 %** | 2 % | 11–12 % | 8 % |
| SortedDescending | 4,88 | 3 % | **73–75 %** | 2 % | 13–14 % | 6–8 % |
| ManyRepeated | 4,27 | 3–4 % | 44–54 % | 3–4 % | 8–9 % | **31–34 %** |
| NormalGaussian | 14,27 | 1 % | 12–14 % | 21–24 % | **56–60 %** | 4–5 % |
| Concentrated | 6,76 | 2 % | 45–46 % | 26–28 % | 6–7 % | **18–19 %** |
| SmallRangeManyEl | 3,32 | 4 % | 39–45 % | 4 % | 11 % | **34–37 %** |
| HugeRangeFewEl | 13,71 | 1 % | 14–15 % | 15 % | **64 %** | 4 % |
| FullRangeExtremes | 13,70 | 1 % | 15–16 % | 15 % | **63–64 %** | 4 % |
| AdversarialPeeling | 38,39 | 0 % | 5–6 % | **56 %** | 34–35 % | 2 % |

**Los porcentajes de la máquina histórica no transfieren, y por mucho.**
`RandomUniform`: `distribute` 46,8 % → **14,3 %**; `localSort` 34,1 % →
**61,4 %**. Coherente con el hardware — `distribute` está limitado por
memoria y el M4 la resuelve mucho mejor; `localSort` es Insertion Sort,
limitado por cómputo.

### 4.2 Asignaciones de heap por `sort()` [MEDIDO]

Exactamente **3 por subdivisión** (`bucketStart`, `bucketSize`, vector de
hijos), como se estimaba. Pero el reparto importa más que el total:

| Grupo | asignaciones |
|---|---|
| `RandomUniform`, `NormalGaussian`, `HugeRangeFewEl`, `FullRangeExtremes` | 22.000–24.500 |
| `AdversarialPeeling` | 168.293 |
| `Concentrated` | 50 |
| **`ManyRepeated`, `SmallRangeManyEl`, `SortedAscending`, `SortedDescending`** | **27–38** |

Los cuatro datasets con `merge` alto **no tienen subdivisiones**, luego el
paso 6 no puede ayudarles en absoluto.

### 4.3 Instrumentación reparada y verificada

- `addApproxMemory()` ya contabiliza los scratch. La cifra pasa de ~16 MB
  a **22,89 MB exactos** para `ManyRepeated` (= `2n·8 + n·8`): la
  subestimación era del **48 %**, exactamente lo predicho.
- **I-TESELADO verificado experimentalmente por primera vez**: aserciones
  activas (`pos == node.start` por hoja, `pos == n` al final) en los diez
  datasets a n = 10⁵ y 10⁶, `make test` y sanitizers. Ninguna se disparó.
  Es la precondición de corrección del paso 7.

---

## 5. Pasos restantes

Cada uno lleva su motivación **posterior a O8**, ahora además contrastada
con el reparto por fase de §4.1, y su criterio en contadores
deterministas siempre que sea posible.

> **Reordenación decidida por el paso 4.** El orden anterior
> (5 → 6 → 7 → 8) dejaba en último lugar el paso que ataca la fase
> dominante. El nuevo orden es **4b → (5, 6, 7 según lo que diga 4b)**:
>
> **Paso 4b = Fase A del paso 8**, un barrido de `targetElementsPerBin`
> que **no necesita ningún cambio de código** (es parámetro del
> constructor desde v3). Va primero porque ataca el 60–64 % del tiempo,
> porque su riesgo es nulo, y sobre todo porque **de-riesga a los otros
> tres**: bajar `λ` sube el número de bins y baja el tamaño de hoja, es
> decir cambia la magnitud objetivo de los pasos 6 y 7. Optimizar antes
> contra un reparto de fases que va a dejar de existir sería trabajo
> tirado.
>
> El argumento original para dejar el 8 al final («que el barrido
> encuentre el λ\* de la implementación definitiva») sigue siendo válido
> para su **Fase B**, la implementación, que se mantiene al final.

### Paso 5 — CC-C: certificado de «ya ordenado»

**Motivación [MEDIDO], independiente de CC1.** Cuando `refine()` detecta
`span == 0` ya sabe que el bin está ordenado, pero devuelve el nodo sin
marca y `sortLeaf()` vuelve a recorrerlo entero con `detectRun()`. El
coste es exactamente visible en los contadores actuales:

| Dataset | comparaciones | de dónde salen |
|---|---|---|
| ManyRepeated | 999.995 | 5 bins monovaluados de ~200.000, reescaneados |
| SmallRangeManyEl | 999.900 | 100 bins monovaluados de ~10.000 |
| Concentrated | 993.445 | mayoritariamente lo mismo |

**En `ManyRepeated`, el 100 % de las comparaciones del algoritmo son este
reescaneo redundante**, y no dependía de CC1 en ningún momento.

**Magnitud real, acotada por §4.1:** el reescaneo vive dentro de
`localSort`, que en esos datasets pesa **8–9 %** (`ManyRepeated`),
**11 %** (`SmallRangeManyEl`) y **6–7 %** (`Concentrated`). Es decir: la
ganancia máxima ronda el 10 % en tres datasets y cero en los otros siete.
Roza el umbral de significación. **Comparaciones ≠ tiempo**, y conviene no
repetir el error del paso 2 de confundir una magnitud grande en unidades
con una magnitud grande en tiempo.

**Hipótesis única:** propagar el hecho de que un bin es monovaluado
elimina el reescaneo.
**Criterio único:** las comparaciones de `ManyRepeated` y
`SmallRangeManyEl` caen a ≈ 0; las de los otros ocho datasets quedan
idénticas byte a byte.
**Riesgo:** ninguno estructural; es propagar información ya calculada.

### Paso 6 — CC-D: eliminar las asignaciones por llamada

**Motivación, condicionada al paso 4.** `refine()` asigna dos
`std::vector<std::size_t>` en cada subdivisión, y cada nodo interno del
árbol asigna su vector de hijos. v7 ya midió 7–10 % de mejora al eliminar
asignaciones equivalentes en `countAndPlace()`, así que la magnitud es
plausible — pero **no está medida aquí**.

Dos partes, que van juntas porque atacan el mismo coste:
- `bucketStart`/`bucketSize` pasan a una arena indexada por profundidad
  (la recursión es estrictamente en profundidad y `depth ≤ 64`).
- El árbol `RefinedRange` se sustituye por una lista plana de hojas,
  apoyada en I-TESELADO. Elimina un vector por nodo interno y convierte
  dos recorridos recursivos en dos bucles secuenciales.

**Hipótesis única:** las asignaciones de heap por `sort()` dominan un
porcentaje medible del tiempo.
**Criterio único:** asignaciones por `sort()` de ~22.000 a O(1), **y**
mejora ≥ 6 % en `RandomUniform` medida alternando.
**Alcance medido (§4.2), mucho más estrecho de lo previsto:** sólo cinco
datasets tienen subdivisiones. `ManyRepeated`, `SmallRangeManyEl`,
`SortedAscending` y `SortedDescending` hacen **menos de 40 asignaciones en
total**: este paso no puede hacer nada por ellos. El objetivo real es la
fase `refine`, que pesa **16 %** en los cuatro datasets sin redundancia.

> **Advertencia tomada del paso 2:** reducir el número de bins en un 50 %
> no produjo ningún efecto medible. Que una magnitud sea grande en
> unidades no implica que lo sea en tiempo. Este paso debe justificarse
> con el reparto por fase del paso 4, no con el recuento.

### Paso 7 — CC-E: el array de entrada como uno de los dos buffers

**Motivación [MEDIDA en unidades, no en tiempo], independiente de CC1.**
v8 usa `bufferA_` y `bufferB_` de tamaño `n` cada uno y después copia todo
a `data`. Usando `data` como uno de los dos buffers:

| | ahora | con CC-E |
|---|---|---|
| buffers de datos | `2n·8` = 16,0 MB | `n·8` = 8,0 MB |
| `bucketOf` | `n·8` = 8,0 MB | `n·8` = 8,0 MB |
| **total auxiliar** | **~24 MB** | **~16 MB** |

Por I-TESELADO, además, una hoja que quede en `data` **ya está en su
sitio**: la fase `merge` sólo copia las hojas de paridad impar.

> **Corrección obligada por el paso 4.** La versión anterior de este
> documento decía «el criterio es de memoria, no de tiempo; `merge` era el
> 4,1 %». **Es falso en esta máquina:** `merge` pesa **31–37 %** en
> `ManyRepeated` y `SmallRangeManyEl`, y **18–19 %** en `Concentrated`
> (§4.1). Este paso pasa de ser cosmético a ser el segundo de mayor valor.

**Hipótesis única:** la memoria auxiliar baja un tercio y la fase `merge`
deja de copiar las hojas que ya están en `data`.
**Criterio único:** `approxMemoryBytes()` (ya reparado y verificado en el
paso 4) baja según la tabla, **y** `ManyRepeated` o `SmallRangeManyEl`
mejoran ≥ 6 % medido alternando, **y** la aserción de I-TESELADO sigue sin
dispararse.
**Riesgo:** es el cambio con más riesgo de corrección de los que quedan.
Escribir la salida de una hoja mientras hay bins pendientes en `data`
exige que los rangos sean disjuntos, que es exactamente I-TESELADO. Por
eso su aserción es prerrequisito y va en el paso 4.

### Paso 8 — CC-F: separar ocupación objetivo del umbral de hoja

**Motivación [MEDIDA en otra máquina], la más grande que queda y la menos
verificada aquí.** `targetElementsPerBin` hace hoy tres trabajos con el
mismo número: ocupación media de los bins, umbral de hoja y divisor del
abanico. Con `λ = t`, la ocupación es Poisson(`t`) y `P(X > t) ≈ 0,5`: la
mitad de los elementos entra en refinamiento **por aritmética, no por los
datos**.

`ANALYSIS_v5.md` §2.1 midió, en el Xeon, que `target ≈ 19` era 18–23 %
más rápido que `target = 64` en los tres datasets sin redundancia, con 27
repeticiones en 3 ejecuciones independientes. **Ese resultado no se ha
reproducido en esta máquina, y §2.1 de este documento demuestra que los
resultados de aquella máquina no transfieren automáticamente.**

**Este paso empieza por medir, no por implementar.**

**Fase A (medición).** Barrido bidimensional `λ × t` sobre
`{8,16,24,32,48,64} × {16,32,64}`, los diez datasets, midiendo
`comparisons`, `totalBins`, `workByDepth` y tiempo alternado. Ningún
barrido anterior fue bidimensional: v4 y v5 movían un único número que
arrastraba los dos efectos a la vez, con signos opuestos.

**Fase B (implementación), sólo si la fase A encuentra un punto que
mejora `RandomUniform` ≥ 6 % sin empeorar ningún dataset > 6 %.**

**Restricción obligatoria (§2.2):** cualquier `target` elegido debe
mantenerse **muy por debajo de 1024**, o el término superlineal reaparece.
Con los valores del barrido no hay riesgo, pero la comprobación debe
quedar escrita en `Config.hpp`.

---

## 6. Pasos eliminados del plan, y por qué

| Paso anterior | Motivo de la eliminación |
|---|---|
| **CC1 — refinamiento terminal** | [MEDIDO] Refutado por O8. No gana en ningún punto del barrido de peor caso; pierde hasta +109 %. El problema que resolvía no existe a `target = 64`. |
| **CC7 — borrar los seis sorts** | Caía con CC1. Con el tope puesto, QuickSort e Introsort se ejecutan 9.346 veces: no son código muerto. |
| **CC8 — unificar `distribute()` y `refine()`** | **No tiene criterio medible.** Su justificación era evitar que los dos caminos volvieran a divergir, que es un argumento de mantenimiento, no de comportamiento. Tras CC-B ambos aplican ya la misma regla. Si los pasos 6 y 7 dejan las dos funciones idénticas, unificarlas será limpieza gratuita en ese momento; no merece un paso propio. |

---

## 7. Limitaciones conocidas que v9 NO resuelve

Se declaran aquí para que no se descubran al final:

1. **`SortedAscending`, 6,02x más lento que `std::sort`.** [MEDIDO] Es el
   peor dataset de la batería y **ningún paso restante lo mejora**. DRS
   hace como mínimo tres pasadas (contar, colocar, copiar) donde libc++
   resuelve en una. Arreglarlo exigiría una detección de «ya ordenado» en
   `analyze()`, es decir **una heurística nueva** — fuera del alcance de
   v9 y del tipo que v6 y v7 eliminaron por perjudicial. Se acepta
   explícitamente.
2. **`SortedDescending` (3,96x) y `ManyRepeated` (1,76x)** por la misma
   razón. `ManyRepeated` sí mejorará en comparaciones con el paso 5, pero
   su tiempo está dominado por el movimiento de datos, no por comparar.
3. **El peor caso de DRS sigue sin estar acotado experimentalmente para
   `target ≥ 1024`.** §2.2 dice que ahí reaparece el término superlineal;
   no se ha construido el adversario que lo demuestre.
4. **El reparto por fase en esta máquina es desconocido** hasta el paso 4.
   Todo lo que este documento dice sobre dónde conviene optimizar es
   provisional hasta entonces.

---

## 8. Reglas de trabajo (vigentes, y por qué)

Derivadas de los errores cometidos en los pasos 1–3, no de principios
generales:

1. **Una hipótesis, una implementación, una medición, una decisión
   binaria.** Sin excepciones.
2. **Medir el terreno ANTES de fijar el criterio.** El paso 2 falló su
   criterio por aplicar un umbral a un denominador que no había
   descompuesto. El paso 3 pasó su criterio y aun así hubo que
   revertirlo.
3. **El criterio debe medir que el cambio SIRVA, no sólo que FUNCIONE.**
   Es el error exacto del paso 3: el criterio comprobaba que CC1 hiciera
   lo que decía, no que eso mejorara nada.
4. **Comparar siempre alternando** las dos versiones en la misma sesión.
   Comparar tandas recogidas por separado dio un falso +8,8 % en el
   paso 1.
5. **Preferir contadores deterministas al reloj.** Son exactos; el reloj
   no resuelve por debajo del 6 % aquí, ni por debajo del 18 % en
   `ManyRepeated`.
6. **Un enunciado asintótico no es una motivación** hasta que se
   comprueba que es relevante a las constantes reales. La revisión
   adversaria encontró este patrón tres veces (C-A, C-C, C-E) y O8 lo
   encontró una cuarta.

---

## 9. Estado y siguiente acción

| Paso | Contenido | Estado |
|---|---|---|
| 0 | Línea base + dos datasets | **Hecho** (`BASELINE_v8.md`) |
| 1 | CC-A: aritmética de span | **Hecho, aceptado** (`STEP1_span.md`) |
| 2 | CC-B: tope de abanico | **Hecho, aceptado** (`STEP2_fanout_cap.md`) |
| 3 | CC1 + CC7 | **Revertido** (`O8_resolucion_y_reversion_paso3.md`) |
| 4 | Medición: fases, asignaciones, memoria, I-TESELADO | **Hecho** (`STEP4_profile.md`) |
| **4b** | **Fase A del 8: barrido de `target`, sin cambios de código** | **Siguiente** |
| 5 | CC-C: certificado de ordenado | Pendiente. Techo medido ~10 %, en 3 datasets |
| 6 | CC-D: asignaciones por llamada | Pendiente. Objetivo: `refine`, 16 %, en 4 datasets |
| 7 | CC-E: `data` como buffer | Pendiente. **Sube de prioridad**: `merge` es 31–37 %, no 4 % |
| 8 | CC-F Fase B: implementar la separación `λ`/`t` | Pendiente, al final por diseño |

**Siguiente acción: paso 4b.** No toca el algoritmo — `target` ya es
parámetro del constructor. Su resultado fija el orden real de los pasos
5, 6 y 7, porque bajar `λ` cambia la magnitud objetivo de todos ellos.
