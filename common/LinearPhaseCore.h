#pragma once
// Fase lineal para TIT Q: la curva de magnitud de las 10 bandas se convierte en un filtro FIR simétrico
// (sin desfase) que se aplica con convolución por bloques. Este archivo es C++ puro (sin JUCE) para poder
// probarlo con un compilador normal; el hilo de diseño y la integración con el plugin están en LinearPhaseEngine.h.
#include "EqBand.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace titq
{
constexpr int kLpBands = 10;           // = kNumBands
constexpr int kLpMaxPartitions = 64;   // = longitud máxima / bloque (16384 / 256)

// ---- calidades: longitud del FIR (la latencia es la mitad más un bloque) ----
inline int lpBaseLength (int quality) noexcept
{
    static const int n[4] = { 2048, 4096, 8192, 16384 };
    return n[std::clamp (quality, 0, 3)];
}
inline int lpScale (double fs) noexcept { return fs >= 176000.0 ? 4 : (fs >= 88000.0 ? 2 : 1); }   // la resolución en Hz se mantiene
inline int lpFirLength (int quality, double fs) noexcept { return lpBaseLength (quality) * lpScale (fs); }
inline int lpBlockSize (double fs) noexcept { return 256 * lpScale (fs); }
inline int lpPartitions (int quality) noexcept { return lpBaseLength (quality) / 256; }
inline int lpLatency (int quality, double fs) noexcept { return lpFirLength (quality, fs) / 2 + lpBlockSize (fs); }

// Lo que necesita el diseño de cada banda en un momento dado.
struct LpBandSnap
{
    bool active = false;       // la banda existe y está (o se está) encendiendo
    BandSettings eff;          // ajustes con la dinámica ya aplicada
    double scale = 1.0;        // factor de escala de EqBand::design (BOOBTEC)
    float fade = 0.f;          // 0..1, fundido de encendido/apagado
    int chmode = 0;            // 0 Stereo, 1 Mid, 2 Side, 3 Left, 4 Right

    bool operator== (const LpBandSnap& o) const noexcept
    {
        return active == o.active && eff == o.eff && scale == o.scale && fade == o.fade && chmode == o.chmode;
    }
    bool operator!= (const LpBandSnap& o) const noexcept { return ! (*this == o); }
};

struct LpSnapshot
{
    std::array<LpBandSnap, kLpBands> band;
    bool operator== (const LpSnapshot& o) const noexcept { return band == o.band; }
    bool operator!= (const LpSnapshot& o) const noexcept { return ! (*this == o); }
};

// ---------------------------------------------------------------------------------------------
// FFT compleja radix-2 (datos intercalados re,im). La inversa no escala.
// ---------------------------------------------------------------------------------------------
template <class T>
class LpFft
{
public:
    void init (int size)
    {
        n = size;
        int logn = 0;
        while ((1 << logn) < n) ++logn;
        rev.assign ((size_t) n, 0);
        cs.assign ((size_t) std::max (1, n / 2), (T) 1);
        sn.assign ((size_t) std::max (1, n / 2), (T) 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < logn; ++b) if (i & (1 << b)) r |= 1 << (logn - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = 2.0 * kPi * (double) k / (double) n;
            cs[(size_t) k] = (T) std::cos (a);
            sn[(size_t) k] = (T) std::sin (a);
        }
    }

    int size() const noexcept { return n; }

    void transform (T* x, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (x[2 * i], x[2 * j]); std::swap (x[2 * i + 1], x[2 * j + 1]); }
        }
        const T sgn = inverse ? (T) 1 : (T) -1;
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
            {
                T* a = x + 2 * i;
                T* b = a + 2 * half;
                for (int k = 0; k < half; ++k)
                {
                    const T wr = cs[(size_t) (k * step)], wi = sgn * sn[(size_t) (k * step)];
                    const T tr = b[2 * k] * wr - b[2 * k + 1] * wi;
                    const T ti = b[2 * k] * wi + b[2 * k + 1] * wr;
                    b[2 * k]     = a[2 * k]     - tr;
                    b[2 * k + 1] = a[2 * k + 1] - ti;
                    a[2 * k]     += tr;
                    a[2 * k + 1] += ti;
                }
            }
        }
    }

