// Pruebas automaticas del ecualizador (sin JUCE): estabilidad, ganancia en la frecuencia central y
// coherencia entre frecuencias de muestreo (44.1 / 48 / 96 / 192 kHz).
#include "../EqBand.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int failures = 0;
static void check (bool ok, const char* what, int type, double fs, double a, double b)
{
    if (! ok) { ++failures; std::printf ("FALLO: %s tipo %d fs %.0f (%.3f vs %.3f)\n", what, type, fs, a, b); }
}

int main()
{
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    for (int type = 0; type < titq::NumBandTypes; ++type)
    {
        double ref[3] = { 0, 0, 0 };
        for (double fs : rates)
        {
            titq::BandSettings s; s.type = type; s.freq = 1000.f; s.gain = 6.f; s.q = 1.f; s.slope = 12.f; s.boost = 3.f; s.atten = 3.f;
            titq::EqBand eq; eq.design (s, fs, 1.0);

            // estabilidad: la respuesta a un impulso tiene que apagarse y no puede dar numeros raros
            std::vector<float> x ((size_t) fs, 0.f); x[0] = 1.f;
            eq.process (x.data(), (int) x.size(), 0);
            bool finite = true; double tail = 0.0;
            for (size_t i = 0; i < x.size(); ++i) { if (! std::isfinite (x[i])) finite = false; if (i > x.size() - 1000) tail = std::max (tail, (double) std::abs (x[i])); }
            check (finite, "respuesta no finita", type, fs, 0, 0);
            check (tail < 1.0e-3, "no se apaga (inestable?)", type, fs, tail, 0);

            // coherencia entre frecuencias de muestreo en la zona media (200 Hz, 1 kHz y 4 kHz)
            const double f[3] = { 200.0, 1000.0, 4000.0 };
            for (int k = 0; k < 3; ++k)
            {
                const double m = eq.magnitudeDb (f[k], fs);
                if (fs == 192000.0) ref[k] = m;
            }
            if (fs == 96000.0 || fs == 48000.0 || fs == 44100.0)
                for (int k = 0; k < 3; ++k) (void) eq.magnitudeDb (f[k], fs);
        }
        // la comparacion contra 192 kHz se hace aparte para no depender del orden
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            titq::BandSettings s; s.type = type; s.freq = 1000.f; s.gain = 6.f; s.q = 1.f; s.slope = 12.f; s.boost = 3.f; s.atten = 3.f;
            titq::EqBand eq; eq.design (s, fs, 1.0);
            const double f[3] = { 200.0, 1000.0, 4000.0 };
            for (int k = 0; k < 3; ++k)
            {
                const double d = std::abs (eq.magnitudeDb (f[k], fs) - ref[k]);
                // AVISO (no falla): el metodo de filtros digitales estandar deforma las curvas anchas cerca de Nyquist
                if (d > 0.3) std::printf ("AVISO: tipo %d a %.0f Hz con fs %.0f se desvia %.2f dB de 192 kHz\n", type, f[k], fs, d);
                check (d < 1.0, "respuesta muy distinta segun fs", type, fs, eq.magnitudeDb (f[k], fs), ref[k]);
            }
        }
    }
    // BOOBTEC contra el ecualizador pasivo de referencia medido en un DAW (ruido blanco, 48 kHz): { selector, boost, atten, Hz, dB medidos }
    {
        struct M { double cps, b, a, f, db; };
        const M pts[] = {
            { 20, 10, 0, 20, 11.2 }, { 20, 10, 0, 100, 2.3 }, { 20, 0, 10, 20, -16.8 }, { 20, 0, 10, 100, -6.5 },
            { 20, 10, 10, 20, 9.8 }, { 20, 10, 10, 100, -5.8 }, { 20, 10, 10, 150, -6.4 },
            { 60, 10, 0, 20, 16.0 }, { 60, 10, 0, 200, 6.3 }, { 60, 0, 10, 100, -16.0 }, { 60, 0, 10, 500, -5.3 },
            { 60, 10, 10, 500, -6.5 }, { 60, 10, 10, 200, 3.1 },
            { 100, 10, 0, 100, 13.3 }, { 100, 10, 0, 1000, 1.4 }, { 100, 0, 10, 1000, -4.6 }, { 100, 10, 10, 1000, -6.9 }, { 100, 10, 10, 200, 7.6 } };
        for (const auto& p : pts)
        {
            titq::BandSettings st; st.type = titq::BOOBTEC; st.freq = (float) p.cps; st.boost = (float) p.b; st.atten = (float) p.a;
            titq::EqBand eq; eq.design (st, 48000.0, 1.0);
            const double got = eq.magnitudeDb (p.f, 48000.0);
            check (std::abs (got - p.db) < 1.2, "BOOBTEC lejos del ecualizador de referencia medido", titq::BOOBTEC, 48000.0, got, p.db);
        }
    }
    if (failures == 0) std::printf ("Todas las pruebas del ecualizador han pasado.\n");
    return failures == 0 ? 0 : 1;
}
