# Paso 4 — Perfil por fase y asignaciones

**SPEC_v9.md §4. Completado. Sin cambios de comportamiento en el
algoritmo.** Reproducible con `make profile`.

**Resultado principal: el reparto de tiempo en esta máquina no se parece
al de la máquina histórica, y eso invierte las prioridades del plan.**

---

## 1. Los cuatro entregables

| # | Entregable | Estado |
|---|---|---|
| 1 | Reparto de tiempo por fase, 10 datasets | §2 |
| 2 | Asignaciones de heap por `sort()` | §3 |
| 3 | `addApproxMemory()` reparado | §4 |
| 4 | Aserción de I-TESELADO | §5 |

## 2. Reparto de tiempo por fase [MEDIDO]

`n = 10⁶`, `target = 64`, mediana de 7 repeticiones. Rango sobre 3
ejecuciones independientes; las fases suman 97–101 % del tiempo total, es
decir la instrumentación da cuenta de prácticamente todo.

| Dataset | total (ms) | analyze | distribute | refine | **localSort** | merge |
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

### 2.1 Contraste con la máquina histórica

`RandomUniform`, `n = 10⁶`:

| | distribute | refine | localSort | merge |
|---|---|---|---|---|
| Xeon + GCC (`ANALYSIS_v8.md`) | **46,8 %** | 13,7 % | 34,1 % | 4,1 % |
| **M4 + clang (aquí)** | **14,3 %** | 16,2 % | **61,4 %** | 3,8 % |

`distribute` cae de 46,8 % a 14,3 %; `localSort` sube de 34,1 % a 61,4 %.
Es coherente con el hardware: `distribute` es dispersión limitada por
memoria, que el M4 resuelve mucho mejor, mientras `localSort` es Insertion
Sort, limitado por cómputo y sin margen de mejora arquitectónica.

**Verificación de consistencia interna:** `localSort` = 61,4 % de 13,73 ms
= 8,4 ms para los 11.995.911 comparaciones medidas → 0,70 ns por
comparación. Es el orden correcto para Insertion Sort con los datos en L1.
El número no viene de ningún ajuste: cuadra por sí solo.

### 2.2 Consecuencia directa sobre el plan

**La hipótesis P1 del análisis original de v9 —«el margen está en
`distribute()` y `localSort()`»— era correcta a medias y por el motivo
equivocado.** En esta máquina `distribute()` es la cuarta parte de lo que
era, y **`localSort()` solo es más del 60 % del tiempo en los cuatro
datasets sin redundancia**.

`localSort()` es exactamente lo que ataca el paso 8 (separar la ocupación
`λ` del umbral de hoja `t`): el coste de Insertion Sort es `n·λ/4`, luego
es **proporcional a λ**. Es, con diferencia, el paso de mayor valor
esperado del plan — y está el último.

Y `merge` no es el 4 % que la especificación suponía: es el **31–37 %** en
`ManyRepeated` y `SmallRangeManyEl`, y el **18–19 %** en `Concentrated`.

## 3. Asignaciones de heap por `sort()` [MEDIDO]

Contadas sustituyendo `operator new`/`delete` globales, activados sólo
alrededor de la llamada a `sort()`. **No requiere tocar el algoritmo.**

| Dataset | asignaciones | bytes pedidos | subdivisiones | allocs/subdiv |
|---|---|---|---|---|
| RandomUniform | 23.844 | 27,3 MB | 7.930 | 3,01 |
| NormalGaussian | 24.457 | 27,7 MB | 8.133 | 3,01 |
| HugeRangeFewEl | 22.113 | 27,2 MB | 7.353 | 3,01 |
| FullRangeExtremes | 22.002 | 27,2 MB | 7.316 | 3,01 |
| AdversarialPeeling | 168.293 | 39,6 MB | 56.076 | 3,00 |
| Concentrated | 50 | 25,4 MB | 2 | 25,00 |
| SortedAscending | **38** | 25,4 MB | 0 | — |
| SortedDescending | **38** | 25,4 MB | 0 | — |
| SmallRangeManyEl | **31** | 24,0 MB | 0 | — |
| ManyRepeated | **27** | 24,0 MB | 0 | — |

**Exactamente 3 asignaciones por subdivisión**, como predecía el análisis:
`bucketStart`, `bucketSize` y el `vector` de hijos del nodo. La cifra
estimada en la especificación (~22.000 para `RandomUniform`) era correcta.

**Pero el reparto importa más que el total:** los cuatro datasets con
`merge` alto (`ManyRepeated`, `SmallRangeManyEl`, `SortedAscending`,
`SortedDescending`) hacen **menos de 40 asignaciones en total**. El paso 6
no puede ayudarles en absoluto, porque no tienen subdivisiones.

## 4. `addApproxMemory()` reparado [MEDIDO]

