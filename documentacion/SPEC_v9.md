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
| ¿Qué estructuras nuevas hacen falta? | Arena; un buffer menos | **[ABIERTO]** — sólo si el paso 4 demuestra que las asignaciones pesan. |
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
  hoja está ya en su posición final. *No comprobado por aserción todavía*
  — pendiente del paso 4.
- **I-PROGRESO.** Toda subdivisión produce ≥ 2 buckets no vacíos, luego
  `max hijo ≤ m − 1`.
- **I-BITS.** Cada nivel de refinamiento consume ≥ 1 bit del span, luego
  ningún camino raíz-hoja supera los 64 niveles. Es lo que acota el
  residuo de §2.2.

---

## 4. Paso 4 — Medición, sin cambios en el algoritmo

**Es obligatorio y va primero.** Los pasos que quedan son conjeturas sobre
dónde se va el tiempo *en esta máquina*, y **el reparto por fase nunca se
ha medido aquí**: los porcentajes que la especificación anterior usaba
(distribute 46,8 %, localSort 34,1 %, refine 13,7 %, merge 4,1 %) son del
Xeon con GCC, y §2.1 ya demostró que los ratios de esa máquina no
transfieren.

**Entregables:**

1. **Reparto de tiempo por fase** (`analyze`, `distribute`, `refine`,
   `localSort`, `merge`) para los diez datasets. La instrumentación ya
   existe (`DRSMetrics::startPhase/endPhase`); nadie la ha tabulado aquí.
2. **Número de asignaciones de heap por `sort()`**. Determina si CC-D
   merece existir. Estimación a confirmar: ~7.350 vectores de hijos +
   ~14.700 de `bucketStart`/`bucketSize` ≈ 22.000 por `sort()` en
   `RandomUniform`.
3. **Reparación de `addApproxMemory()`**, que no contabiliza
   `bucketOfScratch_` y subestima el consumo real en ~50 %. Sin esto, el
   criterio de CC-E no es medible con la métrica del proyecto.
4. **Aserción de I-TESELADO** en compilación de depuración
   (`Σ count de hojas == n` y `start` estrictamente creciente). Es
   prerrequisito de CC-E, que depende de ese invariante para escribir la
   salida en el sitio.

**Criterio de aceptación:** que los cuatro entregables existan y sean
reproducibles. No hay decisión binaria sobre el algoritmo porque el
algoritmo no se toca.

**Lo que decide:** cada uno de los pasos 5–8 sigue vivo sólo si este paso
muestra que su magnitud objetivo es suficiente para superar el umbral del
6 %. Cualquiera que no lo supere se elimina del plan aquí mismo, sin
implementarlo.

---

## 5. Pasos restantes

Cada uno lleva su motivación **posterior a O8** y su criterio en
contadores deterministas siempre que sea posible, porque el reloj de esta
máquina no resuelve por debajo del 6 %.

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
reescaneo redundante.** Es la magnitud más grande y mejor identificada que
queda en el proyecto, y no dependía de CC1 en ningún momento.

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
**Se elimina del plan si** el paso 4 mide que las asignaciones cuestan
menos del 6 %.

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

**Hipótesis única:** la memoria auxiliar baja un tercio y la fase `merge`
copia aproximadamente la mitad de los elementos.
**Criterio único:** `approxMemoryBytes()` (ya reparado en el paso 4) baja
según la tabla, **y** correctitud intacta con la aserción de I-TESELADO
activa. **El criterio es de memoria, no de tiempo**: la fase `merge` era
el 4,1 % del total en la máquina histórica, así que no se espera —ni se
exige— efecto temporal.
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
| 4 | Medición: fases, asignaciones, memoria, I-TESELADO | **Siguiente** |
| 5 | CC-C: certificado de ordenado | Pendiente, motivación firme |
| 6 | CC-D: asignaciones por llamada | Pendiente, **condicionado al paso 4** |
| 7 | CC-E: `data` como buffer | Pendiente, criterio de memoria |
| 8 | CC-F: barrido `λ × t` | Pendiente, empieza por medir |

**Siguiente acción: paso 4.** No toca el algoritmo. Su resultado decide
si los pasos 6 y 7 siguen existiendo.
