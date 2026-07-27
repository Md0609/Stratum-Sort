#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace drs::testing {

using DataVector = std::vector<int64_t>;

// ============================================================
// DatasetGenerator
// ============================================================
// Produces the dataset shapes requested in the TESTS section of
// the specification. A fixed seed is used by default so runs are
// reproducible.
// ============================================================
class DatasetGenerator {
public:
    explicit DatasetGenerator(uint64_t seed = 42) : rng_(seed) {}

    DataVector randomUniform(std::size_t n, int64_t minV = 0, int64_t maxV = 1'000'000) {
        std::uniform_int_distribution<int64_t> dist(minV, maxV);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector sortedAscending(std::size_t n) {
        DataVector v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(i);
        return v;
    }

    DataVector sortedDescending(std::size_t n) {
        DataVector v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(n - i);
        return v;
    }

    DataVector manyRepeated(std::size_t n, int distinctValues = 5) {
        std::uniform_int_distribution<int> dist(0, distinctValues - 1);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector normalDistribution(std::size_t n, double mean = 500'000.0, double stddev = 50'000.0) {
        std::normal_distribution<double> dist(mean, stddev);
        DataVector v(n);
        for (auto& x : v) x = static_cast<int64_t>(std::llround(dist(rng_)));
        return v;
    }

    // Most values packed into a very narrow sub-range, with a small
    // fraction of far-away outliers. Exercises subdivision heavily.
    DataVector concentrated(std::size_t n) {
        std::uniform_int_distribution<int64_t> narrow(100'000, 100'100);
        std::uniform_int_distribution<int64_t> outlier(0, 1'000'000);
        std::bernoulli_distribution isOutlier(0.01);
        DataVector v(n);
        for (auto& x : v) x = isOutlier(rng_) ? outlier(rng_) : narrow(rng_);
        return v;
    }

    DataVector smallRangeManyElements(std::size_t n) {
        std::uniform_int_distribution<int64_t> dist(0, 99);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector hugeRangeFewElements(std::size_t n) {
        std::uniform_int_distribution<int64_t> dist(std::numeric_limits<int64_t>::min() / 2,
                                                      std::numeric_limits<int64_t>::max() / 2);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    // ========================================================================
    // Datasets anadidos en el paso 0 de SPEC_v9.md (secciones 9.5 y 9.6).
    // Ninguno de los ocho anteriores ejerce los dos casos que la
    // especificacion de v9 necesita poder observar, asi que ninguna
    // afirmacion sobre ellos estaba verificada.
    // ========================================================================

    // SPEC_v9 s9.6 - "caso limite de rango completo".
    // Contiene explicitamente INT64_MIN e INT64_MAX, de modo que
    // max - min + 1 = 2^64, que es exactamente el valor que desborda a 0 en
    // aritmetica de 64 bits sin signo. Es la entrada que motiva el
    // prerrequisito de correccion de SPEC_v9 s2.1 (aritmetica de span).
    // Los dos extremos se plantan a proposito: una muestra uniforme sobre
    // todo el universo practicamente nunca los alcanza.
    DataVector fullRangeExtremes(std::size_t n) {
        const int64_t lo = std::numeric_limits<int64_t>::min();
        const int64_t hi = std::numeric_limits<int64_t>::max();
        DataVector v(n);
        if (n == 0) return v;
        std::uniform_int_distribution<int64_t> dist(lo, hi);
        for (auto& x : v) x = dist(rng_);
        v[0] = lo;
        if (n > 1) v[n - 1] = hi;
        std::shuffle(v.begin(), v.end(), rng_);
        return v;
    }

    // SPEC_v9 s9.5 - dataset adversario, ausente del proyecto hasta ahora.
    // Construccion del Teorema 9' de REVIEW_refinamiento_terminal.md: grupos
    // que pierden exactamente UN elemento por nivel de refinamiento.
    //
    // Dentro de un grupo, se parte de un nucleo de 'target' valores contiguos
    // (span S) y se le anade repetidamente un unico valor lejano colocado en
    // el desplazamiento s*S, donde s = ceil(tamano/target) es el abanico que
    // usara DRS. Con esa separacion, el ancho de intervalo resultante es
    // S+1 > S, de modo que TODO el nucleo cae en el intervalo 0 y el valor
    // nuevo cae en un intervalo superior: la subdivision reduce el mayor
    // subproblema en un solo elemento, que es el peor caso posible.
    //
    // El span de un grupo crece geometricamente, asi que el numero de niveles
    // que se pueden forzar esta limitado por los bits disponibles por grupo,
    // que a su vez dependen de cuantos grupos hay que alojar en el universo
    // de forma disjunta (esa restriccion es el error C-B que la revision
    // adversaria encontro en la version publicada del teorema).
    DataVector adversarialPeeling(std::size_t n, std::size_t target = 64) {
        DataVector v;
        v.reserve(n);
        if (n == 0) return v;
        if (target < 1) target = 1;

        // Presupuesto de span por grupo: el universo se reparte a partes
        // iguales entre los grupos, que deben ser disjuntos en valor.
        // Se usa la mitad positiva del universo para dejar sitio a la
        // separacion entre grupos sin desbordar.
        const std::size_t coreSize = std::min(target, n);
        std::size_t groupsEstimate = n / std::max<std::size_t>(coreSize * 2, 1);
        if (groupsEstimate == 0) groupsEstimate = 1;
        const uint64_t budget =
            (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / groupsEstimate) / 2;

        // Desplazamientos de un grupo, construidos una sola vez y reutilizados:
        // todos los grupos tienen la misma forma, solo cambia su base.
        std::vector<uint64_t> offsets;
        offsets.reserve(coreSize + 64);
        uint64_t span = 0;
        for (std::size_t i = 0; i < coreSize; ++i) {
            offsets.push_back(static_cast<uint64_t>(i));
            span = static_cast<uint64_t>(i);
        }
        while (offsets.size() < n) {
            const std::size_t sizeAfter = offsets.size() + 1;
            const uint64_t splits = (sizeAfter + target - 1) / target;
            if (splits < 2) break;
            if (span > budget / splits) break; // se agotaron los bits del grupo
            const uint64_t next = span * splits;
            if (next <= span) break;
            offsets.push_back(next);
            span = next;
        }

        const uint64_t stride = span + 2; // separacion entre grupos
        uint64_t base = 0;
        while (v.size() < n) {
            for (uint64_t off : offsets) {
                if (v.size() == n) break;
                v.push_back(static_cast<int64_t>(base + off));
            }
            base += stride;
        }
        std::shuffle(v.begin(), v.end(), rng_);
        return v;
    }

private:
    std::mt19937_64 rng_;
};

} // namespace drs::testing
