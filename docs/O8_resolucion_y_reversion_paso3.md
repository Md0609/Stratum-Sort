# O8 — Resolución: el paso 3 se REVIERTE

**Hipótesis refutada. `MAX_SUBDIVISION_DEPTH` vuelve al algoritmo, y con
él los seis algoritmos de ordenación. CC1 y CC7 de `SPEC_v9.md` quedan
sin fundamento a las constantes en uso.**

## 1. Hipótesis y criterio

> **Hipótesis (O8).** El +111 % medido en el paso 3 es específico del
> régimen de grupos pequeños (`splits = 2`, 1 bit de span por nivel). En
> el régimen de grupos grandes —el que produce el término Θ(n log n) que
> CC1 dice eliminar— la versión sin capar debería ganar.

> **Criterio (binario).** Si sin capar **gana** en el régimen de grupos
> grandes, CC1 está justificado y el paso 3 se mantiene. Si no, se
> revierte.

## 2. Instrumento

Se añadió un tercer parámetro al generador adversario
(`adversarialPeeling(n, target, coreParam)`, con `0` = comportamiento
anterior, de modo que **todas las medidas previas siguen siendo válidas**)
y se barrió el tamaño del núcleo contiguo del grupo, que es lo que fija el
régimen:

```
nucleo pequeno -> splits = 2         -> 1 bit/nivel          -> muchos niveles, residuo diminuto
nucleo grande  -> splits = C/target  -> log2(C/target) bits  -> pocos niveles, residuo grande
```

`experimentos/AdversarialCoreSweep.cpp` se compila dos veces —contra el
algoritmo capado (paso 2) y contra el sin capar (paso 3)— y los dos
binarios se ejecutan alternados.

## 3. Resultado del barrido (n = 10⁶, target = 64, 3 rondas alternadas)

| núcleo | capado (ms) | sin capar (ms) | Δ | prof c/s | maxHoja c/s | fallbacks c/s | veredicto |
|---|---|---|---|---|---|---|---|
| 64 | 38,56 | 80,63 | **+109,1 %** | 6/42 | 100/64 | 9.346/0 | **capado** |
| 128 | 39,32 | 59,59 | **+51,6 %** | 6/27 | 148/64 | 6.451/0 | **capado** |
| 256 | 41,42 | 43,86 | +5,9 % | 6/18 | 267/64 | 3.649/0 | empate |
| 1.024 | 28,56 | 28,68 | +0,4 % | 6/10 | 1.027/64 | 1/0 | empate |
| 4.096 | 24,50 | 24,67 | +0,7 % | 6/7 | 4.096/64 | 1/0 | empate |
| 16.384 | 21,42 | 21,48 | +0,3 % | 5/5 | 64/64 | 0/0 | empate |
| 65.536 | 18,80 | 18,75 | −0,3 % | 3/3 | 64/64 | 0/0 | empate |

**La versión sin capar no gana en ningún punto del barrido.** Pierde
gravemente en tres y empata en cuatro. El criterio no se cumple.

## 4. Por qué: el régimen Θ(n log n) no existe a estas constantes

La columna `fallbacks` lo dice: con núcleo ≥ 1.024, la versión capada
entrega a un sort por comparación **un solo bin**, y con núcleo ≥ 16.384,
**ninguno**. El escenario que CC1 estaba diseñado para eliminar —muchos
bins grandes cayendo a Introsort— no aparece en ninguna
parametrización que se pueda construir.

La razón es el propio presupuesto de bits, aplicado ahora a la versión
**capada**, que es donde no lo había mirado. Para que un bin de tamaño `m`
sobreviva 6 niveles degenerados hace falta

```
   6 · log₂(m / target)  ≤  bits disponibles  ≤  64
```

| target | `m` máximo que sobrevive 6 niveles | ¿alcanza n = 10⁶? |
|---|---|---|
| 16 | 26.008 | imposible |
| 32 | 52.016 | imposible |
| **64** | **104.032** | **imposible** |
| 128 | 208.064 | imposible |
| 256 | 416.128 | imposible |
| 1.024 | 1.664.511 | alcanzable |

**Con `target = 64`, el residuo más grande que el tope puede entregar a
Introsort es ~104.000 elementos, independientemente de `n`.** Su coste es
`O(104.000 · log₂104.000)` por bin y caben a lo sumo `n/104.000` bins,
luego el total es `O(17n) = O(n)`.

