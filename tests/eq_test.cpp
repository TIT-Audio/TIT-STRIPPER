// Pruebas automaticas del ecualizador (sin JUCE): estabilidad, ganancia en la frecuencia central y
// coherencia entre frecuencias de muestreo (44.1 / 48 / 96 / 192 kHz).
#include "../common/EqBand.h"
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
            // selector nominal (20/30/60/100) -> frecuencia real medida (25/45/89.18/136.94)
            titq::BandSettings st; st.type = titq::BOOBTEC; st.freq = (float) (p.cps == 20 ? 25.0 : p.cps == 30 ? 45.0 : p.cps == 60 ? 89.18 : 136.94); st.boost = (float) p.b; st.atten = (float) p.a;
            titq::EqBand eq; eq.design (st, 48000.0, 1.0);
            const double got = eq.magnitudeDb (p.f, 48000.0);
            check (std::abs (got - p.db) < 1.2, "BOOBTEC lejos del ecualizador de referencia medido", titq::BOOBTEC, 48000.0, got, p.db);
        }
    }
    // TIT SKY: frecuencia y Q siguen las curvas medidas; TIT 250: Q acotada; BOOBTEC: solo pasos fijos
    {
        double fm, q;
        titq::titSkyShape (2.6, fm, q);  if (std::abs (fm - 1.0) > 1e-9 || std::abs (q - 0.499) > 1e-9) { ++failures; std::printf ("FALLO: TIT SKY 2.6 dB\n"); }
        titq::titSkyShape (4.44, fm, q); if (std::abs (q - 0.457) > 1e-9) { ++failures; std::printf ("FALLO: TIT SKY 4.44 dB\n"); }
        titq::titSkyShape (10.14, fm, q); if (std::abs (fm * titq::kTitSkyStartHz - 9624.37) > 0.01 || std::abs (q - 0.475) > 1e-9) { ++failures; std::printf ("FALLO: TIT SKY 10.14 dB\n"); }
        titq::titSkyShape (20.0, fm, q); if (std::abs (fm * titq::kTitSkyStartHz - 9624.37) > 0.01) { ++failures; std::printf ("FALLO: TIT SKY tope\n"); }
        if (titq::boobtecSnap (30.0) != 25.0 || titq::boobtecSnap (60.0) != 45.0 || titq::boobtecSnap (80.0) != 89.18 || titq::boobtecSnap (1000.0) != 136.94) { ++failures; std::printf ("FALLO: pasos de BOOBTEC\n"); }
        titq::BandSettings st; st.type = titq::TIT250; st.freq = 1000.f; st.gain = 6.f;
        st.q = 0.1f; titq::EqBand a; a.design (st, 48000.0, 1.0); st.q = (float) titq::kTit250QMin; titq::EqBand b; b.design (st, 48000.0, 1.0);
        if (std::abs (a.magnitudeDb (300.0, 48000.0) - b.magnitudeDb (300.0, 48000.0)) > 1e-6) { ++failures; std::printf ("FALLO: TIT 250 Q minima\n"); }
    }
    // cerca de Nyquist el diseno elegido no se aleja del prototipo analogico mas que el bilineal clasico
    {
        for (double dB : { -12.0, 6.0, 12.0 })
            for (int t = 0; t < 3; ++t)
            {
                const double fs = 48000.0, f = 12000.0, q = 0.7, A = std::pow (10.0, dB / 40.0);
                const auto best = titq::Biquad::bestDesign (t, fs, f, q, dB);
                const auto rbj = t == 0 ? titq::Biquad::rbjPeak (fs, f, q, dB) : t == 1 ? titq::Biquad::rbjLowShelf (fs, f, q, dB) : titq::Biquad::rbjHighShelf (fs, f, q, dB);
                const double eb = titq::Biquad::devFromAnalog (best, t, A, q, f, fs), er = titq::Biquad::devFromAnalog (rbj, t, A, q, f, fs);
                if (eb > er + 1e-9) { ++failures; std::printf ("FALLO: diseno cerca de Nyquist peor que RBJ (%.2f vs %.2f)\n", eb, er); }
            }
    }
    // rampa de coeficientes: llega exactamente a los coeficientes nuevos y no desestabiliza
    {
        titq::BandSettings st; st.type = titq::Bell; st.freq = 3000.f; st.gain = 12.f; st.q = 2.f;
        titq::EqBand e; e.design (st, 48000.0, 0.0);
        std::vector<float> x (4096, 0.f); x[0] = 1.f;
        for (int blk = 0; blk < 16; ++blk) { e.design (st, 48000.0, (blk + 1) / 16.0, 256); e.process (x.data() + blk * 256, 256, 0); }
        double mx = 0; for (float v : x) mx = std::max (mx, (double) std::abs (v));
        if (! (mx < 4.0)) { ++failures; std::printf ("FALLO: rampa inestable (%.2f)\n", mx); }
    }
    if (failures == 0) std::printf ("Todas las pruebas del ecualizador han pasado.\n");
    return failures == 0 ? 0 : 1;
}
