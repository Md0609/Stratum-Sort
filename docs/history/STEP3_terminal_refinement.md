# Paso 3 — Refinamiento terminal (CC1 + CC7)

**SPEC_v9.md §11, paso 3. Decisión: ACEPTADO, con un coste real y medido
que se documenta sin atenuar.**

## Medición previa (la lección del paso 2, aplicada)

Antes de fijar el criterio se construyó una **sonda**: copia del
algoritmo con `MAX_SUBDIVISION_DEPTH = 200`, es decir el tope
prácticamente desactivado, sin modificar nada del código de producción.
Sirve para ver el efecto del cambio antes de comprometerse a él.

Resultado de la sonda (n = 10⁶, capado → sin capar):

| Dataset | tiempo | prof | maxHoja | QuickSort | contadores |
|---|---|---|---|---|---|
| RandomUniform | 13,71 → 13,78 | 1→1 | 64→64 | 0→0 | idénticos |
| SortedAscending | 4,36 → 4,34 | 0→0 | 64→64 | 0→0 | idénticos |
| SortedDescending | 4,82 → 4,78 | 0→0 | 64→64 | 0→0 | idénticos |
| ManyRepeated | 4,17 → 4,16 | 0→0 | 200.812→200.812 | 0→0 | idénticos |
| NormalGaussian | 14,32 → 14,21 | 2→2 | 64→64 | 0→0 | idénticos |
| Concentrated | 6,46 → 6,69 | 1→1 | 10.028→10.028 | 0→0 | idénticos |
| SmallRangeManyEl | 3,28 → 3,15 | 0→0 | 10.205→10.205 | 0→0 | idénticos |
| HugeRangeFewEl | 13,88 → 13,83 | 1→1 | 64→64 | 0→0 | idénticos |
| FullRangeExtremes | 13,96 → 13,65 | 1→1 | 64→64 | 0→0 | idénticos |
| **AdversarialPeeling** | **38,66 → 80,82** | **6→42** | **100→64** | **9.346→0** | **cambian** |

**El tope de profundidad se activa en exactamente un dataset de los diez,
y es el sintético que construí en el paso 0 para provocarlo.** En los
nueve restantes, quitarlo no cambia ni un contador.

## Hipótesis (única)

> Sin tope de profundidad el refinamiento es **terminal**: toda hoja es
> `≤ t` o monovaluada. Por tanto QuickSort, Introsort y HeapSort son
> inalcanzables y pueden borrarse.

## Criterio (único)

> `QuickSort` = `Introsort` = 0 en los diez datasets, ninguna hoja no
> monovaluada supera `t`, y contadores idénticos en los ocho datasets
> históricos.

**Coste declarado por adelantado, ya conocido por la sonda y que el
criterio deliberadamente no cubre:** `AdversarialPeeling` +109 %. Se
trata en §5, no se esconde dentro del criterio.

## Resultado: criterio **cumplido**

- `QuickSort` = 0 y `Introsort` = 0 en los diez datasets, en n = 10⁵ y
  n = 10⁶.
- `maxHoja` = 64 = `t` en todos los datasets salvo los que tienen hojas
  **monovaluadas** (`ManyRepeated` 200.812, `SmallRangeManyEl` 10.205,
  `Concentrated` 10.028), que es exactamente el invariante I-HOJA: hoja
  `≤ t` **o** monovaluada.
- Contadores idénticos byte a byte en los ocho datasets históricos.
- Profundidad máxima observada: **42**, muy por debajo de la cota 66.

### Verificación por aserción en tiempo de ejecución

No es sólo un recuento de contadores. El `Makefile` **no define
`NDEBUG`**, así que las dos aserciones nuevas estuvieron **activas**
durante todas las mediciones:

```
assert(depth <= DEPTH_ASSERT_BOUND)                  // en refine()
assert(count <= targetElementsPerBin_)               // en sortLeaf()
```

Ninguna se disparó en los diez datasets a n = 10⁵ y n = 10⁶, ni en
`make test`, ni en los 19 casos límite bajo sanitizers. **El Teorema 4
queda confirmado experimentalmente, no sólo por conteo.**

(La segunda aserción está *después* del `switch` de `detectRun()`, por eso
una hoja monovaluada de 200.812 elementos no la dispara: sale antes por la
rama `Ascending`.)

## 5. El coste: +111 % en `AdversarialPeeling`

Medición alternada paso 2 → paso 3 → paso 2 → paso 3, 4 rondas, n = 10⁶:

| Dataset | paso 2 | paso 3 | Δ | |
|---|---|---|---|---|
| RandomUniform | 14,21 | 14,32 | +0,8 % | se solapan |
| SortedAscending | 4,39 | 4,45 | +1,4 % | se solapan |
| SortedDescending | 4,84 | 4,84 | −0,1 % | se solapan |
| ManyRepeated | 4,02 | 4,28 | +6,6 % | se solapan (dataset ruidoso, O5) |
| NormalGaussian | 14,57 | 14,93 | +2,5 % | se solapan |
| Concentrated | 6,74 | 6,78 | +0,6 % | se solapan |
| SmallRangeManyEl | 3,33 | 3,29 | −1,2 % | se solapan |
| HugeRangeFewEl | 14,10 | 14,14 | +0,3 % | se solapan |
| FullRangeExtremes | 14,14 | 14,21 | +0,5 % | se solapan |
| **AdversarialPeeling** | **38,92** | **82,16** | **+111,1 %** | **SEPARADOS** |

