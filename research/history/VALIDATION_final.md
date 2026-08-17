# Validación final antes de publicar

Relectura completa del código como revisor externo, sin apoyarse en
ninguna conclusión anterior. Objetivo declarado: encontrar defectos
reales, no confirmar que el código está bien.

**Resultado: cinco defectos reales, todos corregidos.** Ninguno afecta al
orden que produce el algoritmo. Cuatro afectan a lo que el proyecto
*afirma*, que para algo que va a publicarse es igual de grave.

---

## D-1. La tabla de rendimiento del README no era del build que decía

**Severidad: alta.** Es el defecto que más lejos habría llegado.

El README encabezaba su tabla con "Release build" y remitía a
`make baseline` para reproducirla. Pero `benchmarks/BenchmarkRunner.hpp`
contiene:

```cpp
#ifndef DRS_ENABLE_METRICS
#error "BenchmarkRunner.hpp requires the research build..."
#endif
```

`make baseline` **no puede** compilarse sin instrumentación: lee
`sorter.metrics()`. La tabla publicada era, por construcción, del build de
laboratorio.

Es literalmente la trampa que este proyecto ya había documentado en el
paso 5, donde un cambio dio −19,8% instrumentado y ~0% en producción. La
regla se escribió y luego se incumplió en el sitio más visible del
repositorio.

**Medido.** Producción frente a investigación, mismos datasets, misma
máquina:

| dataset | research | release | Δ |
|---|---|---|---|
| ManyRepeated | 2,87 | 2,31 | −20% |
| FullRangeExtremes | 11,75 | 10,41 | −11% |
| NormalGaussian | 12,77 | 11,65 | −9% |
| RandomUniform | 11,14 | 10,57 | −5% |
| SmallRangeManyEl | 1,72 | 1,95 | **+13%** |

No es un factor constante, y ni siquiera tiene signo constante. No se
podía corregir escalando la tabla.

**Corrección.** `benchmarks/ReleaseTimings.cpp`, compilado con
`PROD_CXXFLAGS`, con un `#error` si alguien lo compila con métricas, y sin
contadores porque no los tiene. `make timings` es ahora la única fuente de
cifras citables; `make baseline` conserva los contadores, que sí son
exactos. La tabla del README está remedida con la primera.

## D-2. `bucketOf()` era código muerto

**Severidad: media** (mantenibilidad, no corrección).

Declarado en la cabecera, definido en el `.tpp`, llamado desde ningún
sitio: el bucle caliente repetía la expresión a mano. Peor que inofensivo,
porque tres comentarios —incluida la demostración del invariante de
teselado— lo citaban como si fuera el mecanismo real. Un lector que
siguiera el comentario acabaría leyendo una función que no se ejecuta.

**Corrección.** Pasa a tomar escalares por valor y a ser el único sitio
donde vive el cálculo. El motivo de no pasar el `Partitioning` está
documentado: por referencia, el compilador no puede descartar el aliasing
y el bucle se degrada.

**Verificado que no cuesta tiempo**, porque el proyecto ya tiene el
antecedente de un refactor de legibilidad que costó 6–15%. Alternando
contra el commit anterior en configuración de producción, cuatro datasets,
tres rondas: diferencias entre −0,4% y +1,5%, rangos solapados, muy por
debajo del umbral de 6%.

`DRSMetrics::recordComparison()` (singular) tampoco lo usaba nadie:
eliminado.

## D-3. La cifra de memoria auxiliar era falsa para claves estrechas

**Severidad: media.** Una garantía documentada, incorrecta.

Se documentaba como `3n * sizeof(T)`. El coste real es:

```
  2n · sizeof(T)        los dos buffers
+  n · sizeof(size_t)   un índice de bin por elemento
+  O(n / λ)             cursores y el árbol de refinamiento
```

El término del medio **no escala con `T`**. Para `int64_t` sale ~3,1× la
entrada y la cifra publicada era correcta por coincidencia; para `int8_t`
son ~10×. Corregido en la cabecera, el README y `ALGORITHM.md`, y añadida
la nota de que ése es el punto donde el diseño paga por ser genérico.

## D-4. Doce programas sin regla de compilación, uno de ellos roto

**Severidad: media.**

`experiments/`, `analysis/` y `benchmarks/` contenían doce fuentes con
`main()` que ningún objetivo del Makefile compilaba. Como nadie los
compilaba, nadie se enteró de que `experiments/TargetSweep.cpp` llevaba un
literal de cadena sin cerrar —resto de un `sed` anterior— y no compilaba
en absoluto. El mismo `sed` había truncado otros tres comentarios, uno de
ellos dejando media frase en inglés y media en español.

Esto importa más de lo que parece: cada uno de esos programas produjo
exactamente un número del que el proyecto depende. Un estudio cuyo código
ya no compila no se puede reejecutar, y un resultado que nadie puede
reejecutar no es evidencia, es una anécdota.

