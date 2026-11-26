// Pruebas automaticas del nucleo del compresor (sin JUCE).
//  1) Curva estatica de cada modo: no debe cambiar por accidente (valores de referencia).
//  2) Ataque del TIT MU: coincide con las 5 posiciones medidas en un DAW, a 44.1, 48, 96 y 192 kHz.
#include "../comp/CompCore.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
static void check (bool ok, const char* what, double a, double b)
{
    if (! ok) { ++failures; std::printf ("FALLO: %s (%.3f vs %.3f)\n", what, a, b); }
}

int main()
{
    // ---- 1) curva estatica, entradas -60..0 dBFS de 6 en 6, ajustes por defecto de cada modo ----
    static const double golden[9][11] = {
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.5625,4.5000,9.0000,13.5000 },
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0454,1.4290,4.3748,7.4622,10.5496 },
        { 0.0000,0.0000,0.0000,0.2934,4.6942,9.3883,14.0825,18.7767,23.4709,28.1650,32.8592 },
        { 0.0000,0.0000,0.0000,0.2812,4.5000,9.0000,13.5000,18.0000,22.5000,27.0000,31.5000 },
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.9889,3.9778,7.9778,11.9778,15.9778 },
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.2820,2.9080,7.0140 },
        { 0.0700,0.1700,0.4200,1.0000,2.1400,4.2600,7.2800,10.3200,13.6800,16.8100,19.4800 },
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,4.4416,8.8831,13.3247 },
        { 0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.5625,4.5000,9.0000,13.5000 } };   // TIT DIGITAL con rodilla normal = Clean
    for (int m = 0; m < titc::NumModes; ++m)
    {
        titc::Settings s; s.mode = m; titc::constrain (s); titc::derive (s);
        for (int i = 0; i < 11; ++i)
            check (std::abs (titc::gainReductionDb (-60.0 + 6.0 * i, s) - golden[m][i]) < 0.01, "curva estatica", titc::gainReductionDb (-60.0 + 6.0 * i, s), golden[m][i]);
    }

    // ---- 2) ataque del TIT MU: reduccion (dB) a 2, 5, 10, 20, 50, 100, 200, 500 y 1000 ms de un escalon de 19.29 dB ----
    static const double tMs[9] = { 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
    static const double meas[5][9] = {
        { 19.28, 19.28, 19.28, 19.28, 19.28, 19.28, 19.28, 19.28, 19.28 },
        { 18.21, 19.16, 19.28, 19.28, 19.28, 19.28, 19.28, 19.28, 19.28 },
        {  6.00,  7.79,  9.23, 10.70, 15.70, 17.42, 18.52, 19.23, 19.30 },
        {  6.88,  8.67, 10.10, 11.56, 15.93, 17.59, 18.64, 19.30, 19.30 },
        {  6.72,  8.50,  9.94, 11.41, 15.82, 17.88, 18.93, 19.30, 19.30 } };
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int a = 0; a < 5; ++a)
        {
            titc::Settings s; s.mode = titc::MU; s.muAtt = a; s.muRec = 2;
            titc::Coefs c = titc::makeCoefs (s, fs);
            titc::Ballistics b;
            int k = 0;
            const int n = (int) (1.0 * fs);
            for (int i = 1; i <= n && k < 9; ++i)
            {
                const double e = titc::step (b, 19.29, c);
                if (i == (int) std::lround (tMs[k] * 0.001 * fs))
                {
                    check (std::abs (e - meas[a][k]) < 1.0, "ataque MU", e, meas[a][k]);
                    ++k;
                }
            }
        }

    // ---- 2b) TIT MU: Threshold (0 = MIN = maxima compresion) medido en un compresor variable-mu de referencia; GR a -6 y -30 dBFS ----
    {
        static const double thr[] = { 0.0, 5.0, 7.5, 10.0 }, g6[] = { 19.48, 10.14, 4.95, 0.50 }, g30[] = { 8.29, 0.91, 0.23, 0.01 };
        for (int i = 0; i < 4; ++i)
        {
            titc::Settings s; s.mode = titc::MU; s.muThr = thr[i]; titc::constrain (s); titc::derive (s);
            check (std::abs (titc::gainReductionDb (-6.0, s) - g6[i]) < 0.8, "MU threshold -6", titc::gainReductionDb (-6.0, s), g6[i]);
            check (std::abs (titc::gainReductionDb (-30.0, s) - g30[i]) < 0.8, "MU threshold -30", titc::gainReductionDb (-30.0, s), g30[i]);
        }
        titc::Settings s; s.mode = titc::MU; s.muThr = 0.0; s.muTrim = 24.0; titc::constrain (s); titc::derive (s);   // Trim +24: satura en ~19.5 y no suma ganancia
        check (std::abs (titc::gainReductionDb (-6.0, s) - 19.5) < 0.1, "MU trim +24 satura", titc::gainReductionDb (-6.0, s), 19.5);
        check (std::abs (s.makeupDb) < 1.0e-9, "MU trim sin ganancia", s.makeupDb, 0.0);
    }

    // ---- 2c) TIT DIGITAL (rodilla infinita): la compresion empieza en el umbral y la ratio instantanea crece con el nivel ----
    {
        titc::Settings s; s.mode = titc::DIG; s.thr = -30.0; s.ratio = 1000.0; s.knee = 31.0; titc::constrain (s); titc::derive (s);
        check (titc::gainReductionDb (-30.0, s) < 1.0e-9 && titc::gainReductionDb (-31.0, s) < 1.0e-9, "TIT DIGITAL sin compresion bajo el umbral", titc::gainReductionDb (-31.0, s), 0.0);
        double prev = 0.0, prevSlope = 0.0;
        for (double x = -30.0; x <= 0.0; x += 1.0)
        {
            const double g = titc::gainReductionDb (x, s), slope = g - prev;
            check (g >= prev && slope >= prevSlope - 1.0e-9 && slope < 1.0, "TIT DIGITAL pendiente creciente", slope, prevSlope);   // la pendiente de GR sube sin saltos y no pasa de 1
            prev = g; prevSlope = slope;
        }
        // curvas medidas (umbral -30, rodilla al maximo): {ratio, dB sobre el umbral, GR}
        static const double m[][3] = { {2,8,0.97},{2,20,5.01},{2,26,7.51},{4,8,1.93},{4,20,9.01},{4,28,14.55},{10,8,2.59},{10,20,11.58},{1000,8,3.06},{1000,20,13.31},{1000,28,20.99} };
        for (auto& r : m)
        {
            titc::Settings k = s; k.ratio = r[0]; k.range = 40.0; titc::constrain (k); titc::derive (k);
            check (std::abs (titc::gainReductionDb (-30.0 + r[1], k) - r[2]) < 0.3, "TIT DIGITAL vs curva medida", titc::gainReductionDb (-30.0 + r[1], k), r[2]);
        }
        titc::Settings h = s; h.mode = titc::Clean; h.knee = 0.0; h.ratio = 4.0; titc::constrain (h);   // Clean y su rodilla normal no cambian
        check (std::abs (titc::gainReductionDb (-20.0, h) - 7.5) < 1.0e-9, "rodilla dura intacta", titc::gainReductionDb (-20.0, h), 7.5);
    }

    // ---- 3) DXX TIT (compresor VCA RMS de referencia, medido): efectiva de cada posicion de Compression y ataque de ~16 ms ----
    {
        static const double pos[] = { 1.5, 3.0, 6.0, 100.0 }, g6[] = { 4.34, 8.14, 10.14, 12.03 };   // GR medida a -6 dBFS con el umbral por defecto
        for (int i = 0; i < 4; ++i)
        {
            titc::Settings s; s.mode = titc::DXX; s.thr = -18.26; s.dxComp = pos[i]; titc::constrain (s); titc::derive (s);
            check (std::abs (titc::gainReductionDb (-6.0, s) - g6[i]) < 0.35, "DXX compresion", titc::gainReductionDb (-6.0, s), g6[i]);
        }
        // ataque dependiente del salto: tiempo (ms) hasta el 63 % de la reduccion final, medido con un salto desde -60 dBFS (compresion infinita)
        static const double lv[] = { -16, -12, -9, -6, -2 }, m63[] = { 61, 31, 21, 15, 10 };
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int k = 0; k < 5; ++k)
            {
                titc::Settings s; s.mode = titc::DXX; s.thr = -18.26; s.dxComp = 100.0; titc::constrain (s); titc::derive (s);
                const auto dt = titc::detectorFor (titc::DXX);
                const double aD = std::exp (-1.0 / (dt.atkMs * 0.001 * fs));
                titc::Coefs c = titc::makeCoefs (s, fs); titc::Ballistics b;
                const double P = std::pow (10.0, lv[k] / 10.0) * 0.5, fin = titc::gainReductionDb (lv[k], s);
                double det = std::pow (10.0, -6.0) * 0.5; int t63 = -1;
                for (int i = 1; i <= (int) (0.3 * fs) && t63 < 0; ++i)
                {
                    det = aD * det + (1.0 - aD) * P;
                    const double e = titc::step (b, titc::gainReductionDb (10.0 * std::log10 (det) + dt.offsetDb, s), c);
                    if (e >= 0.632 * fin) t63 = i;
                }
                const double ms = t63 * 1000.0 / fs;
                check (std::abs (ms - m63[k]) < 0.15 * m63[k] + 1.5, "ataque DXX", ms, m63[k]);
            }
    }

    // ---- Color apagado = señal intacta; encendido, la saturacion de FET/FETB/OPTO/DXX/FAIR es pequena a nivel bajo y se nota a 0 dBFS ----
    for (int m = 0; m < titc::NumModes; ++m)
    {
        titc::Settings s; s.mode = m; titc::constrain (s); titc::derive (s);
        for (double v : { 0.01, 0.1, 0.5, 0.9 })
            check (titc::colorPre (s, v) == v && titc::colorPost (s, v) == v, "Color apagado", titc::colorPost (s, v), v);
        s.color = true;
        check (std::abs (titc::colorPost (s, 0.001) - 0.001) < 2.0e-6 && std::abs (titc::colorPre (s, 0.001) - 0.001) < 2.0e-6, "Color a nivel bajo", titc::colorPost (s, 0.001), 0.001);
    }
    // ---- ataque de FAIR: dos constantes (TC1 rapido con cola corta, TC6 con cola larga); la reduccion llega al 90 % antes en TC1 ----
    {
        double t90[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k)
        {
            titc::Settings s; s.mode = titc::VARI; s.releaseMs = k == 0 ? 100.0 : 2500.0; titc::constrain (s); titc::derive (s);
            const double fs = 48000.0; const titc::Coefs c = titc::makeCoefs (s, fs); titc::Ballistics b;
            for (int i = 1; i <= 2400 && t90[k] == 0; ++i) if (titc::step (b, 10.0, c) >= 9.0) t90[k] = i * 1000.0 / fs;
        }
        check (t90[0] > 0 && t90[1] > 0 && t90[0] < 12.0 && t90[1] < 16.0, "ataque FAIR 90 %", t90[0], 12.0);
    }

    if (failures == 0) std::printf ("Todas las pruebas del nucleo han pasado.\n");
    return failures == 0 ? 0 : 1;
}
