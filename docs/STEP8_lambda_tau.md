# Paso 8 — Separar λ (ocupación) de t (umbral de hoja)

**ACEPTADO.** Primer paso de v10. Criterios 1 y 2 cumplidos, ambos
pre-registrados antes de tocar el código.

---

## 1. Hipótesis y criterios (fijados antes de implementar)

> **Hipótesis.** `targetElementsPerBin` hace dos trabajos con el mismo
> número: la ocupación objetivo λ (vía `initialBins` y `splits`) y el
> umbral de hoja t. Con λ = t la ocupación es Poisson(λ) y
> `P(X > t) ≈ 0,5`: **la mitad de los elementos entra en `refine()` por
> aritmética, no por los datos.** Separarlos (λ=32, t=64) debería vaciar
> `refine()`, porque `P(Poisson(32) > 64) ≈ 10⁻⁸`.

> **Criterio 1 — el mecanismo.** `refine()` cae por debajo del **3 %** del
> tiempo total en `RandomUniform`.
>
> **Criterio 2 — que sirva.** El total mejora **≥ 6 %** en producción,
> medido alternando, **y ningún dataset empeora > 6 %**.

Criterio pre-registrado en `STEP4b_target_sweep.md` §7 al cerrar v9.

## 2. Intento de falsación previo, antes de tocar el código

`experimentos/LambdaTauPotential.cpp` replica la partición de nivel 0 con
las fórmulas exactas de `computeRangeParameters()` y cuenta la ocupación
real. **Predijo que el mecanismo sólo funciona en la mitad de la batería:**

| Dataset | % que refina, λ=t=64 | predicho con λ=32, t=64 | |
|---|---|---|---|
| RandomUniform | 56,5 % | **0,0 %** | ✓ |
| HugeRangeFewEl | 52,1 % | **0,0 %** | ✓ |
| FullRangeExtremes | 51,8 % | **0,0 %** | ✓ |
| NormalGaussian | 89,1 % | **73,7 %** | ✗ |
| Concentrated | 99,0 % | **99,0 %** | ✗ |
| ManyRepeated / SmallRangeManyEl | 100 % | **100 %** | ✗ |

En la gaussiana la ocupación **no** es Poisson(32): la masa central
concentra bins muy por encima de 64. En los datasets de rango estrecho los
bins están acotados por el span, así que λ no los toca.

La misma sonda avisó del coste en el otro platillo: al subir el umbral de
hoja las hojas dejan de partirse y crecen, y Insertion Sort es cuadrático
en el tamaño de hoja. **Las dos estimaciones que pude construir del efecto
neto daban −29 % y +3 %: no sabía el signo.**

## 3. El cambio: mínimo

Tres modificaciones, ninguna optimización adicional:

1. Miembro nuevo `leafThreshold_`, y constructor de dos parámetros
   (`leafThreshold` por defecto = λ ⟹ comportamiento de v9 sin cambio).
2. La condición de hoja por tamaño en `refine()` usa `leafThreshold_` en
   vez de `targetElementsPerBin_`. **Una línea.**
3. Defectos: λ = 32, t = 64.

**Y una corrección obligada, no una optimización:** `INSERTION_SORT_THRESHOLD`
y `QUICKSORT_THRESHOLD` se derivaban de λ. Con λ=32 y t=64, las hojas de
33 a 64 elementos se habrían ido a QuickSort sin que nadie lo pidiera —
que es exactamente la observación **O11** de v9. Ahora siguen al umbral de
hoja. Sin esto, la configuración medida y la configurada serían distintas.

## 4. Criterio 1: CUMPLIDO

| Dataset | subdiv. base | subdiv. prop | refine % base | refine % prop |
|---|---|---|---|---|
| **RandomUniform** | 7.930 | **0** | 16,8 % | **2,4 %** |
| HugeRangeFewEl | 7.353 | **0** | 16,4 % | **1,4 %** |
| FullRangeExtremes | 7.316 | **0** | 14,6 % | **1,4 %** |
| NormalGaussian | 8.133 | 7.402 | 22,4 % | 17,9 % |
| Concentrated | 2 | 4 | 29,8 % | **38,6 %** |
| AdversarialPeeling | 56.076 | 56.076 | 56,5 % | 57,5 % |

`RandomUniform`: **16,8 % → 2,4 %**, por debajo del 3 % exigido, y las
subdivisiones caen de 7.930 a **cero**. Comparaciones 11,996 M → 8,986 M.

Y el mecanismo falla exactamente donde la sonda previa dijo que fallaría.
**La predicción se sostuvo en los seis casos.**

## 5. Criterio 2: CUMPLIDO — tras resolver una falsa alarma

### 5.1 La falsa alarma

