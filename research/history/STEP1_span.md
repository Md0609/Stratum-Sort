# Paso 1 — Aritmética de span

**SPEC_v9.md §11, paso 1. Completado y aceptado.**

## Hipótesis (única)

> Representar el rango observado por su **span** (`max − min`) en lugar de
> `max − min + 1` elimina el desbordamiento a 64 bits, y con él las dos
> pasadas O(n) degeneradas que `BASELINE_v8.md` (D1) midió en
> `FullRangeExtremes`.

## Criterio de éxito (único)

> `FullRangeExtremes` baja de 20,3 ms a ≈ 14 ms y su profundidad de 3 a 1;
> ningún otro dataset se mueve más del 6 % (umbral de ruido medido en el
> paso 0). `make test` pasa.

## Resultado: **cumplido**

Mediciones **alternadas** v8 → paso 1 → v8 → paso 1 (4 rondas, mismo
binario para el arnés, `n = 10⁶`), que es el protocolo que el proyecto usa
desde v7 para controlar la deriva del entorno:

| Dataset | v8 | paso 1 | Δ | rango v8 | rango paso 1 | |
|---|---|---|---|---|---|---|
| **FullRangeExtremes** | **20,16** | **14,19** | **−29,6 %** | [19,84 – 20,65] | [13,77 – 14,28] | **separados** |
| RandomUniform | 14,14 | 14,33 | +1,4 % | [13,54 – 14,21] | [13,80 – 14,44] | se solapan |
| SortedAscending | 4,36 | 4,41 | +1,0 % | [4,26 – 4,49] | [4,36 – 4,47] | se solapan |
| SortedDescending | 4,83 | 4,83 | +0,1 % | [4,75 – 4,85] | [4,75 – 4,96] | se solapan |
| ManyRepeated | 4,14 | 4,39 | +6,0 % | [3,88 – 4,57] | [4,34 – 4,53] | se solapan |
| NormalGaussian | 14,61 | 14,68 | +0,5 % | [14,01 – 15,03] | [14,35 – 14,99] | se solapan |
| Concentrated | 6,88 | 6,87 | −0,1 % | [6,72 – 7,23] | [6,68 – 7,05] | se solapan |
| SmallRangeManyEl | 3,39 | 3,38 | −0,1 % | [3,36 – 3,63] | [3,17 – 3,50] | se solapan |
| HugeRangeFewEl | 14,17 | 14,21 | +0,3 % | [13,69 – 14,29] | [13,75 – 14,34] | se solapan |
| AdversarialPeeling | 38,41 | 38,75 | +0,9 % | [37,62 – 39,12] | [38,21 – 40,49] | se solapan |

**Profundidad de `FullRangeExtremes`: 3 → 1.** Y `workByDepth` pasa de
`d1=1.000.000  d2=999.999  d3=515.985` a `d1=518.254`, que es
indistinguible del control `HugeRangeFewEl` (`d1=520.559`). Las dos
pasadas completas sobre el array han desaparecido.

### La evidencia decisiva no es el reloj

**Los contadores deterministas son idénticos byte a byte en 9 de los 10
datasets**, y sólo cambian en `FullRangeExtremes` (comparaciones
12.418.718 → 12.396.371, profundidad 3 → 1) — exactamente el dataset al
que apunta la hipótesis. Es decir: el algoritmo hace *literalmente el
mismo trabajo* en todo lo demás, así que cualquier diferencia de tiempo
en esos nueve es ruido por construcción, no efecto.

### Corrección de una lectura mía anterior

En una primera medición (tandas separadas, no alternadas) leí
`ManyRepeated` como una mejora del 8,8 %. **Era ruido.** Bajo el protocolo
alternado el signo se invierte (+6,0 %) y los rangos se solapan por
completo: el rango de v8 [3,88 – 4,57] contiene íntegro el del paso 1
[4,34 – 4,53]. `ManyRepeated` es el dataset más ruidoso de la batería en
esta máquina (18 % de dispersión intra-versión). Sus contadores son
idénticos, así que no hay nada que explicar: no cambió nada.

**Consecuencia metodológica para los pasos siguientes: comparar tandas
recogidas por separado no sirve ni siquiera con 3 ejecuciones. Todo lo
que se decida con el reloj a partir de aquí se medirá alternando.**

