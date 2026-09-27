# Paso 5 — Certificado de ordenado (CC-C)

**SPEC_v9.md §5, paso 5. Decisión: ACEPTADO por eliminación de trabajo
muerto a coste cero, NO por rendimiento.** La ganancia en producción está
por debajo del suelo de medición, y este documento explica por qué la
medición inicial decía lo contrario.

---

## 1. Medición previa (antes de fijar el criterio)

Lección del paso 2 aplicada: se descompone de dónde salen las
comparaciones **antes** de escribir ningún umbral.
`experimentos/SortedCertificatePotential.cpp` usa la API de introspección
de solo lectura (`debugPartitionOnly` + `debugBufferA/B`) para clasificar
cada hoja. No toca el algoritmo.

| Dataset | comparaciones | de `detectRun` | **eliminables** | % | hojas > t | monovaluadas |
|---|---|---|---|---|---|---|
| **ManyRepeated** | 999.995 | 999.995 | **999.995** | **100,0 %** | 5 | 5 |
| **SmallRangeManyEl** | 999.900 | 999.900 | **999.900** | **100,0 %** | 100 | 100 |
| **Concentrated** | 993.445 | 992.479 | **989.854** | **99,6 %** | 101 | 101 |
| RandomUniform | 11.995.911 | 976.685 | 0 | 0,0 % | 0 | 0 |
| SortedAscending | 984.375 | 984.375 | 0 | 0,0 % | 0 | 0 |
| SortedDescending | 984.375 | 984.375 | 0 | 0,0 % | 0 | 0 |
| NormalGaussian | 11.070.493 | 974.122 | 0 | 0,0 % | 0 | 0 |
| HugeRangeFewEl | 12.398.687 | 977.022 | 0 | 0,0 % | 0 | 0 |
| FullRangeExtremes | 12.396.371 | 977.059 | 0 | 0,0 % | 0 | 0 |
| AdversarialPeeling | 5.719.963 | 928.299 | 0 | 0,0 % | **9.346** | **0** |

El certificado **sólo** puede ayudar a hojas que llegaron a `refine()` con
`count > target` y resultaron monovaluadas: las que salen por tamaño
retornan antes de calcular min/max, así que no hay nada que certificar.
`AdversarialPeeling` lo ilustra: tiene 9.346 hojas grandes y **ninguna**
monovaluada.

## 2. Criterio (dos cláusulas, escrito contra la hipótesis)

> **Cláusula 1 — el mecanismo funciona.** Comparaciones: `ManyRepeated`
> 999.995 → **0**, `SmallRangeManyEl` 999.900 → **0**, `Concentrated`
> 993.445 → **3.591**. Idénticas byte a byte en los otros siete.
>
> **Cláusula 2 — sirve.** La fase `localSort` cae ≥ 50 % en esos tres,
> medido alternando, y ningún dataset empeora > 6 % en tiempo total.

Las dos cláusulas son la lección del paso 3: comprobar que el cambio
*haga lo que dice* no basta; hay que comprobar que *sirva*.

## 3. Resultado

### Cláusula 1: **CUMPLIDA, exacta a la unidad**

| Dataset | antes | predicción | medido | otros contadores |
|---|---|---|---|---|
| ManyRepeated | 999.995 | 0 | **0** | idénticos |
| SmallRangeManyEl | 999.900 | 0 | **0** | idénticos |
| Concentrated | 993.445 | 3.591 | **3.591** | idénticos |
| los otros siete | — | sin cambio | **sin cambio** | idénticos |

Las tres predicciones aciertan exactamente, y `bins`, `vacíos`,
`profundidad` y `maxHoja` quedan idénticos en los diez datasets.

### Cláusula 2: cumplida **en el build de investigación**… y ahí está el problema

Alternado paso 4 → paso 5, 3 rondas:

| Dataset | `localSort` antes | después | caída | total antes | después | Δ |
|---|---|---|---|---|---|---|
| ManyRepeated | 0,38 ms | **0,00 ms** | **100 %** | 4,01 | 3,77 | −6,0 % |
| SmallRangeManyEl | 0,37 ms | **0,00 ms** | **100 %** | 3,68 | 2,95 | **−19,8 %** |
| Concentrated | 0,44 ms | **0,07 ms** | **83 %** | 6,58 | 6,26 | −4,9 % |
| los otros siete | — | — | ±1 % | — | — | ±2 % |

El −19,8 % de `SmallRangeManyEl` era sospechoso: `localSort` sólo era el
10 % de su tiempo total. **No puede ganarse un 19,8 % eliminando una fase
que pesa un 10 %.**

## 4. La ganancia desaparece en producción

`detectRun()` no sólo recorría el bin: llamaba a `recordComparison()` un
millón de veces. **Esas llamadas sólo existen con `DRS_ENABLE_METRICS`.**
Al medir en el build de investigación, el certificado se lleva por delante
tanto el escaneo como el millón de incrementos de contador, y la ganancia
sale inflada.

Medición en **configuración de producción** (sin métricas), 4 rondas
alternadas × 9 repeticiones:

| Dataset | paso 4 | paso 5 | Δ | rango paso 4 | rango paso 5 | |
|---|---|---|---|---|---|---|
| ManyRepeated | 3,79 | 3,96 | +4,6 % | [3,25–5,06] | [3,47–4,98] | se solapan |
| SmallRangeManyEl | 3,13 | 2,97 | −5,0 % | [2,55–3,26] | [2,94–3,00] | se solapan |
| Concentrated | 6,20 | 6,37 | +2,8 % | [5,97–6,39] | [6,08–6,42] | se solapan |
| RandomUniform | 13,41 | 13,35 | −0,5 % | [13,30–13,51] | [13,22–13,49] | se solapan |

**Ningún dataset separa rangos en ninguna dirección.** La ganancia real
está por debajo del suelo de medición.

Y es coherente con la aritmética: el certificado elimina **7,6 MB de
lectura secuencial** por dataset. A la banda de memoria del M4 son
**~0,08 ms** sobre totales de 3–6 ms, es decir **~2 % esperado**, muy por
debajo del umbral del 6 %.

## 5. Decisión: ACEPTADO — y no por rendimiento

Consistente con el precedente del paso 2 (CC-B), y con la misma
franqueza:

1. **El mecanismo está demostrado exactamente** (cláusula 1, predicción a
   la unidad). No es una estimación.
2. **Elimina trabajo real**: 7,6 MB de lectura secuencial por dataset
   afectado, y el 100 % de las comparaciones del algoritmo en dos de
   ellos.
3. **Coste estructural nulo.** `bool sorted` cae en el relleno que ya
   existía junto a `inBufferA`: `sizeof(RefinedRange)` no cambia, y
   `sizeof(DynamicRangeSort<int64_t>)` sigue siendo **128 bytes**,
   idéntico al de v7. El único coste es una rama por hoja, en camino frío.
4. **Riesgo de corrección nulo y verificado**: los 10 datasets ordenan
   correctamente, `make test` pasa, ASan+UBSan limpios sobre
   `tests/main.cpp` y los 19 casos límite.

**Lo que NO justifica la aceptación: el rendimiento.** En producción la
ganancia es de ~2 %, por debajo del suelo de medición, y así queda
registrado. Si el criterio del proyecto fuese exclusivamente el tiempo,
este paso debería revertirse.

## 6. Error metodológico: la cláusula 2 midió el build equivocado

La cláusula 2 decía «medido alternando» sin especificar la configuración.
**La fase `localSort` sólo existe en el build de investigación**, porque
el temporizador de fases es instrumentación. Cualquier criterio basado en
tiempos por fase es, por construcción, un criterio del build de
investigación, y **hay que contrastarlo en producción antes de decidir**.

Es la cuarta vez que el criterio no mide lo que la decisión necesita:

| Paso | El criterio medía | Lo que la decisión necesitaba |
|---|---|---|
| 2 | vacíos totales | vacíos *inalcanzables* |
| 3 | que CC1 funcionara | que CC1 *sirviera* |
| 4b | el óptimo *fusionado* | el efecto de *separar* λ y t |
| **5** | **el build de investigación** | **el build de producción** |

**Regla que se añade a §8 de `SPEC_v9.md`:** todo criterio que use tiempo
debe declarar la configuración de compilación, y si depende de
instrumentación, debe cerrarse con una medición de producción.

Merece la pena señalar que la trampa la tendió el propio proyecto: v7
separó producción de investigación precisamente porque la instrumentación
distorsiona, y aun así caí en ella.

## 7. Observaciones — NO aplicadas

**O12 → trabajo futuro.** Este es el **segundo** cambio consecutivo
(tras CC-B en el paso 2) que elimina trabajo demostrablemente muerto sin
efecto medible en tiempo. Es un dato sobre el estado del algoritmo, no
sobre los cambios: **a v9 le queda poca grasa de coste constante que se
manifieste en el reloj**. Conviene tenerlo en cuenta al decidir si los
pasos 6 y 7 merecen la pena.

**O13 → trabajo futuro.** `detectRun()` sigue recorriendo entera cualquier
hoja ascendente que no tenga certificado — 984.375 comparaciones en
`SortedAscending` y `SortedDescending`, el 100 % de las suyas. Esas hojas
salen de `refine()` por `count <= target`, así que no tienen min/max
calculado y el certificado no las alcanza. Darles uno exigiría calcular el
span de bins que hoy no lo calculan, es decir **añadir** una pasada para
ahorrar otra: no es obviamente rentable y **no se intenta aquí**.

## 8. Estado

Paso 5 cerrado y aceptado. `make test` pasa, sanitizers limpios,
correctitud verificada en los diez datasets, contadores exactos.

Siguiente: **paso 6** (CC-D, eliminar las asignaciones por llamada), cuyo
objetivo medido es la fase `refine` — 16 % en los cuatro datasets sin
redundancia, y **0 % en los seis restantes**, que no tienen
subdivisiones. Su criterio debe escribirse ya con la regla de §6:
declarar la configuración y cerrarse en producción.
