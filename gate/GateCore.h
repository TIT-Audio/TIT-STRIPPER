#pragma once
// Nucleo del Gate / Expander de TIT STRIPPER (sin JUCE, para poder probarlo).
// Detector de pico con caida corta, curva de expansor descendente (ratio 1..inf), histeresis, hold, ataque / release,
// filtros de paso alto y bajo en la senal de deteccion (sidechain) y lookahead.
#include <algorithm>
#include <cmath>
#include <vector>
#include "../common/Biquad.h"

namespace titg
{
struct Settings
{
    double thr = -50.0;        // umbral (dBFS)
    double range = 90.0;       // atenuacion maxima (dB, >= 0)
    double ratio = 100.0;      // 1..100 (>= 99 = gate duro)
    double attackMs = 0.5;     // apertura
    double holdMs = 50.0;
    double releaseMs = 150.0;  // cierre
    double hystDb = 3.0;       // histeresis: una vez abierto, cierra 'hyst' dB por debajo del umbral
    double lookMs = 0.0;       // lookahead (latencia)
    double hpHz = 20.0;        // filtros de la senal de deteccion
    double lpHz = 20000.0;
    double link = 1.0;         // 0..1 enlace estereo
    bool   eqOn = false;       // EQ de 3 bandas en la senal de deteccion (low shelf, campana, high shelf)
    double eqF[3] { 100.0, 1000.0, 6000.0 };
    double eqG[3] { 0.0, 0.0, 0.0 };
    double eqQ[3] { 0.7, 1.0, 0.7 };
};

constexpr double kDetDecayMs = 10.0;   // caida del detector de pico

// Atenuacion (dB, >= 0) de la curva estatica; 'open' desplaza el umbral por la histeresis
inline double staticGr (double levelDb, const Settings& s, bool open) noexcept
{
    const double thrEff = open ? s.thr - s.hystDb : s.thr;
    const double under = thrEff - levelDb;
    if (under <= 0.0) return 0.0;
    return std::min (std::max (0.0, s.range), under * (std::max (1.0, s.ratio) - 1.0));
}

// Respuesta (dB) del filtrado de la senal de deteccion: paso alto + paso bajo + EQ de 3 bandas (si esta activo). La usa el editor para dibujar la curva.
inline double scResponseDb (const Settings& s, double f, double fs) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * std::min (f, 0.499 * fs) / fs;
    titq::Biquad b;
    double db = 0.0;
    auto add = [&] (const titq::Biquad::Coeffs& c) { b.set (c); db += 20.0 * std::log10 (std::max (1.0e-9, b.magnitude (w))); };
    add (titq::Biquad::highpass (fs, std::clamp (s.hpHz, 10.0, 0.45 * fs), 0.7071));
    add (titq::Biquad::lowpass  (fs, std::clamp (s.lpHz, 100.0, 0.45 * fs), 0.7071));
    if (s.eqOn)
        for (int i = 0; i < 3; ++i)
        {
            const double fc = std::clamp (s.eqF[i], 20.0, 0.45 * fs), q = std::max (0.1, s.eqQ[i]);
            add (i == 0 ? titq::Biquad::lowShelf (fs, fc, q, s.eqG[i])
               : i == 1 ? titq::Biquad::peak (fs, fc, q, s.eqG[i])
                        : titq::Biquad::highShelf (fs, fc, q, s.eqG[i]));
        }
    return db;
}

class Gate
{
public:
    static constexpr int kMaxCh = 2;

