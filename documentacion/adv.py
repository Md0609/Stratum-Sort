import math
# Adversario recursivo: grupos de tamano g que pierden 1 elemento por nivel.
# Restriccion de universo: los grupos son disjuntos en valor.
#   bits disponibles por grupo = w - log2(n/g)
# Coste de bits del descenso de g hasta t con sigma(m)=ceil(m/t):
#   sum_{m=t+1..g} log2(ceil(m/t))
def bits_needed(g, t):
    return sum(math.log2(math.ceil(m/t)) for m in range(t+1, g+1))

def work_per_elem(g, t):
    return sum(range(t+1, g+1)) / g

def best_g(t, n, w):
    best = (0, None)
    g = t+1
    while g <= 20000:
        budget = w - math.log2(n/g)
        if bits_needed(g, t) > budget:
            break
        best = max(best, (work_per_elem(g, t), g))
        g += 1
    return best

n, w = 10**6, 64
print(f"n={n}, w={w}   sigma(m)=ceil(m/t)   [coste por elemento en el peor caso]")
print(f"{'t':>5} {'g*':>7} {'refine':>9} {'hoja t/4':>9} {'TOTAL':>9}")
for t in (1,2,4,8,16,32,64,128):
    wpe, g = best_g(t, n, w)
    leaf = t/4
    print(f"{t:>5} {str(g):>7} {wpe:>9.1f} {leaf:>9.1f} {wpe+leaf:>9.1f}")

print()
print("Contraste con la formula del documento  kappa(t) = w/log2(t) + t/4 :")
for t in (4,8,16,32,64,128):
    print(f"  t={t:>4}  documento={w/math.log2(t)+t/4:>6.1f}")
