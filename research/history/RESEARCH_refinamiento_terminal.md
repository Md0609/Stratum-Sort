# Refinamiento terminal: ¿puede la partición adaptativa por rango observado ordenar por sí misma en tiempo lineal?

> ## ⚠ ERRATA — leer antes que nada
>
> Una revisión adversaria posterior encontró **errores que invalidan
> partes de este documento**. Está pendiente de reescritura. Ver
> `REVIEW_refinamiento_terminal.md` para el informe completo.
>
> **Falso y retirado:**
> - **§5.3 y §6.3** (óptimo de `t`): la fórmula `κ(t) = w/log₂(ct) + t/4`
>   evalúa la profundidad en `m ≈ t` en vez de maximizar sobre el tamaño
>   de grupo que elige el adversario. Corregida, la función es
>   **monótona creciente** y el óptimo está en `t = 1`, no en `[16,32]`.
>   La «coincidencia» con el `target ≈ 19` empírico de v5 **es un
>   artefacto de ese error de cálculo**.
> - **Corolario 4.2**, en su afirmación de que `t = 1` es subóptimo: es
>   al revés.
> - **Teorema 11** (`O(n)` ⟺ `w = O(log n)`): falso. Para
>   `w = c·log₂n`, `c > 1`, el coste es `Θ(n log n / log log n) = ω(n)`.
>   Umbral real: `w ≤ log₂ n + O(1)`.
> - **Teorema 9**: la construcción ignora que los grupos deben ocupar
>   subrangos disjuntos del universo. El presupuesto por grupo es
>   `w − log₂(n/g)`, no `w`.
> - **Teorema 8** y **Corolario 9.1**: la cota superior no se sigue de su
>   demostración, y por tanto el `Θ` de cotas ajustadas no está
>   establecido. Sustituir por `O(n(w+t))`, que sí es demostrable.
> - **§8** (especificación de DRS-Ω) fija `t ∈ [16,32]` citando «dos
>   derivaciones independientes»: son el mismo error repetido.
>
> **Sobrevive sin cambios:** los Lemas 1, 3 y 3', el **Teorema 4**
> (completitud del refinamiento) y el **Corolario 4.1** (el `Θ(n log n)`
> es artefacto de `MAX_SUBDIVISION_DEPTH`). La conclusión operativa del
> documento no depende de ninguna de las partes refutadas.
>
> **Sobrevive con hipótesis añadidas:** Teoremas 2, 3, 6, 7 y 10 — ver
> las nueve hipótesis ocultas de la Parte 2 del informe de revisión. El
> Teorema 3 generalizado resulta ser **más fuerte** que el publicado.

**Documento de investigación. Sin código, sin implementación, sin
propuesta de cambio pendiente de medir.** Todo lo que sigue son
definiciones, invariantes, teoremas, demostraciones y contraejemplos.
Cuando un resultado es una conjetura y no un teorema, se dice
explícitamente.

---

## Resumen

Se formaliza la familia de algoritmos definida por la filosofía de
Dynamic Range Sort y se responde a la pregunta de investigación
planteada. Los resultados principales son cinco, y dos de ellos
contradicen la dirección propuesta en el encargo:

1. **(Positivo, Teorema 4)** El refinamiento de DRS **ya es, por sí
   solo, un algoritmo de ordenación completo**. Sin cambiar ni una
   fórmula, si se elimina el corte artificial de profundidad, ninguna
   hoja mayor que `t` llega jamás a un algoritmo de comparación. El
   Θ(n log n) del peor caso actual **no es una propiedad de DRS: es un
   artefacto de `MAX_SUBDIVISION_DEPTH = 6`**.

2. **(Positivo, Teoremas 8 y 9)** El peor caso de DRS sin ese corte es
   **Θ(n · mín(t, w/log₂(ct)))**, con cota superior e inferior que
   coinciden. Para palabra fija (w = 64) **eso es O(n) estricto en el
   peor caso**, con una constante explícita, acotada y minimizable.

3. **(Negativo, Teorema 2)** Exigir progreso sobre el **tamaño** del
   mayor subproblema —la dirección propuesta— es *anti-lineal*: si se
   pudiera garantizar, forzaría Θ(n log n). El progreso por tamaño es la
   noción de progreso de la ordenación por comparación; importarla a DRS
   reintroduce exactamente la barrera que DRS evita.

4. **(Negativo, Teorema 3)** Además de ser indeseable, es
   **inconstruible**: ninguna regla de partición que dependa solo del
   rango observado y del tamaño del bin puede garantizar reducción del
   mayor subproblema. Se da la demostración y el contraejemplo, válidos
   para *cualquier* regla de esa familia, no solo para la anchura
   uniforme.

5. **(Estructural, Teorema 6)** La razón por la que DRS puede ser
   sublineal-en-comparaciones es exactamente la restricción que el
   encargo declara innegociable: la localización de un valor en su
   intervalo se hace por **aritmética** y no por **búsqueda**. Cualquier
   mecanismo que garantice equilibrio obliga a fronteras arbitrarias,
   que obligan a búsqueda, que devuelve Ω(n log n). **La filosofía no es
   una restricción estética: es la fuente de la linealidad.**

Y una respuesta a la pregunta final del encargo (§7): **O(n) estricto es
alcanzable y demostrable si y solo si el ancho de palabra w es O(log n)**.
Para w arbitrario, exigirlo a DRS equivale a resolver un problema abierto
de la teoría de ordenación de enteros, no a un problema de diseño de DRS.

---

## 1. Modelo formal

### 1.1 Entrada y máquina

Entrada: un multiconjunto `S` de `n` enteros de `w` bits, universo
`U = [0, 2^w)`. Máquina RAM de coste unitario con palabras de `w` bits:
comparación, suma, resta, división entera y desplazamiento cuestan O(1).
Este es el modelo estándar en el que radix sort es lineal y en el que la
cota Ω(n log n) de comparación **no** aplica, porque los algoritmos
pueden hacer aritmética sobre las claves.

### 1.2 Los tres axiomas de la identidad de DRS