Es real, reproducible y grande. **Y estaba predicho**: la revisión
adversaria, tras corregir el error C-A, calculó para `t = 64` un peor caso
de ~55 operaciones por elemento sin capar frente a las ~20 de un Introsort
a n = 10⁶ — un factor ~2,75. Lo medido es ~2,1. Mismo orden, misma
dirección. **El modelo corregido predijo este resultado antes de medirlo.**

### Por qué se acepta pese al coste

1. **Coste cero en los ocho datasets históricos.** Contadores idénticos
   byte a byte y tiempos dentro del ruido. El cambio no toca nada real.
2. **La ganancia estructural es incondicional**: el algoritmo ya no tiene
   camino alternativo. `maxHoja` pasa a ser exactamente `t`, verificado
   por aserción activa.
3. **El signo del compromiso depende de un parámetro que no he barrido**
   — ver O8. `AdversarialPeeling` usa grupos de ~107 elementos, que con
   `t = 64` da `splits = 2`, es decir **1 bit de span consumido por
   nivel**: el régimen que maximiza el número de niveles degenerados y por
   tanto el peor caso *para la versión sin capar*. Con grupos grandes el
   cálculo se invierte (§O8). Llamar a esto «el peor caso ha empeorado»
   sería ir más allá de lo medido.
4. Se borran 6 funciones y 127 líneas de código **inalcanzable**, que es
   un pasivo de corrección: hoy nadie podía afirmar que esas ramas se
   ejecutaran, y `QUICKSORT_THRESHOLD` seguía participando en decisiones.

**Lo que NO se afirma:** que el peor caso global de DRS haya mejorado.
Eso requiere O8.

## 6. Qué se cambió

- `refine()`: la condición de hoja pierde `depth >= MAX_SUBDIVISION_DEPTH`.
  Queda `count <= targetElementsPerBin_` y `span == 0`.
- `Config.hpp`: `MAX_SUBDIVISION_DEPTH` desaparece como política y se
  sustituye por `DEPTH_ASSERT_BOUND = 66`, usada **sólo** por una
  aserción. No es un tope: es la cota que el argumento del span garantiza.
  `QUICKSORT_THRESHOLD` se elimina.
- `sortLeaf()`: sin despachador. Certificado de ordenado → nada; tramo
  descendente → invertir; resto → Insertion Sort.
- **Borradas: `quickSort`, `introSort`, `introSortImpl`, `heapSort`,
  `siftDown`, `partition`** (127 líneas del `.tpp`, 6 declaraciones del
  `.hpp`). Con ellas, el `#include <cmath>` que sólo usaba `introSort`.

Balance: `algoritmo/` pasa de 160 líneas borradas a 41 añadidas. El
`.tpp` baja de 646 a 528 líneas.

## 7. Verificación

- `make test`: PASS.
- ASan + UBSan sobre `tests/main.cpp` y los 19 casos límite: limpio.
- Aserciones activas durante toda la batería: ninguna disparada.
- Correctitud verificada contra `std::sort` en las 7 repeticiones de cada
  uno de los 10 datasets, a n = 10⁵ y n = 10⁶.

## 8. Observaciones aparecidas — NO aplicadas

**O8 → BLOQUEANTE antes del informe final de v9.** El signo del
compromiso de CC1 depende del tamaño de grupo del adversario, y sólo he
medido un punto. Con `t = 64`:
- grupos de ~107 (`splits = 2`, 1 bit/nivel): permiten ~43 niveles
  degenerados. Sin capar cuesta 43 pasadas; capado cuesta 6 pasadas más
  QuickSort sobre bins de ~100. **Gana capado** (lo medido).
- grupos de ~10⁵ (`splits ≈ 1.600`, ~10,7 bits/nivel): sólo permiten ~6
  niveles degenerados, y el residuo que el tope entregaría a Introsort
  sería de ~10⁵ elementos → `10⁵·log₂10⁵ ≈ 1,7 M` comparaciones por
  grupo. **Debería ganar sin capar, y por mucho.**

Ese segundo régimen es el que produce el término Θ(n log n) que CC1 dice
eliminar, y **no está medido**. Hay que añadir un parámetro de tamaño de
grupo al generador y barrerlo. Si sin capar pierde en todo el barrido, la
justificación de CC1 se cae y hay que reconsiderarlo.

**O9 → paso 4.** `sortRefined()` y `mergeRefined()` siguen recorriendo el
árbol, y `AdversarialPeeling` pasa de 71.701 a 212.335 bins: el coste de
materializar el árbol ha subido 3x precisamente en el dataset que ahora es
el más lento. CC5 (no materializar el árbol) debería recuperar parte del
+111 %. **Es la primera vez que CC5 tiene un efecto grande esperable en
un dataset concreto**, y conviene medirlo ahí.

**O10 → sin destino.** `detectRun()` ahora sólo ve hojas de `≤ t`
elementos o monovaluadas. Para las de `≤ t` ya ordenadas no aporta nada
(Insertion Sort sobre entrada ordenada ya es O(t)); su único valor real
es la hoja descendente y la monovaluada grande. Su utilidad debería
re-medirse cuando CC4 (certificado de ordenado) esté en su sitio, porque
CC4 se lleva el caso monovaluado.

## 9. Estado

Paso 3 cerrado. **Listo para el paso 4** (CC5 + CC4: no materializar el
árbol; certificado de ordenado), cuyo criterio único, informado por O9,
será que las asignaciones por `sort()` caigan a O(1) y que
`AdversarialPeeling` recupere parte del coste del paso 3 — medido
alternando, con el resto de datasets sin moverse.