**Corrección.** Regla genérica `build/study_%` y objetivo `make studies`,
incluidos en `make all` para que la próxima rotura salga a la primera.
`tests/edge_sanitizers.cpp` tenía el mismo problema: ahora es
`make sanitizers`.

## D-5. Contaminación del namespace público

**Severidad: baja.**

`floorLog2` y `verifyTiling` eran funciones libres en `drs`. Ambas son
detalles internos y ninguna forma parte de la API. Pasan a `drs::detail`.
En el mismo barrido: rutas obsoletas (`documentacion/`, `experimentos/`,
`analisis/`) en comentarios, y la última referencia a un nombre de versión
histórico (`pre-v10`) en `Config.hpp`, que no significa nada para quien
llega de fuera.

---

## Lo que se buscó y no se encontró

Para que el "no hay más defectos" signifique algo, esto es lo que se
revisó explícitamente sin encontrar nada:

- **Aritmética de rango.** Los tres sitios que restan valores
  (`planPartition`, `planRefinement`, `bucketOf`) operan en `uint64_t`. El
  único punto donde `width` podía desbordar es `binCount == 1`, que
  `planPartition` normaliza a 1 con una postcondición aseverada. La
  propiedad 2 garantiza que el índice cae siempre en rango, y hay una
  aserción en el bucle interno que lo comprueba en cada elemento.
- **Los centinelas de `partition()`.** Los dos bucles no comprueban sus
  límites. La mediana de tres deja `arr[left] <= pivote <= arr[right]` y
  aparca el pivote en `right-1`; esos tres elementos son los centinelas.
  La precondición (`right - left >= 2`) está aseverada, y las dos únicas
  llamadas la cumplen con margen, porque sólo particionan por encima de
  `LOCAL_PARTITION_CUTOFF = 12`.
- **Índices con signo.** `Index` es `ptrdiff_t`, no `long`. El bucle de
  construcción del montículo depende de que sea con signo para terminar
  cuando `left == 0`; lo es.
- **Reutilización de los scratch entre niveles.** `bucketOfScratch_` y
  `writeCursorScratch_` son miembros compartidos por toda la recursión.
  `countAndPlace` termina con ambos antes de volver, y la recursión entra
  en los hijos después; no hay solapamiento.
- **Garantía fuerte frente a excepciones.** Nada escribe en el array del
  llamante hasta la fase de unión, y esa fase no asigna. En el build de
  investigación sí (el cierre de fase asigna después de escribir), y esa
  asimetría está documentada y fijada por un test.
- **`memcpy` en la unión.** Origen y destino son tres objetos distintos,
  `T` es trivialmente copiable por el `static_assert` de clase, y el rango
  destino es exactamente el de la hoja por el invariante de teselado, que
  además se comprueba ejecutablemente en builds con aserciones.

## Evidencia nueva

Se añadió `tests/differential_fuzz.cpp` (`make fuzz`), que no existía. Es
la prueba que faltaba: las otras dos suites comprueban lo que al autor se
le ocurrió comprobar.

Sortea tipo de elemento (los ocho enteros), tamaño, λ, `t` y forma de la
distribución, y exige coincidencia exacta con `std::sort`. Se compila con
aserciones y bajo ASan/UBSan, así que cada caso ejecuta tres oráculos
independientes a la vez: la comparación contra `std::sort`, los
invariantes internos y la vista que tienen los sanitizers de la aritmética
de rango — que es justo lo que hace falta para sostener la afirmación
central sobre el span.

**100 000 casos, ningún desacuerdo, ningún fallo de aserción, ningún aviso
de sanitizer.** Semilla fija, así que un fallo futuro se reproduce.

Que no encontrara nada es, en sí, el resultado que se buscaba.

## Veredicto

**Listo para publicar.**

No porque no se hayan encontrado defectos —se encontraron cinco— sino
porque los que se encontraron eran del tipo que se encuentra: uno de
código muerto, uno de higiene de namespace, uno de infraestructura de
compilación y dos de afirmaciones publicadas que no se correspondían con
lo medido. Ninguno tocaba el orden que produce el algoritmo, y ese sigue
sin fallar en 100 000 casos aleatorios con tres oráculos encima.

La razón principal para confiar en el código no es que se haya leído bien.
Es que dos de los defectos históricos del proyecto los encontró una
aserción y un test, no una lectura, y una auditoría completa línea a línea
se los había pasado por alto. Lo que hace publicable este código es que
sus invariantes están escritos de forma ejecutable: la aserción del índice
en rango, la comprobación de teselado, el test que fija la asimetría entre
builds y ahora el fuzz diferencial. Un invariante escrito sólo en un
comentario no está comprobado.

Lo que queda sin verificar está dicho en el README y no se disimula: las
cifras vienen de una sola máquina, `MAX_SUBDIVISION_DEPTH = 6` funciona
pero nunca se ha barrido, y las tres constantes de ordenación local son
valores de libro de texto que en este proyecto no se han medido.