Se añaden al final de `sort()` las capacidades de `bucketOfScratch_` y
`writeCursorScratch_`, que no se contabilizaban desde v7 (ver
`ANALYSIS_v9_propuesta.md` §3.5).

Verificación por construcción: `2n·8 = 15,26 MB` de buffers `+ n·8 =
7,63 MB` de `bucketOfScratch_` `= 22,89 MB`. La métrica reporta ahora
**22,89 MB exactos** para `ManyRepeated` (sin árbol de refinamiento) y
**23,73 MB** para `RandomUniform` (los 0,84 MB extra son los nodos del
árbol). Antes reportaba ~16 MB: **la subestimación era del 48 %,
exactamente lo que el análisis predijo.**

Es un cambio de instrumentación puro: vive dentro de
`#ifdef DRS_ENABLE_METRICS` y no existe en el binario de producción.

## 5. Aserción de I-TESELADO [AÑADIDA]

Dos aserciones, activas en cualquier compilación sin `NDEBUG` (el
`Makefile` no lo define, así que están activas en toda la batería):

- En `mergeRefined()`, por hoja: `pos == node.start`.
- Al final de `sort()`: `pos == data.size()`.

**No se disparó ninguna** en los 10 datasets a n = 10⁵ y 10⁶, ni en
`make test`, ni bajo sanitizers. **El invariante I-TESELADO queda
verificado experimentalmente por primera vez.** Es la precondición de
corrección del paso 7, que depende de que los rangos de las hojas sean
disjuntos y estén ya en su posición final.

Consecuencia colateral: el cursor `pos` de `mergeRefined()` queda
demostrado redundante — siempre vale `node.start`.

## 6. Decisión sobre los pasos restantes

El paso 4 existía para decidir cuáles sobreviven. Con los datos:

| Paso | Fase que ataca | Peso medido | Veredicto |
|---|---|---|---|
| **5** — CC-C, certificado de ordenado | `localSort` en los datasets monovaluados | 8–11 % en `ManyRepeated`, `SmallRangeManyEl`; 6–7 % en `Concentrated` | **SIGUE**, pero marginal: roza el umbral del 6 % |
| **6** — CC-D, asignaciones | `refine` | 16 % en los 4 datasets sin redundancia; **0 % en los otros 6** | **SIGUE**, con alcance mucho más estrecho de lo previsto |
| **7** — CC-E, `data` como buffer | `merge` | **31–37 %** en `ManyRepeated` y `SmallRangeManyEl`, 18–19 % en `Concentrated` | **SIGUE, y sube de prioridad.** La especificación decía «no se espera efecto temporal»: **falso**, medido |
| **8** — CC-F, separar `λ` de `t` | `localSort` | **60–64 %** en los 4 datasets sin redundancia | **SIGUE, y es el de mayor valor con diferencia** |

Ninguno se elimina. Pero **dos afirmaciones de la especificación quedan
refutadas por la medición**:

1. «El criterio del paso 7 es de memoria, no de tiempo; `merge` era el
   4,1 %.» **Falso aquí:** `merge` es el 31–37 % en dos datasets.
2. La ordenación 5 → 6 → 7 → 8 colocaba en último lugar el paso que
   ataca la fase dominante.

## 7. Recomendación de reordenación

**Ejecutar primero la Fase A del paso 8**, que es **medición pura y no
necesita ningún cambio de código**: `targetElementsPerBin` ya es un
parámetro del constructor desde v3, así que barrerlo no toca el
algoritmo.

Motivos:

- Ataca el 60–64 % del tiempo; los otros tres pasos juntos atacan menos.
- Es gratis en riesgo: un barrido no modifica nada.
- **De-riesga a los demás.** Si `λ` baja, el número de bins sube y el
  tamaño de hoja baja, lo que cambia el peso de `refine` y de `merge` —
  es decir, cambia la magnitud objetivo de los pasos 6 y 7. Medir primero
  evita optimizar contra un reparto de fases que va a dejar de existir.

El argumento original de la especificación para dejarlo el último («que
el barrido encuentre el λ\* de la implementación definitiva») sigue siendo
válido **para la Fase B**, la implementación. Pero no para la Fase A.

**Propuesta concreta:** paso 4b = Fase A del paso 8 (barrido de `target`,
sin cambios de código, criterio: producir el mapa `λ`/`t` × datasets con
contadores y tiempo alternado). Con ese mapa se decide el orden real de
los pasos 5, 6 y 7.

## 8. Estado

Paso 4 cerrado. Los cuatro entregables existen y son reproducibles
(`make profile`). El algoritmo no ha cambiado de comportamiento:
`make test` pasa, sanitizers limpios, y los contadores deterministas de
los diez datasets siguen idénticos a los del paso 2.
