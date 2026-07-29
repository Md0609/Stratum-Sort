# Informe de revisión adversaria de `RESEARCH_refinamiento_terminal.md`

**Rol:** revisor. **Objetivo:** invalidar el documento. **Resultado:**
cinco afirmaciones son **falsas o mal demostradas** y deben retirarse o
reescribirse; se identifican nueve hipótesis ocultas. **El resultado
central sobrevive**, pero con una demostración distinta y más débil que
la publicada.

**Recomendación editorial: revisión mayor.** El teorema principal
(Teorema 4) es correcto. Toda la sección de constantes (§5.3, §6.3) es
numéricamente incorrecta y su conclusión se invierte al corregirla. El
Teorema 11 es falso.

---

## Parte 0 — Lo que NO logré romper

Por honestidad metodológica, empiezo por el ataque que falló, porque
consumió la mayor parte del esfuerzo y su fracaso es informativo.

### 0.1 Ataque por desbordamiento aritmético (fallido como contraejemplo)

El documento declara una máquina RAM de palabras de `w` bits, pero el
algoritmo calcula `R = b − a + 1`, que necesita `w+1` bits. Busqué ahí un
contraejemplo al Teorema 4 (terminación) y al Teorema 8 (cota `O(nw)`).

**El desbordamiento existe y está verificado numéricamente.** Con
`min = INT64_MIN` y `max = INT64_MAX`:

```
uint64(max) − uint64(min) + 1  =  0        (2^64 mod 2^64)
→ computeRangeParameters:  binCount = 1,  intervalSize = 1
→ refine():  observedRange = 0 → newIntervalSize = 1
→ binIndex(v) = (v − min) recortado a splits−1
     binIndex(min)   = 0
     binIndex(min+1) = 1
     binIndex(0)     = 15   ← todo lo demás colapsa
     binIndex(max)   = 15      en el último bucket
```

El Lema 1 (`rango del hijo ≤ ⌈R/s⌉`) **queda violado**: el hijo grande
conserva casi todo el rango. Si esto pudiera encadenarse, la profundidad
sería `Θ(n)` y el Teorema 8 caería.

**No puede encadenarse, y esto es demostrable.** El desbordamiento de
`(observedRange + splits − 1)` requiere `R ≥ 2^w − s + 1`. Tras un nivel
colapsado, el bucket 0 captura exactamente los elementos de
desplazamiento 0 y el hijo grande tiene desplazamiento mínimo `≥ s−1`,
luego `R' ≤ R − s + 1 ≤ 2^w − s`, y entonces `R' + s' − 1 ≤ 2^w − 1` no
desborda. **A lo sumo un nivel colapsa, siempre.** El coste es `O(1)`
pasadas desperdiciadas sobre `n`, no un cambio de clase.

**Veredicto: defecto real, no contraejemplo.** Debe registrarse como
hipótesis explícita (H-1), no como refutación. Lo señalo porque el
documento recomienda eliminar `MAX_SUBDIVISION_DEPTH` y conviene saber
que el corte **no** está tapando este problema.

---

## Parte 1 — Afirmaciones falsas

### C-A (grave). §5.3 y §6.3: la optimización de `t` es numéricamente incorrecta, y al corregirla la conclusión se invierte

El documento minimiza `κ(t) = w/log₂(ct) + t/4` y concluye un óptimo
plano en `t ∈ [16, 32]` con `κ ≈ 20`.

**El error:** esa fórmula evalúa la cota de profundidad como si los bins
del adversario tuvieran tamaño `≈ t` (de ahí `s ≈ ct`). Pero **el
adversario elige libremente el tamaño de grupo `g`**, y le conviene
`g ≫ t` precisamente para conseguir más niveles. La cota correcta exige
maximizar sobre `g`, no evaluar en `g = t`.

Cálculo corregido (adversario de §5.2, con la restricción de universo de
C-B ya incorporada; `n = 10⁶`, `w = 64`, `σ(m) = ⌈m/t⌉`):

