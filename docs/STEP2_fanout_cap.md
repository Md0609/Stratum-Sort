# Paso 2 — Tope de abanico por rango observado (CC2)

**SPEC_v9.md §11, paso 2. Decisión: ACEPTADO, con el criterio fallado tal
como lo escribí.** Leer §3 antes que nada: este paso salió mal en la
especificación del criterio, no en la implementación.

## Hipótesis (única)

> Acotar `splits` por `observedSpan + 1` en `refine()` elimina los buckets
> **inalcanzables** — aquellos cuyo índice supera el span observado del
> bin, a los que ningún valor del bin puede mapear jamás — que hoy se
> asignan, se ponen a cero, se acumulan en la suma de prefijos, se
> recorren y se registran como hojas vacías.

## Teorema previo (por eso el paso no tiene riesgo de comportamiento)

El tope **no puede cambiar la partición**. Sólo actúa cuando
`observedSpan < splits`, y entonces la anchura es

```
  W  = observedSpan / splits            + 1 = 0 + 1 = 1
  W' = observedSpan / (observedSpan + 1) + 1 = 0 + 1 = 1
```

Anchura idéntica ⟹ asignación valor→bucket idéntica ⟹ hojas idénticas.
Lo único que desaparece es la cola inalcanzable de buckets vacíos.

**Predicción verificable derivada del teorema: las comparaciones deben ser
idénticas byte a byte en los diez datasets.** Si alguna cambiase, la
implementación estaría mal y habría que revertir.

## Criterio (único, tal como lo escribí)

> Los bins vacíos de `Concentrated` caen **>90 %**, con comparaciones
> idénticas en los diez datasets.

## Resultado

| Dataset | comparaciones | bins | vacíos | prof |
|---|---|---|---|---|
| RandomUniform | idénticas | 23.555 → 23.555 | 240 → 240 | 1→1 |
| SortedAscending | idénticas | 15.625 → 15.625 | 0 → 0 | 0→0 |
| SortedDescending | idénticas | 15.625 → 15.625 | 0 → 0 | 0→0 |
| ManyRepeated | idénticas | 5 → 5 | 0 → 0 | 0→0 |
| NormalGaussian | idénticas | 29.581 → 29.581 | 3.703 → 3.703 | 2→2 |
| **Concentrated** | **idénticas** | **31.092 → 15.724** | **23.571 → 8.203** | 1→1 |
| SmallRangeManyEl | idénticas | 100 → 100 | 0 → 0 | 0→0 |
| HugeRangeFewEl | idénticas | 22.978 → 22.978 | 0 → 0 | 1→1 |
| FullRangeExtremes | idénticas | 22.941 → 22.941 | 0 → 0 | 1→1 |
| AdversarialPeeling | idénticas | 71.701 → 71.701 | 0 → 0 | 6→6 |

- **Comparaciones idénticas en los diez: SÍ.** El teorema se sostiene y la
  implementación es correcta.
- **Caída de vacíos en `Concentrated`: 65,2 %.** El criterio pedía >90 %.
  **FALLADO tal como está escrito.**

## 3. Por qué falló el criterio: error mío de especificación, dos veces

Antes de decidir nada, descompuse de dónde salen los 23.571 bins vacíos de
`Concentrated` (programa de verificación independiente, no una explicación
a posteriori):

```
Nivel 0: span=999.864  bins=15.625  W=64
  bins vacios en el NIVEL 0 (alcanzables, sin ocupar): 8.203
  bins que superan target y entran en refine():            2

  bin 1561: count=568.126  span=57  splits 8.877 -> 58   (inalcanzables: 8.819)
  bin 1562: count=421.829  span=42  splits 6.592 -> 43   (inalcanzables: 6.549)

  TOTAL buckets inalcanzables eliminables:  15.368
  vacios irreducibles del nivel 0:           8.203
```

`23.571 = 15.368 + 8.203`, y `23.571 − 8.203 = 15.368` es exactamente lo
que el tope eliminó. **Es decir: el tope eliminó el 100 % de los buckets
inalcanzables.** Los 8.203 restantes son bins del nivel 0 alcanzables
pero sin ocupar — la cola de la distribución de `Concentrated` deja
15.625 bins de anchura 64 sobre un span de 10⁶ con sólo 10.000 valores
atípicos — y **ningún tope de abanico puede eliminarlos**, porque son
buckets a los que sí se podría mapear un valor.

Escribí el umbral contra el denominador equivocado: lo apliqué a *todos*
los bins vacíos (23.571) cuando la magnitud que la hipótesis predice es
la de los *inalcanzables* (15.368). Contra el denominador correcto el
resultado es 100 %.

**Y lo escribí mal dos veces.** El criterio original de `SPEC_v9.md` §11
para este paso era «`ManyRepeated` y `SmallRangeManyEl` bajan a
profundidad 1 y las comparaciones caen». Es sencillamente falso: esos dos
datasets ya están a profundidad **0** en v8, porque
`computeRangeParameters()` ya aplicaba el tope en el nivel superior. CC2
no los toca en absoluto.

