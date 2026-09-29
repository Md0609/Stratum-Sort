#pragma once

// Included by detail/Engine.hpp; not meant to be included directly.

// ============================================================
// The in-place engine: the same partition, no element buffer
// ============================================================
// Engine (Engine.hpp) distributes a node of m elements into ceil(m / lambda)
// buckets in ONE pass, writing into a partner buffer of m elements. Both
// facts cost memory: the buffer is Theta(n), and a one-pass s-way scatter
// needs all s write cursors at once, which is Theta(n / lambda) at the top.
//
// Neither is required by the partition. This engine produces the same
// buckets - bucket for bucket, the same elements in each - under a
// different schedule:
//
//   * IN PLACE. Every pass is an American-flag permutation: count, turn the
//     counts into bucket boundaries, then follow cycles, swapping each
//     element straight into the next free slot of its bucket. No partner
//     buffer; one element in a register.
//
//   * RADIX AT MOST 2^L PER PASS. A split into s buckets is done over the
//     bits of the bucket index, most significant first, at most
//     L = INPLACE_RADIX_BITS bits per pass: ceil(log2(s) / L) passes of at
//     most 2^L sub-buckets. The bucket index is exactly Engine's
//     floor((key - origin) / width); a pass groups buckets by its top bits,
//     so after the last pass every bucket occupies its own contiguous range,
//     in bucket order - the partition of Engine, reached in several passes.
//
// What the order inside a bucket is differs (American flag is not stable),
// so this engine serves the unstable sorts, and the stable ones whose
// element is its own key (equal keys are then identical bit patterns, and
// no order among them is observable).
//
// MEMORY. Only the passes on the current depth-first path hold counters: a
// pass of radix r keeps its r + 1 group boundaries while its groups are
// visited, and every pass shares 3 * 2^L temporaries. Along any
// root-to-leaf path the bucket indices consumed add up to at most
// w + 2D + 1 bits (research/ALGORITHM.md section 14.5, Lemma M1), and a
// pass of b bits holds 2^b + 1 <= b * max(3, (2^L + 1) / L) boundaries (a
// width-1 node at the end of a path, counted instead of spread, holds at
// most 2^b for its b bits), so the live counters never exceed
//
//     3 * 2^L  +  (w + 2D + 1) * max(3L, 2^L + 1) / L        (inPlaceArenaFor)
//
// For w = 64, D = 6, L = 10: 10 964 counters, 43 856 bytes with 32-bit
// counters, whatever n is. The block pass adds (2^L + 3) blocks of
// INPLACE_BLOCK_BYTES, 526 KB at 8-byte elements, and works without them.
// The recursion is as deep as the passes plus the refinement levels, at
// most w + 2D + 1 + 2(D + 1) frames, and the leaves are sorted in place.
//
// TIME. A pass over c elements costs O(c + r) and r <= c is enforced (a
// sub-range of at most INPLACE_SMALL elements is ordered by insertion
// instead), so every pass is O(c). An element crosses at most
// w + 2D + 1 passes (each consumes >= 1 bit of its path, Lemma M1); with
// the balanced split below, at most ceil(log2(s) / L) per refinement level
// and (w + 2D + 1) / L + D + 1 in all: 14 for 64-bit keys. Theta(n) in the
// worst case under the hypotheses of ALGORITHM.md section 8.0, like Engine,
// with a constant that counts passes instead of buffer copies.
// ============================================================

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {
namespace detail {

// Smallest b with 2^b >= x (x >= 1).
inline unsigned bitsFor(uint64_t x) {
    unsigned b = 0;
    while (b < 64 && (uint64_t{1} << b) < x) ++b;
    return b;
}

// Counters of the in-place engine's arena for keys of keyBits bits (see
// the header; research/ALGORITHM.md section 14.6, Lemma M2): the floor of
// every memory budget.
inline std::size_t inPlaceArenaFor(unsigned keyBits) {
    constexpr std::size_t radix = std::size_t{1} << INPLACE_RADIX_BITS;
    constexpr std::size_t perBit = (radix + 1 > 3 * INPLACE_RADIX_BITS) ? radix + 1 : 3 * INPLACE_RADIX_BITS;
    const std::size_t pathBits = keyBits + 2 * MAX_SUBDIVISION_DEPTH + 1;
    return 3 * radix + pathBits * perBit / INPLACE_RADIX_BITS;
}

template <typename Traits, typename Count, typename Sink>
class InPlaceEngine {
public:
    using E = typename Traits::Element;
    static constexpr unsigned kRadixBits = INPLACE_RADIX_BITS;
    static constexpr std::size_t kRadix = std::size_t{1} << kRadixBits;

    // Counters the arena needs, for keys of keyBits bits: 3 * kRadix
    // per-pass temporaries shared by every pass, plus the boundary arrays
    // (r + 1 each) of the passes live on the depth-first path. See the header.
    static std::size_t arenaFor(unsigned keyBits) { return inPlaceArenaFor(keyBits); }
    // Elements the block buffers need for blocks of b elements: one block
    // per sub-bucket, plus the swap block, the carry block and the block
    // that would straddle the end of the range.
    static std::size_t blockBufferFor(std::size_t b) { return (kRadix + 3) * b; }

    // blockBuf may be null (blockSize 0): every pass is then an American
    // flag permutation, and the arena is the only memory.
    InPlaceEngine(const Traits& traits, E* data, Count* arena, std::size_t arenaCapacity, E* blockBuf,
                  std::size_t blockSize, std::size_t lambda, std::size_t leafThreshold, const Probe& probe,
                  Sink& sink)
        : tr_(traits),
          data_(data),
          arena_(arena),
          arenaCapacity_(arenaCapacity),
          blockBuf_(blockBuf),
          blockSize_(blockBuf != nullptr ? blockSize : 0),
          lambda_(lambda),
          leafThreshold_(leafThreshold),
          probe_(probe),
          sink_(sink) {
        arenaTop_ = 3 * kRadix; // the shared per-pass temporaries
        arenaHigh_ = arenaTop_;
    }

    // HYBRID. A node of at most 'capacity' elements is finished by the
    // partner-buffer Engine instead - one pass per refinement level instead
    // of ceil(bits / L) - using 'buffer' (capacity elements) and 'arena'
    // (Engine::arenaFor(ceil(capacity / lambda)) counters). The partition
    // is the same either way, so this only moves the time/memory point.
    void enablePartner(E* buffer, std::size_t capacity, Count* arena, std::size_t arenaCapacity) {
        partnerBuf_ = buffer;
        partnerCap_ = capacity;
        partnerArena_ = arena;
        partnerArenaCap_ = arenaCapacity;
    }

    // Sorts data_[0, n) whose top-level grid is 'top' (>= 2 buckets).
    void run(std::size_t n, const Grid& top) {
        assert(top.binCount >= 2);
        probe_.begin("distribute");
        probe_.memory(arenaCapacity_ * sizeof(Count));
        const FastDivider64 divider(top.width);
        spread(0, n, top, divider, 0, top.binCount, /*childDepth=*/0);
        probe_.end("distribute");
    }

    // The top level has width 1 and at most kRadix buckets, and the element
    // is its own key: count and write.
    void runCounting(std::size_t n, const Grid& top) { countingFill(0, n, top, 0); }

    std::size_t arenaHighWater() const { return arenaHigh_; }

private:
    // ---- Refinement: Engine::process, in place -------------------------
    void process(std::size_t start, std::size_t count, std::size_t depth) {
        if (count == 0) {
            probe_.leaf(0, true);
            return;
        }
        if (partnerCap_ != 0 && count <= partnerCap_ && count > leafThreshold_) {
            Engine<Traits, Count, Sink, /*Stable=*/false>(tr_, data_ + start, partnerBuf_, partnerArena_,
                                                          partnerArenaCap_, lambda_, leafThreshold_, probe_,
                                                          sink_)
                .runNode(count, depth);
            return;
        }
        if (count <= leafThreshold_ || depth >= MAX_SUBDIVISION_DEPTH) {
            probe_.leaf(count, false);
            leaf(start, count);
            return;
        }
        const E* const src = data_ + start;
        uint64_t lo = tr_.key(src[0]);
        uint64_t hi = lo;
        for (std::size_t i = 1; i < count; ++i) {
            const uint64_t k = tr_.key(src[i]);
            if (k < lo) lo = k;
            if (k > hi) hi = k;
        }
        if (lo == hi) { // span 0: sorted by definition
            probe_.leaf(count, false);
            probe_.localAlgorithm("AlreadySorted");
            if constexpr (Sink::kRecord) sink_.record(true, start, count);
            return;
        }
        const Grid g = planSplit(lo, hi, count, lambda_);
        if constexpr (Traits::kElementIsKey && !Sink::kRecord) {
            if (g.width == 1 && g.binCount <= kRadix) {
                countingFill(start, count, g, depth);
                return;
            }
        }
        probe_.split(count, g.binCount, 0, depth + 1);
        const FastDivider64 divider(g.width);
        spread(start, count, g, divider, 0, g.binCount, depth + 1);
    }

    // ---- The in-place distribution -------------------------------------
    // data_[start, start + c) holds exactly the elements whose bucket index
    // (in grid g) lies in [loBucket, loBucket + span). Groups them by bucket,
    // in bucket order, and processes every bucket as a node of childDepth.
    void spread(std::size_t start, std::size_t c, const Grid& g, const FastDivider64& divider,
                uint64_t loBucket, uint64_t span, std::size_t childDepth) {
        if (c == 0) {
            probe_.leaf(0, true);
            return;
        }
        if (span == 1) {
            process(start, c, childDepth);
            return;
        }
        if (c <= INPLACE_SMALL) {
            smallSpread(start, c, g, divider, childDepth);
            return;
        }
        // Balanced split of the index bits into passes of at most kRadixBits,
        // and never a radix above c: a pass over c elements then costs O(c).
        const unsigned bits = bitsFor(span);
        const unsigned passes = (bits + kRadixBits - 1) / kRadixBits;
        unsigned levelBits = (bits + passes - 1) / passes;
        const unsigned cap = static_cast<unsigned>(floorLog2(c));
        if (levelBits > cap) levelBits = cap;
        if (levelBits == 0) levelBits = 1;
        const unsigned shift = bits - levelBits;
        const std::size_t r = static_cast<std::size_t>(((span - 1) >> shift) + 1);

        assert(arenaTop_ + r + 1 <= arenaCapacity_);
        Count* const bound = arena_ + arenaTop_; // r + 1 boundaries, live while the sub-buckets are visited
        arenaTop_ += r + 1;
        if (arenaTop_ > arenaHigh_) arenaHigh_ = arenaTop_;

        const uint64_t origin = g.origin;
        divider.dispatch([&](const auto& bucketOf) {
            auto digit = [&](const E& e) -> std::size_t {
                return static_cast<std::size_t>((bucketOf(tr_.key(e) - origin) - loBucket) >> shift);
            };
            if (blockSize_ != 0 && c >= blockSize_ * INPLACE_BLOCK_MIN_BLOCKS)
                blockPass(start, c, r, digit, bound);
            else
                flagPass(start, c, r, digit, bound);
        });

        const uint64_t stride = uint64_t{1} << shift;
        for (std::size_t j = 0; j < r; ++j) {
            const std::size_t b = bound[j];
            const std::size_t cj = static_cast<std::size_t>(bound[j + 1]) - b;
            const uint64_t subLo = loBucket + static_cast<uint64_t>(j) * stride;
            const uint64_t subSpan = std::min<uint64_t>(stride, span - static_cast<uint64_t>(j) * stride);
            spread(b, cj, g, divider, subLo, subSpan, childDepth);
        }
        arenaTop_ -= r + 1;
    }

    // ---- One pass, American flag -----------------------------------------
    // Groups data_[start, start + c) by digit in [0, r), in digit order, and
    // leaves the group boundaries in bound[0 .. r]. Cycle leader: every
    // element is moved once, straight to the next free slot of its group.
    // O(c + r) time, r cursors, one element in a register. Each step of a
    // cycle depends on the previous one, which is what makes it slow on large
    // ranges; blockPass() is the fast path there.
    template <typename Digit>
    void flagPass(std::size_t start, std::size_t c, std::size_t r, const Digit& digit, Count* bound) {
        E* const a = data_;
        Count* const next = arena_; // shared temporaries [0, kRadix)
        std::fill(bound, bound + r + 1, Count{0});
        for (std::size_t i = start; i < start + c; ++i) {
            const std::size_t d = digit(a[i]);
            assert(d < r);
            ++bound[d + 1];
        }
        bound[0] = static_cast<Count>(start);
        for (std::size_t j = 0; j < r; ++j) {
            bound[j + 1] = static_cast<Count>(bound[j + 1] + bound[j]);
            next[j] = bound[j];
        }
        assert(bound[r] == start + c);
        for (std::size_t j = 0; j < r; ++j) {
            std::size_t i = next[j];
            const std::size_t endj = bound[j + 1];
            while (i < endj) {
                E v = a[i];
                std::size_t d = digit(v);
                while (d != j) {
                    const std::size_t pos = next[d];
                    next[d] = static_cast<Count>(pos + 1);
                    const E displaced = a[pos];
                    a[pos] = v;
                    v = displaced;
                    d = digit(v);
                }
                a[i] = v;
                ++i;
            }
            next[j] = static_cast<Count>(endj);
        }
    }

    // ---- One pass, by blocks ---------------------------------------------
    // The same result as flagPass(), with the memory traffic of a streaming
    // pass instead of a chain of dependent loads (the scheme of in-place
    // samplesort/radix sort: Axtmann, Witt, Ferizovic, Sanders, IPS4o, 2017).
    // B = blockSize_ elements per block.
    //
    // 1. CLASSIFY. Read the range once; each element goes to its group's
    //    buffer block; a full buffer is written back into the range at the
    //    write position w, which trails the read position (every slot
    //    written was read before). Afterwards [start, start + w) is a
    //    sequence of full blocks, each of one group, and the buffers hold
    //    the remaining < B elements of every group.
    // 2. PERMUTE BLOCKS. Group j owns the block slots between its boundaries
    //    rounded up to a multiple of B. Within its slots, [wp, rp) hold
    //    unprocessed blocks and [rp, ...) are empty. Take the last
    //    unprocessed block of some group, and move it to the next slot of its
    //    own group - swapping out the block there if it was unprocessed,
    //    ending the cycle if the slot was empty. Every block moves O(1) times.
    //    A slot that would cross the end of the range goes to a spare block.
    // 3. CLEAN UP, group by group in order. Group j's full blocks now fill
    //    [A, W), A = its start rounded up to B. Its last block may overhang
    //    into group j + 1's first partial slot, and its head [start_j, A)
    //    and gap [W, end_j) are still to fill: the overhang and the group's
    //    buffer are exactly enough, and group j - 1 has already emptied the
    //    head by moving its own overhang out.
    //
    // O(c + r) time, (r + 3) * B elements and 3r cursors, whatever c is.
    template <typename Digit>
    void blockPass(std::size_t start, std::size_t c, std::size_t r, const Digit& digit, Count* bound) {
        const std::size_t B = blockSize_;
        E* const a = data_ + start;
        E* const bufs = blockBuf_;            // r buffers of B
        E* const S = blockBuf_ + kRadix * B;  // carry block
        E* const T = S + B;                   // swap block
        E* const over = T + B;                // the block that would cross the end
        Count* const fill = arena_;           // shared temporaries
        Count* const wp = arena_ + kRadix;
        Count* const rp = arena_ + 2 * kRadix;
        const std::size_t bytes = B * sizeof(E);

        // 1. classify
        std::fill(bound, bound + r + 1, Count{0});
        std::fill(fill, fill + r, Count{0});
        std::size_t w = 0;
        for (std::size_t i = 0; i < c; ++i) {
            const E x = a[i];
            const std::size_t d = digit(x);
            assert(d < r);
            ++bound[d + 1];
            E* const bb = bufs + d * B;
            bb[fill[d]] = x;
            if (static_cast<std::size_t>(++fill[d]) == B) {
                std::memcpy(static_cast<void*>(a + w), static_cast<const void*>(bb), bytes);
                w += B;
                fill[d] = 0;
            }
        }
        bound[0] = 0;
        for (std::size_t j = 0; j < r; ++j) bound[j + 1] = static_cast<Count>(bound[j + 1] + bound[j]);

        // 2. permute blocks
        const std::size_t filled = w / B;     // slots [0, filled) hold full blocks
        const std::size_t inside = c / B;     // slot k lies inside the range iff k < inside
        for (std::size_t j = 0; j < r; ++j) {
            const std::size_t s0 = (static_cast<std::size_t>(bound[j]) + B - 1) / B;
            const std::size_t s1 = (static_cast<std::size_t>(bound[j + 1]) + B - 1) / B;
            wp[j] = static_cast<Count>(s0);
            rp[j] = static_cast<Count>(std::min(std::max(filled, s0), s1));
        }
        // A block already in its own group's slots stays where it is: on
        // presorted input most are, and moving them would rotate blocks
        // inside a group and scramble what the leaves find there.
        auto skipHome = [&](std::size_t t) {
            while (wp[t] < rp[t] && digit(a[static_cast<std::size_t>(wp[t]) * B]) == t)
                wp[t] = static_cast<Count>(wp[t] + 1);
        };
        for (std::size_t j = 0; j < r; ++j) {
            for (;;) {
                skipHome(j);
                if (rp[j] <= wp[j]) break;
                rp[j] = static_cast<Count>(rp[j] - 1);
                std::memcpy(static_cast<void*>(S), static_cast<const void*>(a + rp[j] * B), bytes);
                std::size_t t = digit(S[0]);
                for (;;) {
                    skipHome(t);
                    const std::size_t slot = wp[t];
                    wp[t] = static_cast<Count>(slot + 1);
                    if (slot < rp[t]) { // an unprocessed block: swap it out
                        std::memcpy(static_cast<void*>(T), static_cast<const void*>(a + slot * B), bytes);
                        std::memcpy(static_cast<void*>(a + slot * B), static_cast<const void*>(S), bytes);
                        std::memcpy(static_cast<void*>(S), static_cast<const void*>(T), bytes);
                        t = digit(S[0]);
                    } else {            // empty: place, end of this cycle
                        E* const dst = slot < inside ? a + slot * B : over;
                        std::memcpy(static_cast<void*>(dst), static_cast<const void*>(S), bytes);
                        break;
                    }
                }
            }
        }

        // 3. clean up
        for (std::size_t j = 0; j < r; ++j) {
            const std::size_t s = bound[j];
            const std::size_t e = bound[j + 1];
            if (s == e) continue;
            const std::size_t full = (e - s - fill[j]) / B;   // full blocks of group j
            const std::size_t A = (s + B - 1) / B * B;
            const std::size_t W = A + full * B;
            const std::size_t headEnd = std::min(A, e);
            std::size_t hole = s;
            auto nextHole = [&]() {
                if (hole == headEnd) hole = std::max(W, s);
                return hole++;
            };
            // Elements of the block that crossed the end (in 'over') whose
            // positions are inside the range, before anything else uses 'over'.
            if (W > c) {
                for (std::size_t p = std::max(inside * B, A); p < std::min(W, e); ++p)
                    a[p] = over[p - inside * B];
            }
            for (std::size_t p = std::max(e, A); p < W; ++p) {
                const E x = p < inside * B ? a[p] : over[p - inside * B];
                a[nextHole()] = x;
            }
            const E* const bb = bufs + j * B;
            for (std::size_t q = 0; q < static_cast<std::size_t>(fill[j]); ++q) a[nextHole()] = bb[q];
        }
        for (std::size_t j = 0; j <= r; ++j) bound[j] = static_cast<Count>(bound[j] + start);
    }

    // At most INPLACE_SMALL elements over several buckets: order them by
    // bucket index with an insertion sort (O(c * INPLACE_SMALL) = O(c)) and
    // process each run of equal index as a node.
    void smallSpread(std::size_t start, std::size_t c, const Grid& g, const FastDivider64& divider,
                     std::size_t childDepth) {
        E* const a = data_ + start;
        const uint64_t origin = g.origin;
        divider.dispatch([&](const auto& bucketOf) {
            for (std::size_t i = 1; i < c; ++i) {
                const E v = a[i];
                const uint64_t bv = bucketOf(tr_.key(v) - origin);
                std::size_t j = i;
                while (j > 0 && bucketOf(tr_.key(a[j - 1]) - origin) > bv) {
                    a[j] = a[j - 1];
                    --j;
                }
                a[j] = v;
            }
        });
        std::size_t runStart = 0;
        while (runStart < c) {
            std::size_t runEnd = runStart + 1;
            divider.dispatch([&](const auto& bucketOf) {
                const uint64_t b = bucketOf(tr_.key(a[runStart]) - origin);
                while (runEnd < c && bucketOf(tr_.key(a[runEnd]) - origin) == b) ++runEnd;
            });
            process(start + runStart, runEnd - runStart, childDepth);
            runStart = runEnd;
        }
    }

    // Width-1 grid of at most kRadix keys, element == key: count and write
    // (Engine::countingFill, in place and with a bounded histogram).
    void countingFill(std::size_t start, std::size_t count, const Grid& g, std::size_t depth) {
        const std::size_t s = g.binCount;
        assert(g.width == 1 && s <= kRadix);
        assert(arenaTop_ + s <= arenaCapacity_);
        Count* const counts = arena_ + arenaTop_;
        if (arenaTop_ + s > arenaHigh_) arenaHigh_ = arenaTop_ + s;
        assert(arenaTop_ + s <= arenaCapacity_);
        std::fill(counts, counts + s, Count{0});
        E* const a = data_ + start;
        const uint64_t origin = g.origin;
        for (std::size_t i = 0; i < count; ++i) ++counts[tr_.key(a[i]) - origin];
        probe_.split(count, s, 0, depth + 1);
        E* out = a;
        for (std::size_t b = 0; b < s; ++b) {
            const std::size_t k = counts[b];
            probe_.leaf(k, k == 0);
            if (k == 0) continue;
            const E value = Traits::fromKey(origin + b);
            std::fill(out, out + k, value);
            out += k;
        }
        probe_.localAlgorithm("CountingFill");
    }

    void leaf(std::size_t start, std::size_t count) {
        if constexpr (Sink::kRecord) {
            sink_.record(true, start, count);
            return;
        }
        probe_.begin("localSort");
        LocalSort<Traits>(tr_, probe_).sortLeaf(data_ + start, count);
        probe_.end("localSort");
    }

    const Traits& tr_;
    E* const data_;
    Count* const arena_;
    const std::size_t arenaCapacity_;
    std::size_t arenaTop_ = 0;
    std::size_t arenaHigh_ = 0;
    E* const blockBuf_;
    const std::size_t blockSize_;
    E* partnerBuf_ = nullptr;
    std::size_t partnerCap_ = 0;
    Count* partnerArena_ = nullptr;
    std::size_t partnerArenaCap_ = 0;
    const std::size_t lambda_;
    const std::size_t leafThreshold_;
    const Probe probe_;
    Sink& sink_;
};

} // namespace detail
} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