private:
    int n = 0;
    std::vector<int> rev;
    std::vector<T> cs, sn;
};

// Z = FFT(a + j b) con a y b reales de n puntos  ->  mitades 0..n/2 de A = FFT(a) y B = FFT(b).
template <class T>
inline void lpSplit (const T* Z, int n, T* A, T* Bo) noexcept
{
    for (int k = 0; k <= n / 2; ++k)
    {
        const int m = (n - k) & (n - 1);
        const T zr = Z[2 * k], zi = Z[2 * k + 1], cr = Z[2 * m], ci = -Z[2 * m + 1];
        A[2 * k]      = (T) 0.5 * (zr + cr);
        A[2 * k + 1]  = (T) 0.5 * (zi + ci);
        Bo[2 * k]     = (T) 0.5 * (zi - ci);
        Bo[2 * k + 1] = (T) -0.5 * (zr - cr);
    }
}

// ---------------------------------------------------------------------------------------------
// Un juego de filtros ya diseñados, partidos en trozos y pasados al dominio de la frecuencia.
// Pares: 0 = (salida 0 <- entrada 0), 1 = (0 <- 1), 2 = (1 <- 0), 3 = (1 <- 1)
//   domain 0: canales L y R por separado (kernel 0 para L, kernel 3 para R; si 'shared', el 0 vale para los dos)
//   domain 1: Mid y Side por separado (kernel 0 para M, kernel 3 para S)
//   domain 2: matriz completa 2x2 en L/R (hay bandas Mid/Side y bandas solo-L o solo-R a la vez)
// ---------------------------------------------------------------------------------------------
struct LpKernelSet
{
    int N = 0, B = 0, P = 0, domain = 0, generation = 0;
    bool shared = false;
    std::array<std::vector<float>, 4> spec;     // cada uno: P trozos x (B+1) bins x (re,im)

    void allocate (int block)
    {
        for (auto& s : spec) s.assign ((size_t) kLpMaxPartitions * (size_t) (block + 1) * 2, 0.f);
    }
};