**El término Θ(n log n) que CC1 elimina no existe para `n > ~10⁵` con las
constantes en uso.** Sólo reaparecería con `target ≥ 1024`, y `SPEC_v9`
planea *bajar* el target (λ = 16, t = 32) en el paso 6, lo que lo aleja
aún más.

## 5. Decisión: REVERTIR el paso 3

El criterio era binario y no se cumple. Se restauran
`algoritmo/Config.hpp`, `algoritmo/DynamicRangeSort.{hpp,tpp}` y
`benchmarks/BaselineV8.cpp` al estado exacto del paso 2.

**Verificación de la reversión:** los contadores deterministas de los diez
datasets son idénticos a los del paso 2. `make test` pasa.

Se **conservan** los instrumentos de medición, que son el resultado útil
de este trabajo: el parámetro `coreParam` del generador y
`experimentos/AdversarialCoreSweep.cpp`.

## 6. Qué queda invalidado de la especificación

- **CC1 («el refinamiento es terminal») pierde su justificación
  principal.** El enunciado sigue siendo cierto —sin tope, toda hoja es
  `≤ t` o monovaluada, y el paso 3 lo verificó con aserciones activas—
  pero el problema que resolvía no se materializa a estas constantes, y
  su coste sí: hasta +109 % en entradas construibles.
- **CC7 («borrar los seis sorts») cae con CC1.** Con el tope puesto,
  QuickSort e Introsort **no** son inalcanzables: se ejecutan 9.346 veces
  con núcleo 64.
- **El Corolario 4.1 de `RESEARCH_refinamiento_terminal.md`** («el
  Θ(n log n) actual es un artefacto de `MAX_SUBDIVISION_DEPTH`) es
  técnicamente correcto pero **prácticamente vacío** a `target = 64`: el
  presupuesto de bits acota el residuo a una constante, así que la caída
  a Introsort cuesta O(n) de todas formas. Es el mismo tipo de error que
  la revisión adversaria ya encontró tres veces: un enunciado asintótico
  correcto cuya relevancia práctica no se había comprobado.
- **`SPEC_v9.md` §11 pasos 3 y siguientes** deben re-escribirse: los
  pasos 4 (CC5+CC4), 5 (CC6+CC8) y 6 (CC3) **no dependían de CC1** y
  siguen en pie, pero el plan ya no puede prometer «eliminar el término
  superlineal» como resultado de v9.

## 7. Lo que sí se ha ganado

1. **`MAX_SUBDIVISION_DEPTH = 6` deja de ser un número mágico.** Ahora hay
   una razón cuantitativa para él: con `target = 64` acota el residuo a
   ~104.000 elementos, y el presupuesto de bits impide que un bin mayor
   llegue a agotarlo. Es la primera justificación no empírica de esa
   constante en todo el proyecto.
2. **Una condición de peligro concreta y comprobable:** el término
   superlineal reaparece si `target ≥ 1024`. Eso es un límite de validez
   del algoritmo actual que nadie había identificado, y que conviene
   documentar en `Config.hpp` si se toca el target.
3. **El generador adversario parametrizado**, que es el primer
   instrumento del proyecto capaz de explorar el peor caso de forma
   sistemática en vez de en un solo punto.

## 8. Lección metodológica

El paso 3 pasó su criterio y aun así había que revertirlo. El criterio
medía **que CC1 hiciera lo que decía** (cero fallbacks, hoja máxima = t),
no **que eso sirviera para algo**. Las dos preguntas son distintas y hay
que escribir las dos.

El error de fondo se remonta a `SPEC_v9.md`: di por supuesto que el
Θ(n log n) del peor caso era alcanzable porque el análisis asintótico lo
permitía, sin comprobar que el presupuesto de bits lo impide a las
constantes reales. **La revisión adversaria encontró exactamente este
patrón tres veces (C-A, C-C, C-E) y aun así lo repetí.**

## 9. Estado

Paso 3 revertido. El algoritmo está en el estado del paso 2. **Listo para
el paso 4** (CC5 + CC4: no materializar el árbol de refinamiento y
propagar el certificado de ordenado), que no depende de CC1 y cuya
observación motivadora (O6, del paso 2) sigue intacta: 8.203 bins vacíos
en `Concentrated`, 3.703 en `NormalGaussian` y 240 en `RandomUniform` se
siguen registrando como hojas y recorriendo en `sortRefined()` y
`mergeRefined()`.

Nota: **O9 queda anulada** por la reversión (dependía de los 212.335 bins
que producía la versión sin capar). **O10 queda en pie**, pero su
motivación cambia: `detectRun()` vuelve a ver hojas grandes.