Formalizo la filosofía de cinco pasos como tres axiomas sobre el operador
de refinamiento. Un algoritmo es *DRS-admisible* si los cumple los tres.

> **(A1) Partición en intervalos, con orden.**
> Un bin `B` se parte en `s` intervalos de valor **consecutivos**
> `I₁ < I₂ < … < I_s` que cubren `[a_B, b_B]`. Todo elemento va al
> intervalo que contiene su valor.
> *Consecuencia:* si cada hijo se ordena, la concatenación en orden de
> índice es el bin ordenado. El paso 5 (unir) es una concatenación
> trivial, sin fusión.

> **(A2) Autonomía de rango local.**
> Las fronteras de los hijos de `B` dependen **únicamente** de
> `(a_B, b_B, m_B)`: el mínimo observado, el máximo observado y el
> cardinal **de ese bin**. Nunca del rango global, nunca de otros bins,
> nunca de la distribución empírica dentro de `B` más allá de sus
> extremos.
> *Es la regla "refinar solo por rango observado" del encargo.*

> **(A3) Colocación aritmética.**
> El índice del intervalo de un valor `v` es calculable en O(1)
> operaciones aritméticas a partir de `(v, a_B, b_B, m_B)`. Sin
> búsqueda, sin comparaciones contra una tabla de fronteras.

**Observación 1.1.** A3 no está escrito en el enunciado de la filosofía,
pero está implícito en «construir intervalos a partir de la
distribución»: es lo que distingue *construir* intervalos de
*seleccionar* separadores. El §4 demuestra que A3 es la única fuente de
la ventaja asintótica de DRS, y que A2 es lo que hace A3 posible. Los
tres axiomas no son independientes en su valor: **A2 ⟹ A3 es viable ⟹
sublinealidad en comparaciones**.

**Definición 1.2 (operador de refinamiento).** Un operador DRS-admisible
es un par `(σ, g)` donde `σ(a,b,m) = s ∈ ℕ` es el número de intervalos y
`g_{a,b,m}: [0, R) → [0, s)` es una función **monótona no decreciente**
computable en O(1) que asigna a cada desplazamiento `u = v − a` su índice
de intervalo. `R = b − a + 1` es el rango observado.

- **Uniforme** `𝒰`: `g(u) = ⌊u / ⌈R/s⌉⌋`. Es el operador de DRS v1–v8.
- **Geométrico** `𝒢`: `g(u) = ⌊log₂(u+1)⌋`, con `s = ⌈log₂ R⌉`.
- En general, **cualquier reindexación monótona de `[0,R)` computable en
  O(1)** es un operador admisible. Ése es el espacio de diseño completo
  de la "nueva generación" (§6).

### 1.3 Modelo de coste

Una pasada de refinamiento sobre un bin de tamaño `m` con `s` intervalos
cuesta `Θ(m + s)`: hay que recorrer los `m` elementos (contar y colocar)
y recorrer los `s` contadores (prefijos y enumeración ordenada de los
hijos). El coste total del algoritmo es

```
  Coste(S) = Σ_{B ∈ árbol}  Θ(m_B + s_B)   +   Σ_{hojas L}  Coste_local(|L|)
```

**Definición 1.3 (umbral de hoja `t`).** Un bin con `m ≤ t` se declara
hoja y se resuelve con el algoritmo local del paso 4. `t` es una
constante del algoritmo.

---

## 2. Las dos nociones de progreso

Aquí está el núcleo conceptual del documento. Hay exactamente dos
magnitudes que un refinamiento puede reducir, y **no son
intercambiables**.

### 2.1 Progreso por rango: el presupuesto de bits

**Lema 1 (colapso exacto del rango).** Sea `𝒰` el operador uniforme. Si
un bin tiene rango `R` y se parte en `s` intervalos, cada hijo tiene
rango observado `≤ ⌈R/s⌉`. Iterando por un camino raíz-hoja con factores
`s₁, s₂, …, s_d`:

```
  R_d  ≤  ⌈ R₀ / (s₁·s₂·…·s_d) ⌉
```

*Demostración.* Para enteros positivos vale la identidad de techos
anidados `⌈⌈x/a⌉/b⌉ = ⌈x/(ab)⌉`. El rango de un hijo está acotado por la
anchura del intervalo que lo contiene, `⌈R/s⌉`; inducción sobre `d`. ∎

**Corolario 1.1 (presupuesto de bits).** El refinamiento termina en
cuanto `∏ sᵢ ≥ R₀`. Por tanto, a lo largo de **cualquier** camino
raíz-hoja:

```
  Σᵢ log₂ sᵢ  ≤  log₂ R₀ + log₂ s_max  ≤  w + log₂ s_max
```

**Interpretación.** Cada pasada de refinamiento *consume* `log₂ sᵢ` bits
de resolución de la clave, y solo hay `w` bits que consumir. El
refinamiento no es un proceso que "reduzca el problema": es un proceso
que **agota un presupuesto de información fijo, independiente de `n`**.
Ésta es la propiedad que separa a DRS de la ordenación por comparación, y
es toda la fuente de su linealidad.

**Corolario 1.2 (profundidad).** Si todo bin que se refina usa
`sᵢ ≥ s_min`, la profundidad del árbol es
`d ≤ ⌈w / log₂ s_min⌉ + 1`. **La profundidad no depende de `n`.**

### 2.2 Progreso por tamaño: por qué es anti-lineal

**Teorema 2 (el equilibrio es anti-lineal).** Supóngase un operador que
garantiza `max_j |B_j| ≤ α·m_B` para una constante `α < 1`. Entonces el
coste total del refinamiento es `Θ(n · log_{1/α}(n/t)) = Θ(n log n)`, y
la cota es ajustada.