| `t` | `g*` | refine | hoja `t/4` | **TOTAL corregido** | *documento* |
|---|---|---|---|---|---|
| 1 | 16 | 8,4 | 0,2 | **8,7** | — |
| 2 | 21 | 10,9 | 0,5 | **11,4** | — |
| 4 | 27 | 13,6 | 1,0 | **14,6** | *33,0* |
| 8 | 37 | 18,0 | 2,0 | **20,0** | *23,3* |
| 16 | 52 | 23,9 | 4,0 | **27,9** | *20,0* |
| 32 | 75 | 31,0 | 8,0 | **39,0** | *20,8* |
| 64 | 114 | 39,3 | 16,0 | **55,3** | *26,7* |
| 128 | 179 | 43,9 | 32,0 | **75,9** | *41,1* |

**La función corregida es monótona creciente. No hay óptimo interior. El
mínimo está en `t = 1`.** El documento publica una curva en U con mínimo
en [16,32]; la curva real no tiene forma de U.

**Tres consecuencias, todas contra el documento:**

1. La celebrada coincidencia entre el óptimo teórico y el `target ≈ 19`
   medido empíricamente en `ANALYSIS_v5.md` **es espuria**. Desaparece
   al corregir la aritmética. El documento ya la marcaba como "puede ser
   fortuita"; la revisión establece que es directamente un artefacto de
   un error de cálculo. Debe retirarse por completo, no atenuarse.
2. El Corolario 4.2 afirma que `t = 1` (refinamiento puro sin
   comparaciones) es «demostrablemente subóptimo». **Es exactamente al
   revés**: en el peor caso corregido, `t = 1` es el óptimo.
3. La especificación de DRS-Ω (§8) fija `t ∈ [16,32]` citando «dos
   derivaciones independientes que coinciden» (§5.3 y §6.3). Las dos
   derivaciones comparten el mismo error, así que no son independientes:
   son el mismo cálculo equivocado hecho dos veces.

### C-B (grave). Teorema 9: la construcción del adversario ignora que los grupos deben ser disjuntos en el universo

La demostración replica `n/g` grupos «separados entre sí lo suficiente
como para caer en bins distintos» y asigna a **cada** grupo un
presupuesto de `w` bits.

**Imposible.** Los grupos ocupan subrangos disjuntos de un universo de
tamaño `2^w`, luego `Σ (rango de grupo) ≤ 2^w`, y por concavidad el
presupuesto medio por grupo es

```
   log₂ R_grupo  ≤  w − log₂(n/g)
```

Para `n = 10⁶`, `t = 64`, `g = 128`: el presupuesto real es
`64 − 12,9 = 51,1` bits, no 64. La cota inferior enunciada,
`Ω(n·mín(t, (w − log₂t)/log₂(2ct)))`, **es falsa tal como está escrita**:
le falta el término `−log₂(n/g)`.

Esto tiene una consecuencia conceptual que el documento pierde por
completo: **el adversario se debilita al crecer `n` con `w` fijo.**
Cuando `n → 2^w`, el presupuesto por grupo tiende a cero y el peor caso
se vuelve trivial. La constante del peor caso no es una constante: es
una función de `n` que **crece, alcanza un máximo y decrece**. El
documento la presenta como constante.

### C-C (grave). Teorema 11 es falso

Enunciado: *«ordena en O(n) si y solo si `w = O(log n)`»*.

**La dirección (⟸) es falsa.** La demostración publicada argumenta que
«en el nivel superior `log₂ s = log₂ n`, luego la profundidad es O(1)».
Eso solo acota el **primer** nivel: los bins profundos son pequeños, su
`s` es pequeño, y los bits por nivel caen a `log₂(t+1) = O(1)`.

Contraejemplo (verificado numéricamente sobre 10 órdenes de magnitud,
`t = 4`, coste de refinamiento por elemento):

