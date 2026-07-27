# Paso 6 — Medición de techos, y `merge` por `memcpy`

**SPEC_v9.md. Los pasos 6 y 7 planificados quedan DESCARTADOS por techo
insuficiente. En su lugar se ejecuta el candidato que la medición de
techos identificó como muy superior: sustituir el bucle de copia de
`mergeRefined()` por `memcpy`.**

**Resultado: −42 %, −38 % y −23 % en tres datasets, en producción, con
rangos separados. Es la mejora más grande de toda la v9, y es una línea.**

---

## 1. Metodología: techos medidos en PRODUCCIÓN, no estimados

La lección del paso 5 fue que los porcentajes por fase son del build de
investigación y están inflados. Los techos de esta sección se miden así:

- **Asignaciones:** cronómetro dentro de `operator new`/`delete` globales,
  activado sólo alrededor de `sort()`. Da el tiempo **real** en
  `malloc`/`free`. Techo exacto, no estimación.
- **`merge`:** **ablación** — se elimina la fase en una copia del
  algoritmo y se mide cuánto baja el tiempo. El resultado es incorrecto,
  pero el tiempo es el techo absoluto de cualquier optimización de esa
  fase.
- **Relleno de ceros:** cronómetro alrededor de los dos `assign()`
  **dentro del algoritmo real**. Un microbenchmark aislado dio 0,192 ms
  en una ejecución y 0,572 ms en otra — 3x de variación, inservible. La
  sonda interna da 0,16–0,22 ms de forma estable en ejecuciones repetidas.

## 2. Techos medidos, y el fallo del paso 7 planificado

`merge` cuesta mucho más de lo que yo había estimado aritméticamente:

| Dataset | coste de `merge` (ablación) | % del total |
|---|---|---|
| SmallRangeManyEl | 1,16 ms | **41,1 %** |
| ManyRepeated | 1,02 ms | **28,0 %** |
| Concentrated | 1,19 ms | **19,9 %** |
| SortedAscending | 0,36 ms | 8,4 % |
| HugeRangeFewEl | 0,33 ms | 2,5 % |
| RandomUniform | 0,31 ms | 2,3 % |

**Pero el paso 7 planificado (CC-E, usar `data` como uno de los buffers)
sólo evita copiar las hojas de paridad impar.** Medido:

| Dataset | en `bufferA` (par) | en `bufferB` (impar) | **CC-E evita** |
|---|---|---|---|
| Concentrated | 10.045 | 989.955 | **99,0 %** |
| NormalGaussian | 292.165 | 707.835 | 70,8 % |
| RandomUniform | 435.281 | 564.719 | 56,5 % |
| HugeRangeFewEl | 479.441 | 520.559 | 52,1 % |
| **ManyRepeated** | 1.000.000 | 0 | **0,0 %** |
| **SmallRangeManyEl** | 1.000.000 | 0 | **0,0 %** |
| **SortedAscending / Descending** | 1.000.000 | 0 | **0,0 %** |

**CC-E salva exactamente 0,0 % en los tres datasets donde `merge` es
caro.** Esos datasets no se refinan en absoluto: todas sus hojas están en
paridad 0. El paso 7 estaba apuntando a una fase cara por un mecanismo
que no la toca.

## 3. La causa real: aliasing

`mergeRefined()` copiaba elemento a elemento:

```cpp
for (std::size_t i = node.start; i < node.start + node.count; ++i)
    out[pos++] = buf[i];
```

`out` y `buf` son ambos `std::vector<T>&`. **El compilador no puede
descartar que se solapen**, así que emite un bucle escalar con dependencia
entre el almacenamiento y la carga siguiente: medido, **15,7 GB/s**, muy
por debajo de la banda real de la máquina.

Nada impide usar `memcpy`, y no hace falta comprobar nada en ejecución:

- `out` es el vector del usuario y `buf` es `bufferA_` o `bufferB_`: tres
  objetos distintos, no pueden solapar.
- `T` es entero por el `static_assert` de la clase, luego trivialmente
  copiable.
- Por **I-TESELADO** (verificado por aserción en el paso 4) el rango
  destino es exactamente el rango de la hoja.

## 4. Ranking de candidatos (impacto × riesgo × esfuerzo)

| Candidato | Techo medido | Datasets | Riesgo | Esfuerzo |
|---|---|---|---|---|
| **`merge` con `memcpy`** | **17–41 %** | **3** | mínimo | **1 línea** |
| Paso 7 — CC-E | ~20 % en `Concentrated`; **0 %** donde `merge` es caro | 1 | alto | alto (reescribe la salida) |
| Paso 6 — asignaciones | 5,8–6,2 %; 13,0 % en el adversario; ~0 % en 5 | 4 | medio | medio |
| Relleno de ceros | 9,5–10,1 % en 2; 1,6 % en los grandes | 2 | medio | medio (exige sustituir `std::vector`) |

El primero domina en las tres dimensiones. Se ejecuta ese.

## 5. El cambio ejecutado

**Hipótesis única:** el bucle elemento a elemento no se vectoriza por
aliasing no descartable; `memcpy` lo elimina.

**Criterio (dos cláusulas, configuración declarada — regla 5b):**

> **1. Mecanismo.** Salida correcta en los diez datasets y **todos los
> contadores idénticos byte a byte**: el cambio sólo altera cómo se
> copian los bytes, no qué se calcula.
>
> **2. Beneficio, en build de PRODUCCIÓN.** Al menos dos datasets mejoran
> ≥ 6 % con rangos separados, medido alternando, y ninguno empeora > 6 %.

