# 🎯 objective.md — Stratum Sort

> **Brújula del proyecto.** Este archivo dice *hacia dónde* va Stratum Sort. Las incidencias de
> GitHub son la cola de trabajo; este archivo decide qué entra en ella, en qué orden y **con qué
> pruebas** se acepta. `research/ALGORITHM.md` sigue siendo la referencia técnica y
> `research/BENCHMARKS.md` la metodología de medida: aquí no se repiten, se convierten en reglas.

Última revisión: 2026-10-22

---

## 1. Hito actual: **Θ(n) estricto en todos los casos y ganar a `std::sort` en todos los benchmarks**

Dos metas, **las dos a la vez**: una no puede conseguirse a costa de la otra.

| Meta | Qué significa exactamente (§3 y §4 lo concretan) |
|---|---|
| 🧮 **A. Θ(n) estricto** | El peor caso es lineal con una constante **explícita y pequeña**, sin ninguna fase `O(m log m)` escondida detrás de un tamaño de hoja "constante" pero enorme, y la medida de trabajo total por elemento es plana en todos los datasets, adversarios incluidos |
| 🏁 **B. Ganar a `std::sort`** | En **todos** los datasets de la batería (§4), en **las dos plataformas de referencia** y en todos los tamaños desde `10⁴`, por encima del umbral de ruido |

### 📍 Dónde estamos (medido)

`n = 10⁶`, `int64_t`. Ratio = tiempo de Stratum Sort ÷ tiempo de `std::sort` (menor que 1 = gana).

| Dataset | macOS arm64 · Apple M4 · libc++ (README, v0.10.0) | Linux x86_64 · Xeon 2.1 GHz · GCC 13 / libstdc++ (2026-10-22, 1 sesión) |
|---|---|---|
| RandomUniform | 0,68 ✅ | 0,58 ✅ |
| SortedAscending | **5,95 ❌** | **1,31 ❌** |
| SortedDescending | **3,86 ❌** | **1,98 ❌** |
| ManyRepeated | 0,94 ⚪ empate (ruido ~18 %) | 0,83 ⚪ (ruido sin medir en esta máquina) |
| NormalGaussian | 0,80 ✅ | 0,62 ✅ |
| Concentrated | 0,90 ⚪ (ruido ~12 %) | 0,74 ✅? (ruido sin medir) |
| SmallRangeManyEl | 0,36 ✅ | 0,68 ✅ |
| HugeRangeFewEl | 0,67 ✅ | 0,57 ✅ |
| FullRangeExtremes | 0,67 ✅ | 0,55 ✅ |
| AdversarialPeeling | **1,66 ❌** | 0,97 ⚪ empate |

Lo que hay que cerrar: **ordenado**, **invertido** y **adversarial** en las dos plataformas, los
**empates** y los datasets nuevos del §4 que todavía no se han medido. Respecto a la meta A, el
README ya demuestra `Θ(n)` bajo H1–H4, pero con hojas de hasta ~5,7·10⁶ elementos ordenadas con
introsort (`O(m log m)` con `m` "constante"), y el contador de comparaciones del adversario crece con
exponente 1,0137. Eso es lineal en sentido formal, no en sentido estricto.

### ✅ Definición de terminado

- [ ] Meta A cumplida según el §3 (demostración actualizada + medida de trabajo total).
- [ ] Meta B cumplida según el §4 en **las dos plataformas**, con la tabla de veredictos en `research/baselines/`.
- [ ] Ningún paso aceptado ha empeorado otro caso (puerta del §5 superada en cada PR).
- [ ] `make test`, `make sanitizers` y `make fuzz N=200000` en verde, con la batería ampliada incluida en el fuzz.
- [ ] README (tablas de rendimiento y *Limitations*), `research/ALGORITHM.md`, `BENCHMARKS.md` y `CHANGELOG.md` al día.

---

## 2. 🧬 La idea del algoritmo (lo que no se puede cambiar)

Estas cinco propiedades **son** Stratum Sort. Una solución que rompa alguna no es una mejora: es otro
algoritmo, y solo puede decidirlo el autor (ver §6, condición de límite).

1. **Estratos de anchura igual con el rango local**: cada nivel reparte en intervalos de igual anchura
   calculados con el mínimo y el máximo **del propio intervalo**, no los globales.
2. **Profundidad acotada por los bits de la clave**, no por `n`.
3. **Sin fusión**: las piezas ya quedan en orden y en su sitio; el resultado se obtiene concatenando.
4. **Claves enteras de hasta 64 bits**, sin comparador del usuario.
5. **Biblioteca de cabeceras en C++17 portable**: sin dependencias, sin intrínsecos ni código
   específico de una plataforma.

