# Paso 4b — Barrido de `target` (Fase A del paso 8)

**Medición pura: `targetElementsPerBin` es parámetro del constructor desde
v3, así que el barrido no toca el algoritmo.**

**Decisión: criterio FALLADO. Se elimina del plan el retuneo del `target`
fusionado. `target = 64` se mantiene.**

---

## 1. Hipótesis y criterio

> **Hipótesis.** El coste de `localSort` es `n·λ/4` (Insertion Sort), es
> decir proporcional a λ, y `localSort` es el 60–64 % del tiempo (paso 4).
> Bajar `target` debe reducir el tiempo total de forma medible.

> **Criterio (binario).** Existe un `target` que mejora `RandomUniform`
> ≥ 6 % sin empeorar ningún dataset > 6 % → la Fase B sigue viva. Si no,
> el paso 8 se elimina.

## 2. Resultado: hipótesis refutada

`n = 10⁶`, 5 repeticiones, **los targets se recorren dentro de cada
repetición** para que la deriva del entorno afecte por igual a todos.

Variación relativa frente a `target = 64` (negativo = más rápido):

| Dataset | 8 | 12 | 16 | 24 | 32 | 48 | **64** | 96 | 128 | 256 |
|---|---|---|---|---|---|---|---|---|---|---|
| RandomUniform | +43,1 | +15,8 | +12,9 | −0,2 | −0,5 | **−2,1** | 0 | +14,7 | +30,6 | +48,6 |
| NormalGaussian | +21,9 | +24,3 | +6,6 | +1,9 | **−2,1** | −0,7 | 0 | +15,4 | +24,0 | +38,2 |
| HugeRangeFewEl | +32,3 | +14,8 | +7,8 | −0,9 | **−4,2** | −2,0 | 0 | +19,0 | +29,1 | +47,4 |
| FullRangeExtremes | +31,9 | +15,1 | +7,8 | −0,9 | −2,6 | −2,0 | 0 | +16,2 | +30,5 | +45,5 |
| SortedAscending | +33,2 | +27,4 | +20,3 | +9,7 | +6,2 | +7,4 | 0 | −1,3 | −0,3 | +1,6 |
| SortedDescending | +34,6 | +28,5 | +15,9 | +9,7 | +5,8 | −0,9 | 0 | −1,3 | −2,5 | −2,8 |
| ManyRepeated | +0,4 | +10,6 | +6,8 | −3,0 | +6,7 | −5,9 | 0 | −3,9 | −4,8 | +0,5 |
| Concentrated | +8,7 | +7,7 | −13,3 | −5,6 | −9,4 | −3,7 | 0 | −5,0 | +0,2 | −1,0 |
| SmallRangeManyEl | −0,8 | +0,6 | +9,8 | +6,0 | −3,8 | +0,6 | 0 | +0,7 | 0,0 | −0,7 |

**El mejor `target` para `RandomUniform` es 48, con −2,1 %.** Muy por
debajo del umbral del 6 %. **El criterio no se cumple.**

La curva es una **U con fondo plano entre 24 y 64**, y `target = 64` está
a menos del 2 % del óptimo. No hay nada que retunear.

### 2.1 El resultado de v5 no se reproduce

`ANALYSIS_v5.md` §2.1 midió en el Xeon que `target ≈ 19` era **18–23 %
más rápido** que `target = 64`, verificado con 27 repeticiones en 3
ejecuciones independientes. **Aquí, `target = 16` es un 12,9 % más
lento.** El signo se invierte.

No es un error de aquella medición: es otra máquina. Es la tercera
confirmación de lo que `BASELINE_v8.md` §5 estableció — los resultados de
la máquina histórica no transfieren — y esta vez afecta a la hipótesis
más prometedora del proyecto.

## 3. Por qué: los dos brazos de la U

### 3.1 Brazo izquierdo (λ pequeño): la dispersión desborda la L2