### Resultado: ambas cumplidas

**Cláusula 1:** contadores idénticos a los del paso 5 en los diez
datasets; correctitud verificada; `make test` y ASan+UBSan limpios sobre
`tests/main.cpp` y los 19 casos límite.

**Cláusula 2** (producción, 4 rondas alternadas × 9 repeticiones):

| Dataset | paso 5 | memcpy | Δ | |
|---|---|---|---|---|
| **ManyRepeated** | 3,78 | **2,20** | **−41,8 %** | separados |
| **SmallRangeManyEl** | 2,92 | **1,77** | **−39,5 %** | separados |
| **Concentrated** | 6,14 | **5,10** | **−17,0 %** | separados |
| SortedDescending | 4,65 | 4,55 | −2,0 % | separados |
| SortedAscending | 4,24 | 4,18 | −1,5 % | separados |
| los otros cinco | — | — | −1,1 % … +0,1 % | se solapan |

Ninguno empeora. **Es la primera vez en toda la v9 que un cambio produce
una mejora grande, separada y en producción.**

## 6. Techos restantes tras el cambio → recomendación de cierre

Re-medidos sobre el código ya con `memcpy`:

| Candidato | Techo restante |
|---|---|
| Paso 6 — asignaciones | 5,8–6,2 % en 4 datasets, ~0 % en 5, 13,0 % sólo en el adversario sintético |
| Paso 7 — CC-E | `merge` restante × paridad impar: **~5,3 % en `Concentrated`, ~1 % en el resto, 0 % en `ManyRepeated`/`SmallRangeManyEl`/`SortedAscending`** |
| Relleno de ceros | 9,5–10,1 % en `ManyRepeated` y `SmallRangeManyEl`, 5,2 % en `SortedAscending`, 1,6 % en los grandes |

**Los tres están en el umbral de significación (6 %) o por debajo, sobre
un subconjunto de datasets, y los tres exigen cambios estructurales de
riesgo medio o alto** — sustituir `std::vector` por almacenamiento sin
inicializar, o reescribir la etapa de salida completa.

**Recomendación: cerrar v9 aquí.** Es coherente con la observación O12
del paso 5 (a v9 le quedaba poca grasa de coste constante visible en el
reloj) y con el objetivo de terminar una versión estable en vez de
perseguir mejoras marginales.

## 7. Balance final de v9 frente a v8

Producción, 4 rondas alternadas × 9 repeticiones, `n = 10⁶`,
`target = 64`:

| Dataset | v8 | v9 | cambio | |
|---|---|---|---|---|
| **SmallRangeManyEl** | 3,31 | **1,92** | **−42,1 %** | separados |
| **ManyRepeated** | 4,03 | **2,48** | **−38,4 %** | separados |
| **FullRangeExtremes** | 19,26 | **13,20** | **−31,5 %** | separados |
| **Concentrated** | 6,62 | **5,08** | **−23,2 %** | separados |
| SortedAscending | 4,28 | 4,15 | −3,0 % | separados |
| SortedDescending | 4,68 | 4,58 | −2,3 % | separados |
| HugeRangeFewEl | 13,28 | 13,22 | −0,4 % | se solapan |
| RandomUniform | 13,46 | 13,45 | −0,0 % | se solapan |
| NormalGaussian | 13,69 | 13,70 | +0,1 % | se solapan |
| AdversarialPeeling | 37,05 | 37,50 | **+1,2 %** | separados |
| **suma** | **119,65** | **109,28** | **−8,7 %** | |

**Regresión honesta:** `AdversarialPeeling` empeora un **1,2 %** con
rangos separados. Es atribuible al coste por subdivisión de la
comprobación del tope de abanico (CC-B): ese dataset hace 56.076
subdivisiones, muchas más que ningún otro. Está muy por debajo del umbral
del 6 % y no viola ningún criterio de aceptación, pero se registra.

**`RandomUniform` y `NormalGaussian` no mejoran.** v9 no ha tocado el
caso medio sin redundancia: su tiempo está dominado por `localSort`
(60–64 %), y el paso 4b demostró que `target = 64` ya está a menos del
2 % del óptimo de esa curva.

## 8. Observaciones — trabajo futuro, NO aplicadas

**O14.** El relleno de ceros de `bufferA_`/`bufferB_` son **2n
escrituras que nadie lee jamás** (`bufferA_` se sobrescribe entero;
`bufferB_` sólo se lee donde se escribió). Techo medido y estable:
0,16–0,22 ms, o sea **~10 % en `ManyRepeated` y `SmallRangeManyEl`**.
Eliminarlo exige sustituir `std::vector` por almacenamiento con
*default-init* (`std::unique_ptr<T[]>`), porque `assign`/`resize` siempre
inicializan. Es el candidato con mejor relación de los que quedan.

**O15.** `countAndPlace()` conserva un bucle de copia elemento a elemento
en el caso `numBuckets == 1`, con el mismo problema de aliasing que
acabamos de arreglar en `merge`. Sólo se alcanza cuando toda la entrada
cabe en un bin (`n ≤ target`), así que el impacto es nulo — pero es el
mismo defecto y conviene arreglarlo si se toca esa función.

**O16.** El paso 7 (CC-E) mantiene su justificación de **memoria**
(auxiliar de ~24 MB a ~16 MB) aunque haya perdido la de tiempo. Si algún
día la memoria importa más que el tiempo, sigue siendo el cambio
correcto, y además se llevaría por delante la mitad del relleno de ceros
de O14 como efecto colateral.