La primera medición (4 rondas × 9 reps) dio `ManyRepeated` **+14,9 %**, lo
que habría hecho fallar el criterio. Antes de aceptarlo hice tres
controles, porque `ManyRepeated` tiene `span = 4`: sus bins están acotados
por el rango a **5 en ambas configuraciones**, y sus contadores son
`subdivisiones 0→0`, `comparaciones 0→0`. **El algoritmo ejecuta trabajo
bit a bit idéntico, luego una regresión causal es imposible.**

| Control | Resultado |
|---|---|
| v9 vs código nuevo, **ambos a λ=t=64** (semántica idéntica) | +10,6 % → el «coste» está en el binario, no en λ/t |
| **Mismo binario**, sólo cambian los parámetros, 8 rondas × 25 reps | **+1,7 %**, rangos solapados |
| Configuración por defecto vs v9, 5 rondas × 9 reps | **−0,2 %** |

La regresión no existía. Es el dataset que **O5** declaró irresoluble por
reloj (18 % de dispersión), y las muestras se solapan casi por completo.

`SortedDescending` hizo lo mismo a menor escala: +6,9 % en una tanda de 9
reps, **+2,3 %** con 25 reps, +3,5 % en la verificación final.

### 5.2 Verificación final, configuración por defecto

Producción, 5 rondas alternadas × 9 repeticiones, `n = 10⁶`:

| Dataset | v9 | v10 | cambio | |
|---|---|---|---|---|
| **HugeRangeFewEl** | 13,46 | **10,39** | **−22,8 %** | separados |
| **RandomUniform** | 13,31 | **10,44** | **−21,6 %** | separados |
| **FullRangeExtremes** | 13,28 | **10,47** | **−21,1 %** | separados |
| **NormalGaussian** | 13,68 | **11,69** | **−14,6 %** | separados |
| **Concentrated** | 5,18 | **4,73** | **−8,7 %** | separados |
| ManyRepeated | 2,59 | 2,58 | −0,2 % | se solapan |
| SortedAscending | 4,20 | 4,26 | +1,3 % | se solapan |
| SmallRangeManyEl | 1,82 | 1,85 | +1,6 % | se solapan |
| AdversarialPeeling | 37,37 | 37,92 | +1,5 % | se solapan |
| SortedDescending | 4,59 | 4,75 | +3,5 % | separados |
| **suma** | **109,49** | **99,09** | **−9,5 %** | |

**Cinco datasets mejoran entre 8,7 % y 22,8 % con rangos separados. La
peor regresión es +3,5 %, por debajo del umbral.** Criterio 2 cumplido.

## 6. Efecto colateral: la cota del peor caso mejora

La cota de O8 es `m_max = λ · 2^(w/D)` — entra **λ**, no el umbral de
hoja, porque el factor de ramificación es `splits = ⌈m/λ⌉`. Bajar λ de 64
a 32 **reduce a la mitad el residuo máximo** que el tope de profundidad
puede entregar a un sort por comparación:

```
   antes:  64 · 2^(64/6)  ~= 104.032
   ahora:  32 · 2^(64/6)  ~=  52.016
```

y el coste agregado de ese residuo pasa de `~17n` a `~16n`. No era el
objetivo del paso, pero conviene registrarlo: **la separación no debilita
la garantía de linealidad, la refuerza.** Documentado en `Config.hpp`.

## 7. Verificación de corrección

`make test` PASS. ASan + UBSan limpios sobre `tests/main.cpp` y los 19
casos límite de `tests/edge_sanitizers.cpp`. Correctitud comprobada contra
`std::sort` en las 9 repeticiones de los 10 datasets de cada ronda.

## 8. Lo que este paso NO consigue

- **`SortedAscending` sigue ~6x por detrás de `std::sort`.** No se ha
  movido y no lo hará sin una heurística de detección previa.
- **`AdversarialPeeling` no mejora** (+1,5 %, dentro del ruido): sus bins
  se refinan por rango degenerado, no por ocupación.
- **`ManyRepeated` y `SmallRangeManyEl` no mejoran**: sus bins están
  acotados por el span, λ no los toca. Ya lo predijo la sonda de §2.

## 9. Observación — NO aplicada

**O17 → trabajo futuro.** El barrido de λ y t es unidimensional: sólo se
ha probado (32, 64). El paso 4b de v9 barrió el parámetro fusionado, pero
**el plano (λ, t) está sin explorar**. Con `refine()` ya vaciado en los
datasets uniformes, el óptimo de λ puede haberse desplazado — la
restricción de caché (huella `bins × 64 B` frente a la L2 de 4 MiB) sitúa
el límite inferior de λ en torno a 16, y t podría subir por encima de 64
si el despachador lo permite. **No se toca hasta tener un criterio escrito
contra esa hipótesis y no contra el instrumento.**