*Demostración.* **Superior:** en cada nivel, el trabajo total es a lo
sumo `n` (cada elemento vivo se toca una vez). Un elemento deja de estar
vivo cuando su bin baja de `t`. Como el bin de un elemento se multiplica
por a lo sumo `α` en cada nivel, tras `d` niveles su tamaño es `≥ ...`
—no importa la cota inferior—, y el número de niveles necesarios en el
peor caso para pasar de `n` a `t` es exactamente `log_{1/α}(n/t)`.
**Inferior:** tómese el operador que produce siempre `1/α` hijos de
tamaño exactamente `α·m`. Todos los `n` elementos siguen vivos en cada
uno de los `log_{1/α}(n/t)` niveles, luego el trabajo es
`n · log_{1/α}(n/t)`. ∎

**Corolario 2.1.** El árbol de recursión de un refinamiento equilibrado
es, término a término, el árbol de recursión de Mergesort o de un
Quicksort con pivote perfecto. **Exigir progreso por tamaño convierte a
DRS en un algoritmo con forma de ordenación por comparación, y le impone
su barrera.**

**Contraste directo (el resultado central de esta sección):**

| Noción de progreso | Profundidad | Coste total |
|---|---|---|
| Rango (`R → R/s`) | `≤ w / log₂ s` — **independiente de n** | `O(n·w/log₂ s)` |
| Tamaño (`m → αm`) | `log_{1/α}(n/t)` — **crece con n** | `Θ(n log n)` |

**La división `999.999 / 1` del encargo no es un fracaso: es un éxito
que cuesta `log₂ s` bits del presupuesto.** El presupuesto total es `w`,
así que ese patrón puede repetirse a lo sumo `w/log₂ s` veces a lo largo
de un camino — no `n` veces. La intuición de que es un desperdicio es una
intuición de ordenación por comparación aplicada a un algoritmo de
distribución.

### 2.3 Verificación de la autocorrección

Compruébese sobre el propio ejemplo del encargo. Bin
`{0, 1, …, m−2} ∪ {M}` con `M` enorme, `m = 10⁶`, `t = 64`.

- **Nivel 1:** `R = M`, `s = 15.625`, anchura `M/15.625`. Si
  `M/15.625 > m`, los `m−1` primeros caen en el intervalo 0 y `M` en el
  último. División `999.999 / 1`. Coste: una pasada. Bits consumidos:
  13,9.
- **Nivel 2:** el hijo grande **recalcula su propio min/max**: su rango
  ahora es `m−1 = 999.999`, no `M`. `s = 15.625`, anchura 64. Se reparte
  de forma perfectamente uniforme. Todos los nietos son hoja.

**El patrón se autocorrige en una sola pasada extra.** Para que no se
autocorrija hay que construir un adversario recursivo (§5.2), y ese
adversario paga `log₂ s` bits por nivel, luego se agota.

---

## 3. El refinamiento ya es un algoritmo de ordenación completo

Ésta es la respuesta directa a la pregunta de investigación.

**Lema 3 (terminación por anchura 1).** Sea un bin con `m > t`, rango
observado `R` y `s = σ(m) ≥ R` intervalos bajo el operador uniforme.
Entonces la anchura es `⌈R/s⌉ = 1`, cada intervalo contiene **exactamente
un valor distinto**, y por tanto **todos los hijos son monovaluados y
están ordenados por definición**. El bin queda completamente ordenado en
esa única pasada, sin una sola comparación.

*Demostración.* Con anchura 1, `g(u) = u`, luego dos elementos comparten
intervalo si y solo si tienen el mismo valor. Un multiconjunto de un solo
valor distinto está ordenado. ∎

**Lema 3'** (variante sin la condición `s ≥ R`). Mientras `R > s ≥ 2`, la
anchura `⌈R/s⌉ ≥ 2` y el rango de todo hijo es `≤ ⌈R/s⌉ < R`: **el rango
decrece estrictamente**. Como es un entero positivo, la iteración alcanza
`R ≤ s` en un número finito de pasos, y ahí aplica el Lema 3.

**Teorema 4 (completitud del refinamiento).** El refinamiento DRS
uniforme **sin corte de profundidad** termina siempre, y toda hoja que
produce cumple una de estas dos condiciones:

1. `m ≤ t` (hoja por tamaño), o
2. es monovaluada (hoja por rango degenerado), y por tanto ya ordenada.

**En consecuencia, el algoritmo de comparación del paso 4 nunca recibe un
bin de más de `t` elementos.**

*Demostración.* Por el Lema 3', el rango observado decrece estrictamente
en cada nivel mientras el bin siga refinándose, y está acotado
inferiormente por 1. Luego el proceso termina. Al terminar, un bin o bien
no se refinó (`m ≤ t`, caso 1) o bien alcanzó `R = 1` (`observedMin ==
observedMax`, caso 2), o bien alcanzó `R ≤ s` y sus hijos son
monovaluados (caso 2 en el nivel siguiente). No hay tercera salida. ∎

**Corolario 4.1 (el resultado que responde al encargo).** El peor caso
Θ(n log n) de DRS v8 **no proviene del algoritmo**. Proviene
exclusivamente de la línea

```
if (count <= targetElementsPerBin_ || depth >= MAX_SUBDIVISION_DEPTH) → hoja
```

es decir, de la **tercera** condición de salida, que es artificial y que
entrega a Introsort bins de tamaño arbitrario. Sin ella, Introsort y
QuickSort son código inalcanzable: toda hoja tiene `≤ t` elementos o está
ordenada.

**Corolario 4.2 (refinamiento puro, `t = 1`).** Con `t = 1` el paso 4
desaparece por completo: todas las hojas son monovaluadas y la
ordenación se produce **enteramente por el refinamiento**, sin ejecutar
ni una comparación entre elementos. La filosofía se preserva
íntegramente: el paso 4 («resolver cada subproblema») se resuelve
trivialmente para un multiconjunto de un solo valor. **La respuesta a
"¿puede el refinamiento ser el algoritmo de ordenación y no un
preprocesado?" es: sí, demostrablemente, y sin cambiar ninguna fórmula.**

El §5.3 muestra que `t = 1` **no** es la mejor elección de constante, y
calcula el óptimo.

**Observación 4.3 (una asimetría real en la especificación actual).** El
cálculo del nivel superior **sí** acota el número de intervalos por el
rango:

```
initialBins = min( ceil(n/t), range )        // computeRangeParameters
```