## Verificación de corrección

- `make test`: PASS.
- **ASan + UBSan**, build de producción: limpio.
- **19 casos límite adicionales bajo sanitizers**, elegidos para ejercitar
  precisamente el recorte de índice eliminado (si el índice se saliera de
  rango, la escritura en `outBucketSize[idx]` sería fuera de límites y
  ASan lo detectaría): `fullRangeExtremes` y `adversarialPeeling` en
  n ∈ {2, 63, 64, 65, 129, 5.000, 50.000}, `{INT64_MIN, INT64_MAX}`, 64
  elementos iguales, 100 elementos con span 2⁶⁴−1 casi todos al mínimo,
  200 alternando extremos, y un escalón uniforme sobre todo el universo.
  **Todos PASS, sin un solo diagnóstico dentro de `DynamicRangeSort`.**

## Qué se cambió exactamente

1. `computeRangeParameters()` y `refine()` operan sobre
   `span = max − min` (nunca desborda) en vez de `range = span + 1`.
   Ancho: `W = span / s + 1`, que es igual a `⌈(span+1)/s⌉` donde la
   fórmula antigua no desbordaba (Propiedad 1, en el comentario del
   `.tpp`).
2. El ancho de intervalo pasa de `T` a `uint64_t`: es una **magnitud**, no
   un valor del tipo ordenado, y puede no ser representable en `T`.
3. **Se elimina el recorte `if (index >= binCount) index = binCount − 1`**,
   demostrablemente innecesario (Propiedad 2: `s·W > span`).
4. `countAndPlace()` trata `numBuckets == 1` como partición degenerada sin
   calcular índices. Es el único caso donde el ancho (`span+1`) puede no
   ser representable, y aislarlo es lo que permite quitar el recorte.
   `refine()` nunca llega ahí (sus `splits` son siempre ≥ 2); sólo
   `distribute()` puede, cuando toda la entrada cabe en un bin.
5. Se retira el `#include <limits>` del `.tpp`, que quedó sin uso al
   desaparecer `std::numeric_limits` (residuo del propio cambio).

## Observaciones aparecidas durante el paso — NO aplicadas

Se documentan para pasos posteriores, no se mezclan aquí.

**O1 → paso 2 (ya previsto como CC2).** `refine()` sigue sin acotar
`splits` por el span observado. Ahora que ambos sitios usan la misma
aritmética, la diferencia entre `computeRangeParameters()` y `refine()`
es **exactamente una línea** (`if (span < s) s = span + 1`). CC2 es un
cambio más pequeño de lo que la especificación suponía.

**O2 → paso 5 (CC8).** Por lo mismo, unificar los dos caminos de código en
uno solo es ahora casi mecánico: tras el paso 2 las dos funciones
calcularían la misma fórmula con los mismos tipos, y la única asimetría
restante sería de qué buffer se lee.

**O3 → paso 6, o a descartar.** El caso `numBuckets == 1` copia
`src → dst` elemento a elemento. Es un `memcpy` en potencia, pero sólo se
alcanza cuando toda la entrada cabe en un bin (`n ≤ target`), es decir
para entradas diminutas. **Probablemente no merece la pena**; se anota
para no volver a descubrirlo.

**O4 → riesgo para el paso 6.** `SortedAscending` sigue en 4,4 ms frente a
los 0,7 ms de `std::sort` (6,3x). No se ha movido y no se moverá con
ningún paso previsto: es el riesgo ya registrado en `SPEC_v9.md` §10.

**O5 → sin destino asignado.** `ManyRepeated` tiene una dispersión del
18 % en esta máquina, muy por encima del 1,5–5,9 % del resto. Si algún
paso posterior necesita decidir algo sobre ese dataset con el reloj, hará
falta subir repeticiones o aislarlo; con los contadores basta mientras el
trabajo no cambie.

## Estado

Paso 1 cerrado. **Listo para el paso 2** (CC2: acotar el abanico de
`refine()` por el span observado), cuya hipótesis única será que
`ManyRepeated` y `SmallRangeManyEl` bajan a profundidad 1 y que las
comparaciones caen, medido con contadores y no con el reloj.