La fase `distribute` escribe en `bins` flujos simultáneos. Cada flujo
mantiene viva una línea de caché, luego la huella de escritura es
`bins × 64 B`. La L2 del M4 es de 4 MiB:

| target | bins | huella | vs L2 | Δ `RandomUniform` |
|---|---|---|---|---|
| 8 | 125.000 | 7,63 MB | **1,91x** | **+43,1 %** |
| 12 | 83.333 | 5,09 MB | **1,27x** | +15,8 % |
| 16 | 62.500 | 3,81 MB | 0,95x | +12,9 % |
| 24 | 41.666 | 2,54 MB | 0,64x | −0,2 % |
| 32 | 31.250 | 1,91 MB | 0,48x | −0,5 % |
| 48 | 20.833 | 1,27 MB | 0,32x | −2,1 % |
| 64 | 15.625 | 0,95 MB | 0,24x | 0 % |

**La degradación empieza exactamente donde la huella se acerca a la L2.**
No hay ningún parámetro ajustado en esta tabla: la huella se calcula, la
L2 se lee del sistema y el delta se mide.

**Esto confirma la hipótesis oculta H-9 de la revisión adversaria**, que
decía que el modelo de coste `Θ(m+s)` «borra los efectos de caché y TLB
que son la razón por la que las implementaciones reales de partición usan
`s ≈ 2⁸–2¹¹`». El modelo RAM predecía que λ pequeño gana; la máquina dice
lo contrario, y por el motivo que la revisión anticipó.

### 3.2 Brazo derecho (λ grande): **el barrido está contaminado**

`Config.hpp` define `INSERTION_SORT_THRESHOLD = DEFAULT_TARGET_ELEMENTS_PER_BIN`,
es decir **una constante de compilación fijada en 64**, no el `target` de
ejecución. Con `target > 64`, las hojas de 65–96 elementos superan ese
umbral y `sortLeaf()` las manda a **QuickSort**:

| target | comparaciones | maxHoja | QuickSort |
|---|---|---|---|
| 48 | 9.257.309 | 48 | 0 |
| **64** | **11.995.911** | **64** | **0** |
| 96 | 9.883.616 | 96 | **5.671** |
| 128 | 7.453.617 | 128 | **9.529** |

Por eso las comparaciones *bajan* al subir el target por encima de 64
mientras el tiempo *sube*: no se está midiendo un λ mayor, se está
midiendo **otro algoritmo de ordenación local**.

**Los puntos 96, 128 y 256 no son comparables con el resto y no deben
usarse para nada.** La parte válida del barrido es `target ≤ 64`.

### 3.3 El modelo `n·λ/4` sobreestima la ganancia

Tiempo absoluto de `localSort` en `RandomUniform`, y coste por
comparación:

| target | localSort | comparaciones | ns/comparación |
|---|---|---|---|
| 64 | 9,01 ms | 11,996 M | **0,75** |
| 48 | 8,21 ms | 9,257 M | 0,89 |
| 32 | 7,45 ms | 6,258 M | 1,19 |
| 16 | 7,90 ms | 3,448 M | **2,29** |

Bajar λ de 64 a 32 reduce las comparaciones a la mitad (11,996 M →
6,258 M) pero el tiempo de `localSort` sólo baja un **17 %**, no un 50 %.
El coste por comparación **se triplica** de λ=64 a λ=16.

**Causa: `localSort` tiene un coste fijo por hoja** (llamada, `detectRun`,
el `switch`, la recursión de `sortRefined`) que el modelo `n·λ/4` ignora.
Al bajar λ el número de hojas se multiplica por 4 (23.555 → 94.513) y ese
coste fijo se amortiza sobre cada vez menos comparaciones.

Es el mismo error de razonamiento que el paso 2: **una magnitud grande en
unidades (comparaciones) no es una magnitud grande en tiempo.**

## 4. Decisión