pero el refinamiento **no**:

```
splits = ceil(count/t)                       // refine(), sin acotar por observedRange
```

Bajo el Lema 3, acotar `splits` por `observedRange` en `refine()` no es
una optimización: es la condición que activa la terminación en una sola
pasada para bins de rango pequeño. La regla del nivel superior y la del
refinamiento deberían ser la misma regla; hoy no lo son. Esto es una
observación matemática sobre la especificación, no una propuesta de
implementación.

---

## 4. Por qué la filosofía es la fuente de la linealidad

**Teorema 6 (el dividendo de la colocación aritmética).** Considérese una
pasada de refinamiento sobre `m` elementos con `s` intervalos.

- **(a) Fronteras arbitrarias (cuantiles, muestreo, densidad).** Si las
  fronteras son valores arbitrarios dependientes de los datos,
  localizar un elemento entre `s` intervalos requiere `Ω(log₂ s)`
  comparaciones en el modelo de comparación (cota de teoría de la
  información: hay `s` respuestas posibles). Coste de la pasada:
  `Θ(m log₂ s)`. Bits de **rango de clave** ganados: a lo sumo
  `log₂ s`. **Coste por bit: Θ(1).**
- **(b) Fronteras aritméticamente indexables (A3).** El índice se calcula
  en O(1). Coste de la pasada: `Θ(m)`. Bits ganados: `log₂ s`.
  **Coste por bit: Θ(1/log₂ s).**

*Demostración.* (a) es la cota de árbol de decisión para localización.
(b) es inmediata del modelo de coste. ∎

**Corolario 6.1.** Cualquier operador que garantice equilibrio debe
colocar sus fronteras en cuantiles empíricos, que son valores arbitrarios
dependientes de los datos, luego viola A3, luego cae en el caso (a), y
por el Teorema 2 su coste total es `Θ(n log n)`. **El equilibrio y la
linealidad son mutuamente excluyentes en esta familia.**

**Corolario 6.2 (Sample Sort no es una vía).** El algoritmo obtenido al
sustituir el operador uniforme por selección de separadores por muestreo
es exactamente Sample Sort. Es un algoritmo conocido (lo que el encargo
excluye), es comparativo, y es `Θ(n log n)`. Se descarta por tres motivos
independientes.

**Teorema 7 (el reescaneo de min/max es la identidad de DRS).** Sea `𝒰⁺`
el operador que recalcula `(a,b)` de cada hijo con un escaneo O(m), y
`𝒰⁻` el que hereda como rango del hijo la anchura del intervalo del padre
(cota superior gratuita). Entonces:

- Bajo `𝒰⁻`, la profundidad está gobernada por `log_s(R₀)`: el rango
  decrece exactamente en factor `s` por nivel, **pase lo que pase con los
  datos**.
- Bajo `𝒰⁺`, la profundidad está gobernada por `log_s(diam(cúmulo))`,
  donde `diam` es el diámetro real de los datos del hijo, que puede ser
  arbitrariamente menor que la anchura heredada.

*Consecuencia.* `𝒰⁻` es un esquema de resolución **fija**: divide el
universo en potencias de `s` sin mirar dónde están los datos. Eso es,
estructuralmente, un radix sort de base `s` con base adaptativa solo en
la raíz. **`𝒰⁺` es lo que hace que DRS sea adaptativo**: es el mecanismo
por el cual un cúmulo denso escondido dentro de un intervalo ancho se
detecta y se resuelve inmediatamente, en vez de tras `log_s(anchura)`
niveles.

**Esto tiene una lectura retrospectiva importante.** La iteración v8
intentó eliminar el escaneo de min/max de `refine()` (obtenerlo como
subproducto de la pasada del padre) y midió que no compensaba. El
Teorema 7 dice algo más fuerte que "no compensa": esa transformación
**cambia la clase de complejidad adaptativa del algoritmo**, de
`log_s(diam real)` a `log_s(rango heredado)`. Que empeorara el
rendimiento no fue mala suerte de implementación; el escaneo es
constitutivo, no accesorio.

---

## 5. Cotas ajustadas de la familia DRS

### 5.1 Cota superior

**Teorema 8 (cota superior).** Sea DRS uniforme sin corte de
profundidad, con `σ(m) = s ≥ s_min` para todo bin que se refina, y umbral
de hoja `t`. Entonces

```
  Coste(S)  =  O( n · mín( t, ⌈w / log₂ s_min⌉ )  +  n · t )
```

donde el segundo término es el coste de la ordenación local
(Insertion Sort sobre hojas de tamaño ≤ t, `O(t²)` por hoja, `O(n·t)`
agregado).

*Demostración.* El primer término tiene dos cotas independientes sobre la
profundidad. **(i)** Por el Corolario 1.2, `d ≤ ⌈w/log₂ s_min⌉`.
**(ii)** Por el Lema 3', cada nivel reduce el rango, y por A1 cada
división produce al menos dos intervalos no vacíos (el elemento igual a
`a` cae en `I₁`, el igual a `b` cae en `I_j` con `j ≥ 2` siempre que
`R > ⌈R/s⌉`), luego el mayor hijo tiene `≤ m − 1` elementos: en el peor
caso un bin de tamaño `m` sobrevive `m − t` niveles. Como los bins que
se refinan tienen `m > t`, y en el peor caso `m = t+1`, esto da `d ≤ t`
para los bins pequeños. El trabajo por nivel es a lo sumo `n`. ∎

**Corolario 8.1.** Para `w` constante (64 bits) y `t` constante,
`Coste(S) = O(n)` **en el peor caso, sin excepciones ni supuestos
distribucionales.**

### 5.2 Cota inferior: el adversario recursivo

**Teorema 9 (cota inferior).** Para cualquier política `σ(m) ≤ c·m` con
`c` constante, existe una entrada de tamaño `n` sobre la que DRS uniforme
realiza

```
  Ω( n · mín( t, (w − log₂ t) / log₂(2ct) ) )
```

operaciones de refinamiento.