// ---------------------------------------------------------------------------------------------
// Diseño: respuesta de magnitud de las bandas -> FIR simétrico (ventana Blackman) -> espectros por trozos.
// ---------------------------------------------------------------------------------------------
class LpDesigner
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        B = lpBlockSize (fs);
        const int maxN = lpFirLength (3, fs);
        for (int q = 0; q < 4; ++q)
        {
            const int N = lpFirLength (q, fs);
            fftN[(size_t) q].init (N);
            win[(size_t) q].resize ((size_t) N);
            for (int n = 0; n < N; ++n)
            {
                const double x = 2.0 * kPi * (double) (n - N / 2) / (double) N;
                win[(size_t) q][(size_t) n] = (float) (0.42 + 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));   // Blackman, centrada en N/2
            }
        }
        fftB.init (2 * B);
        for (auto& v : m) v.assign ((size_t) maxN / 2 + 1, 1.0);
        db.assign ((size_t) maxN / 2 + 1, 0.0);
        grid.assign (4096, 0.0);
        zN.assign ((size_t) maxN * 2, 0.0);
        hA.assign ((size_t) maxN, 0.f);
        hB.assign ((size_t) maxN, 0.f);
        zP.assign ((size_t) B * 4, 0.f);
        pA.assign ((size_t) B * 2 + 2, 0.f);
        pB.assign ((size_t) B * 2 + 2, 0.f);
    }

    // Filtro "identidad": solo retrasa la señal N/2 muestras.
    void makeIdentity (LpKernelSet& out, int quality) const
    {
        const int N = lpFirLength (quality, fs), P = N / B;
        out.N = N; out.B = B; out.P = P; out.domain = 0; out.shared = true;
        for (auto& s : out.spec) std::fill (s.begin(), s.end(), 0.f);
        const int p0 = (N / 2) / B, o = (N / 2) % B;
        float* d = out.spec[0].data() + (size_t) p0 * (size_t) (B + 1) * 2;
        for (int k = 0; k <= B; ++k)
        {
            const double a = -2.0 * kPi * (double) k * (double) o / (double) (2 * B);
            d[2 * k] = (float) std::cos (a);
            d[2 * k + 1] = (float) std::sin (a);
        }
    }

    void design (const LpSnapshot& snap, int quality, LpKernelSet& out)
    {
        quality = std::clamp (quality, 0, 3);
        const int N = lpFirLength (quality, fs), half = N / 2;
        out.N = N; out.B = B; out.P = N / B;

        bool anyMS = false, anyLR = false;
        for (const auto& b : snap.band)
            if (b.active && b.fade > 0.f)
            {
                if (b.chmode == 1 || b.chmode == 2) anyMS = true;
                else if (b.chmode == 3 || b.chmode == 4) anyLR = true;
            }
        out.domain = anyMS ? (anyLR ? 2 : 1) : 0;
        out.shared = (out.domain == 0 && ! anyLR);

        const int bins = half + 1;
        for (int i = 0; i < 4; ++i)
            std::fill (m[(size_t) i].begin(), m[(size_t) i].begin() + bins, (out.domain == 2 && (i == 1 || i == 2)) ? 0.0 : 1.0);   // matriz identidad

        for (const auto& b : snap.band)
        {
            if (! (b.active && b.fade > 0.f)) continue;
            bandDb (b, N, half);

            if (out.domain < 2)
            {
                // domain 0: m[0] = L, m[1] = R.  domain 1: m[0] = Mid, m[1] = Side
                bool toA = true, toB = true;
                if (out.domain == 0) { if (b.chmode == 3) toB = false; else if (b.chmode == 4) toA = false; }
                else                 { if (b.chmode == 1) toB = false; else if (b.chmode == 2) toA = false; }
                if (out.shared) toB = false;
                double* a = m[0].data(); double* c = m[1].data();
                for (int k = 0; k < bins; ++k)
                {
                    const double H = std::exp (db[(size_t) k] * 0.11512925464970229);   // 10^(dB/20)
                    if (toA) a[k] *= H;
                    if (toB) c[k] *= H;
                }
            }
            else
            {
                double *m00 = m[0].data(), *m01 = m[1].data(), *m10 = m[2].data(), *m11 = m[3].data();
                for (int k = 0; k < bins; ++k)
                {
                    const double H = std::exp (db[(size_t) k] * 0.11512925464970229);
                    double b00 = H, b01 = 0, b10 = 0, b11 = H;
                    switch (b.chmode)
                    {
                        case 1: b00 = b11 = 0.5 * (H + 1.0); b01 = b10 = 0.5 * (H - 1.0); break;   // Mid
                        case 2: b00 = b11 = 0.5 * (1.0 + H); b01 = b10 = 0.5 * (1.0 - H); break;   // Side
                        case 3: b00 = H; b11 = 1.0; break;
                        case 4: b00 = 1.0; b11 = H; break;
                        default: break;
                    }
                    const double n00 = b00 * m00[k] + b01 * m10[k], n01 = b00 * m01[k] + b01 * m11[k];
                    const double n10 = b10 * m00[k] + b11 * m10[k], n11 = b10 * m01[k] + b11 * m11[k];
                    m00[k] = n00; m01[k] = n01; m10[k] = n10; m11[k] = n11;
                }
            }
        }

        if (out.domain == 2)
        {
            build (m[0].data(), m[1].data(), quality, N, out.spec[0], out.spec[1]);
            build (m[2].data(), m[3].data(), quality, N, out.spec[2], out.spec[3]);
        }
        else if (out.shared)
            build (m[0].data(), nullptr, quality, N, out.spec[0], out.spec[3]);
        else
            build (m[0].data(), m[1].data(), quality, N, out.spec[0], out.spec[3]);
    }

    int blockSize() const noexcept { return B; }
    double sampleRate() const noexcept { return fs; }

    // respuesta de magnitud de una banda (dB) en los bins 0..N/2, calculada con el filtro diseñado a 4x la frecuencia de muestreo
    // (sin la deformación del bilineal cerca de Nyquist); es la misma que dibuja la interfaz
    void bandDb (const LpBandSnap& b, int N, int half)
    {
        const double fsd = fs * 4.0;
        EqBand eq;
        eq.design (b.eff, fsd, b.scale);
        const double df = fs / (double) N, fade = (double) b.fade;
        constexpr int kx = 150;                                   // hasta aquí se evalúa bin a bin; después, en rejilla logarítmica
        const int ke = std::min (kx, half);
        for (int k = 0; k <= ke; ++k) db[(size_t) k] = fade * eq.magnitudeDb ((double) k * df, fsd);
        if (half > kx)
        {
            const double fLo = (double) kx * df, fHi = (double) half * df;
            const double span = std::log (fHi / fLo);
            const int ng = std::min ((int) grid.size(), (int) std::ceil (span / std::log (2.0) * 100.0) + 2);
            const double r = span / (double) (ng - 1);
            for (int j = 0; j < ng; ++j) grid[(size_t) j] = fade * eq.magnitudeDb (fLo * std::exp (r * (double) j), fsd);
            for (int k = kx + 1; k <= half; ++k)
            {
                const double pos = std::log ((double) k / (double) kx) / r;
                int j = (int) pos;
                if (j > ng - 2) j = ng - 2;
                const double t = pos - (double) j;
                db[(size_t) k] = grid[(size_t) j] + t * (grid[(size_t) j + 1] - grid[(size_t) j]);
            }
        }
    }