```
w =  1·log₂n :  n=1e4→1.8   1e6→1.8   1e8→1.8   1e10→1.8   1e12→1.8   1e14→1.8   (acotado)
w = 1.5·log₂n:  n=1e4→5.1   1e6→6.2   1e8→7.3   1e10→8.4   1e12→8.9   1e14→9.5   (crece)
w =  2·log₂n :  n=1e4→7.3   1e6→8.9   1e8→10.5  1e10→11.6  1e12→13.1  1e14→14.1  (crece)
w =  3·log₂n :  n=1e4→10.5  1e6→13.1  1e8→15.2  1e10→17.7  1e12→19.7  1e14→21.8  (crece)
```

Con `w = c·log₂n` y `c > 1`, el coste por elemento **no está acotado**:
crece como `Θ(log n / log log n)`, luego el coste total es
`Θ(n log n / log log n) = ω(n)`. La derivación analítica coincide: el
adversario elige `g` con `g·log₂(g/t) ≈ (c−1)log₂n`, dando
`g ≈ (c−1)log₂n / log₂log₂n` y trabajo total `≈ n·g/2`.

**Umbral correcto:** el coste queda acotado si y solo si
`w ≤ log₂ n + O(1)`, es decir, **universo lineal en `n`** — el régimen de
counting sort, mucho más estrecho que `O(log n)`. La tabla `δ` lo
confirma: para `w = log₂n + δ` con `δ` constante el coste es plano en `n`
(δ=0→1,8; δ=8→5,7; δ=16→7,9), y deja de serlo en cuanto `w` crece más
rápido que `log₂ n`.

El teorema debe retirarse y sustituirse por el enunciado con el umbral
correcto.

### C-D (grave). Teorema 8: la cota superior no se sigue de su demostración

La demostración da dos cotas de profundidad y concluye
`O(n·mín(t, ⌈w/log₂ s_min⌉))`. La segunda rama se justifica así: «cada
división produce al menos dos intervalos no vacíos, luego el mayor hijo
tiene `≤ m−1` elementos: en el peor caso un bin de tamaño `m` sobrevive
`m − t` niveles […] esto da `d ≤ t` para los bins pequeños».

**El salto de `d ≤ m − t` a `d ≤ t` no está justificado.** Es válido solo
si `m ≤ 2t`, y no hay ninguna razón para que el adversario se limite a
`m ≤ 2t` — el cálculo corregido de C-A muestra que **no lo hace**
(`g* = 114` para `t = 64`, es decir `m ≈ 1,8·t` … pero `g* = 52` para
`t = 16`, es decir `m ≈ 3,3·t`, fuera del rango donde el argumento vale).
Falta el paso de maximizar sobre `m`, y al hacerlo la expresión
`mín(t, w/log₂ s_min)` no reaparece.

*Cota correcta y sencilla que sí se demuestra:* como `s ≥ 2` en todo bin
que se refina, cada nivel consume `≥ 1` bit, y por el Corolario 1.1 la
profundidad de cualquier camino es `d ≤ log₂ R₀ ≤ w`. Luego
`W ≤ n·w`, y con la ordenación local `Total = O(n(w + t))`. **Es más
débil que lo publicado pero es correcta**, y basta para el Corolario 8.1.

### C-E (moderada). Corolario 9.1: el «Θ» no está establecido

Se afirma `Coste_peor = Θ(n·mín(t, w/log₂(ct)))` porque «los Teoremas 8
y 9 coinciden salvo constantes». No coinciden: el Teorema 8 está mal
derivado (C-D) y el Teorema 9 está mal acotado (C-B). Con las versiones
corregidas, la cota superior demostrable es `O(n(w+t))` y la inferior
construida da `≈ 8,7n … 55,3n` según `t` — **separadas por un factor que
depende de `t` y de `n`**. No hay resultado ajustado. El `Θ` debe pasar a
`O` y `Ω` por separado, y declararse abierto el hueco.

