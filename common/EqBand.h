#pragma once
#include "Biquad.h"
#include "CutTable.h"
#include <array>

namespace titq
{
enum BandType
{
    Bell = 0, LowShelf, HighShelf, LowCut, HighCut, TiltShelf, TiltFlat,
    SLTIT, BOOBTEC, TITSKY, BIGTIT, APTIT, NumBandTypes
};

// ---- Constantes de las emulaciones (ajustables) ----
constexpr double kSltitQMin   = 0.303;   // Q más ancha de SLTIT
constexpr double kSltitQMax   = 2.254;   // Q más estrecha de SLTIT
constexpr double kBigTitQ     = 0.197;   // Q fija de BIGTIT
constexpr double kTitSkyQ     = 0.475;   // Q del high shelf TIT SKY (12 dB/oct), medida con la banda AIR en 40k
constexpr double kTitSkyStartHz = 9107.48; // frecuencia de partida de TIT SKY al elegir ese tipo
constexpr double kBoobtecStartHz = 10.3;   // frecuencia de partida de BOOBTEC al elegir ese tipo
// BOOBTEC: medido en un DAW contra un ecualizador pasivo de referencia (ruido blanco, función de transferencia).
//  - BOOB (boost): low shelf de Q 0.445 y 16.6 dB con el mando a 10; su frecuencia central sube al bajar el mando.
//  - ATTIT (atten): low shelf hacia abajo (Q 0.43, -20.4 dB a 10) que parte de la zona de la campana del boost.
//  - Con boost y atten a la vez, el atten deja de ser shelf y pasa a ser una campana (Q 0.73, -8.4 dB) que hunde por encima del boost.
constexpr double kBoobtecDbPerStep    = 1.66;  // mando BOOB 0..10 -> 0..16.6 dB
constexpr double kBoobtecAttDbPerStep = 2.04;  // mando ATTIT 0..10 -> 0..-20.4 dB
constexpr double kBoobtecBoostQ = 0.445, kBoobtecAttQ = 0.43, kBoobtecBellQ = 0.73;
constexpr double kBoobtecFreqExp = 1.4;        // la frecuencia del shelf sube como 10^(exp*(dB de menos)/40)

struct BandSettings
{
    bool  on = true;
    int   type = Bell;
    float freq = 1000.f, gain = 0.f, q = 1.f, slope = 12.f, boost = 0.f, atten = 0.f;