**La lección: la descomposición de arriba debería haberse ejecutado
ANTES de fijar el criterio, no después de fallarlo.** Un criterio
numérico sobre una magnitud cuya procedencia no se ha medido es una
adivinanza, y adivinar el umbral es exactamente lo que esta metodología
existe para impedir.

## 4. Tiempo: sin efecto medible

Medición alternada paso 1 → paso 2 → paso 1 → paso 2, 4 rondas, n = 10⁶:

| Dataset | paso 1 | paso 2 | Δ | | Dataset | paso 1 | paso 2 | Δ |
|---|---|---|---|---|---|---|---|---|
| RandomUniform | 14,06 | 14,25 | +1,4 % | | Concentrated | 6,74 | 6,78 | +0,6 % |
| SortedAscending | 4,41 | 4,42 | +0,3 % | | SmallRangeManyEl | 3,38 | 3,29 | −2,4 % |
| SortedDescending | 4,80 | 4,86 | +1,0 % | | HugeRangeFewEl | 14,19 | 14,27 | +0,6 % |
| ManyRepeated | 4,01 | 4,12 | +2,6 % | | FullRangeExtremes | 14,03 | 14,11 | +0,6 % |
| NormalGaussian | 14,81 | 14,75 | −0,4 % | | AdversarialPeeling | 38,42 | 39,14 | +1,9 % |

**Ningún dataset separa sus rangos, en ninguna dirección.** Incluido
`Concentrated`, que es el único que el cambio toca.

La razón es aritmética y conviene dejarla escrita: los 15.368 slots
eliminados suponen ~46.000 operaciones (un cero, una iteración de
prefijos y una de enumeración por slot) frente a un trabajo total de
varios millones. Y **la parte más visible de ese desperdicio —una llamada
a `recordBin()` por bucket vacío— sólo existe en el build de
investigación**: en producción esas llamadas no se compilan. El tope
elimina trabajo real, pero de un orden por debajo de la resolución de
medición.

## 5. Decisión: ACEPTADO

Es una decisión que tomo con el criterio escrito fallado, así que la
justifico explícitamente en vez de relajar el umbral a posteriori:

1. **La hipótesis se confirma al 100 %** contra la magnitud que predice
   (15.368 de 15.368 buckets inalcanzables), verificado con un programa
   independiente cuyo resultado no depende de ningún umbral ajustado.
2. **Riesgo de regresión nulo, y demostrado, no supuesto**: el teorema
   prueba que la partición no puede cambiar, y la medición lo confirma
   con comparaciones idénticas byte a byte en los diez datasets. Esto no
   es cierto de un cambio de rendimiento típico.
3. **El tiempo es neutro**, no negativo. El coste añadido es una
   comparación por llamada a `refine()` — camino frío, una vez por
   subdivisión, no por elemento.
4. **Es requisito de CC8** (paso 5): unificar `computeRangeParameters()` y
   `refine()` en un solo camino de código exige que apliquen la misma
   regla. Era la observación O1 del paso 1.

**Lo que NO justifica la aceptación: el rendimiento.** Como cambio de
rendimiento este paso no entrega nada medible, y así queda registrado. Si
el criterio de aceptación del proyecto fuese exclusivamente el tiempo,
este paso debería revertirse.

Nótese la asimetría con lo que v6 y v8 sí eliminaron: aquéllos **añadían**
coste por elemento sin ganancia. Éste **quita** trabajo sin ganancia
medible. No son el mismo caso.

## 6. Verificación

- `make test`: PASS.
- ASan + UBSan sobre `tests/main.cpp` y sobre los 19 casos límite de
  `tests/edge_sanitizers.cpp`: limpio.
- Comparaciones idénticas en los diez datasets (§2).

## 7. Observaciones aparecidas — NO aplicadas

**O6 → paso 4 o 5.** Los 8.203 bins vacíos irreducibles de
`Concentrated` (y los 3.703 de `NormalGaussian`, y los 240 de
`RandomUniform`) se siguen registrando como hojas y se siguen recorriendo
en `sortRefined()` y `mergeRefined()`. CC5 los descarta por completo al
no materializar el árbol. **Ahí sí hay un efecto medible en potencia**,
porque son bins del nivel 0, que es el nivel con más bins.

**O7 → sin destino.** `AdversarialPeeling` marcó +1,9 % con rangos que
casi no se solapan ([37,72–38,73] vs [38,43–39,23]). El cambio no lo toca
(0 buckets inalcanzables) y sus contadores son idénticos, así que es
ruido o efecto de disposición del código. Vigilar si reaparece en pasos
posteriores.

## 8. Estado

Paso 2 cerrado. **Listo para el paso 3** (CC1 + CC7: eliminar el tope de
profundidad y borrar los seis algoritmos de ordenación inalcanzables),
cuyo criterio único —esta vez sí derivado de una medición previa— es que
`AdversarialPeeling` pase de **9.346 hojas a QuickSort a exactamente 0**,
y que ningún dataset registre `QuickSort` ni `Introsort`.

Y con una precaución tomada de este paso: **antes de fijar el criterio del
paso 3, medir dónde está hoy el tope de profundidad activándose**, en vez
de predecirlo.