---

## Parte 2 — Hipótesis ocultas

**H-1. Anchura de palabra.** §1.1 declara palabras de `w` bits pero el
algoritmo opera sobre `R = b − a + 1 ∈ [1, 2^w]`, que necesita `w+1`
bits. Todos los lemas de colapso de rango presuponen aritmética exacta
sobre `R`. Hipótesis necesaria: `R ≤ 2^w − s`. Ver Parte 0.

**H-2. Modelo de coste inconsistente en el Teorema 6.** El documento
declara modelo RAM (§1.1) y luego invoca en el Teorema 6(a) una cota
inferior **del modelo de comparación** (`Ω(log s)` para localizar entre
`s` fronteras). En RAM, la búsqueda de predecesor entre `s` fronteras
arbitrarias es `O(log_w s)` con fusion trees, no `Ω(log s)`. Por tanto
el Corolario 6.1 («equilibrio ⟹ `Θ(n log n)`») debe debilitarse a
`Θ(n log n / log w)`, que sigue sin ser lineal pero **no es la separación
limpia que el documento presenta**. La conclusión sobrevive como
argumento de *identidad* (fusion trees y hashing abandonan A1/A3), no
como separación de complejidad.

**H-3. El Teorema 3 necesita una hipótesis de dispersión.** La
demostración coloca `m−2` elementos en el intervalo más ancho `I*`, de
anchura `≥ R/s`. Eso exige que `I*` admita `≥ αm+1` valores **distintos**
si la entrada debe tener valores distintos, es decir `R = Ω(s·αm)`. Para
entradas densas (`R ≤ s`) la anchura es 1, **todo hijo es monovaluado**,
y el equilibrio se consigue trivialmente: **el teorema es falso tal como
está enunciado universalmente**. Además, un hijo grande formado por
duplicados es una hoja gratuita, así que el enunciado relevante debe
exigir que el hijo dominante sea no degenerado. Con la hipótesis añadida
el teorema es correcto y los Corolarios 3.1 y 3.3 se mantienen.

**H-4. Se acredita el axioma equivocado.** §4 sostiene la cadena
«A2 ⟹ A3 viable ⟹ linealidad» y presenta A2 (autonomía de rango) como lo
que impide el equilibrio. **Es A3 + asequibilidad de la resolución.**
Prueba: generalícese a fronteras informadas por histograma (contar en `k`
subintervalos y elegir fronteras alineadas a esa rejilla). Eso viola A2
tal como yo lo formalicé, pero **el proyecto ya trató ese mecanismo como
compatible con la filosofía** (`ANALYSIS_v6.md`, Investigación 2). Y aun
así el equilibrio sigue siendo inalcanzable, porque un cúmulo de diámetro
`δ` dentro de un subintervalo de la rejilla es inseparable salvo que
`k > R/δ`, inasequible para `δ=1`, `R=2^w`. **Luego A2 no es la hipótesis
operativa, y mi formalización de A2 es más estricta que la filosofía real
del proyecto.** El argumento mejora al reformularse, pero la atribución
publicada es incorrecta.

**H-5. El Teorema 2 sobregeneraliza.** Se enuncia «equilibrio ⟹
`Θ(n log n)`». La cota inferior solo vale si el tamaño es la **única**
medida de progreso. Un operador que fuese simultáneamente equilibrado y
colapsante de rango tendría profundidad `≤ mín(log_{1/α}(n/t),
w/log₂ s)`, posiblemente `O(1)`. Enunciado correcto: *la garantía que
aporta el equilibrio por sí solo es `O(n log n)`, y es ajustada para
operadores por cuantiles.* (Que tal operador no exista es el Teorema 3,
que es un resultado distinto y no puede invocarse dentro del Teorema 2
sin circularidad.)

