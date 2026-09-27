M = 1 << 64
def u64(x): return x % M
mn, mx = -(1<<63), (1<<63)-1
t, count = 64, 1000
splits = (count + t - 1)//t
observedRange = u64(u64(mx) - u64(mn) + 1)
nis = (observedRange + splits - 1)//splits or 1
def binIndex(v):
    off = u64(u64(v) - u64(mn))
    i = off // nis
    return splits-1 if i >= splits else i
print("observedRange(uint64) =", observedRange, " newIntervalSize =", nis)
for v,label in [(mn,"min"),(mn+1,"min+1"),(0,"cero"),(mx-1,"max-1"),(mx,"max")]:
    print(f"  binIndex({label}) = {binIndex(v)}   (splits={splits})")