    void prepare (double sampleRate, double maxLookMs = 20.0)
    {
        fs = sampleRate;
        dlen = (int) std::ceil (maxLookMs * 0.001 * fs) + 4;
        for (auto& d : delay) d.assign ((size_t) dlen, 0.f);
        pos = 0;
        reset();
    }
    void reset() noexcept
    {
        for (int c = 0; c < kMaxCh; ++c) { env[c] = 0.0; gr[c] = s.range; open[c] = false; holdLeft[c] = 0; }
        hp[0].reset(); hp[1].reset(); lp[0].reset(); lp[1].reset();
        lastHp = lastLp = -1.0;
        for (auto& b : eqb) b.reset();
        lastEq[0] = -1.0;
    }
    void setSettings (const Settings& n) noexcept
    {
        s = n;
        if (s.hpHz != lastHp) { hp[0].set (titq::Biquad::highpass (fs, std::clamp (s.hpHz, 10.0, 0.45 * fs), 0.7071)); hp[1].c = hp[0].c; lastHp = s.hpHz; }
        if (s.lpHz != lastLp) { lp[0].set (titq::Biquad::lowpass  (fs, std::clamp (s.lpHz, 100.0, 0.45 * fs), 0.7071)); lp[1].c = lp[0].c; lastLp = s.lpHz; }
        for (int i = 0; i < 3; ++i)
        {
            const double key = s.eqF[i] * 1000.0 + s.eqG[i] * 7.0 + s.eqQ[i] * 13.0 + i;
            if (key == lastEq[i] ) continue;
            lastEq[i] = key;
            const double f = std::clamp (s.eqF[i], 20.0, 0.45 * fs), q = std::max (0.1, s.eqQ[i]);
            eqb[i].set (i == 0 ? titq::Biquad::lowShelf (fs, f, q, s.eqG[i])
                      : i == 1 ? titq::Biquad::peak (fs, f, q, s.eqG[i])
                               : titq::Biquad::highShelf (fs, f, q, s.eqG[i]));
        }
        aD = std::exp (-1.0 / (kDetDecayMs * 0.001 * fs));
        aA = std::exp (-1.0 / (std::max (0.01, s.attackMs) * 0.001 * fs));
        aR = std::exp (-1.0 / (std::max (1.0, s.releaseMs) * 0.001 * fs));
        holdN = (int) std::lround (std::max (0.0, s.holdMs) * 0.001 * fs);
        look = std::clamp ((int) std::lround (s.lookMs * 0.001 * fs), 0, dlen - 2);
    }
    int latencySamples() const noexcept { return look; }

    // Procesa una muestra de cada canal. in[]: senal a procesar; key[]: senal de deteccion (la misma o el sidechain externo).
    // out[]: resultado; keyOut[]: senal de deteccion filtrada (para el modo Listen). Devuelve la atenuacion maxima actual (dB).
    double process (const float* in, const float* key, int numCh, float* out, float* keyOut) noexcept
    {
        double lev[kMaxCh] { -200.0, -200.0 };
        for (int c = 0; c < numCh; ++c)
        {
            float k = key[c];
            hp[c].process (&k, 1, 0);   // un filtro por canal (estado z[0] de cada instancia)
            lp[c].process (&k, 1, 0);
            if (s.eqOn) for (auto& b : eqb) b.process (&k, 1, c);
            keyOut[c] = k;
            const double a = std::fabs ((double) k);
            env[c] = a > env[c] ? a : aD * env[c] + (1.0 - aD) * a;
            lev[c] = 20.0 * std::log10 (env[c] + 1.0e-9);
        }
        const double mx = numCh > 1 ? std::max (lev[0], lev[1]) : lev[0];
        double worst = 0.0;
        for (int c = 0; c < numCh; ++c)
        {
            const double l = s.link * mx + (1.0 - s.link) * lev[c];
            // abierto / cerrado con histeresis y hold
            if (l > s.thr) { open[c] = true; holdLeft[c] = holdN; }
            else if (open[c] && l < s.thr - s.hystDb) { if (holdLeft[c] > 0) --holdLeft[c]; else open[c] = false; }
            else if (open[c]) holdLeft[c] = holdN;   // entre umbral y umbral - histeresis: sigue abierto
            const double trg = (open[c] && holdLeft[c] > 0 && l < s.thr - s.hystDb) ? 0.0 : staticGr (l, s, open[c]);
            gr[c] = trg < gr[c] ? trg + (gr[c] - trg) * aA : trg + (gr[c] - trg) * aR;   // menos atenuacion = apertura (ataque)
            worst = std::max (worst, gr[c]);
        }
        for (int c = 0; c < numCh; ++c)
        {
            auto& d = delay[c];
            d[(size_t) pos] = in[c];
            int rp = pos - look; if (rp < 0) rp += dlen;
            out[c] = d[(size_t) rp] * (float) std::exp (-gr[c] * 0.11512925464970229);
        }
        if (++pos >= dlen) pos = 0;
        return worst;
    }

private:
    Settings s;
    double fs = 44100.0, aD = 0.0, aA = 0.0, aR = 0.0, lastHp = -1.0, lastLp = -1.0;
    int holdN = 0, look = 0, dlen = 0, pos = 0;
    double env[kMaxCh] {}, gr[kMaxCh] {};
    bool open[kMaxCh] {};
    int holdLeft[kMaxCh] {};
    titq::Biquad hp[kMaxCh], lp[kMaxCh], eqb[3];
    double lastEq[3] { -1.0, -1.0, -1.0 };
    std::vector<float> delay[kMaxCh];
};
} // namespace titg