*Construcción.* Se fabrica un grupo de `g = 2t` elementos que fuerza `k`
niveles degenerados, y se replican `n/g` grupos independientes (separados
entre sí lo suficiente como para caer en bins distintos en el nivel 0).

Dentro de un grupo, sean los valores `v₁ < v₂ < … < v_g`. Queremos que en
la pasada `i` el bin actual `{v₁,…,v_{g−i+1}}`, de tamaño `mᵢ = g−i+1`,
se parta dejando `{v₁,…,v_{g−i}}` como único hijo grande y `{v_{g−i+1}}`
solo. La condición es que el diámetro del subconjunto grande sea menor
que la anchura del intervalo:

```
  R_{i+1}  ≤  ⌈ Rᵢ / sᵢ ⌉ ,     sᵢ = σ(mᵢ) ≤ c·mᵢ ≤ 2ct
```

Construyendo de abajo arriba con `R_{k+1} = t` y `Rᵢ = R_{i+1}·sᵢ`, se
obtiene `R₁ ≤ t·(2ct)^k`. La restricción del universo `R₁ ≤ 2^w` da

```
  k  ≤  (w − log₂ t) / log₂(2ct)
```

y la restricción de elementos da `k ≤ g − t = t`. El trabajo del grupo
es `Σᵢ mᵢ ≥ k·t`, y hay `n/(2t)` grupos: total `≥ n·k/2`. ∎

**Corolario 9.1 (la cota es ajustada).** Los Teoremas 8 y 9 coinciden
salvo constantes:

```
  Coste_peor(DRS)  =  Θ( n · mín( t , w / log₂(c·t) ) )
```

### 5.3 Elección óptima de las constantes

Sustituyendo en el Teorema 8 y sumando el coste de la ordenación local
(`≈ t/4` operaciones por elemento para Insertion Sort sobre hojas de
tamaño medio `t`), el coste por elemento en el peor caso es

```
  κ(t, c)  =  w / log₂(c·t)   +   t/4
```

Con `w = 64` y `c = 1` (es decir, `s = m`: un intervalo por elemento,
que es el máximo asequible según el §5.4):

| `t` | `w/log₂ t` | `t/4` | **κ** |
|---|---|---|---|
| 4 | 32,0 | 1 | 33,0 |
| 8 | 21,3 | 2 | 23,3 |
| **16** | **16,0** | **4** | **20,0** |
| 24 | 14,0 | 6 | 20,0 |
| **32** | **12,8** | **8** | **20,8** |
| 64 | 10,7 | 16 | 26,7 |
| 128 | 9,1 | 32 | 41,1 |

El óptimo es **plano en `t ∈ [16, 32]`**, con `κ ≈ 20`. Dos lecturas:

- `t = 1` (Corolario 4.2, refinamiento puro sin comparaciones) da
  `κ = ∞` en esta fórmula porque `log₂(c·1) = 0`: **el refinamiento puro
  es demostrablemente subóptimo como constante**, aunque sea correcto y
  conceptualmente puro. Un caso base de comparación de tamaño constante
  no es una concesión: es la elección óptima.
- El mínimo teórico cae en `t ≈ 16–32`. El barrido empírico de
  `ANALYSIS_v5.md` §2.1 midió, con 27 repeticiones en 3 ejecuciones
  independientes, que `target ≈ 19` batía a `target = 64` por un 18–23 %.
  **La coincidencia entre el óptimo teórico del peor caso y el óptimo
  empírico del caso medio no está demostrada y puede ser fortuita**
  —son dos regímenes distintos— pero es una consistencia que merece ser
  registrada y contrastada.

### 5.4 El techo de la familia: por qué no se puede ir más rápido

**Teorema 10 (asequibilidad del factor de ramificación).** En un nivel
que procesa bins `B₁…B_k` con `Σmᵢ = N`, si cada bin usa `sᵢ`
intervalos y el nivel debe **enumerar sus hijos en orden creciente**
(exigido por A1 para que la unión sea una concatenación), el nivel cuesta
`Ω(N + Σsᵢ)`. Los bits ganados son `Σ mᵢ log₂ sᵢ`. Por tanto los bits por
unidad de coste se maximizan en `sᵢ = Θ(mᵢ)`, y el coste por bit es
`Θ(mᵢ / log₂ mᵢ)`.

*Consecuencia.* `s = Θ(m)` es la política óptima dentro de la familia, y
no hay margen para mejorar la constante `w/log₂(ct)` sin violar A1 (la
enumeración ordenada) o sin pagar `ω(m)` por pasada.

**Observación 10.1 (dónde está la frontera de la investigación).** El
único modo conocido de escapar de `Ω(Σsᵢ)` es no enumerar los intervalos
vacíos: registrar durante la pasada de conteo la lista de índices
tocados (a lo sumo `m`) y procesar solo ésos. Pero entonces hay que
**ordenar esa lista de índices** para respetar A1 — un subproblema de
ordenación de `m` enteros sobre un universo de tamaño `s`. Eso es
recursión sobre el mismo problema, y es exactamente el punto donde vive
la literatura de ordenación de enteros (Han: `O(n log log n)`
determinista; Han–Thorup: `O(n √(log log n))` esperado), con maquinaria
—hashing, ordenación empaquetada— que abandona A1 y A3. **DRS no puede
cruzar esa frontera sin dejar de ser DRS.**

---

## 6. El espacio de diseño de la nueva generación

Por la Definición 1.2, el espacio completo de operadores DRS-admisibles
es **el conjunto de reindexaciones monótonas de `[0, R)` computables en
O(1)**. Ésta es la caracterización formal de "nueva familia de algoritmos
de ordenación" que el encargo pide. Los candidatos no triviales:

| Operador | `g(u)` | `s` | Adversario | Bits/pasada | Veredicto |
|---|---|---|---|---|---|
| `𝒰_legacy` | `⌊u/⌈R/s⌉⌋` | `⌈m/t⌉`, tope de profundidad 6 | cúmulo denso + atípico lejano | `≥1` | **Θ(n log n)** por el corte, no por el operador |
| `𝒰_∞` | igual | `⌈m/t⌉`, sin tope | igual | `≥1` | `Θ(n·mín(t,w))`. Correcto y lineal, constante mala |
| `𝒰_Θ` | igual | `Θ(m)` | igual | `≥log₂(ct)` | `Θ(n·w/log₂(ct))`. **Óptimo de la familia** (Thm 10) |
| `𝒰_sat` | igual | `mín(Θ(m), R)` | igual | igual | `𝒰_Θ` + terminación en 1 pasada si `R ≤ s` (Lema 3) |
| `𝒢` geométrico | `⌊log₂(u+1)⌋` | `⌈log₂ R⌉` | datos **uniformes** (mitad superior en un intervalo) | `≤ log₂ w ≈ 6` | Adversario **complementario** al de `𝒰`; ver 6.1 |
| `𝒬` cuantiles | búsqueda | libre | ninguno | `log₂ s` | **Descartado**: viola A3 ⟹ `Θ(n log n)` (Cor. 6.1) |
| `ℰ` escalada especulativa | `𝒰` con `s` creciente | adaptativo | igual | igual | **Dominado** por `𝒰_Θ`; ver 6.2 |

### 6.1 Los adversarios de `𝒰` y `𝒢` son complementarios — y aun así no hay salvación

- El adversario de `𝒰` (anchura uniforme) es el **cúmulo denso con un
  atípico lejano**: la anchura `R/s` es enorme comparada con el diámetro
  del cúmulo.
- El adversario de `𝒢` (anchura geométrica) es lo contrario: datos
  **uniformemente repartidos** sobre `[a,b]`, de los cuales la mitad cae
  en el último intervalo `[R/2, R)`.

Es tentador proponer un operador híbrido que alterne `𝒰` y `𝒢` por
nivel, o que elija entre ambos. **No funciona**, y la razón es el
siguiente teorema, que es el resultado negativo más fuerte del
documento.

**Teorema 3 (no existe divisor universal).** Sea `(σ, g)` **cualquier**
operador DRS-admisible —incluidos los híbridos, los dependientes del
nivel, y los aleatorizados con aleatoriedad ajena a los datos. Para todo
`m` y todo `α < 1`, existe un multiconjunto de tamaño `m` sobre el que la
partición produce un hijo con más de `α·m` elementos.

*Demostración.* Por A2, las fronteras quedan determinadas por
`(a, b, m)` **antes de mirar los datos interiores**. Con `s` intervalos
cubriendo un rango `R`, existe por el principio del palomar un intervalo
`I*` de anchura `≥ R/s`. Constrúyase el multiconjunto colocando `a`, `b`,
y los `m − 2` elementos restantes dentro de `I*` (posible siempre que
`R/s ≥ 1`, es decir, siempre). El hijo correspondiente a `I*` tiene
`m − 2 > α·m` elementos para `m` suficientemente grande. ∎

**Corolario 3.1 (la dirección propuesta es inconstruible).** El criterio
del encargo —«una subdivisión solo es aceptable si demuestra progreso
sobre el tamaño del mayor subproblema»— **no puede satisfacerse por
ningún operador DRS-admisible**. No existe una subdivisión alternativa
que sí lo cumpla y a la que recurrir cuando la primera falla.

**Corolario 3.2 (la aleatorización no ayuda).** Desplazar aleatoriamente
las fronteras no cambia nada: un cúmulo de diámetro `δ ≪` anchura cae
íntegro en un intervalo **para cualquier desplazamiento**. El problema es
de *resolución*, no de *alineación*. Solo se resuelve con anchura `< δ`,
es decir, con `s > R/δ`, que es inasequible por el Teorema 10 cuando
`δ = 1` y `R = 2^w`.

**Corolario 3.3 (qué se puede hacer con el criterio, y qué no).** El
criterio de progreso por tamaño es **calculable gratis** —la pasada de
conteo produce todos los tamaños de hijo antes de mover un solo dato— y
por tanto sirve como **diagnóstico**. Pero por el Corolario 3.1 no puede
usarse como **guarda**: al detectar el fallo, las únicas salidas son
(a) aceptar la división igualmente —que es DRS actual, y que el
Corolario 1.1 justifica—, (b) entregar el bin a un algoritmo de
comparación —que es DRS actual con el corte de profundidad, y que es
exactamente la causa del Θ(n log n) que se quiere eliminar—, o
(c) abandonar A2 o A3 —que por el Corolario 6.1 devuelve Θ(n log n).
**Las tres salidas son peores que no comprobar nada.**

### 6.2 Escalada especulativa: por qué queda dominada

Idea: contar con `s₀ = ⌈m/t⌉`; si el mayor hijo supera `α·m`, **duplicar
`s` y volver a contar** (la pasada de conteo es idempotente y aún no se
han movido datos); repetir hasta lograr equilibrio o hasta el tope de
asequibilidad `c·m`.

*Análisis.* Por el Teorema 3, el adversario garantiza que la escalada
falla en todos sus intentos, y entonces se han pagado `log₂(ct)` pasadas
de conteo para acabar usando `s = c·m` —exactamente el `s` que `𝒰_Θ`
habría usado directamente en una sola pasada. Luego `ℰ` está **dominada
por `𝒰_Θ` en el peor caso**, con factor `log₂(ct)`.

En el caso medio `ℰ` ahorra memoria de contadores frente a `𝒰_Θ`. Ese es
un argumento del modelo de coste con jerarquía de memoria, no del modelo
RAM de este documento, y por tanto queda fuera de su alcance. Se registra
como conjetura práctica, no como resultado.

### 6.3 El único margen real: la envolvente de las dos cotas

Los Teoremas 8 y 9 acotan la profundidad por `mín(t, w/log₂(ct))`. Las
dos ramas del mínimo se activan en regímenes distintos:

- La rama `w/log₂(ct)` domina cuando `t` es grande: el problema es el
  **presupuesto de bits**.
- La rama `t` domina cuando `t` es pequeño: el problema es que un bin de
  `t+1` elementos solo puede perder un elemento por nivel.

