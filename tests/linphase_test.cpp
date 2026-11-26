// Pruebas automaticas de la fase lineal (sin JUCE): FFT, filtro identidad, magnitud contra el EQ de fase minima,
// simetria (fase lineal de verdad), Mid/Side, matriz completa, cambio de filtro sin saltos y coste de CPU.
#include "../common/LinearPhaseCore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace titq;

static int failures = 0;
static void check (bool ok, const char* what, double a = 0, double b = 0)
{
    if (! ok) { ++failures; std::printf ("FALLO: %s (%.5f vs %.5f)\n", what, a, b); }
}

static LpBandSnap band (int type, double f, double g, double q, int ch = 0, double slope = 12.0)
{
    LpBandSnap s; s.active = true; s.fade = 1.f; s.chmode = ch;
    s.eff.on = true; s.eff.type = type; s.eff.freq = (float) f; s.eff.gain = (float) g; s.eff.q = (float) q; s.eff.slope = (float) slope;
    return s;
}

// pasa una señal por el convolver (en bloques de B) y devuelve la salida (con la latencia N/2 incluida)
static void run (LpConvolver& cv, int B, const LpKernelSet& k, const LpKernelSet* kPrev, std::vector<float>& l, std::vector<float>& r, int nch)
{
    const size_t n = l.size();
    std::vector<float> ol (n, 0.f), orr (n, 0.f), zl ((size_t) B), zr ((size_t) B);
    for (size_t pos = 0; pos + (size_t) B <= n; pos += (size_t) B)
        cv.processBlock (l.data() + pos, nch > 1 ? r.data() + pos : nullptr, ol.data() + pos, orr.data() + pos, nch, k, kPrev);
    l = ol; if (nch > 1) r = orr;
}

// amplitud (pico de la envolvente) de una senal en el tramo central
static double amp (const std::vector<float>& x, size_t from, size_t to)
{
    double a = 0.0;
    for (size_t i = from; i < to; ++i) a = std::max (a, (double) std::abs (x[i]));
    return a;
}