**Criterio fallado ⇒ se elimina del plan el retuneo del `target`
fusionado.** `DEFAULT_TARGET_ELEMENTS_PER_BIN = 64` se mantiene, y ahora
por primera vez con respaldo experimental en esta máquina: está a menos
del 2 % del óptimo de una curva plana entre 24 y 64.

## 5. Lo que este barrido NO puede decir — y mi criterio, mal escrito por tercera vez

`targetElementsPerBin` es hoy **el mismo número** para la ocupación
objetivo (λ, vía `initialBins` y `splits`) y para el umbral de hoja (t).
El barrido los mueve a la vez, luego **sólo puede encontrar el óptimo de
la variable fusionada. No puede decir nada sobre separarlas**, que es la
hipótesis real de CC-F.

Escribí el criterio contra el barrido en vez de contra la hipótesis. **Es
la tercera vez:**

- paso 2: umbral aplicado al denominador equivocado;
- paso 3: el criterio comprobaba que CC1 *funcionara*, no que *sirviera*;
- paso 4b: el criterio comprueba el óptimo fusionado, no la separación.

El patrón es siempre el mismo: **escribo el criterio sobre lo que la
medición disponible produce, no sobre lo que la hipótesis afirma.**

### 5.1 Lo que el barrido sí deja cuantificado sobre la separación

Reparto por fase de `RandomUniform` según `target`:

| target | total | distribute | **refine** | localSort | merge |
|---|---|---|---|---|---|
| 16 | 18,08 | 17,3 % | **25,0 %** | 43,7 % | 6,5 % |
| 32 | 14,36 | 17,9 % | **21,4 %** | 51,9 % | 5,3 % |
| 48 | 13,77 | 17,3 % | **16,6 %** | 59,6 % | 4,4 % |
| 64 | 14,35 | 15,4 % | **15,8 %** | 62,8 % | 3,8 % |

Con λ = t, la ocupación es Poisson(λ) y `P(X > λ) ≈ 0,5`: la mitad de los
elementos entra en `refine` **por aritmética, no por los datos**. Con
λ = 32 y t = 64, `P(Poisson(32) > 64) ≈ 10⁻⁶`: `refine` se vaciaría.

**Objetivo cuantificado de la separación: los 3,07 ms (21,4 %) que
`refine` consume a λ = 32**, manteniendo hojas de tamaño 32 y una huella
de dispersión de 1,91 MB, cómodamente dentro de la L2.

Eso **no está medido** y no lo mide ningún barrido del parámetro
fusionado: exige el cambio de código de la Fase B.

## 6. Observación nueva

**O11 → a corregir antes de cualquier trabajo sobre `localSort`.**
`INSERTION_SORT_THRESHOLD` y `QUICKSORT_THRESHOLD` se derivan de
`DEFAULT_TARGET_ELEMENTS_PER_BIN` en tiempo de compilación, pero el
`target` es un parámetro de ejecución. **Con cualquier `target` > 64 el
despachador de `sortLeaf()` cambia de algoritmo sin que nadie lo haya
pedido**, y el resultado es que las comparaciones bajan mientras el tiempo
sube. Es una inconsistencia latente desde v3 que nadie había detectado
porque nadie había barrido el target por encima de 64 midiendo el
despachador.

## 7. Estado

Paso 4b cerrado. El algoritmo no se ha tocado. `target = 64` se confirma
como valor por defecto, ahora con respaldo experimental local.

**La Fase B de CC-F (separar λ de t) queda como hipótesis viva y
cuantificada (21,4 % de `refine` a λ=32), pero necesita un criterio
escrito contra la hipótesis, no contra el instrumento disponible.**
Propuesta de criterio pre-registrado:

> Con λ = 32 y t = 64, `refine` cae por debajo del 3 % del tiempo total en
> `RandomUniform`, **y** el tiempo total mejora ≥ 6 % frente a
> `target = 64` medido alternando, **y** ningún dataset empeora > 6 %.

La primera cláusula comprueba que el mecanismo funcione; la segunda, que
sirva. Las dos son necesarias: es exactamente la lección del paso 3.