**Permitido sin consultar** (no rompe la idea): pasadas previas `O(n)` con salida temprana
(detectar ya ordenado, invertido o constante), otra estrategia para las hojas siempre que sea lineal
o esté acotada por una constante pequeña, ajuste de constantes y de λ en función de `n` **dentro de
los techos de H2**, disposición de memoria y buffers, y cambios en las fases que mantengan las cinco
propiedades.

**Fuera de la idea** (piden decisión del autor): fusionar ejecuciones ya ordenadas con una mezcla
(merge), cambiar de familia de algoritmo para algunos casos (por ejemplo, delegar en `std::sort` o
en una radix pura), código específico de una plataforma o con intrínsecos, y ampliar el tipo de clave.

---

## 3. 🧮 Meta A — "Θ(n) estricto"

| | Criterio verificable |
|---|---|
| **A1. Formal** | `ALGORITHM.md` demuestra que el coste total es ≤ `C·n` para **toda** entrada y **todo** parámetro admisible, con `C` **escrita como número** y sin términos `O(m log m)` cuyo `m` crezca con `2^(w/(D+1))` o con los techos de λ/t. Hoy las hojas que agotan la profundidad pueden llegar a ~5,7·10⁶ elementos con introsort: deben terminarse con un método lineal coherente con la idea, o acotarse a un tamaño pequeño y fijo (≤ 384, la banda actual de quicksort). |
| **A2. Instrumento** | El build de investigación tiene un **contador de trabajo total**: lecturas y escrituras de elementos y comparaciones en **todas** las fases (`analyze`, `scanRange`, distribución, refinamiento, unión y ordenaciones locales). Hoy solo se cuentan las comparaciones de las hojas; el README admite que una superlinealidad en la distribución sería invisible. |
| **A3. Empírico** | Con ese contador, para **cada** dataset del §4 y todos los casos de `AdversaryBattery` (hoy 8): exponente ajustado ≤ **1,01** entre `n = 10³` y `n = 10⁷`, y trabajo/n ≤ `C` en todos los puntos. |
| **A4. Revisión adversaria** | La demostración incluye una sección que intenta romperla: entradas que maximicen la profundidad, el tamaño de hoja y la reentrada en refinamiento. |

> Aviso del propio historial (`DESIGN_HISTORY.md`, O8): **quitar el límite de profundidad costó hasta un
> +109 % y se revirtió**. La meta A no se cumple por la vía ingenua. Si la única forma de cumplirla es
> perder benchmarks, eso es una condición de límite (§6), no una licencia para empeorar el rendimiento.

---

## 4. 🏁 Meta B — "ganar en todos los benchmarks"

### Batería (lo que cuenta como "todos")

| Grupo | Datasets |
|---|---|
| Actuales (10) | RandomUniform, SortedAscending, SortedDescending, ManyRepeated, NormalGaussian, Concentrated, SmallRangeManyEl, HugeRangeFewEl, FullRangeExtremes, AdversarialPeeling |
| Nuevos (los añade SS-0) | **NearlySorted** (1 % de intercambios aleatorios), **SortedPlusTail** (ordenado + 1 % aleatorio al final), **OrganPipe**, **Sawtooth** (tramos de 1000), **FewDistinct** (2, 16 y 256 valores), **AllEqual** y los **8 casos** de `AdversaryBattery` |
| Tamaños | `n = 10⁴, 10⁵, 10⁶, 10⁷` (meta B). `n = 10³` solo exige **no perder**: con hojas de inserción en los dos lados, el coste fijo domina. Cambiarlo es decisión del autor. |
| Tipos | `int64_t` (principal) y `uint32_t` |
| Generador adversario | Se **reconstruye contra la configuración que se mide**: es una trampa documentada en `BENCHMARKS.md` |

### Qué es "ganar"

Mediana del ratio ≤ **1 − umbral**, y los rangos (mínimo–máximo) de **3 sesiones** de 9
repeticiones alternadas **no se solapan** con 1,0. El umbral es **6 %** (medido en `BENCHMARKS.md`),
salvo en los datasets con un suelo de ruido conocido mayor: **ManyRepeated 18 %**, **Concentrated
12 %**. Para Linux, SS-1 mide su propio suelo de ruido por dataset, y se usa el mayor de los dos.

### Plataformas de referencia

| Plataforma | Quién mide |
|---|---|
| **Linux x86_64**, GCC/libstdc++ | Las tareas programadas, en la nube (es donde se ejecutan) |
| **macOS arm64, Apple M4**, Apple clang/libc++ — la máquina de referencia del README | **El autor**, con un solo comando (§7) |

Ganar en una plataforma y perder en la otra **no** cumple la meta: el propio historial muestra que las
conclusiones se invierten entre estas dos máquinas.

---

## 5. 🔒 Puerta de no regresión (la "revisión estricta")