**H-6. El Teorema 7 no es un teorema.** No contiene demostración. `𝒰⁻`
y `𝒰⁺` tienen la **misma clase asintótica** (ambos respetan el
presupuesto de bits); solo difiere la constante de adaptatividad. La
frase «cambia la clase de complejidad adaptativa» no está definida ni
demostrada. Debe degradarse a observación, y con ello se debilita la
lectura retrospectiva sobre v8 que el documento construye encima.

**H-7. El Teorema 10 presupone el barrido completo de buckets.** `Ω(N +
Σsᵢ)` solo se sigue si el nivel enumera los `s` buckets. La alternativa
(lista de índices tocados) se menciona en la Observación 10.1 pero el
enunciado no la excluye. La hipótesis debe estar en el enunciado.

**H-8. Mezcla de peor caso y caso medio.** El término de ordenación
local `t/4` es el coste **medio** de Insertion Sort; el peor caso es
`t²/2`, es decir `t/2` por elemento. Se está sumando una cantidad de caso
medio a un análisis de peor caso. (Corregirlo no cambia la dirección de
C-A: la tabla sigue siendo monótona.)

**H-9. El modelo de coste elegido predetermina la respuesta y oculta el
conflicto real.** Valorar una pasada como `Θ(m + s)` es exactamente lo
que hace ganar a `s = Θ(m)` (Teorema 10) y a `t` pequeño (C-A). Ese mismo
modelo borra los efectos de caché y TLB que son la razón por la que las
implementaciones reales de partición usan `s ≈ 2⁸–2¹¹`. El documento
usó esa libertad para fabricar un acuerdo con el dato empírico de v5;
corregida la aritmética, **teoría y experimento se contradicen** (la
teoría pide `t → 1`, la medición dio `t ≈ 19`). Eso no es un defecto del
proyecto: es el hallazgo interesante que el error de cálculo estaba
tapando. Debe presentarse como conflicto abierto.

---

## Parte 3 — Lo que sobrevive, reescrito con rigor

### Lema 1 (colapso del rango) — correcto bajo H-1

*Hipótesis:* aritmética exacta sobre `R` (`R ≤ 2^w − s`), `s ≥ 2`.

Sea `W = ⌈R/s⌉`. El intervalo `j` cubre los desplazamientos
`[jW, min((j+1)W − 1, R−1)]`, luego contiene `≤ W` valores distintos y el
rango observado de todo hijo es `≤ W`.

*Comprobación de que el recorte de índice nunca actúa:* como
`W ≥ R/s`, para todo `u ≤ R−1` se tiene `⌊u/W⌋ ≤ (R−1)s/R < s`. ∎

*Iteración:* por monotonía de `⌈·⌉` y la identidad de techos anidados
`⌈⌈x/a⌉/b⌉ = ⌈x/(ab)⌉` (enteros positivos),
`R_d ≤ ⌈R₀ / ∏ᵢ sᵢ⌉`. ∎

### Corolario 1.1 (presupuesto de bits) — correcto

El refinamiento cesa a más tardar cuando `∏ sᵢ ≥ R₀`, luego a lo largo
de todo camino raíz-hoja `Σᵢ log₂ sᵢ ≤ log₂ R₀ ≤ w`. Con `sᵢ ≥ 2`, la
profundidad es `d ≤ w`. ∎

*(Ésta es la única cota de profundidad que el documento demuestra
correctamente, y es la que debe sostener todo lo demás.)*

### Lema 3' (decrecimiento estricto) — correcto

Si `R ≥ 2` y `s ≥ 2`, entonces `W = ⌈R/s⌉ ≤ ⌈R/2⌉ < R`. El rango de todo
hijo es `≤ W < R`. Como `R` es un entero positivo, la iteración termina.
∎

### Lema 3 (terminación por anchura 1) — correcto

Si `R ≤ s` entonces `W = 1` y `g(u) = u`: dos elementos comparten
intervalo si y solo si son iguales. Todo hijo es monovaluado, luego está
ordenado. ∎

### Teorema 4 (completitud) — **correcto; es el resultado principal y resiste**

