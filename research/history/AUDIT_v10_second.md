# Segunda auditoría técnica — código limpio

**Revisor externo, partida en frío sobre `algoritmo/` y `tests/`. No se
asume ninguna conclusión de la primera auditoría; donde coinciden, es
porque el hallazgo se ha vuelto a producir de forma independiente.**

Veredicto adelantado: **el código está sustancialmente mejor y sigue sin
estar listo para publicar**, por razones distintas a las de la primera
auditoría. Los defectos funcionales están cerrados; lo que queda es
estructural y de entorno.

---

## 1. Intento de refutación

### 1.1 Lo que se rompió

**D-1. `T = bool` producía un error incomprensible.** `std::is_integral<bool>`
es cierto, así que el `static_assert` lo aceptaba, y la compilación
fallaba 200 líneas más abajo dentro de `memcpy` porque `std::vector<bool>`
es la especialización empaquetada y no tiene `data()`. **Corregido**
durante esta ronda con un `static_assert` propio y mensaje explícito.

**D-2. `width == 0` escapaba de `computeRangeParameters`.** Lo encontró
una aserción añadida, no la lectura. Con `binCount == 1` y
`span == 2^64−1` la anchura exacta es `2^64`, que da la vuelta a 0. El
código era correcto **sólo** porque `countAndPlace` cortocircuita
`numBuckets == 1` antes de dividir — orden de comprobaciones, no
contrato. **Corregido**: la postcondición `width >= 1` ahora se cumple
incondicionalmente.

**D-3. La garantía fuerte era falsa en el build de investigación.** Lo
encontró el test de excepciones. `endPhase("merge")` asigna memoria
después de escribir la salida. **Documentado por configuración** y fijado
por un test; no se "arregla" un build de laboratorio.

**D-4. La configuración llamada «producción» no era una release.** El
`Makefile` nunca definió `NDEBUG`. **Corregido**: tres configuraciones
explícitas.

### 1.2 Lo que no se pudo romper, y por qué

| Ataque | Resultado |
|---|---|
| Recursión infinita en `refine` | Imposible. El span decrece estrictamente (width ≤ ⌊span/s⌋ < span para s ≥ 2) y hay tope de profundidad. Doble garantía. |
| Recursión profunda en `quickSort`/`introSortImpl` | Ambas recursan sobre la partición menor e iteran sobre la mayor: pila O(log k). |
| Desbordamiento de `span` | `(uint64)max − (uint64)min` es exacto y modular para cualquier par de enteros. |
| Desbordamiento de `width` | Cerrado por D-2. Postcondición `width ≥ 1` con `assert`. |
| Índice fuera de rango | Property 2, con `assert(idx < numBuckets)` en el bucle. Verificado además por ASan sobre 19 casos límite. |
| `memcpy` con solapamiento | Imposible por construcción (tres objetos distintos); ahora con `assert(&src != &dst)` que lo declara. |
| Tipos exóticos | `char`, `signed char`, `unsigned`, `uint8_t`, `int16_t`, `uint64_t` abarcando su tipo entero: **todos correctos** bajo ASan+UBSan. |
| Configuraciones extremas | `λ=1,t=1` y `λ=10⁹,t=10⁹` sobre 200.000 y 50.000 elementos: correctos. |
| Fronteras del despachador | k = 64, 65, 384, 385, ya ordenado y ya invertido: correctos. |
| Fallo de asignación | 40 puntos de fallo forzados; la entrada queda intacta en release en los 40. |
| Estado obsoleto entre llamadas | Una instancia reutilizada sobre tamaños 50.000→0 en orden mezclado: correcta. |

**No he conseguido producir una entrada incorrecta.**

### 1.3 Hipótesis ocultas que siguen ahí

1. **`w` constante.** Toda la afirmación de linealidad lo asume. Está
   declarado en el header.
2. **`n < 2^64/λ`.** `(length + λ − 1)` desbordaría antes. Irrelevante en
   la práctica, **no comprobado ni comentado**.
3. **`std::size_t` de 64 bits.** `bucketOfScratch_` gasta 8 bytes por
   elemento; en 32 bits el consumo y los límites cambian. Sin comentario.
4. **La caché es de ~4 MiB.** El λ por defecto está ajustado a esta
   máquina y a n≈10⁶. **Documentado** en `Config.hpp`, pero la API no
   ofrece forma de adaptarlo salvo pasando λ a mano.
5. **Recursión de un solo hilo y en profundidad.** De ello depende la
   reutilización de los scratch. Comentado, **no impuesto**.

---

## 2. Arquitectura y complejidad

Sin cambios respecto de la primera auditoría; las demostraciones siguen
en pie y ahora están **junto al código que las usa**, no dispersas.

- **Terminación:** el span decrece estrictamente. Demostrado en `refine`.
- **Cobertura (teselado):** las hojas cubren `[0,n)` en orden. Ahora
  **verificado ejecutablemente** por `verifyTiling`, que recorre el árbol
  en cada `sort()` del build de tests.
- **Profundidad:** `min(w, D) = 6`.
- **Residuo máximo al sort por comparación:** `λ·2^(w/D) ≈ 52.016`,
  constante independiente de `n`.