int main()
{
    // ---- FFT contra DFT ----
    {
        LpFft<double> f; f.init (64);
        std::vector<double> x (128), ref (128);
        std::mt19937 rng (1); std::uniform_real_distribution<double> u (-1, 1);
        for (auto& v : x) v = u (rng);
        for (int k = 0; k < 64; ++k)
        {
            double re = 0, im = 0;
            for (int n = 0; n < 64; ++n) { const double a = -2 * kPi * k * n / 64.0; re += x[2*n] * std::cos (a) - x[2*n+1] * std::sin (a); im += x[2*n] * std::sin (a) + x[2*n+1] * std::cos (a); }
            ref[2*k] = re; ref[2*k+1] = im;
        }
        f.transform (x.data(), false);
        double err = 0; for (int i = 0; i < 128; ++i) err = std::max (err, std::abs (x[(size_t) i] - ref[(size_t) i]));
        check (err < 1e-9, "FFT contra DFT", err, 0);
    }

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LpDesigner D; D.prepare (fs);
        LpConvolver cv; cv.prepare (fs);
        const int B = lpBlockSize (fs);
        std::printf ("--- fs %.0f ---\n", fs);

        for (int q = 0; q < 4; ++q)
        {
            const int N = lpFirLength (q, fs), P = N / B;
            LpKernelSet K; K.allocate (B);

            // identidad: un impulso sale retrasado exactamente N/2
            D.makeIdentity (K, q);
            cv.configure (P);
            std::vector<float> l ((size_t) N * 2, 0.f), r;
            l[0] = 1.f;
            run (cv, B, K, nullptr, l, r, 1);
            size_t pk = 0; for (size_t i = 0; i < l.size(); ++i) if (std::abs (l[i]) > std::abs (l[pk])) pk = i;
            check (pk == (size_t) N / 2 && std::abs (l[pk] - 1.f) < 1e-3, "identidad: impulso en N/2", (double) pk, N / 2);

            // el diseño con una instantanea vacia tambien es la identidad
            LpSnapshot empty; D.design (empty, q, K);
            cv.configure (P);
            std::fill (l.begin(), l.end(), 0.f); l[0] = 1.f;
            run (cv, B, K, nullptr, l, r, 1);
            pk = 0; for (size_t i = 0; i < l.size(); ++i) if (std::abs (l[i]) > std::abs (l[pk])) pk = i;
            check (pk == (size_t) N / 2 && std::abs (l[pk] - 1.f) < 1e-3, "vacio: impulso en N/2", (double) pk, N / 2);
        }

        // ---- magnitud: respuesta del FIR contra la de las bandas ----
        for (int q : { 1, 3 })
        {
            const int N = lpFirLength (q, fs), P = N / B;
            LpKernelSet K; K.allocate (B);
            LpSnapshot s;
            s.band[0] = band (Bell, 1000, 12, 1.0);
            s.band[1] = band (LowShelf, 120, 6, 0.7);
            s.band[2] = band (HighCut, 9000, 0, 0.7, 0, 48.0);
            s.band[3] = band (LowCut, 40, 0, 0.7, 0, 24.0);
            s.band[4] = band (Bell, 3000, -9, 4.0);
            s.band[5] = band (BOOBTEC, 60, 0, 1.0);
            s.band[5].eff.boost = 6.f; s.band[5].eff.atten = 3.f;
            D.design (s, q, K);
            cv.configure (P);

            std::vector<float> l ((size_t) N * 2 + (size_t) B * 4, 0.f), r;
            l[0] = 1.f;
            run (cv, B, K, nullptr, l, r, 1);

            // simetria alrededor de N/2
            double asym = 0.0, peak = 0.0;
            for (int k = 1; k < N / 2 - 2; ++k) { asym = std::max (asym, (double) std::abs (l[(size_t) (N/2 + k)] - l[(size_t) (N/2 - k)])); peak = std::max (peak, (double) std::abs (l[(size_t) (N/2 + k)])); }
            check (asym < 1e-5 * std::max (1.0, peak) + 1e-6, "FIR simetrico (fase lineal)", asym, peak);

            // magnitud con una DFT directa en frecuencias elegidas
            double worst = 0.0, worstF = 0.0;
            EqBand eqs[6];
            for (int i = 0; i < 6; ++i) eqs[i].design (s.band[(size_t) i].eff, fs * 4.0, 1.0);
            for (double f : { 25.0, 40.0, 60.0, 90.0, 150.0, 300.0, 600.0, 900.0, 1000.0, 1200.0, 2000.0, 3000.0, 3500.0, 5000.0, 8000.0, 9000.0, 11000.0, 15000.0, 19000.0 })
            {
                if (f > 0.45 * fs) continue;
                double re = 0, im = 0;
                for (int n = 0; n < N; ++n) { const double a = -2 * kPi * f * n / fs; re += l[(size_t) n] * std::cos (a); im += l[(size_t) n] * std::sin (a); }
                const double got = 20.0 * std::log10 (std::hypot (re, im) + 1e-12);
                double want = 0.0; for (auto& e : eqs) want += e.magnitudeDb (f, fs * 4.0);
                const double err = std::abs (got - want);
                // el filtro de corte profundo en los extremos tiene un suelo; se compara solo donde la respuesta es mayor de -60 dB
                if (std::getenv ("LP_VERBOSE")) std::printf ("   f %7.0f  fir %8.3f  banda %8.3f  err %6.3f\n", f, got, want, err);
                if (f < (q >= 3 ? 20.0 : 60.0)) continue;   // por debajo, la resolucion del filtro (fs/N) limita la precision
                if (want > -60.0 && err > worst) { worst = err; worstF = f; }
            }
            std::printf ("calidad %d  N %d  latencia %d  error maximo de magnitud %.3f dB (a %.0f Hz)\n", q, N, lpLatency (q, fs), worst, worstF);
            check (worst < (q >= 2 ? 0.6 : 1.5), "magnitud del FIR contra las bandas", worst, worstF);
        }

        // ---- Mid / Side: una banda Side no toca el contenido mono, y una Mid no toca el contenido solo-side ----
        {
            const int q = 1, N = lpFirLength (q, fs), P = N / B;
            LpKernelSet K; K.allocate (B);
            LpSnapshot s; s.band[0] = band (Bell, 1000, 12, 1.0, 2);     // Side +12 dB en 1 kHz
            D.design (s, q, K);
            check (K.domain == 1, "dominio Mid/Side", K.domain, 1);
            const size_t len = (size_t) N * 4;
            std::vector<float> l (len), r (len);
            for (size_t i = 0; i < len; ++i) { const float v = (float) std::sin (2 * kPi * 1000.0 * (double) i / fs); l[i] = v; r[i] = v; }
            cv.configure (P); run (cv, B, K, nullptr, l, r, 2);
            check (std::abs (amp (l, len / 2, len) - 1.0) < 0.02 && std::abs (amp (r, len / 2, len) - 1.0) < 0.02, "Side no toca el centro", amp (l, len/2, len), 1.0);

            for (size_t i = 0; i < len; ++i) { const float v = (float) std::sin (2 * kPi * 1000.0 * (double) i / fs); l[i] = v; r[i] = -v; }
            cv.configure (P); run (cv, B, K, nullptr, l, r, 2);
            check (std::abs (amp (l, len / 2, len) - 3.98) < 0.1, "Side +12 dB sobre contenido side", amp (l, len/2, len), 3.98);
        }

        // ---- matriz completa: banda solo-L (+6 dB) y banda Mid (-6 dB) a la vez ----
        {
            const int q = 1, N = lpFirLength (q, fs), P = N / B;
            LpKernelSet K; K.allocate (B);
            LpSnapshot s; s.band[0] = band (Bell, 1000, 6, 1.0, 3); s.band[1] = band (Bell, 1000, -6, 1.0, 1);
            D.design (s, q, K);
            check (K.domain == 2, "dominio matriz completa", K.domain, 2);
            const size_t len = (size_t) N * 4;
            std::vector<float> l (len), r (len, 0.f);
            for (size_t i = 0; i < len; ++i) l[i] = (float) std::sin (2 * kPi * 1000.0 * (double) i / fs);
            cv.configure (P); run (cv, B, K, nullptr, l, r, 2);
            const double h6 = std::pow (10.0, 6.0 / 20.0), hm = std::pow (10.0, -6.0 / 20.0);
            const double M = 0.5 * h6 * hm, S = 0.5 * h6;                  // L entra con 1: tras la banda L queda h6; Mid x hm
            check (std::abs (amp (l, len / 2, len) - (M + S) * 1.0) < 0.05, "matriz: canal L", amp (l, len/2, len), (M + S));
            check (std::abs (amp (r, len / 2, len) - std::abs (M - S)) < 0.05, "matriz: canal R", amp (r, len/2, len), std::abs (M - S));
        }

        // ---- cambio de filtro: el fundido no produce saltos ni valores raros ----
        {
            const int q = 1, N = lpFirLength (q, fs), P = N / B;
            LpKernelSet A, Bk; A.allocate (B); Bk.allocate (B);
            LpSnapshot s0, s1; s0.band[0] = band (Bell, 1000, 0, 1.0); s1.band[0] = band (Bell, 1000, 12, 1.0);
            D.design (s0, q, A); D.design (s1, q, Bk);
            const size_t len = (size_t) N * 6;
            std::vector<float> in (len), out (len, 0.f), r;
            for (size_t i = 0; i < len; ++i) in[i] = (float) std::sin (2 * kPi * 1000.0 * (double) i / fs);
            cv.configure (P);
            double maxJump = 0.0; bool finite = true; float last = 0.f; (void) last;
            for (size_t pos = 0, blk = 0; pos + (size_t) B <= len; pos += (size_t) B, ++blk)
            {
                const bool sw = blk >= len / (size_t) B / 2;
                const LpKernelSet& cur = sw ? Bk : A;
                const LpKernelSet* prev = (blk == len / (size_t) B / 2) ? &A : nullptr;
                cv.processBlock (in.data() + pos, nullptr, out.data() + pos, nullptr, 1, cur, prev);
            }
            size_t jumpAt = 0; last = out[(size_t) N - 1];
            for (size_t i = (size_t) N; i < len; ++i) { if (! std::isfinite (out[i])) finite = false; const double j = std::abs (out[i] - last); if (j > maxJump) { maxJump = j; jumpAt = i; } last = out[i]; }
            std::printf ("  salto maximo %.3f en la muestra %zu (cambio en %zu, amplitud local %.2f)\n", maxJump, jumpAt, len / 2, amp (out, jumpAt > 20 ? jumpAt - 20 : 0, jumpAt + 20));
            check (finite, "cambio de filtro: salida finita");
            const double natural = 3.98 * 2.0 * std::sin (kPi * 1000.0 / fs) * 1.1;   // el paso maximo de un seno de esa amplitud
            check (maxJump < natural, "cambio de filtro: sin saltos", maxJump, natural);
            check (std::abs (amp (out, len - (size_t) N / 2, len) - 3.98) < 0.1, "cambio de filtro: llega al nuevo filtro", amp (out, len - (size_t) N/2, len), 3.98);
        }
    }

    // ---- coste de CPU: calidad maxima, matriz completa y fundido (el peor caso) ----
    {
        const double fs = 48000.0; const int B = lpBlockSize (fs), q = 3, N = lpFirLength (q, fs), P = N / B;
        LpDesigner D; D.prepare (fs);
        LpConvolver cv; cv.prepare (fs); cv.configure (P);
        LpKernelSet A, Bk; A.allocate (B); Bk.allocate (B);
        LpSnapshot s; for (int i = 0; i < 8; ++i) s.band[(size_t) i] = band (Bell, 200.0 * (i + 1), 3, 1.0, i % 5);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) D.design (s, q, A);
        auto t1 = std::chrono::steady_clock::now();
        D.design (s, q, Bk);
        std::vector<float> in ((size_t) B), o1 ((size_t) B), o2 ((size_t) B);
        for (auto& v : in) v = 0.1f;
        auto t2 = std::chrono::steady_clock::now();
        const int reps = 2000;
        for (int i = 0; i < reps; ++i) cv.processBlock (in.data(), in.data(), o1.data(), o2.data(), 2, A, (i & 1) ? &Bk : nullptr);
        auto t3 = std::chrono::steady_clock::now();
        const double dsg = std::chrono::duration<double, std::milli> (t1 - t0).count() / 10.0;
        const double blk = std::chrono::duration<double, std::micro> (t3 - t2).count() / reps;
        std::printf ("diseno (calidad Max, 8 bandas, matriz): %.2f ms   convolucion: %.0f us por bloque de %d (%.1f %% de CPU en tiempo real, con fundido la mitad de los bloques)\n",
                     dsg, blk, B, 100.0 * blk / (1.0e6 * B / fs));
    }

    if (failures == 0) std::printf ("TODO OK\n");
    return failures == 0 ? 0 : 1;
}