private:
    // dos respuestas de magnitud (reales y pares) -> dos filtros FIR -> espectros por trozos
    void build (const double* magA, const double* magB, int quality, int N, std::vector<float>& specA, std::vector<float>& specB)
    {
        const int half = N / 2;
        double* z = zN.data();
        for (int k = 0; k < N; ++k)
        {
            const int kk = k <= half ? k : N - k;
            z[2 * k] = magA[kk];
            z[2 * k + 1] = magB ? magB[kk] : 0.0;
        }
        fftN[(size_t) quality].transform (z, true);
        const double inv = 1.0 / (double) N;
        const float* w = win[(size_t) quality].data();
        for (int n = 0; n < N; ++n)
        {
            const int idx = n < half ? n + half : n - half;          // respuesta al impulso de fase cero, centrada en N/2
            hA[(size_t) n] = (float) (z[2 * idx] * inv) * w[n];
            hB[(size_t) n] = (float) (z[2 * idx + 1] * inv) * w[n];
        }
        partitionSpectra (hA.data(), N, specA);
        if (magB) partitionSpectra (hB.data(), N, specB);
    }

    void partitionSpectra (const float* h, int N, std::vector<float>& spec)
    {
        const int P = N / B, stride = (B + 1) * 2;
        for (int p = 0; p < P; p += 2)
        {
            std::fill (zP.begin(), zP.end(), 0.f);
            for (int i = 0; i < B; ++i)
            {
                zP[(size_t) (2 * i)] = h[p * B + i];
                zP[(size_t) (2 * i + 1)] = h[(p + 1) * B + i];
            }
            fftB.transform (zP.data(), false);
            lpSplit<float> (zP.data(), 2 * B, pA.data(), pB.data());
            std::memcpy (spec.data() + (size_t) p * (size_t) stride, pA.data(), sizeof (float) * (size_t) stride);
            std::memcpy (spec.data() + (size_t) (p + 1) * (size_t) stride, pB.data(), sizeof (float) * (size_t) stride);
        }
    }

    double fs = 48000.0;
    int B = 256;
    std::array<LpFft<double>, 4> fftN;
    LpFft<float> fftB;
    std::array<std::vector<float>, 4> win;
    std::array<std::vector<double>, 4> m;
    std::vector<double> db, grid, zN;
    std::vector<float> hA, hB, zP, pA, pB;
};

