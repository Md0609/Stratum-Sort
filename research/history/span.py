import random
M = 1 << 64
# Propuesta: representar el rango por su SPAN = max-min (nunca desborda),
# en vez de range = span+1 (desborda cuando span = 2^64-1).
#   width     = span//s + 1
#   binIndex  = (v-min)//width          <- se afirma que SIEMPRE cae en [0,s-1]
#   monovalor <=> span == 0
#   tope      : if span < s: s = span+1  -> anchura 1, todos los hijos monovaluados
bad_idx = bad_w = bad_mono = 0
cases = 0
random.seed(7)
spans = [0,1,2,3,4,5,63,64,65,999,1000,(1<<32)-1,(1<<63),(1<<64)-2,(1<<64)-1]
spans += [random.randrange(0, M) for _ in range(3000)]
for span in spans:
    for s in [2,3,5,16,17,64,1000,2048,65536]:
        cases += 1
        seff = span+1 if span < s else s
        width = span//seff + 1
        assert width >= 1
        if width > M: bad_w += 1
        # indice del maximo desplazamiento posible
        if span//width > seff-1: bad_idx += 1
        # anchura 1 <=> cada bucket un unico valor distinto
        if seff == span+1 and width != 1: bad_mono += 1
        # comprobacion exhaustiva de monotonia en spans pequenos
        if span <= 300:
            prev = -1
            for off in range(span+1):
                i = off//width
                if i < prev or i > seff-1: bad_idx += 1
                prev = i
print(f"casos={cases}  indice fuera de [0,s-1]: {bad_idx}   width desborda: {bad_w}   anchura!=1 con tope: {bad_mono}")
print("equivalencia con la formula vieja ceil((span+1)/s) donde no desborda:",
      all((span//s + 1) == -((-(span+1))//s) for span in range(0,5000) for s in (2,3,7,64,1000)))