    bool operator== (const BandSettings& o) const noexcept
    {
        return on == o.on && type == o.type && freq == o.freq && gain == o.gain
            && q == o.q && slope == o.slope && boost == o.boost && atten == o.atten;
    }
    bool operator!= (const BandSettings& o) const noexcept { return ! (*this == o); }
};

// Interpolación log-log (extrapola linealmente en log-log fuera de la tabla).
inline double loglogInterp (double x, const double* xs, const double* ys, int n) noexcept
{
    int i = 0;
    while (i < n - 2 && x > xs[i + 1]) ++i;
    const double t = (std::log (x) - std::log (xs[i])) / (std::log (xs[i + 1]) - std::log (xs[i]));
    return std::exp (std::log (ys[i]) + t * (std::log (ys[i + 1]) - std::log (ys[i])));
}

// Convierte los ajustes estáticos de una banda en los ajustes "en vivo" según cuánto está actuando la dinámica.
// Devuelve el factor de escala para EqBand::design (BOOBTEC escala sus mandos; el resto suma Range*amount a la ganancia).
inline double applyDynamics (const BandSettings& s, bool dynOn, double rangeDb, double amount, BandSettings& out) noexcept
{
    out = s;
    if (! dynOn) return 1.0;
    if (s.type == BOOBTEC) return amount;
    out.gain = s.gain + (float) (rangeDb * amount);
    return 1.0;
}

// ---------------------------------------------------------------------------------------------
// Filtros de corte con pendiente CONTINUA (de 1 dB/oct en 1 dB/oct, 0..96).
// Se parte del prototipo Butterworth de orden entero N = floor(slope/6) y N+1, y se interpola
// sección a sección (frecuencia y Q propias de cada sección, en escala logarítmica). Así la respuesta
// cambia de forma continua con el slope, sin saltos ni clics, y en los múltiplos de 6 dB/oct es
// exactamente Butterworth. Después se calibra la frecuencia para que siempre haya -3 dB en fc.
// ---------------------------------------------------------------------------------------------
constexpr double kFarPole = 150.0;    // polo "lejano" (invisible) que convierte un polo real en una sección de 2º orden

struct CutSec { double w0, q; };

// Secciones del Butterworth de orden N, ordenadas por Q ascendente. En orden impar la primera sección
// es un polo real en 1 más un polo lejano.
inline int butterSections (int N, CutSec* out) noexcept
{
    int c = 0;
    if (N <= 0) return 0;
    if (N % 2 == 1)
        out[c++] = { std::sqrt (kFarPole), std::sqrt (kFarPole) / (1.0 + kFarPole) };
    for (int k = 1; k <= N / 2; ++k)
    {
        // ángulo del polo respecto al eje real negativo (par: (2k-1)pi/2N ; impar: k pi/N)
        const double th = (N % 2 == 0) ? (2.0 * k - 1.0) * kPi / (2.0 * N) : k * kPi / N;
        out[c++] = { 1.0, 1.0 / (2.0 * std::cos (th)) };
    }
    return c;
}

// Devuelve el número de secciones (siempre 8) del prototipo pasa-bajos para una pendiente continua en dB/oct.
// Las secciones salen de una tabla ajustada por mínimos cuadrados a |H|^2 = 1/(1+(f/fc)^(slope/3)) (CutTable.h),
// una fila por dB/oct, interpolada en el dominio logarítmico. Valores por debajo de 6 dB/oct se tratan como 6.
inline int cutPrototype (double slopeDbOct, CutSec* out) noexcept
{
    const double s = std::clamp (slopeDbOct, 6.0, 96.0);   // el corte más suave es 6 dB/oct
    const double sc = s - 6.0;
    int r = (int) std::floor (sc);
    if (r > kCutRows - 2) r = kCutRows - 2;
    const double t = sc - r;
    for (int i = 0; i < 8; ++i)
    {
        out[i] = { std::exp ((1.0 - t) * kCutTable[r][i]     + t * kCutTable[r + 1][i]),
                   std::exp ((1.0 - t) * kCutTable[r][8 + i] + t * kCutTable[r + 1][8 + i]) };
    }

    // calibración: -3 dB exactos en fc para cualquier pendiente (escala global de frecuencia)
    auto gain2 = [&] (double kappa)
    {
        double p = 1.0;
        for (int i = 0; i < 8; ++i)
        {
            const double w0 = kappa * out[i].w0, d = w0 * w0 - 1.0, e = w0 / out[i].q;
            p *= (w0 * w0 * w0 * w0) / (d * d + e * e);
        }
        return p;
    };
    {
        double lo = std::log (0.2), hi = std::log (5.0);
        for (int it = 0; it < 40; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            if (gain2 (std::exp (mid)) < 0.5) lo = mid; else hi = mid;
        }
        const double kappa = std::exp (0.5 * (lo + hi));
        for (int i = 0; i < 8; ++i) out[i].w0 *= kappa;
    }
    return 8;
}

// AP TIT: Q proporcional a |ganancia|, interpolación cúbica monótona (PCHIP).
// Fuera de 2.10..12.00 dB se extrapola con la pendiente del extremo; las ganancias negativas usan su magnitud.
inline double apTitQ (double gainDb) noexcept
{
    static const double xs[5] = { 2.10, 4.03, 6.10, 8.90, 12.00 };
    static const double ys[5] = { 0.332, 0.435, 0.487, 0.565, 0.608 };
    const double x = std::abs (gainDb);

    double h[4], d[4], m[5];
    for (int i = 0; i < 4; ++i) { h[i] = xs[i + 1] - xs[i]; d[i] = (ys[i + 1] - ys[i]) / h[i]; }

    for (int i = 1; i < 4; ++i)
    {
        if (d[i - 1] * d[i] <= 0.0) { m[i] = 0.0; continue; }
        const double w1 = 2.0 * h[i] + h[i - 1], w2 = h[i] + 2.0 * h[i - 1];
        m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
    }
    auto endSlope = [] (double h0, double h1, double d0, double d1)
    {
        double e = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (e * d0 <= 0.0) e = 0.0;
        else if (d0 * d1 <= 0.0 && std::abs (e) > 3.0 * std::abs (d0)) e = 3.0 * d0;
        return e;
    };
    m[0] = endSlope (h[0], h[1], d[0], d[1]);
    m[4] = endSlope (h[3], h[2], d[3], d[2]);

    double q;
    if (x <= xs[0])       q = ys[0] + m[0] * (x - xs[0]);
    else if (x >= xs[4])  q = ys[4] + m[4] * (x - xs[4]);
    else
    {
        int i = 0;
        while (i < 3 && x > xs[i + 1]) ++i;
        const double t = (x - xs[i]) / h[i], t2 = t * t, t3 = t2 * t;
        q = (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h[i] * m[i]
          + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h[i] * m[i + 1];
    }
    return std::clamp (q, 0.05, 4.0);
}

class EqBand
{
public:
    static constexpr int kMaxStages = 8;

    // dyn = factor 0..1 que escala la ganancia (lo usa el modo dinámico)
    void design (const BandSettings& s, double fs, double dyn) noexcept
    {
        active = s.on;
        int n = 0;
        auto add = [&] (const Biquad::Coeffs& c) { if (n < kMaxStages) stages[(size_t) n++].set (c); };

        const double nyq = 0.49 * fs;
        const double f = std::clamp ((double) s.freq, 10.0, nyq);
        const double g = (double) s.gain * dyn;
        const double q = std::max (0.025, (double) s.q);

        switch (s.type)
        {
            case Bell:   add (Biquad::peak (fs, f, q, g)); break;
            case SLTIT:  add (Biquad::peak (fs, f, std::clamp (q, kSltitQMin, kSltitQMax), g)); break;
            case BIGTIT: add (Biquad::peak (fs, f, kBigTitQ, g)); break;
            case APTIT:  add (Biquad::peak (fs, f, apTitQ (g), g)); break;   // Q bloqueada: depende de los dB
            case TITSKY: add (Biquad::highShelf (fs, f, kTitSkyQ, g)); break;

            case LowShelf:
            case HighShelf:
            {
                const int sec = std::clamp ((int) std::lround (s.slope / 12.0), 1, kMaxStages);
                for (int i = 0; i < sec; ++i)
                    add (s.type == LowShelf ? Biquad::lowShelf  (fs, f, q, g / sec)
                                            : Biquad::highShelf (fs, f, q, g / sec));
                break;
            }

            case LowCut:
            case HighCut:
            {
                CutSec secs[8];
                const int cnt = cutPrototype ((double) s.slope, secs);
                const double C = 1.0 / std::tan (kPi * f / fs);          // pre-warp global en fc
                for (int k = 0; k < cnt; ++k)
                    add (Biquad::cutSection (C, secs[k].w0, secs[k].q, s.type == LowCut));
                break;
            }

            case TiltShelf:
            case TiltFlat:
            {
                const double tq = (s.type == TiltShelf) ? std::clamp (q, 0.2, 2.0) : 0.3;
                add (Biquad::lowShelf  (fs, f, tq, -g * 0.5));
                add (Biquad::highShelf (fs, f, tq,  g * 0.5));
                break;
            }

            case BOOBTEC:
            {
                // anclas medidas en las 4 posiciones del selector (20, 30, 60, 100 Hz)
                static const double xs[4]     = { 20.0, 30.0, 60.0, 100.0 };
                static const double fBoost[4] = { 32.33, 76.99, 145.84, 239.56 };   // centro del shelf de boost a 10
                static const double fAtt[4]   = { 59.6, 102.4, 247.4, 436.6 };      // centro del shelf de atten a 10
                static const double fBell[4]  = { 120.2, 237.2, 506.3, 862.6 };     // centro de la campana (boost + atten)
                static const double gBell[4]  = { -8.3, -7.3, -8.4, -8.7 };         // profundidad de la campana a 10
                const double fi = std::clamp (f, 10.0, 200.0);
                const double bn = std::clamp ((double) s.boost * dyn / 10.0, 0.0, 1.0);
                const double an = std::clamp ((double) s.atten * dyn / 10.0, 0.0, 1.0);
                const double gB10 = 10.0 * kBoobtecDbPerStep, gA10 = 10.0 * kBoobtecAttDbPerStep;
                auto lowerFreq = [] (double f10, double g, double g10) { return f10 * std::pow (10.0, kBoobtecFreqExp * (g10 - g) / 40.0); };
                const double b10 = loglogInterp (fi, xs, fBoost, 4), a10 = loglogInterp (fi, xs, fAtt, 4);
                const double l10 = loglogInterp (fi, xs, fBell, 4);
                double bell10 = gBell[3];
                if (fi <= xs[0]) bell10 = gBell[0];
                else for (int i = 0; i < 3; ++i)
                    if (fi <= xs[i + 1]) { const double t = std::log (fi / xs[i]) / std::log (xs[i + 1] / xs[i]); bell10 = gBell[i] + t * (gBell[i + 1] - gBell[i]); break; }

                if (bn > 0.0)
                {
                    const double g = bn * gB10;
                    add (Biquad::lowShelf (fs, std::clamp (lowerFreq (b10, g, gB10), 5.0, nyq), kBoobtecBoostQ, g));
                }
                if (an > 0.0)
                {
                    // sin boost: shelf hacia abajo; con boost el atten se convierte en campana (mezcla continua)
                    const double w = bn > 0.0 ? 1.0 - std::pow (1.0 - bn, 1.4) : 0.0;
                    const double g = an * gA10;
                    if (w < 0.999)
                        add (Biquad::lowShelf (fs, std::clamp (lowerFreq (a10, g, gA10), 5.0, nyq), kBoobtecAttQ, -g * (1.0 - w)));
                    if (w > 0.001)
                    {
                        const double fb = l10 * (1.0 + 0.47 * (1.0 - an));
                        add (Biquad::peak (fs, std::clamp (fb, 5.0, nyq), kBoobtecBellQ, bell10 * std::pow (an, 0.5) * w));
                    }
                }
                break;
            }
            default: break;
        }

        for (int i = numStages; i < n; ++i) stages[(size_t) i].reset();
        numStages = n;
    }

    void process (float* d, int n, int ch) noexcept
    {
        for (int i = 0; i < numStages; ++i) stages[(size_t) i].process (d, n, ch);
    }

    double magnitudeDb (double f, double fs) const noexcept
    {
        if (! active || numStages == 0) return 0.0;
        const double w = 2.0 * kPi * std::min (f, 0.499 * fs) / fs;   // nunca por encima de Nyquist: evita que la curva dibujada "rebote"
        double m = 1.0;
        for (int i = 0; i < numStages; ++i) m *= stages[(size_t) i].magnitude (w);
        return 20.0 * std::log10 (std::max (m, 1.0e-9));
    }

    void reset() noexcept { for (auto& s : stages) s.reset(); }
    bool isActive() const noexcept { return active && numStages > 0; }

private:
    std::array<Biquad, kMaxStages> stages;
    int numStages = 0;
    bool active = false;
};
} // namespace titq
