import math
def bits_needed(g,t): return sum(math.log2(math.ceil(m/t)) for m in range(t+1,g+1))
def wpe(g,t): return sum(range(t+1,g+1))/g
def best(t,n,w):
    b=0.0; g=t+1
    while g<=200000:
        if bits_needed(g,t) > w - math.log2(n/g): break
        b=max(b,wpe(g,t)); g+=1
    return b

t=4
print("Teorema 11 afirma: O(n) <=> w = O(log n).")
print("Si fuese cierto, con w = c*log2(n) el coste por elemento debe ser O(1) (acotado).\n")
for c,label in [(1.0,"w =   log2 n"),(1.5,"w = 1.5 log2 n"),(2.0,"w = 2   log2 n"),(3.0,"w = 3   log2 n")]:
    row=[]
    for n in (10**4,10**6,10**8,10**10,10**12,10**14):
        w=c*math.log2(n)
        row.append(f"{best(t,n,w):6.1f}")
    print(f"{label}:  n=1e4..1e14 -> " + " ".join(row))
print("\n(t=4 fijo; 'coste por elemento' del refinamiento en el peor caso)")
print("\nUmbral real: w = log2(n) + delta")
for d in (0,1,2,4,8,16):
    row=[]
    for n in (10**4,10**6,10**8,10**10,10**12):
        row.append(f"{best(t,n,math.log2(n)+d):5.1f}")
    print(f"  delta={d:>3}: " + " ".join(row))