- **Mejor caso Θ(n)**, constante ≈ 6; **caso medio Θ(n)**, `(λ+1)/4 = 8,25`
  comparaciones por elemento; **peor caso Θ(n)** con constante acotada.
- **Memoria ≈ 25n bytes** = 3,1× la entrada.

---

## 3. Legibilidad

**Mejorado:** cero referencias de versión, un solo idioma en
`algoritmo/`, tipos de índice portables, literales nombrados, sin coma
flotante, sin miembros muertos, sin ramas inalcanzables, y comentarios que
explican *por qué* y *qué invariante*, no *qué hace la línea*.

**Lo que sigue mal:**

**L-1. `countAndPlace` tiene 10 parámetros.** Es la función más importante
y la más difícil de invocar. Un tipo «rango» (`buffer`, `start`, `count`)
reduciría a 6.

**L-2. `refine` sigue siendo la función más larga** (~90 líneas) con
cuatro salidas y seis bloques `#ifdef`. La lógica real son 15 líneas.

**L-3. `sort()` tiene más `#ifdef` que código.** Diez bloques de
instrumentación alrededor de seis llamadas. Un `PhaseTimer` RAII las
reduciría a cero.

**L-4. `debugPartitionOnly` sigue duplicando `sort()`.** Marcado con una
advertencia, pero la duplicación existe y divergirá.

**L-5. El árbol `RefinedRange` no aporta nada.** Ninguna fase posterior
usa la estructura, sólo la lista de hojas en orden. Es una asignación por
nodo interno y dos recorridos recursivos para transportar información que
cabe en un vector plano.

---

## 4. Deuda técnica restante

### Imprescindible antes de publicar
1. **`README` del algoritmo.** No existe. Un tercero tiene que leer 800
   líneas de plantilla para entender cinco fases.
2. **Especificación independiente del código**: las cuatro fórmulas, las
   condiciones de hoja y los invariantes, implementables sin leer C++.
3. **Reproducir la batería en al menos una segunda máquina.** El proyecto
   tiene evidencia medida de que las conclusiones se invierten entre
   plataformas; publicar números de una sola es engañoso.
4. **Declarar la licencia.** No hay ninguna.

### Recomendable
5. Eliminar el relleno de ceros muerto (~10 % en baja cardinalidad).
6. `PhaseTimer` RAII para vaciar `sort()` de `#ifdef`.
7. Aplanar el árbol a una lista de hojas (L-5).
8. Reducir los parámetros de `countAndPlace` (L-1).
9. Unificar `debugPartitionOnly` con `sort()` (L-4).
10. Comprobar o comentar el límite `n < 2^64/λ`.

### Opcional
11. `bucketOfScratch_` a `uint32_t`.
12. Un `struct` de parámetros con nombre (riesgo de intercambio).
13. Explicar el residuo `k=2` vs `k=3` de la anomalía.
14. Barrer `D` y `LOCAL_PARTITION_CUTOFF`, que nadie ha medido nunca.

---

## 5. Preparación para publicación

| Necesidad de un tercero | Estado |
|---|---|
| Leer el código | **Aceptable.** Un idioma, nombres coherentes, comentarios que explican el porqué |
| Entenderlo | **Insuficiente.** Falta el README y un diagrama de cómo interactúan λ, t y D |
| Implementarlo en otro lenguaje | **Insuficiente.** Falta la especificación independiente |
| Verificar la demostración | **Bueno.** Los lemas están junto al código, y el teselado se comprueba en ejecución |
| Reproducir los benchmarks | **Parcial.** `make baseline` y `make profile` funcionan; falta documentarlos y falta una segunda máquina |
| Confiar en la corrección | **Bueno.** 257 comprobaciones de contrato en dos configuraciones, ASan+UBSan, garantía fuerte verificada forzando fallos de asignación |

---

## 6. Veredicto

**¿El algoritmo está terminado?** Sí. No he encontrado ninguna entrada que
produzca un resultado incorrecto, y los cuatro defectos que encontré esta
ronda están cerrados.

**¿Está preparada para publicarse?** **No, pero por razones distintas.**
La primera auditoría bloqueaba por defectos y comentarios falsos; los
cuatro puntos que quedan son de **empaquetado**: README, especificación,
licencia y una segunda máquina. Ninguno toca el código.

**¿Queda alguna optimización importante (>5 %) sin explorar?** Una medida
y no aplicada: el relleno de ceros muerto, ~10 % en baja cardinalidad.
Y dos parámetros que **nadie ha medido nunca**: `MAX_SUBDIVISION_DEPTH` y
`LOCAL_PARTITION_CUTOFF`.

**¿Qué haría antes de publicar?** Los cuatro puntos imprescindibles. Nada
de código.

**¿Qué haría después?** Segunda y tercera máquina; exponer λ como algo
derivable del tamaño de caché; y sólo entonces volver al rendimiento.

---

## 7. Nota sobre el proceso

Dos de los cuatro defectos de esta ronda —`width == 0` y la garantía
fuerte— **no los encontró la lectura, los encontró código nuevo**: una
aserción y un test. La primera auditoría los tuvo delante y no los vio.
La lección práctica es que un invariante escrito en un comentario no está
comprobado; escribirlo como `assert` o como test es lo que lo comprueba.