El punto de cruce es `t ≈ w/log₂(ct)`, que para `w = 64`, `c = 1` da
`t ≈ 12–16` — coincidente con el óptimo de §5.3 por una vía
independiente. **Ése es el punto de diseño de la nueva generación, y es
un resultado, no una elección.**

---

## 7. ¿Es posible O(n) estricto? Respuesta formal

Hay que separar dos afirmaciones que se confunden con facilidad.

**Afirmación I: «DRS es O(n) en el peor caso para palabra de ancho fijo».**
**VERDADERA Y DEMOSTRADA** (Corolario 8.1). Con `w = 64` y `t = 16`, el
Teorema 8 da un peor caso de `≈ 20n` operaciones, sin ningún supuesto
sobre la distribución de entrada. No requiere cambiar la filosofía; solo
requiere eliminar el corte de profundidad. Éste es el mismo sentido en el
que radix sort es lineal.

**Afirmación II: «DRS es O(n) para `w` arbitrario (creciente con `n`)».**
**FALSA en el estado actual del conocimiento, y no por culpa de DRS.**
El Teorema 9 da `Ω(n·w/log₂(ct))`, que para `w = ω(log n)` no es lineal.
Y esto no es un defecto reparable: ningún algoritmo conocido ordena `n`
enteros de `w` bits arbitrarios en `O(n)`. Las mejores cotas conocidas
son `O(n log log n)` determinista y `O(n √(log log n))` esperado, y ambas
usan hashing y ordenación empaquetada, incompatibles con A1 y A3.
**Exigir a DRS la Afirmación II es exigirle que resuelva un problema
abierto de la teoría de ordenación de enteros.**

### 7.1 Caracterización exacta

**Teorema 11.** DRS uniforme sin corte de profundidad, con `s = Θ(m)` y
`t` constante, ordena en `O(n)` **si y solo si** `w = O(log n)`.

*Demostración.* (⟸) Por el Teorema 8, coste `≤ n·(w/log₂(ct) + t/4)`.
Con `w = O(log n)` y `s = Θ(m)`, en el nivel superior `log₂ s = log₂ n`,
luego la profundidad es `O(w/log n) = O(1)`. (⟹) Por el Teorema 9 con
`t` constante, el coste es `Ω(n·w/log₂(ct))`, que es `ω(n)` si
`w = ω(1)` con `t` constante. ∎

**`w = O(log n)` significa: claves acotadas polinómicamente en `n`.** Es
exactamente el régimen en el que counting sort y radix sort son lineales,
y es el régimen de casi todo uso real (n = 10⁶ y claves de 64 bits está
en la frontera práctica: `w/log₂ n = 64/20 ≈ 3,2` pasadas).

### 7.2 Cambio mínimo de filosofía necesario para O(n) con `w` arbitrario

Ordenados de menor a mayor daño a la identidad de DRS:

| Relajación | Qué compra | Qué cuesta |
|---|---|---|
| **(R0)** Restringir la entrada a `w = O(log n)` | O(n) estricto, demostrado (Thm 11) | **Nada de la filosofía.** Es una hipótesis sobre la entrada, no un cambio del algoritmo. **Recomendada.** |
| **(R1)** Abandonar A1 (concatenación ordenada) para permitir enumerar solo los intervalos no vacíos | Elimina el término `Σsᵢ` (Thm 10) | Obliga a ordenar los índices tocados: recursión sobre el mismo problema. Sin hashing no compra nada. |
| **(R2)** Abandonar A3 (colocación aritmética) | Permite equilibrio garantizado | `Θ(n log n)` por el Cor. 6.1. **Estrictamente peor.** Descartada. |
| **(R3)** Abandonar A2 (autonomía de rango) usando hashing / claves empaquetadas | `O(n log log n)` det., `O(n√(log log n))` esp. | Ya no es una partición por rango observado: **deja de ser DRS**. |

**Ninguna relajación da O(n) con `w` arbitrario.** (R0) da O(n) sin tocar
nada. (R3) mejora la dependencia en `w` pero disuelve la identidad. **La
conclusión honesta es que la Afirmación I es el techo alcanzable, y ya
está alcanzado.**

---

## 8. La nueva generación: especificación matemática

Reuniendo los resultados, el algoritmo que se deduce —lo llamo **DRS-Ω
(refinamiento terminal)**— queda especificado enteramente por teoremas,
sin una sola decisión arbitraria:

> **Operador.** Uniforme sobre el rango observado (A1–A3 intactos).
>
> **Número de intervalos.**
> `s_B = mín( máx(2, ⌈c·m_B⌉), R_B )`
> — el `Θ(m)` viene del Teorema 10 (óptimo de bits por unidad de coste);
> el `mín(·, R_B)` viene del Lema 3 (terminación en una pasada), y es la
> misma regla que el nivel superior ya aplica y el refinamiento no
> (Obs. 4.3).
>
> **Rango de cada hijo.** Recalculado por escaneo propio, no heredado
> (Teorema 7: es lo que hace a DRS adaptativo en vez de radix de base
> fija).
>
> **Condición de hoja.** `m_B ≤ t` **o** `R_B = 1`. **Sin corte de
> profundidad** (Teorema 4: el corte es la única fuente del Θ(n log n)).
>
> **Umbral de hoja.** `t ∈ [16, 32]` (§5.3 y §6.3, dos derivaciones
> independientes que coinciden).
>
> **Paso 4.** Insertion Sort sobre hojas de tamaño `≤ t`. Por el
> Teorema 4, QuickSort e Introsort son inalcanzables y desaparecen del
> algoritmo.
>
> **Diagnóstico, no guarda.** El tamaño del mayor hijo se registra
> (es gratis) pero **no condiciona ninguna decisión** (Corolario 3.3).

**Invariantes que satisface, todos demostrados arriba:**