// ---------------------------------------------------------------------------------------------
// Convolución por bloques (overlap-save, trozos uniformes). Procesa exactamente B muestras por llamada.
// Admite dos juegos de filtros a la vez para pasar del anterior al nuevo con un fundido de un bloque.
// ---------------------------------------------------------------------------------------------
class LpConvolver
{
public:
    void prepare (double sampleRate)
    {
        B = lpBlockSize (sampleRate);
        n2 = 2 * B;
        fft.init (n2);
        const size_t stride = (size_t) (B + 1) * 2;
        for (int c = 0; c < 2; ++c)
        {
            fdl[(size_t) c].assign ((size_t) kLpMaxPartitions * stride, 0.f);
            prevIn[(size_t) c].assign ((size_t) B, 0.f);
            acc[(size_t) c].assign (stride, 0.f);
            outA[(size_t) c].assign ((size_t) B, 0.f);
            outB[(size_t) c].assign ((size_t) B, 0.f);
        }
        z.assign ((size_t) n2 * 2, 0.f);
        xa.assign (stride, 0.f);
        xb.assign (stride, 0.f);
        P = 8; head = 0;
    }

    void configure (int partitions) { P = std::clamp (partitions, 1, kLpMaxPartitions); reset(); }

    void reset()
    {
        for (auto& v : fdl) std::fill (v.begin(), v.end(), 0.f);
        for (auto& v : prevIn) std::fill (v.begin(), v.end(), 0.f);
        head = 0;
    }

    int blockSize() const noexcept { return B; }