**Toda PR que toque `include/`, `datasets/` o `benchmarks/` debe superar la puerta completa**, con la
evidencia pegada en la PR. Sin evidencia, la PR se rechaza aunque los tests pasen.

| # | Comprobación | Rechazo si… |
|---|---|---|
| G1 | **Corrección**: `make test`, `make sanitizers` y `make fuzz N=200000` (con la batería ampliada) | Cualquiera falla |
| G2 | **Contadores** (build de investigación), batería completa, antes y después | El trabajo total sube en **cualquier** dataset sin una justificación escrita en la PR, o el exponente de la meta A empeora |
| G3 | **Reloj** (solo build de *release*, `make timings` o el A/B de SS-0), A/B **alternado** en una sesión, 9 repeticiones × 3 sesiones, batería × tamaños | (a) algún dataset empeora frente a la versión anterior por encima de su suelo de ruido, o (b) algún dataset que estaba en "gana" deja de estarlo |
| G4 | **Memoria auxiliar** (asignaciones y bytes de heap por `sort()`: `make research && ./build/research_PhaseProfile`) | Sube más de un 5 % sin aprobación del autor |
| G5 | **Demostración**: si el cambio toca una fase, se actualiza el lema correspondiente de `ALGORITHM.md` | Cambia una fase y la demostración no |
| G6 | **Adversario nuevo**: cada mecanismo nuevo viene con un intento de entrada que lo derrote (p. ej., para una pasada previa de orden: "ordenado salvo el último elemento"), y esa entrada se añade a la batería | No hay intento de adversario |
| G7 | **Honestidad de medida**: nunca se citan tiempos del build de investigación; los rangos se publican junto a las medianas | Se cita un tiempo de *research* o solo medianas |

La tabla de G3 sale en formato fijo: dataset × tamaño, con **ratio frente a `std::sort`**, **ratio
frente a la versión anterior**, rango y veredicto (`gana` / `empate` / `pierde`).

---

## 6. 🛑 Condición de límite: cuándo parar y avisar

Para cada brecha abierta (un par dataset–plataforma que no gana, o un criterio de la meta A sin
cumplir), se declara **límite** cuando ocurre **cualquiera** de estas tres cosas:

1. **Dos intentos bien fundados** (cada uno con su hipótesis, medido con la puerta del §5) se han
   rechazado, y no queda una tercera vía **sustancialmente distinta** dentro de la idea; o
2. hay un **argumento** (demostración, cota o medición del techo de mejora con `ImprovementCeiling`)
   de que cerrar la brecha exige romper alguna propiedad del §2; o
3. cerrar la brecha hace **perder otro caso** sin remedio (dos metas incompatibles dentro de la idea).

Al declararlo:

- Se abre una incidencia `[LÍMITE] <brecha>` con las etiquetas **`objective-limit`** y **`claude-blocked`**:
  qué se intentó, las tablas, el argumento y qué haría falta **fuera** de la idea para cerrarla, con su
  coste estimado.
- El paso afectado se marca **🛑** en este archivo, en la siguiente PR que lo toque.
- El informe final de la tarea que lo detecta **empieza con la línea**
  `⚠️ LÍMITE ALCANZADO — Stratum-Sort: <brecha>`, para que la notificación al autor lo muestre.
- Nadie sigue intentando cerrar esa brecha hasta que el autor decida (cambiar la idea, aceptar el
  límite o reformular la meta).

---

## 7. 👤 Qué tienes que hacer tú (y qué no)

| Revisión | Quién | Cómo |
|---|---|---|
| Corrección, contadores, memoria, demostración y adversarios (G1, G2, G4–G7) | **Automática**: el Mantenedor la ejecuta, el PR Reviewer la exige y el Auditor la repite cada semana | Sin intervención |
| Reloj en Linux (G3) | **Automática** | Sin intervención (las VM en la nube tienen ruido: SS-1 lo mide) |
| Reloj en el **M4** (G3, segunda plataforma) | **Tú, y solo en PRs que cambian rendimiento** | `make timings` en el M4 y pegar la salida en la PR antes de fusionarla (~2 min). Opcional más adelante: un *runner* de GitHub en el M4 para automatizarlo |
| Decidir en un `[LÍMITE]` | **Tú** | Responder en la incidencia: cambiar la idea, aceptar el límite o reformular la meta |

**No hace falta que revises el código a mano**: la puerta del §5 está pensada para que una PR que
empeore cualquier caso no pueda pasar. La única medida que no se puede automatizar desde la nube es la
del M4.

---

## 8. 🧭 Hoja de ruta (en orden — una PR por paso, todas con la puerta del §5)

