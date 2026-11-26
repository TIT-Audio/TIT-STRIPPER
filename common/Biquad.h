#pragma once
#include <algorithm>
#include <cmath>
#include <complex>

namespace titq
{
constexpr double kPi = 3.14159265358979323846;

// Biquad TDF-II en double, sin asignaciones de memoria (seguro en el hilo de audio).
struct Biquad
{
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    Coeffs c;
    double z1[2] { 0, 0 };
    double z2[2] { 0, 0 };

    Coeffs c0;                      // coeficientes de partida de la rampa
    int rampN = 0, rp[2] { 0, 0 };  // muestras de la rampa y avance de cada canal

    void set (const Coeffs& n) noexcept { c = n; rampN = 0; }
    // coeficientes nuevos con interpolacion lineal muestra a muestra durante las proximas N muestras de cada canal (dinamica sin escalones)
    void setRamp (const Coeffs& n, int N) noexcept { c0 = c; c = n; rampN = N; rp[0] = rp[1] = 0; }
    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    template <typename T>
    void process (T* x, int n, int ch) noexcept
    {
        double l1 = z1[ch], l2 = z2[ch];
        if (rampN > 0 && rp[ch] < rampN)
        {
            int k = rp[ch];
            for (int i = 0; i < n; ++i)
            {
                const double f = k < rampN ? (double) (k + 1) / (double) rampN : 1.0;
                ++k;
                const double b0 = c0.b0 + (c.b0 - c0.b0) * f, b1 = c0.b1 + (c.b1 - c0.b1) * f, b2 = c0.b2 + (c.b2 - c0.b2) * f,
                             a1 = c0.a1 + (c.a1 - c0.a1) * f, a2 = c0.a2 + (c.a2 - c0.a2) * f;
                const double in = x[i];
                const double out = b0 * in + l1;
                l1 = b1 * in - a1 * out + l2;
                l2 = b2 * in - a2 * out;
                x[i] = (T) out;
            }
            rp[ch] = k;
            z1[ch] = l1; z2[ch] = l2;
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const double out = c.b0 * in + l1;
            l1 = c.b1 * in - c.a1 * out + l2;
            l2 = c.b2 * in - c.a2 * out;
            x[i] = (T) out;
        }
        z1[ch] = l1;
        z2[ch] = l2;
    }

    double magnitude (double w) const
    {
        using cd = std::complex<double>;
        const cd e1 = std::polar (1.0, -w), e2 = std::polar (1.0, -2.0 * w);
        return std::abs ((c.b0 + c.b1 * e1 + c.b2 * e2) / (1.0 + c.a1 * e1 + c.a2 * e2));
    }

    // ---- Diseño (RBJ Audio EQ Cookbook) ----
    static Coeffs norm (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    static Coeffs rbjPeak (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }

    static Coeffs rbjLowShelf (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q), be = 2.0 * std::sqrt (A) * al;
        return norm (A * ((A + 1) - (A - 1) * cs + be),
                     2 * A * ((A - 1) - (A + 1) * cs),
                     A * ((A + 1) - (A - 1) * cs - be),
                     (A + 1) + (A - 1) * cs + be,
                     -2 * ((A - 1) + (A + 1) * cs),
                     (A + 1) + (A - 1) * cs - be);
    }

    static Coeffs rbjHighShelf (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q), be = 2.0 * std::sqrt (A) * al;
        return norm (A * ((A + 1) + (A - 1) * cs + be),
                     -2 * A * ((A - 1) + (A + 1) * cs),
                     A * ((A + 1) + (A - 1) * cs - be),
                     (A + 1) - (A - 1) * cs + be,
                     2 * ((A - 1) - (A + 1) * cs),
                     (A + 1) - (A - 1) * cs - be);
    }

    // ---- Respuesta cerca de Nyquist sin sobremuestreo ----
    // El bilineal (RBJ) deforma campanas y shelves cerca de Nyquist. Aqui se prueban tres disenos con el mismo prototipo analogico
    // (RBJ, polos por transformada z igualada y polos RBJ con ceros ajustados a la ganancia analogica en DC, f0 y Nyquist) y se elige
    // el que mas se parece al analogico en toda la banda; por debajo de fs/12 se usa RBJ directamente (ahi no hay deformacion apreciable).
    enum Proto { ProtoPeak = 0, ProtoLow, ProtoHigh };