*Enunciado.* Bajo H-1, el refinamiento uniforme sin corte de profundidad
termina, y toda hoja cumple `m ≤ t` **o** es monovaluada.

*Demostración.* Por el Lema 3' el rango decrece estrictamente mientras el
bin se refine, y está acotado inferiormente por 1; luego el proceso
termina en un número finito de niveles. Un bin deja de refinarse
exactamente por una de dos condiciones del algoritmo: `m ≤ t`, o `R = 1`.
No hay otra salida una vez eliminado el corte de profundidad. Además, por
el Lema 3, en cuanto `R ≤ s` todos los hijos son monovaluados, así que la
segunda condición se alcanza efectivamente. ∎

*Corolario 4.1.* El peor caso `Θ(n log n)` de v8 procede exclusivamente
de la tercera condición de salida (`depth ≥ MAX_SUBDIVISION_DEPTH`), que
entrega a Introsort bins de tamaño arbitrario. Sin ella, el paso 4 nunca
recibe un bin de más de `t` elementos. ∎

*(El Corolario 4.2 —«`t=1` es demostrablemente subóptimo»— se retira por
C-A.)*

### Teorema 8' (cota superior corregida) — demostrable

`Total = O(n·(w + t))`.

*Demostración.* Refinamiento: por el Corolario 1.1, `d ≤ w`; el trabajo
por nivel es `≤ n`; luego `≤ n·w`. Ordenación local: cada hoja tiene
`≤ t` elementos y cuesta `O(t²)`, y hay `≤ n/1` hojas con `Σ|L| = n`,
luego `O(n·t)`. ∎

**Corolario 8.1 sobrevive:** para `w` y `t` constantes, el peor caso es
`O(n)`, sin supuestos distribucionales. **Éste es el único resultado de
linealidad que el documento sostiene, y sí lo sostiene.**

### Teorema 3' (no existe divisor universal) — correcto con H-3

*Hipótesis añadida:* `R ≥ s·(αm + 1)` (dispersión suficiente).

*Demostración.* Por A2 las fronteras quedan fijadas por `(a,b,m)` antes
de observar el interior. Por palomar, algún intervalo `I*` tiene anchura
`≥ R/s ≥ αm + 1`, luego admite `αm + 1` valores distintos. Constrúyase la
entrada con `a`, `b` y `αm + 1` valores distintos dentro de `I*`. El hijo
de `I*` tiene `> αm` elementos y rango `> 1` (no degenerado). ∎

*Generalización (H-4), estrictamente más fuerte:* el resultado se
mantiene para fronteras informadas por un histograma de `k` celdas,
sustituyendo la hipótesis por `R/k ≥ αm + 1`. La hipótesis operativa no
es A2 sino la **asequibilidad de la resolución**: separar un cúmulo de
diámetro `δ` exige `k > R/δ`, inasequible para `δ = 1`, `R = 2^w`.

**Los Corolarios 3.1 y 3.3 sobreviven íntegros**, y con la generalización
son más fuertes que lo publicado: el criterio de progreso por tamaño no
es satisfacible ni siquiera relajando A2.

### Teorema 2' (el equilibrio no compra linealidad) — reenunciado

*La garantía que aporta por sí sola una condición de equilibrio
`max hijo ≤ αm` es `O(n·log_{1/α}(n/t)) = O(n log n)`, y es ajustada para
operadores cuyo único progreso es el tamaño (cuantiles/muestreo).* No se
afirma que el equilibrio **impida** la linealidad a un operador que
además colapse el rango; se afirma que **no la aporta**, y que la
combinación no existe por el Teorema 3'. ∎

### Teorema 9' (cota inferior corregida) — reenunciado

Para `σ(m) ≤ c·m`, existe una entrada sobre la que el refinamiento
realiza `Ω(n·g*/2)` operaciones, donde `g*` es el mayor tamaño de grupo
que satisface