| ID | Paso | Hecho cuando | Estado |
|---|---|---|---|
| SS-0 | Instrumentos | Batería ampliada del §4 en `timings`, en el A/B y en el fuzz; salida en CSV; un objetivo `make` de **A/B alternado** que compara dos *commits* y saca la tabla de veredictos de G3; **contador de trabajo total** (A2) en el build de investigación; todo documentado en `BENCHMARKS.md`. `BENCHMARKS.md` cita `make baseline` y `make profile`, que hoy no existen en el `Makefile`: se añaden como objetivos (sobre `research_ReferenceBaseline` y `research_PhaseProfile`) o se corrige la documentación | ⬜ |
| SS-1 | Líneas base | `research/baselines/v0.10.0-linux-x86_64.csv` con la batería completa, 3 sesiones y el suelo de ruido de Linux por dataset (3 ejecuciones del mismo binario). El CSV del M4 lo aporta el autor; hasta entonces, el veredicto del M4 queda "pendiente" | ⬜ |
| SS-2 | Orden global | Pasada previa `O(n)` con salida temprana para ascendente, descendente y constante. **Gana** en SortedAscending, SortedDescending y AllEqual en las dos plataformas, sin regresión (G3) en ningún otro dataset, con "ordenado salvo el último" en la batería (G6) | ⬜ |
| SS-3 | Casi ordenados | Si la línea base muestra pérdidas en NearlySorted, SortedPlusTail, OrganPipe o Sawtooth: cerrarlas **dentro de la idea**. Si solo se cierran con una mezcla (merge), es un `[LÍMITE]` (§6) | ⬜ |
| SS-4 | Adversarial | Diagnóstico con `PhaseProfile` y todos los casos de `AdversaryBattery` (hoy 8); gana en AdversarialPeeling y en los 8 casos, en las dos plataformas | ⬜ |
| SS-5 | Θ(n) estricto | A1–A4 del §3 cumplidos (sin la banda `O(m log m)`), sin perder ningún benchmark | ⬜ |
| SS-6 | Empates → victorias | ManyRepeated, Concentrated y cualquier otro empate pasan a "gana" con el umbral de su suelo de ruido | ⬜ |
| SS-7 | Escala | Gana en todos los tamaños `10⁴…10⁷`; si λ pasa a depender de `n`, sigue dentro de los techos de H2 y la demostración lo recoge | ⬜ |
| SS-8 | Cierre | README (tablas de las dos plataformas, *Limitations* sin "pierde en ordenado"), `ALGORITHM.md`, `BENCHMARKS.md` y `CHANGELOG.md` al día. Publicar la versión lo decide el autor | ⬜ |

Leyenda: ⬜ pendiente · 🟡 en curso · ✅ hecho · 🛑 límite declarado (§6).

## 9. 🚫 Fuera de alcance

Claves de coma flotante, cadenas o pares clave/valor; estabilidad; paralelismo; `-march=native`,
LTO o intrínsecos; cambiar la API pública; publicar versiones (lo hace el autor).

## 10. ♻️ Código reutilizable de otros repos

Ninguno: es el único proyecto en C++ y no tiene dependencias por diseño. No forzar copias.

---

## 11. 🤖 Cómo usan este archivo las tareas programadas

| Tarea | Qué hace con este archivo |
|---|---|
| **Auditor de portafolio** (domingos) | (1) **Auditoría estricta** si en la semana hubo cambios en `include/`, `datasets/` o `benchmarks/`: repite G1–G3 sobre `master` en Linux contra la última línea base, comprueba que el README no afirma nada que la medición contradiga y busca regresiones fusionadas. (2) Evalúa la **condición de límite** (§6) de cada brecha abierta y, si se cumple, abre el `[LÍMITE]` y encabeza su informe con la línea `⚠️ LÍMITE ALCANZADO`. (3) Propone el siguiente paso ⬜ como incidencia `claude-candidate` + `objective` |
| **Mantenedor autónomo** (L/X/V) | Implementa solo incidencias `claude-ready`, ejecuta la puerta del §5 y pega la tabla de G3 y los contadores de G2 en la PR. Si durante el trabajo se cumple la condición de límite, para, etiqueta la incidencia con `objective-limit` + `claude-blocked` y encabeza su informe con la línea `⚠️` |
| **PR Reviewer** | Rechaza (Cambios solicitados, severidad alta) toda PR de `include/`, `datasets/` o `benchmarks/` sin la evidencia de G1–G7 o que la incumpla. Si la PR cambia el rendimiento, recuerda que falta la medida del M4 (§7) |

**Orden de prioridad:** 🔴 P0 (resultado incorrecto, fallo de sanitizers, regresión fusionada) → 🧭
siguiente paso de la hoja de ruta → 🟡 otros bugs demostrados → ⚪ mejoras sueltas.
Una mejora que ni sirve a la hoja de ruta ni arregla un bug **no se propone**.