    static std::complex<double> analogProto (int t, double A, double q, double u) noexcept
    {
        const std::complex<double> s (0.0, u);
        if (t == ProtoPeak) return (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
        if (t == ProtoLow)  return A * (s * s + s * (std::sqrt (A) / q) + A) / (A * s * s + s * (std::sqrt (A) / q) + 1.0);
        return A * (A * s * s + s * (std::sqrt (A) / q) + 1.0) / (s * s + s * (std::sqrt (A) / q) + A);
    }

    static Coeffs matchedDesign (int t, double fs, double f, double q, double dB, const Coeffs* poleSource) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), O0 = 2.0 * kPi * f, T = 1.0 / fs, w0 = O0 * T;
        double a1, a2;
        if (poleSource != nullptr) { a1 = poleSource->a1; a2 = poleSource->a2; }
        else
        {
            double d2, d1, d0;
            if (t == ProtoPeak)      { d2 = 1.0; d1 = 1.0 / (A * q);        d0 = 1.0; }
            else if (t == ProtoLow)  { d2 = A;   d1 = std::sqrt (A) / q;    d0 = 1.0; }
            else                     { d2 = 1.0; d1 = std::sqrt (A) / q;    d0 = A; }
            const std::complex<double> disc = std::sqrt (std::complex<double> (d1 * d1 - 4.0 * d2 * d0, 0.0));
            const std::complex<double> r1 = (-d1 + disc) / (2.0 * d2), r2 = (-d1 - disc) / (2.0 * d2);
            a1 = -(std::exp (r1 * w0) + std::exp (r2 * w0)).real();
            a2 =  (std::exp (r1 * w0) * std::exp (r2 * w0)).real();
        }
        auto asq = [&] (double w)
        {
            const double p1 = std::sin (w / 2) * std::sin (w / 2), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
            return (1 + a1 + a2) * (1 + a1 + a2) * p0 + (1 - a1 + a2) * (1 - a1 + a2) * p1 - 4.0 * a2 * p2;
        };
        auto hsq = [&] (double w) { return std::norm (analogProto (t, A, q, (w / T) / O0)); };
        const double p1 = std::sin (w0 / 2) * std::sin (w0 / 2), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
        const double B0 = hsq (1.0e-9) * (1 + a1 + a2) * (1 + a1 + a2), B1 = hsq (kPi) * (1 - a1 + a2) * (1 - a1 + a2);
        const double B2 = (hsq (w0) * asq (w0) - B0 * p0 - B1 * p1) / p2;
        const double s0 = std::sqrt (B0), s1 = std::sqrt (B1), W = 0.5 * (s0 + s1);
        const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2))), b1 = 0.5 * (s0 - s1);
        const double b2 = b0 > 1.0e-12 ? -B2 / (4.0 * b0) : 0.0;
        return { b0, b1, b2, a1, a2 };
    }

    static double devFromAnalog (const Coeffs& c, int t, double A, double q, double f, double fs) noexcept
    {
        Biquad b; b.c = c;
        double e = 0.0;
        for (int i = 0; i < 20; ++i)
        {
            const double fr = 0.499 * fs * std::pow (20.0 / (0.499 * fs), i / 19.0);
            const double w = 2.0 * kPi * fr / fs;
            e = std::max (e, std::abs (20.0 * std::log10 (std::max (b.magnitude (w), 1.0e-9)) - 20.0 * std::log10 (std::max (std::abs (analogProto (t, A, q, fr / f)), 1.0e-9))));
        }
        return e;
    }

    static Coeffs bestDesign (int t, double fs, double f, double q, double dB) noexcept
    {
        const Coeffs r = t == ProtoPeak ? rbjPeak (fs, f, q, dB) : t == ProtoLow ? rbjLowShelf (fs, f, q, dB) : rbjHighShelf (fs, f, q, dB);
        if (f < fs / 12.0 || std::abs (dB) < 0.2) return r;
        const double A = std::pow (10.0, dB / 40.0);
        const Coeffs m1 = matchedDesign (t, fs, f, q, dB, nullptr), m2 = matchedDesign (t, fs, f, q, dB, &r);
        auto ok = [] (const Coeffs& c) { return std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2) && std::isfinite (c.a1) && std::isfinite (c.a2)
                                             && std::abs (c.a2) < 0.9999 && std::abs (c.a1) < 1.0 + c.a2; };
        double er = devFromAnalog (r, t, A, q, f, fs), best = er; Coeffs out = r;
        if (ok (m1)) { const double e = devFromAnalog (m1, t, A, q, f, fs); if (e < best) { best = e; out = m1; } }
        if (ok (m2)) { const double e = devFromAnalog (m2, t, A, q, f, fs); if (e < best) { best = e; out = m2; } }
        return out;
    }

    static Coeffs peak      (double fs, double f, double q, double dB) noexcept { return bestDesign (ProtoPeak, fs, f, q, dB); }
    static Coeffs lowShelf  (double fs, double f, double q, double dB) noexcept { return bestDesign (ProtoLow,  fs, f, q, dB); }
    static Coeffs highShelf (double fs, double f, double q, double dB) noexcept { return bestDesign (ProtoHigh, fs, f, q, dB); }

    static Coeffs lowpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
    }

    static Coeffs highpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
    }

    static Coeffs bandpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (al, 0, -al, 1 + al, -2 * cs, 1 - al);
    }

    // Sección de 2º orden de un filtro de corte (pasa-altos o pasa-bajos) a partir del prototipo pasa-bajos normalizado (fc = 1).
    // C = 1/tan(pi*fc/fs): pre-warp global, así todas las secciones comparten la misma frecuencia de corte (bilinear exacta).
    static Coeffs cutSection (double C, double w0, double q, bool highpass) noexcept
    {
        const double c2 = C * C;
        double w, n0, n1, n2;
        if (highpass) { w = 1.0 / w0; n0 = c2;      n1 = -2.0 * c2;  n2 = c2; }
        else          { w = w0;       n0 = w * w;   n1 = 2.0 * w * w; n2 = w * w; }
        const double w2 = w * w, k = (w / q) * C;
        return norm (n0, n1, n2, c2 + k + w2, 2.0 * (w2 - c2), c2 - k + w2);
    }

    static Coeffs firstOrderLP (double fs, double f) noexcept
    {
        const double K = std::tan (kPi * f / fs), n = 1.0 / (1.0 + K);
        return { K * n, K * n, 0.0, (K - 1.0) * n, 0.0 };
    }

    static Coeffs firstOrderHP (double fs, double f) noexcept
    {
        const double K = std::tan (kPi * f / fs), n = 1.0 / (1.0 + K);
        return { n, -n, 0.0, (K - 1.0) * n, 0.0 };
    }
};
} // namespace titq