```
   Σ_{m=t+1}^{g} log₂ σ(m)   ≤   w − log₂(n/g)
```

**con el término `−log₂(n/g)` procedente de la disjunción de los grupos
en el universo** (C-B). Los valores de `g*` y del coste resultante están
tabulados en C-A. No se afirma que esta construcción sea óptima entre
todos los adversarios, ni que iguale la cota superior.

---

## Parte 4 — Efecto sobre las conclusiones del documento

| Afirmación publicada | Veredicto |
|---|---|
| El refinamiento ya es un algoritmo de ordenación completo (Thm 4) | **Sobrevive.** Demostración correcta. |
| El `Θ(n log n)` es artefacto del corte de profundidad (Cor 4.1) | **Sobrevive.** |
| `O(n)` en el peor caso para `w` fijo (Cor 8.1) | **Sobrevive**, con la demostración simple del Teorema 8', no con la publicada. |
| El progreso por tamaño es anti-lineal (Thm 2) | **Sobrevive reenunciado** (H-5): no aporta linealidad; no se demuestra que la impida. |
| El criterio de equilibrio es inconstruible (Thm 3, Cor 3.1/3.3) | **Sobrevive con hipótesis** (H-3) y se **refuerza** al generalizarlo (H-4). |
| `s = Θ(m)` es la política óptima (Thm 10) | **Sobrevive con hipótesis explícita** (H-7), y solo dentro del modelo `Θ(m+s)` (H-9). |
| La colocación aritmética es la fuente de la linealidad (Thm 6) | **Debilitado** (H-2): `Θ(n log n / log w)`, no `Θ(n log n)`. Argumento de identidad, no separación. |
| El reescaneo de min/max es constitutivo (Thm 7) | **Retirado como teorema** (H-6). Observación sin demostrar. |
| Cotas ajustadas `Θ(n·mín(t, w/log₂(ct)))` (Cor 9.1) | **Falso** (C-D, C-E). Hueco entre `O` y `Ω` abierto. |
| Óptimo `t ∈ [16,32]`, `κ ≈ 20` (§5.3, §6.3) | **Falso** (C-A). La función es monótona; el óptimo es `t = 1`. |
| Coincidencia con el `target ≈ 19` empírico de v5 | **Retirada** (C-A). Artefacto de un error de cálculo. |
| Especificación de DRS-Ω con `t ∈ [16,32]` (§8) | **Sin fundamento.** Las «dos derivaciones independientes» son el mismo error repetido. |
| `O(n)` ⟺ `w = O(log n)` (Thm 11) | **Falso** (C-C). Umbral real `w ≤ log₂n + O(1)`. |

---

## Parte 5 — Qué debería contener la versión revisada

1. Retirar §5.3, §6.3 y el Teorema 11. Sustituirlos por la tabla
   corregida de C-A y el umbral corregido de C-C.
2. Sustituir el Teorema 8 por el Teorema 8' (`O(n(w+t))`), que es más
   débil y demostrable, y declarar abierto el hueco con la cota inferior.
3. Añadir H-1 como hipótesis explícita del modelo, y señalar que el
   corte de profundidad **no** está protegiendo de ese desbordamiento.
4. Reenunciar los Teoremas 2, 3, 6, 7, 9 y 10 con las hipótesis de la
   Parte 2. El Teorema 3 generalizado (H-4) es **mejor** que el
   publicado: conviene reescribirlo, no solo corregirlo.
5. Dejar de fijar `t` por teoría. La teoría corregida pide `t → 1`; la
   medición de v5 pidió `t ≈ 19`. **Presentar la contradicción como el
   problema abierto principal**, no resolverla dentro de un modelo de
   coste elegido para que salga una respuesta concreta (H-9).
6. La conclusión operativa del documento —eliminar el corte de
   profundidad— **no depende de ninguna de las partes refutadas**.
   Descansa solo en los Lemas 1/3/3' y el Teorema 4, que resisten la
   revisión completa.