- **(I1)** Terminación: el rango observado decrece estrictamente en cada
  nivel de refinamiento (Lema 3').
- **(I2)** Presupuesto: `Σ log₂ sᵢ ≤ w` a lo largo de todo camino
  raíz-hoja (Corolario 1.1).
- **(I3)** Completitud: ninguna hoja con `m > t` llega al paso 4
  (Teorema 4).
- **(I4)** Peor caso: `Θ(n·mín(t, w/log₂(ct)))`, ajustado
  (Teoremas 8 y 9).
- **(I5)** Concatenación: las hojas teselan `[0,n)` en orden ascendente;
  el paso 5 no compara nada (A1).

**Diferencia con DRS v8, en una línea:** se elimina el corte de
profundidad, se acota `splits` por el rango observado, se sube el
factor de ramificación de `m/t` a `Θ(m)`, y se baja `t` de 64 a ~16–32.
Cuatro constantes y una condición. **La filosofía no se toca en ningún
punto: A1, A2 y A3 se mantienen exactamente.**

---

## 9. Predicciones falsables

Un documento teórico sin condiciones de refutación no vale nada. Cada
teorema implica una predicción medible con la instrumentación que el
proyecto ya tiene:

| Predicción | Se refuta si… | Métrica |
|---|---|---|
| Sin corte de profundidad, ninguna hoja supera `t` (Thm 4) | aparece un solo bin `> t` en el paso 4 sobre cualquier entrada | `algorithmUsage()` registra `QuickSort` o `Introsort` ≠ 0 |
| La profundidad no depende de `n` (Cor. 1.2) | la profundidad máxima crece con `n` a rango constante | `maxSubdivisionDepth()` sobre el barrido de 14 tamaños |
| El presupuesto de bits se agota (Cor. 1.1) | `Σ log₂ sᵢ > w` en algún camino | instrumentar `s` por nivel; hoy no se registra |
| El adversario del Thm 9 existe | el dataset construido por §5.2 **no** produce `≈ min(t, w/log(2ct))` niveles | `workByDepth()` sobre ese dataset |
| El equilibrio no es alcanzable (Thm 3) | algún operador admisible mantiene `max hijo ≤ αm` en todas las entradas | distribución de `reductionPct`, ya registrada en `DRSMetrics` |
| El óptimo está en `t ∈ [16,32]` (§5.3) | el barrido 2D de `t` da un mínimo fuera de ese rango | `comparisons()` + `workByDepth()`, no tiempo |

**Nótese que ninguna de estas predicciones necesita el cronómetro.** Son
todas sobre contadores deterministas — que es precisamente lo que el
informe v9 señalaba que los análisis previos no estaban usando.

**Además: el dataset adversario del §5.2 no existe en el proyecto.** Los
ocho datasets actuales son todos "razonables" y ninguno activa el peor
caso. Cualquier afirmación sobre el peor caso de DRS hecha hasta ahora
—incluida la del informe v9— está sin verificar experimentalmente por
falta de la entrada adecuada. Construir ese generador es, en mi opinión,
la pieza que más falta le hace al proyecto.

---

## 10. Problemas abiertos

1. **¿Existe un operador admisible con `g` no uniforme cuyo peor caso
   mejore `Θ(n·w/log₂(ct))`?** El Teorema 10 acota los bits por unidad de
   coste, pero no descarta que una `g` no uniforme obtenga mejor
   constante en la envolvente de §6.3. No lo he resuelto.

2. **¿Se puede aprovechar el conteo del padre para elegir el `s` del
   hijo?** El vector de conteo del padre es información sobre la
   distribución que A2 permite usar *dentro del propio bin padre*. Si el
   `s` del hijo pudiera derivarse de él sin escaneo adicional, se
   ahorraría una pasada. No he encontrado una formulación que no viole
   A2 al aplicarse al hijo.

3. **Análisis del caso medio.** Todo este documento es peor caso. Para
   datos con densidad `n/R` constante, la ocupación es Poisson y la
   profundidad esperada es 1–2; el análisis probabilístico exacto de la
   profundidad esperada bajo `s = Θ(m)` está sin hacer y es donde vive
   el rendimiento real.

4. **¿Es `w = O(log n)` necesario además de suficiente para O(n) dentro
   de la familia?** El Teorema 11 lo demuestra para `s = Θ(m)` y `t`
   constante. La versión con `t` creciente con `n` no la he analizado.

---

## 11. Conclusión

**A la pregunta de investigación —¿puede el refinamiento ordenar sin
necesitar un algoritmo clásico de comparación sobre hojas grandes?— la
respuesta es sí, y ya era sí.** El Teorema 4 lo demuestra sobre las
fórmulas actuales, sin modificarlas: el rango observado decrece
estrictamente, la anchura llega a 1, y en ese momento cada intervalo
contiene un único valor distinto. El único motivo por el que hoy llegan
hojas grandes a Introsort es una constante, `MAX_SUBDIVISION_DEPTH = 6`,
que corta el proceso antes de que termine. **La nueva generación de DRS
no requiere un mecanismo nuevo: requiere dejar de interrumpir el que ya
tiene.**

**A la dirección propuesta —exigir progreso sobre el tamaño del mayor
subproblema— la respuesta es que debe descartarse, por dos motivos
independientes y ambos demostrados.** Es inconstruible (Teorema 3:
ninguna regla que dependa solo del rango observado puede garantizarlo), y
sería contraproducente aunque fuese construible (Teorema 2: el progreso
por tamaño impone Θ(n log n), que es peor que el Θ(n·w/log s) que el
progreso por rango ya consigue). La división `999.999 / 1` no es un
fracaso del refinamiento: es una pasada que consume `log₂ s` bits de un
presupuesto total de `w`, y por eso solo puede repetirse un número de
veces independiente de `n`.

**Y sobre la identidad de DRS:** el análisis produjo un resultado que no
esperaba al empezar. La regla «refinar únicamente usando el rango
observado del propio intervalo», que el encargo declara innegociable por
razones de identidad, resulta ser también la condición técnica que hace
posible la linealidad (Teorema 6) y la que hace que DRS sea adaptativo en
lugar de un radix de base variable (Teorema 7). **La restricción que
parecía el precio de mantener la identidad es, de hecho, el mecanismo del
que depende el rendimiento.** No hay que elegir entre las dos cosas.