    void processBlock (const float* inL, const float* inR, float* outL, float* outR, int nch,
                       const LpKernelSet& cur, const LpKernelSet* prev)
    {
        const size_t stride = (size_t) (B + 1) * 2;
        head = (head + 1) % P;

        // espectro de las dos mitades (L en la parte real, R en la imaginaria): [bloque anterior | bloque nuevo]
        for (int i = 0; i < B; ++i)
        {
            z[(size_t) (2 * i)]           = prevIn[0][(size_t) i];
            z[(size_t) (2 * i + 1)]       = nch > 1 ? prevIn[1][(size_t) i] : 0.f;
            z[(size_t) (2 * (B + i))]     = inL[i];
            z[(size_t) (2 * (B + i) + 1)] = nch > 1 ? inR[i] : 0.f;
        }
        std::memcpy (prevIn[0].data(), inL, sizeof (float) * (size_t) B);
        if (nch > 1) std::memcpy (prevIn[1].data(), inR, sizeof (float) * (size_t) B);
        fft.transform (z.data(), false);
        lpSplit<float> (z.data(), n2, xa.data(), xb.data());
        std::memcpy (fdl[0].data() + (size_t) head * stride, xa.data(), sizeof (float) * stride);
        std::memcpy (fdl[1].data() + (size_t) head * stride, xb.data(), sizeof (float) * stride);

        accumulate (cur, nch);
        toTime (outA[0].data(), outA[1].data());

        if (prev == nullptr)
        {
            std::memcpy (outL, outA[0].data(), sizeof (float) * (size_t) B);
            if (nch > 1) std::memcpy (outR, outA[1].data(), sizeof (float) * (size_t) B);
            return;
        }

        accumulate (*prev, nch);
        toTime (outB[0].data(), outB[1].data());
        for (int i = 0; i < B; ++i)
        {
            const float w = (float) (i + 1) / (float) B;
            outL[i] = outB[0][(size_t) i] + w * (outA[0][(size_t) i] - outB[0][(size_t) i]);
            if (nch > 1) outR[i] = outB[1][(size_t) i] + w * (outA[1][(size_t) i] - outB[1][(size_t) i]);
        }
    }

private:
    static void mac (float* a, const float* x, const float* h, int bins) noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const float xr = x[2 * k], xi = x[2 * k + 1], hr = h[2 * k], hi = h[2 * k + 1];
            a[2 * k]     += xr * hr - xi * hi;
            a[2 * k + 1] += xr * hi + xi * hr;
        }
    }

    void accumulate (const LpKernelSet& K, int nch)
    {
        const int bins = B + 1;
        const size_t stride = (size_t) bins * 2;
        float* a0 = acc[0].data();
        float* a1 = acc[1].data();
        std::fill (acc[0].begin(), acc[0].end(), 0.f);
        std::fill (acc[1].begin(), acc[1].end(), 0.f);

        for (int p = 0; p < P; ++p)
        {
            const int slot = (head - p + P) % P;
            const float* xl = fdl[0].data() + (size_t) slot * stride;
            const float* xr = fdl[1].data() + (size_t) slot * stride;
            const float* h0 = K.spec[0].data() + (size_t) p * stride;
            const float* h3 = K.spec[3].data() + (size_t) p * stride;

            if (K.domain == 0 || nch < 2)
            {
                mac (a0, xl, h0, bins);
                if (nch > 1) mac (a1, xr, K.shared ? h0 : h3, bins);
            }
            else if (K.domain == 1)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const float sr = xl[2 * k] + xr[2 * k], si = xl[2 * k + 1] + xr[2 * k + 1];   // Mid x2
                    const float dr = xl[2 * k] - xr[2 * k], di = xl[2 * k + 1] - xr[2 * k + 1];   // Side x2
                    a0[2 * k]     += sr * h0[2 * k] - si * h0[2 * k + 1];
                    a0[2 * k + 1] += sr * h0[2 * k + 1] + si * h0[2 * k];
                    a1[2 * k]     += dr * h3[2 * k] - di * h3[2 * k + 1];
                    a1[2 * k + 1] += dr * h3[2 * k + 1] + di * h3[2 * k];
                }
            }
            else
            {
                const float* h1 = K.spec[1].data() + (size_t) p * stride;
                const float* h2 = K.spec[2].data() + (size_t) p * stride;
                mac (a0, xl, h0, bins); mac (a0, xr, h1, bins);
                mac (a1, xl, h2, bins); mac (a1, xr, h3, bins);
            }
        }

        if (K.domain == 1 && nch > 1)
            for (int k = 0; k < bins * 2; ++k)
            {
                const float mm = a0[k], ss = a1[k];     // M = (L+R)/2, S = (L-R)/2: L = M+S, R = M-S (los x2 de arriba se compensan con el 0.5)
                a0[k] = 0.5f * (mm + ss);
                a1[k] = 0.5f * (mm - ss);
            }
    }

    // espectros de salida (L y R, bins 0..B) -> últimas B muestras de cada canal
    void toTime (float* yl, float* yr)
    {
        const float* l = acc[0].data();
        const float* r = acc[1].data();
        for (int k = 0; k <= B; ++k)
        {
            z[(size_t) (2 * k)]     = l[2 * k] - r[2 * k + 1];
            z[(size_t) (2 * k + 1)] = l[2 * k + 1] + r[2 * k];
        }
        for (int k = B + 1; k < n2; ++k)
        {
            const int mi = n2 - k;                                   // L[k] = conj(L[mi])
            z[(size_t) (2 * k)]     = l[2 * mi] + r[2 * mi + 1];
            z[(size_t) (2 * k + 1)] = r[2 * mi] - l[2 * mi + 1];
        }
        fft.transform (z.data(), true);
        const float s = 1.f / (float) n2;
        for (int i = 0; i < B; ++i)
        {
            yl[i] = z[(size_t) (2 * (B + i))] * s;
            yr[i] = z[(size_t) (2 * (B + i) + 1)] * s;
        }
    }

    int B = 256, n2 = 512, P = 8, head = 0;
    LpFft<float> fft;
    std::array<std::vector<float>, 2> fdl, prevIn, acc, outA, outB;
    std::vector<float> z, xa, xb;
};
} // namespace titq
