// Pruebas del nucleo del Gate / Expander (sin JUCE)
#include "../gate/GateCore.h"
#include <cstdio>
static int failures = 0;
static void check (bool ok, const char* what, double a, double b)
{
    if (! ok) { ++failures; std::printf ("FALLO: %s (%.3f vs %.3f)\n", what, a, b); }
}
int main()
{
    // curva estatica
    titg::Settings s; s.thr = -40; s.range = 30; s.ratio = 2; s.hystDb = 0;
    check (std::abs (titg::staticGr (-50, s, false) - 10.0) < 1e-9, "expansor 2:1, 10 dB bajo el umbral", titg::staticGr (-50, s, false), 10.0);
    check (titg::staticGr (-30, s, false) == 0.0, "sobre el umbral no atenua", titg::staticGr (-30, s, false), 0.0);
    s.ratio = 100;
    check (std::abs (titg::staticGr (-45, s, false) - 30.0) < 1e-9, "gate duro llega al rango", titg::staticGr (-45, s, false), 30.0);
    s.hystDb = 6;
    check (titg::staticGr (-43, s, true) == 0.0 && titg::staticGr (-43, s, false) > 0.0, "histeresis", titg::staticGr (-43, s, true), titg::staticGr (-43, s, false));

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        titg::Gate g; g.prepare (fs);
        titg::Settings t; t.thr = -40; t.range = 60; t.ratio = 100; t.attackMs = 0.5; t.holdMs = 50; t.releaseMs = 100; t.hystDb = 3;
        g.setSettings (t);
        const int N = (int) (fs * 3.0);
        std::vector<float> out ((size_t) N);
        auto inSig = [&] (int i) -> float
        {   // 0-1 s silencio con ruido a -70 dB; 1-2 s seno 1 kHz a -10 dBFS; 2-3 s silencio
            const double t0 = i / fs;
            const double a = (t0 >= 1.0 && t0 < 2.0) ? std::pow (10.0, -10.0 / 20.0) : std::pow (10.0, -70.0 / 20.0);
            return (float) (a * std::sin (2.0 * 3.14159265358979 * 1000.0 * t0));
        };
        for (int i = 0; i < N; ++i)
        {
            const float x[2] { inSig (i), inSig (i) }; float y[2], k[2];
            g.process (x, x, 2, y, k);
            out[(size_t) i] = y[0];
        }
        auto peak = [&] (double a, double b) { float p = 0; for (int i = (int) (a * fs); i < (int) (b * fs); ++i) p = std::max (p, std::fabs (out[(size_t) i])); return 20.0 * std::log10 (p + 1e-12); };
        check (peak (0.2, 0.9) < -120.0 + 60.0 - 70.0 + 20.0, "cerrado: -70 dB atenuado ~60 dB", peak (0.2, 0.9), -110.0);
        check (std::abs (peak (1.1, 1.9) - (-10.0)) < 0.3, "abierto: pasa sin atenuar", peak (1.1, 1.9), -10.0);
        check (peak (2.0, 2.08) > -72.0, "hold: sigue abierto tras cortar la senal", peak (2.0, 2.08), -70.0);
        check (peak (2.5, 3.0) < -50.0, "tras el release queda cerrado", peak (2.5, 3.0), -50.0);
    }
    // lookahead: el impulso sale retrasado
    {
        titg::Gate g; g.prepare (48000.0); titg::Settings t; t.thr = -90; t.lookMs = 2.0; g.setSettings (t);
        int first = -1;
        for (int i = 0; i < 400; ++i)
        {
            const float x[2] { i == 10 ? 1.f : 0.f, 0.f }, kk[2] { x[0], x[1] }; float y[2], k[2];
            g.process (x, kk, 2, y, k);
            if (first < 0 && std::fabs (y[0]) > 0.1f) first = i;
        }
        check (first == 10 + g.latencySamples() && g.latencySamples() == 96, "lookahead 2 ms = 96 muestras", first, 106);
    }
    if (failures == 0) std::printf ("Todas las pruebas del gate han pasado.\n");
    return failures != 0;
}
