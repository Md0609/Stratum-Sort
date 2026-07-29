# Dynamic Range Sort (DRS) v8 — Reducir el reprocesamiento en refine()

## Resumen

Esta iteración buscaba un cambio estructural, no una constante: reducir
cuántas veces se recorre y mueve cada elemento durante `refine()`. Se
identificó una redundancia real, se implementaron y midieron **cinco
variantes distintas** de la misma idea estructural, y **ninguna logró una
mejora limpia en el benchmark más común (`RandomUniform`)** — algunas
mejoraban datasets muy concentrados pero empeoraban los demás. Siguiendo
la regla explícita del encargo ("si empeora cualquier benchmark, debe
eliminarse completamente"), **se revirtió el cambio estructural por
completo**. El algoritmo de producción de v8 es, en código, idéntico al
de v7.

Esto no es un resultado vacío. El propio proceso de medición reveló por
qué la idea no funcionaba como se esperaba — un hallazgo con más valor
real que una mejora del 5% habría tenido, y que cambia dónde merece la
pena mirar en la próxima iteración (ver "Hipótesis para v9").

## Análisis previo (lo que pedía el encargo antes de implementar)

Instrumentando la arquitectura de v7 directamente (no estimado, medido),
para cada elemento que atraviesa `refine()`, el patrón de acceso por
nivel de subdivisión es:

```
1 escaneo de min/max (lectura completa del bin)
+ 1 pasada de conteo dentro de countAndPlace (lectura)
+ 1 pasada de colocación dentro de countAndPlace (lectura + escritura)
= 3 lecturas + 1 escritura, por CADA nivel de refinamiento que atraviesa
```

Medido en n=1.000.000, target=64:

```
Dataset            % de n que entra en refine()   Lecturas/elemento   Escrituras/elemento
RandomUniform                       51,6%                  3,55                 1,52
NormalGaussian                     106,7% (2 niveles)       5,20                 2,07
Concentrated                        99,0%                  4,97                 1,99
SortedAscending                      0,0%                  2,00                 1,00
```

(NormalGaussian supera el 100% porque algunos elementos atraviesan dos
niveles de refinamiento, cada uno sumando a la cuenta.) El caso óptimo
—"una única distribución, sin refine"— es exactamente lo que ya ocurre en
`SortedAscending`: 2 lecturas, 1 escritura. La brecha entre eso y los
demás datasets es precisamente el reprocesamiento que se pidió atacar.

## La idea: min/max como subproducto gratuito

El escaneo de min/max al principio de cada `refine()` recalcula algo que
la pasada de colocación del **nivel padre** ya podría saber gratis: esa
pasada ya lee el valor de cada elemento para copiarlo a su bin de
destino, así que actualizar un mínimo/máximo por bin de destino durante
esa misma lectura cuesta dos comparaciones más por elemento, no una
pasada adicional. Si `refine()` recibe el min/max ya calculado en vez de
recalcularlo, cada nivel pasa de 3 lecturas a 2.

## Cinco variantes implementadas y medidas

Todas mantienen la filosofía de cinco pasos y la regla de subdividir por
rango observado; ninguna sustituye el algoritmo. Cada una se comparó
cabeza a cabeza contra una copia limpia de v7, en el mismo binario,
alternando las mediciones para controlar la deriva del entorno (el
patrón de ruido de esta máquina se ha documentado desde v4 en adelante).

**1. Rastreo incondicional (comparación con `if`).** `countAndPlace()`
actualiza el min/max de cada bin de destino en cada llamada, siempre.
Resultado (10 rondas, `RandomUniform`): **0,92x** (peor que v7). En
`Concentrated`: 1,05x (mejor). Mixto.

**2. Rastreo condicional por bucket.** Solo rastrear los buckets que la
pasada de conteo ya mostró que superarán `target` (evitando trabajo para
los que serán hoja). Resultado: **peor que la variante 1** en todos los
datasets — la rama adicional por elemento ("¿este bucket necesita
rastreo?") es esencialmente una moneda al aire para datos aleatorios, y
los predictores de salto de la CPU la gestionan mal, costando más que las
comparaciones que ahorraba.

**3. Rastreo sin rama (branchless, `std::min`/`std::max`).** Mismo
rastreo incondicional que la variante 1, pero con la actualización
escrita para animar al compilador a generar código sin salto (`cmov`).
Resultado: **peor que las variantes 1 y 2** en los tres datasets
probados — no se pudo confirmar que el compilador generara código
realmente distinto; probablemente ambas formas compilan de forma
similar a `-O3`, y la diferencia medida es ruido.

**4. Híbrido por nivel.** `distribute()` no rastrea (coste idéntico a
v7); solo las llamadas recursivas propias de `refine()` rastrean a sus
hijos. Se detectó y corrigió un error real en la primera implementación
(el escaneo se hacía antes de comprobar si el bin ya era una hoja,
desperdiciando trabajo exactamente donde se quería evitar). Tras la
corrección: `RandomUniform` seguía en **0,90-0,98x**, `Concentrated`
volvía a ~1,0x (neutro, porque su ganancia real estaba precisamente en la
transición que esta variante ya no rastrea).

**5. Rastreo condicionado por el principio del palomar.** Activar el
rastreo solo cuando `observedRange < count` (que garantiza al menos un
valor duplicado en el bin, condición exacta bajo la cual el atajo
`observedMin == observedMax` de un hijo se vuelve gratis en vez de
costar un escaneo). Con `target=64` sobre datos enteros, esta condición
resulta ser **casi siempre cierta** para cualquier bin que supere el
target simplemente porque el ancho de bin (64) ya limita a 64 los
valores enteros distintos posibles — así que el criterio no distinguía
`RandomUniform` de `Concentrated` en la práctica. Resultado: **la misma
regresión de las variantes anteriores**, sin mejora real de la
selectividad.

## El hallazgo que explica todo: reparto de tiempo por fase

Antes de decidir, se midió qué fracción del tiempo total ocupa realmente
`refine()` en cada dataset (n=1.000.000, target=64):

```
Dataset            distribute()   refine()   localSort()   merge()   analyze()
RandomUniform            46,8%      13,7%         34,1%       4,1%        1,3%
NormalGaussian           42,3%      21,2%         30,9%       3,9%        1,7%
Concentrated             47,3%      39,6%          4,5%       6,3%        2,3%
```

**Esto explica por qué ninguna variante dio una mejora limpia.** En
`RandomUniform`, `refine()` es solo el 13,7% del tiempo total — incluso
una reducción perfecta de un tercio en su coste (la que predecía el
análisis de lecturas) ahorraría como mucho ~4,5 puntos porcentuales del
tiempo total, un margen fácilmente consumido por el coste añadido de la
propia reestructuración (una función más, un parámetro más, más
bifurcaciones). En `Concentrated`, `refine()` es el 39,6% del tiempo
total — ahí el mismo tipo de mejora sí tiene margen real para notarse, y
de hecho es donde las variantes 1 y 4 mostraron ganancias consistentes.

En otras palabras: **la optimización no falló por estar mal implementada
en última instancia — falló porque `refine()` no es, para los datasets
más comunes, donde más tiempo se va.** `distribute()` y `localSort()`
dominan el tiempo total en `RandomUniform` y `NormalGaussian` (81% y
73% combinado respectivamente), y esta iteración no tocó ninguno de los
dos.

## Decisión final

**Se revierte el cambio estructural por completo.** `algoritmo/
DynamicRangeSort.hpp` y `.tpp` quedan, en código, idénticos a v7 —
verificado con `diff` contra la versión entregada al final de v7. No
queda código muerto ni rutas alternativas: se intentó, se midió
rigurosamente con cinco variantes distintas, ninguna pasó la validación,
y se retiró por completo, tal como pedía el encargo.

Verificación tras revertir:

```
$ ./build/drs_tests
Edge cases: PASS
Dataset sweep (n=1000): PASS
Dataset sweep (n=50000): PASS
All correctness tests passed.

$ nm build/drs_tests | grep -ci metrics
0
```

## Restricciones respetadas

- La filosofía de cinco pasos no se tocó en ningún momento durante la
  investigación.
- Todas las variantes probadas seguían subdividiendo exclusivamente por
  rango observado.
- No se sustituyó DRS por ningún algoritmo conocido.
- Cada modificación se comparó contra la versión actual antes de
  decidir, con mediciones alternadas para controlar el ruido del
  entorno — no se aceptó ninguna mejora sin verificarla varias veces, y
  no se mantuvo ninguna variante que empeorara algún benchmark.

## Hipótesis para Dynamic Range Sort v9

**Hipótesis: el margen de mejora real y medible está en `distribute()` y
`localSort()`, no en `refine()`, para los datasets sin redundancia
estructural (los que dominan el uso típico).**

- **Por qué podría funcionar:** el reparto de tiempo medido en esta
  misma iteración muestra que, en `RandomUniform`, `distribute()`
  (46,8%) y `localSort()` (34,1%) juntos son más de 4 veces el peso de
  `refine()` (13,7%). Cualquier reducción de coste constante en
  cualquiera de los dos tiene, de partida, mucho más margen para
  notarse en el tiempo total que optimizaciones dirigidas a `refine()`.
- **Qué datos apoyan la hipótesis:** la tabla de reparto de tiempo por
  fase de esta sección, medida directamente, no estimada.
- **Qué experimento habría que hacer:** repetir el mismo proceso de esta
  iteración —analizar primero dónde se mueve físicamente cada
  elemento, proponer una modificación estructural, implementarla,
  medirla cabeza a cabeza contra la versión actual con múltiples rondas
  alternadas— pero centrado en `distribute()` (la única pasada
  obligatoria sobre el array completo) y en el despachador de
  `localSort()` (Insertion Sort / QuickSort / Introsort), en vez de en
  `refine()`.

Esta hipótesis no se ha implementado — es, precisamente, lo que esta
iteración deja preparado para la siguiente.
