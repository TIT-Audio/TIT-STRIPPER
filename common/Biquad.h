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

    void set (const Coeffs& n) noexcept { c = n; }
    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    void process (float* x, int n, int ch) noexcept
    {
        double l1 = z1[ch], l2 = z2[ch];
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const double out = c.b0 * in + l1;
            l1 = c.b1 * in - c.a1 * out + l2;
            l2 = c.b2 * in - c.a2 * out;
            x[i] = (float) out;
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

    static Coeffs peak (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }

    static Coeffs lowShelf (double fs, double f, double q, double dB) noexcept
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

    static Coeffs highShelf (double fs, double f, double q, double dB) noexcept
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
