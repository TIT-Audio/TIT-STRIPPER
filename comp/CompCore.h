#pragma once
// Núcleo del compresor TIT COMP (sin dependencias de JUCE): curva estática, detector y balística por modo.
#include <algorithm>
#include <cmath>

namespace titc
{
enum Mode { Clean = 0, VCA, FET, FETB, OPTO, VARI, MU, DXX, DIG, NumModes };

struct Settings
{
    int mode = Clean;
    double thr = -18.0, ratio = 4.0, knee = 6.0;
    double attackMs = 10.0, releaseMs = 120.0, holdMs = 0.0;
    double range = 60.0;     // reducción máxima en dB
    double link = 1.0;       // 0..1
    bool   autoRel = false;  // release automático (Clean y VCA TIT)
    // mandos propios de cada compresor original
    double fin = 36.0, fout = 19.0;   // FET: Input y Output (0..48)
    double gain = 26.0;               // opto: Gain 0..100
    double pr = 40.0;                 // opto: Peak Reduction 0..100
    double vin = 0.0, vthr = 10.0;    // variable-mu: Input Gain (atenuación 0..20) y Threshold 0..10
    // TIT MU (variable-mu de dos canales medido): Input Trim dB, Threshold 0..10, Attack posicion 0..4 (5 posiciones), Recovery posicion 0..4 (5 posiciones), Make Up 0..19 dB, Filtro sidechain Hz (0 = off)
    double muTrim = 0.0, muThr = 2.0, muAtt = 1.0, muRec = 2.0, muMk = 0.0, muFilt = 0.0;
    double dxComp = 4.0;              // DXX TIT: Compression 1.5..100 (100 = infinito)
    double outParam = 0.0;            // mando Out en dB (make-up / Output / Gain según el modo)
    double makeupDb = 0.0;            // resultado: ganancia de salida que se aplica
    bool   color = false;             // Color: añade la saturación / timbre medido de cada compresor original (apagado = limpio)
    double colA = 1.0;                // variable-mu: ganancia lineal de entrada tras Input Gain (1 = a fondo), la fija derive()
    double vShift = 0.0;              // variable-mu: desplazamiento (dB) del nivel que ve la curva de reducción por Input Gain, lo fija derive()
};


// ---- Límites de cada modo: solo se pueden mover los mandos como en el compresor original ----
struct ModeLocks { bool attack, release, ratio, knee, hold, autoRel; };   // true = el mando está bloqueado (autoRel: true = disponible)
inline ModeLocks locksFor (int mode) noexcept
{
    switch (mode)
    {
        case VCA:  return { false, false, false, true,  true,  true  };   // VCA bus: ataque, release y ratio por pasos; rodilla fija; Auto
        case FET:
        case FETB: return { false, false, false, true,  true,  false };   // FET: ataque y release continuos dentro de su rango; ratio 4/8/12/20
        case OPTO: return { true,  true,  true,  true,  true,  false };   // opto: sin ataque ni release ni ratio seleccionables
        case VARI: return { true,  false, true,  true,  true,  false };   // variable-mu: ataque fijo; release = 6 posiciones de Time Constant
        case MU:   return { true,  true,  true,  true,  true,  false };   // TIT MU: mandos propios (Attack y Recovery de 0 a 10)
        case DXX:  return { true,  true,  true,  true,  true,  false };   // DXX TIT: Threshold, Compression y Output Gain; ataque/release dependientes del programa (fijos)
        default:   return { false, false, false, false, false, true  };   // Clean y TIT DIGITAL: libres, con Auto Release
    }
}
inline double snapLog (double x, const double* t, int n) noexcept
{
    double best = t[0];
    for (int i = 1; i < n; ++i)
        if (std::abs (std::log (std::max (1.0e-9, x) / t[i])) < std::abs (std::log (std::max (1.0e-9, x) / best))) best = t[i];
    return best;
}
// Ajusta los valores a lo que permite el original (el procesador lo aplica siempre; el editor refleja el resultado en los mandos)
inline void constrain (Settings& s) noexcept
{
    static const double vcaA[] = { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 }, vcaR[] = { 100.0, 300.0, 600.0, 1200.0 }, vcaRat[] = { 2.0, 4.0, 10.0 };
    static const double fetRat[] = { 4.0, 8.0, 12.0, 20.0 }, variTc[] = { 100.0, 200.0, 800.0, 1100.0, 1500.0, 2500.0 };
    s.autoRel = s.autoRel && (s.mode == VCA || s.mode == Clean || s.mode == DIG);
    switch (s.mode)
    {
        case VCA:
            s.attackMs = snapLog (s.attackMs, vcaA, 6); s.releaseMs = snapLog (s.releaseMs, vcaR, 4);
            s.ratio = snapLog (s.ratio, vcaRat, 3);
            s.knee = s.ratio < 3.0 ? 17.6 : (s.ratio < 7.0 ? 9.6 : 4.2);   // medido: la rodilla depende del boton de ratio (2:1 ancha, 10:1 casi dura)
            s.holdMs = 0.0; break;
        case FET:
        case FETB:
            s.attackMs = std::clamp (s.attackMs, 0.02, 0.8); s.releaseMs = std::clamp (s.releaseMs, 50.0, 1100.0);
            s.ratio = snapLog (s.ratio, fetRat, 4); s.knee = 3.0; s.holdMs = 0.0; break;
        case OPTO:
            s.attackMs = 10.0; s.releaseMs = 1500.0; s.ratio = 3.0; s.knee = 12.0; s.holdMs = 0.0; break;
        case MU:
            s.attackMs = 1.0; s.releaseMs = 500.0; s.ratio = 2.0; s.knee = 20.0; s.holdMs = 0.0;
            s.muTrim = std::clamp (s.muTrim, -24.0, 24.0); s.muThr = std::clamp (s.muThr, 0.0, 10.0); s.muAtt = std::clamp (std::round (s.muAtt), 0.0, 4.0);
            s.muRec = std::clamp (std::round (s.muRec), 0.0, 4.0); s.muMk = std::clamp (s.muMk, 0.0, 19.0);
            s.muFilt = s.muFilt < 25.0 ? 0.0 : std::clamp (std::round (s.muFilt / 50.0) * 50.0, 50.0, 250.0); break;
        case DXX:
            s.attackMs = 16.0; s.releaseMs = 100.0; s.ratio = std::clamp (s.dxComp, 1.5, 100.0); s.knee = 0.0; s.holdMs = 0.0;
            s.thr = std::clamp (s.thr, -58.3, -8.7); break;
        case VARI:
            s.attackMs = 1.5; s.releaseMs = snapLog (s.releaseMs, variTc, 6); s.ratio = 6.5; s.knee = 9.0; s.holdMs = 0.0; break;
        default: break;
    }
}

// Traduce los mandos propios de cada original a umbral interno (dBFS) y ganancia de salida (medido en un DAW con los originales)
inline void derive (Settings& s) noexcept
{
    s.makeupDb = s.outParam;
    switch (s.mode)
    {
        case VCA:   // VCA bus: Threshold libre (dBFS directos); Make-up -5..+15 como el original
            s.makeupDb = std::clamp (s.outParam, -5.0, 15.0); break;
        case FET:
        case FETB:
        {   // FET: Input 0..48 (atenuación; más bajo = más compresión) y Output 0..48 (más bajo = más fuerte, ~0.83 dB por unidad)
            static const double ix[] = { 0.0, 12.0, 28.0, 36.0, 48.0 }, iy[] = { -60.0, -42.0, -30.0, -14.2, -2.0 };
            const double x = 48.0 - std::clamp (s.fin, 0.0, 48.0); int i = 1; while (x > ix[i]) ++i;   // Input: derecha = mas entrada = mas compresion
            s.thr = iy[i - 1] + (x - ix[i - 1]) / (ix[i] - ix[i - 1]) * (iy[i] - iy[i - 1]);
            s.makeupDb = (s.fout - 29.0) * 0.834; break;   // Output: derecha = mas nivel
        }
        case OPTO:
        {   // opto: Peak Reduction (medido en 35/50/62; en 0 no comprime) y Gain independiente de la detección:
            // con Gain 0 no suena nada pero la reducción sigue; con Peak Reduction al mínimo no hay compresión. Gain medido: 40 -> +6.7 dB, 100 -> +34.7 dB
            static const double px[] = { 0.0, 5.0, 10.0, 35.0, 50.0, 62.0, 100.0 }, py[] = { 20.0, 3.0, -9.5, -21.5, -28.9, -34.4, -52.6 };
            const double x = std::clamp (s.pr, 0.0, 100.0); int i = 1; while (x > px[i]) ++i;
            s.thr = py[i - 1] + (x - px[i - 1]) / (px[i] - px[i - 1]) * (py[i] - py[i - 1]);
            const double g = std::clamp (s.gain, 0.0, 100.0);
            const double at10 = 0.467 * (10.0 - 25.6);
            s.makeupDb = g < 10.0 ? -120.0 + (g / 10.0) * (at10 + 120.0) : 0.467 * (g - 25.6);
            break;
        }
        case VARI:
        {   // compresor variable-mu medido (25 pistas): Input Gain desplaza la curva de reducción (att de ganancia 0/5/10/15/20 -> 18.94/14.95/9.91/4.99/0 dB; desplazamiento de la curva
            // 19.2/15.1/10.1/5.1/0 dB), el Threshold 0..10 elige la curva (tabla en gainReductionDb) y la ganancia de reposo con el Input Gain a fondo es +1.58 dB
            static const double vx[] = { 0.0, 5.0, 10.0, 15.0, 20.0 }, ag[] = { 18.94, 14.95, 9.91, 4.99, 0.0 }, as[] = { 19.2, 15.1, 10.1, 5.1, 0.0 };
            const double v = std::clamp (s.vin, 0.0, 20.0); int i = 1; while (v > vx[i]) ++i;
            const double u = (v - vx[i - 1]) / (vx[i] - vx[i - 1]);
            const double attG = ag[i - 1] + u * (ag[i] - ag[i - 1]);
            s.vShift = as[i - 1] + u * (as[i] - as[i - 1]);
            s.colA = std::pow (10.0, -attG / 20.0);
            s.makeupDb = 1.58 + std::clamp (s.outParam, -20.0, 20.0) - attG; break;
        }
        case MU:
        {   // Threshold 0..10 como el original: izquierda (MIN) = mas compresion; el umbral sube 20/31.5/46 dB al 50/75/100 % del recorrido; Input Trim entra antes de la curva
            static const double tx[] = { 0.0, 5.0, 7.5, 10.0 }, ty[] = { 0.0, 20.0, 31.5, 46.0 };   // medido en el 0 / 50 / 75 / 100 % del recorrido
            const double q = std::clamp (s.muThr, 0.0, 10.0); int k = 1; while (q > tx[k]) ++k;
            const double shift = ty[k - 1] + (q - tx[k - 1]) / (tx[k] - tx[k - 1]) * (ty[k] - ty[k - 1]);
            s.thr = shift - s.muTrim;
            s.makeupDb = s.muMk; break;   // medido (Thresh al minimo): Input Trim +/-24 no cambia el nivel de salida, solo la cantidad de compresion
        }
        case DXX:   // compresor VCA de referencia medido: Threshold en dBFS directo (knob .01..3 V: -58.3..-8.7 dBFS, 1 = -18.26); Output Gain -20..+20
            s.makeupDb = std::clamp (s.outParam, -20.0, 20.0); break;
        default: break;
    }
}


// ---- Color: saturación / timbre de cada compresor (medido con tonos de 100 Hz, 1 kHz y 5 kHz a -30..0 dBFS; apagado por defecto) ----
// Antes de la reducción de ganancia (señal de entrada x, 0 dBFS = 1.0)
inline double colorPre (const Settings& s, double x) noexcept
{
    if (! s.color) return x;
    switch (s.mode)
    {
        case VARI: return x + 0.0062 * s.colA * x * x - 0.103 * s.colA * s.colA * x * x * x;   // polinomio estático de la célula de ganancia (independiente de la frecuencia)
        case FET:  return x + 0.0033 * x * x;                                                 // 2º armónico referido a la entrada (-56 dB a 0 dBFS, sube 1 dB/dB)
        case FETB: return x + 0.0020 * x * x;                                                 // -60 dB a 0 dBFS
        default:   return x;
    }
}
// Después de la reducción y del make-up (señal de salida y)
inline double colorPost (const Settings& s, double y) noexcept
{
    if (! s.color) return y;
    switch (s.mode)
    {
        case FET:  return y - 0.0731 * y * y * y;     // 3er armónico referido a la salida (-66 dB con la salida a -16 dBFS)
        case FETB: return y - 0.0359 * y * y * y;
        case DXX:  return y + 0.0043 * y * y - 0.071 * y * y * y;
        case OPTO: return y / std::pow (1.0 + std::pow (std::abs (y) / 1.193, 5.168), 1.0 / 5.168);   // etapa de salida: saturación suave con inicio brusco cerca de 0 dBFS
        default:   return y;
    }
}

// Timbre lineal de cada original (medido con un programa sintético: desfase de baja frecuencia = paso alto de 1er orden; desfase de alta = retardo de unos microsegundos)
struct TimbreSpec { double hpHz, delayUs; };
inline TimbreSpec timbreFor (int mode) noexcept
{
    switch (mode)
    {
        case VARI: return { 18.0, 0.8 };
        case FET:  return { 14.0, 0.0 };
        case FETB: return { 15.0, 0.0 };
        case OPTO: return { 0.0, 1.1 };
        default:   return { 0.0, 0.0 };
    }
}
struct Timbre
{
    double x1 = 0.0, y1 = 0.0, ax1 = 0.0, ay1 = 0.0;
    void reset() noexcept { x1 = y1 = ax1 = ay1 = 0.0; }
    inline double process (double x, const TimbreSpec& sp, double fs) noexcept
    {
        double v = x;
        if (sp.hpHz > 0.0)
        {
            const double w = std::tan (3.14159265358979323846 * sp.hpHz / fs), b0 = 1.0 / (1.0 + w), a1 = (1.0 - w) / (1.0 + w);
            const double y = b0 * (v - x1) + a1 * y1; x1 = v; y1 = y; v = y;
        }
        if (sp.delayUs > 0.0)
        {
            const double d = sp.delayUs * 1.0e-6 * fs, a = (1.0 - d) / (1.0 + d);
            const double y = a * v + ax1 - a * ay1; ax1 = v; ay1 = y; v = y;
        }
        return v;
    }
};

// Rodilla efectiva: los modos "suaves" no bajan de un mínimo
inline double effectiveKnee (const Settings& s) noexcept
{
    double w = std::max (0.0, s.knee);
    if (s.mode == OPTO) w = std::max (w, 10.0);
    if (s.mode == VARI) w = std::max (w, 9.0);   // variable-mu medido: umbral ~-27 dBFS, rodilla ~8-9 dB, ratio ~6.5
    return w;
}

// Interpolación logarítmica en una tabla (x creciente)
inline double logTable (double x, const double* xs, const double* ys, int n) noexcept
{
    if (x <= xs[0]) return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i])
        {
            const double t = std::log (x / xs[i - 1]) / std::log (xs[i] / xs[i - 1]);
            return ys[i - 1] + t * (ys[i] - ys[i - 1]);
        }
    return ys[n - 1];
}

// Curva estática: reducción de ganancia en dB (>= 0) para un nivel de entrada x en dB
inline double gainReductionDb (double x, const Settings& s) noexcept
{
    if (s.mode == MU)
    {   // curva medida con el Threshold en MIN (maxima compresion) y Input Trim 0: reduccion (dB) segun el nivel de entrada; satura en ~19.5 dB
        static const double lx[] = { -90.0, -80.0, -70.0, -68.0, -66.0, -64.0, -62.0, -60.0, -58.0, -56.0, -54.0, -52.0, -50.0, -48.0, -46.0, -44.0, -42.0, -40.0, -38.0, -36.0, -34.0, -32.0, -30.0, -28.0, -26.0, -24.0, -22.0, -20.0, -18.0, -16.0, -14.0, -12.0, -10.0, -8.0, -6.0, -4.0, -2.0, 0.0 };
        static const double ly[] = { 0.00, 0.01, 0.05, 0.07, 0.09, 0.12, 0.17, 0.23, 0.31, 0.42, 0.57, 0.76, 1.00, 1.30, 1.68, 2.14, 2.72, 3.43, 4.26, 5.24, 6.26, 7.28, 8.29, 9.31, 10.32, 11.34, 12.66, 13.68, 14.69, 15.81, 16.81, 17.82, 18.84, 19.48, 19.48, 19.48, 19.48, 19.48 };
        const int n = (int) (sizeof (lx) / sizeof (lx[0]));
        const double v = x - s.thr;   // thr = desplazamiento del Threshold (0 en MIN, ~45 dB en MAX) menos Input Trim
        double gr;
        if (v <= lx[0]) gr = 0.0;
        else if (v >= lx[n - 1]) gr = ly[n - 1];
        else { int i = 1; while (v > lx[i]) ++i; gr = ly[i - 1] + (v - lx[i - 1]) / (lx[i] - lx[i - 1]) * (ly[i] - ly[i - 1]); }
        return std::clamp (gr, 0.0, std::min (19.5, std::max (0.0, s.range)));
    }
    if (s.mode == VARI)
    {   // curvas medidas con Input Gain a fondo (1 kHz, pasos de 2 dB; columnas Threshold 0/2.5/5/7.5/10, filas desde -48 dB); con menos Input Gain la curva se desplaza
        static const double tab[5][24] = {
            { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00 },
            { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.08, 0.56, 1.37, 2.41, 3.62, 4.95, 6.38, 7.87, 9.42, 11.01 },
            { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.20, 0.79, 1.69, 2.80, 4.05, 5.43, 6.89, 8.41, 9.99, 11.61, 13.27, 14.94, 16.62 },
            { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.04, 0.44, 1.20, 2.20, 3.39, 4.71, 6.13, 7.62, 9.18, 10.78, 12.43, 14.10, 15.79, 17.49, 19.21 },
            { 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.09, 0.57, 1.37, 2.42, 3.64, 4.98, 6.41, 7.92, 9.48, 11.10, 12.75, 14.43, 16.13, 17.85, 19.59, 21.33 } };
        const double q = std::clamp (s.vthr, 0.0, 10.0) / 2.5; const int k = std::min (3, (int) q); const double w = q - k;
        const double xe = x - s.vShift;
        auto col = [&] (int c) noexcept
        {
            if (xe <= -48.0) return 0.0;
            const double pos = (xe + 48.0) * 0.5; int i = (int) pos; if (i > 22) i = 22;
            const double f = pos - i; return tab[c][i] + f * (tab[c][i + 1] - tab[c][i]);   // pasa de -2 dB con la pendiente del último tramo
        };
        return std::clamp ((1.0 - w) * col (k) + w * col (k + 1), 0.0, std::max (0.0, s.range));
    }
    if (s.mode == DXX)
    {   // compresor VCA RMS de referencia medido (rodilla dura): pendiente efectiva por posicion del mando Compression (1.5/2/3/4/6/10/20/inf)
        static const double cx[] = { 1.5, 2.0, 3.0, 4.0, 6.0, 10.0, 20.0, 100.0 }, cy[] = { 1.55, 2.13, 2.99, 3.85, 5.8, 8.3, 12.5, 59.0 };
        const double Re = logTable (std::clamp (s.ratio, 1.5, 100.0), cx, cy, 8), over = x - s.thr;
        return std::clamp (over <= 0.0 ? 0.0 : over * (1.0 - 1.0 / Re), 0.0, std::max (0.0, s.range));
    }
    if (s.mode == DIG && s.knee >= 30.95)
    {   // TIT DIGITAL con el Knee al maximo ("inf"): rodilla infinita medida en el compresor de un channel strip con la rodilla al maximo (ratios 2/4/10/1000, umbral -30): la compresion empieza en
        // el umbral y la pendiente de la curva pasa de 1 a 1/ratio de forma exponencial con el nivel sobre el umbral; la constante T depende de la ratio
        // (error maximo 0.23 dB frente a las 4 curvas medidas). No hay rodilla dura.
        const double over = x - s.thr;
        if (over <= 0.0) return 0.0;
        const double Rt = std::max (1.0, s.ratio), T = 7.176 + 14.812 * std::pow (Rt, -1.472);
        const double out = over / Rt + (1.0 - 1.0 / Rt) * T * (1.0 - std::exp (-over / T));
        return std::clamp (over - out, 0.0, std::max (0.0, s.range));
    }
    double R = std::max (1.0, s.ratio);
    if (s.mode == VCA) R = s.ratio < 3.0 ? 1.76 : (s.ratio < 7.0 ? 2.06 : 3.74);   // medido: pendiente efectiva de los botones 2/4/10
    if (s.mode == FET) R = std::pow (R, 1.1);    // FET (LN) medido: la pendiente efectiva de los botones 4/8/12/20 sube a ~5/10/15/27:1   // medido en el VCA bus: ratio 2/4/10 se comporta como ~1.8/3.3/7
    double thr = s.thr;
    if (s.mode == VCA)
        thr *= s.ratio < 3.0 ? 1.54 : (s.ratio < 7.0 ? 1.139 : 0.807);   // medido: el umbral efectivo se desplaza con el boton de ratio (las curvas se cruzan en ~8 dB de reduccion a 0 dBFS)
    if (s.mode == FET || s.mode == FETB)
    {   // FET medido (LN y Rev A): al subir el boton de ratio (4/8/12/20) el umbral efectivo sube ~0/2.9/3.8/5.3 dB
        static const double rx[] = { 4.0, 8.0, 12.0, 20.0 }, ry[] = { 0.0, 2.9, 3.8, 5.3 };
        thr += logTable (std::max (1.0, s.ratio), rx, ry, 4);
    }
    const double W = effectiveKnee (s), over = x - thr;
    double out;
    if (W < 0.01)                         out = over <= 0.0 ? x : thr + over / R;
    else if (2.0 * over < -W)             out = x;
    else if (2.0 * std::abs (over) <= W)  out = x + (1.0 / R - 1.0) * (over + 0.5 * W) * (over + 0.5 * W) / (2.0 * W);
    else                                  out = thr + over / R;
    return std::clamp (x - out, 0.0, std::max (0.0, s.range));
}

// Detector de potencia: tiempos de subida y bajada (ms) y corrección para que un seno a fondo de escala lea 0 dB
struct DetectorTimes { double atkMs, relMs, offsetDb; };
inline DetectorTimes detectorFor (int mode) noexcept
{
    switch (mode)
    {
        case VCA:  return { 1.0, 1.0,  3.01 };
        case FET:  return { 0.02, 3.0,  0.0 };
        case FETB: return { 0.02, 3.0,  0.0 };
        case OPTO: return { 10.0, 10.0, 3.01 };
        case VARI: return { 1.0, 20.0, 0.52 };   // medido: la reducción no depende de la frecuencia (31 Hz..12 kHz); detector de pico con subida de 1 ms y bajada de 20 ms
        case MU:   return { 1.0, 1.0,  3.01 };
        case DXX:  return { 35.0, 35.0, 3.01 };   // compresor VCA RMS de referencia: detector RMS verdadero (~35 ms en potencia); el ataque dependiente del salto y el release de ~125 dB/s salen de aqui
        default:   return { 0.05, 5.0,  0.0 };
    }
}

struct Ballistics
{
    double e = 0.0, ef = 0.0, es = 0.0, mem = 0.0, p1 = 0.0, p2 = 0.0;
    int hold = 0;
    void reset() noexcept { e = ef = es = mem = p1 = p2 = 0.0; hold = 0; }
};

struct Coefs
{
    bool dual = false, mu = false;
    double muRate = 7.0;   // TIT MU: velocidad de recuperacion en dB por segundo (rampa lineal)
    double aA = 0.0, aAs = 0.0, aR = 0.0;
    int holdSamples = 0;
    double aRf = 0.0, tauSlowBase = 1.0, k0 = 1.0, k1 = 0.0, wFast = 0.5, memCoef = 0.0, fs = 44100.0;
    bool vm = false;                         // variable-mu con memoria (Time Constant 5 y 6): la parte lenta de la reducción se carga mientras se comprime
    double smax = 0.7, chg = 0.0, tsMin = 4.0, tsMax = 4.0;
    bool vatk = false;                       // variable-mu: ataque de dos constantes (parte rapida + cola), medido
    double wA1 = 0.5, aA1 = 0.0, aA2 = 0.0;
};

inline Coefs makeCoefs (const Settings& s, double fs) noexcept
{
    Coefs c;
    c.fs = fs;
    const bool fet = (s.mode == FET || s.mode == FETB);
    double atkMs = std::max (fet ? 0.02 : 0.01, s.attackMs);
    if (s.mode == VCA)
    {   // medido en el VCA bus (mando 0.1/0.3/1/3/10/30 ms): la reducción llega al 63 % en ~1/1/2/6/16/37 ms
        static const double kx[] = { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 }, ky[] = { 0.5, 0.8, 1.5, 4.5, 13.0, 34.0 };
        atkMs = logTable (atkMs, kx, ky, 6);
    }
    if (fet)
    {   // FET medido (Rev E): del mando mas rapido al mas lento la reduccion llega al 63 % en ~0.15 ms .. ~3 ms
        const double u = std::clamp ((atkMs - 0.02) / 0.78, 0.0, 1.0);
        const double extra = std::max (0.0, atkMs - 0.8);
        // FET TIT: FET (LN) (0.15 .. 3 ms); FET TIT B: Rev A, algo mas lento (0.4 .. 4.5 ms)
        atkMs = (s.mode == FETB ? 0.4 + 4.1 * u : 0.15 + 4.0 * u) + extra;   // por encima de 0.8 ms (fuera del rango del original) sigue sumando
    }
    if (s.mode == VARI) atkMs = std::max (0.01, s.attackMs);   // ataque del variable-mu (fijo en constrain)
    c.aA = std::exp (-1.0 / (atkMs * 0.001 * fs));
    c.aAs = c.aA;
    if (s.mode == VARI)
    {   // ataque medido (Time Constant 1 y 6 casi iguales): parte rapida + cola
        c.vatk = true; c.aA = 0.0; c.aAs = 0.0;   // los valores por Time Constant se fijan mas abajo
    }
    // medido en un compresor de bus tipo VCA bus: la constante de liberación es ~0.42 x el valor del mando (exponencial única)
    double relMs = std::max (1.0, s.releaseMs);
    if (s.mode == VCA)
    {   // mando 0.1/0.3/0.6/1.2 s -> constante 90/131/267/567 ms (exponencial única)
        static const double rx[] = { 100.0, 300.0, 600.0, 1200.0 }, ry[] = { 90.0, 131.0, 267.0, 567.0 };
        relMs = (relMs >= 1200.0) ? relMs * (567.0 / 1200.0) : logTable (relMs, rx, ry, 4);
    }
    c.aR = std::exp (-1.0 / (std::max (1.0, relMs) * 0.001 * fs));
    c.holdSamples = (int) std::lround (std::max (0.0, s.holdMs) * 0.001 * fs);
    c.memCoef = 1.0 / (8.0 * fs);

    if (s.autoRel && (s.mode == VCA || s.mode == Clean || s.mode == DIG))
    {   // VCA bus en Auto (medido): arranque rápido pequeño (~11 % en 50 ms) y cola lenta de ~2 s
        c.dual = true; c.wFast = 0.1;
        c.aRf = std::exp (-1.0 / (0.05 * fs));
        c.tauSlowBase = 2.0; c.k0 = 1.0; c.k1 = 0.0;
    }
    else if (s.mode == OPTO)
    {   // célula óptica: la liberación es rápida al principio y luego lenta, y depende de cuánto tiempo lleva comprimiendo
        // medido en un opto (Peak Reduction ~50): ataque en dos etapas (~75 % en 5 ms, el resto tarda ~0.3 s),
        // liberación rápida ~80 ms + cola lenta ~1.3 s (con el mando en 1500 ms), memoria pequeña
        c.dual = true; c.wFast = 0.58;
        c.aAs = std::exp (-1.0 / (0.3 * fs));
        c.aRf = std::exp (-1.0 / (0.08 * fs));
        c.tauSlowBase = std::max (1.0, s.releaseMs) * 0.001 * (1.3 / 1.5); c.k0 = 0.9; c.k1 = 0.2;
    }
    else if (s.mode == FETB)
    {   // FET (Rev A) medido: el mando lento da una exponencial de ~1.05 s (meseta inicial ~50 ms), el medio ~0.9 s,
        // el rapido ~0.27 s y el mas rapido de dos etapas (~0.13 s y cola de ~0.3 s)
        static const double rx[] = { 50.0, 120.0, 800.0, 1100.0 }, ry[] = { 0.30, 0.27, 0.90, 1.05 };
        const double rel = std::clamp (s.releaseMs, 50.0, 1100.0);
        c.dual = true;
        c.wFast = rel < 120.0 ? 0.7 * (120.0 - rel) / 70.0 : 0.0;
        c.aRf = std::exp (-1.0 / (0.13 * fs));
        c.tauSlowBase = logTable (rel, rx, ry, 4); c.k0 = 1.0; c.k1 = 0.0;
    }
    else if (fet)
    {   // FET (LN) medido: liberacion con parte rapida (~0.1 s, hasta el 75 % con el mando rapido) y cola de ~0.9-1.25 s
        const double u = std::log (std::clamp (s.releaseMs, 50.0, 1100.0) / 50.0) / std::log (1100.0 / 50.0);   // 0 rapido, 1 lento
        c.dual = true; c.wFast = 0.75 * (1.0 - u);
        c.aRf = std::exp (-1.0 / (0.1 * fs));
        c.tauSlowBase = 0.9 + 0.35 * u; c.k0 = 1.0; c.k1 = 0.0;
    }
    else if (s.mode == VARI)
    {   // variable-mu medido (G y B): Time Constant 1..4 = exponencial unica de 0.10/0.30/0.87/1.70 s sin memoria;
        // 5 y 6: parte rapida (0.30 / 0.14 s) + parte lenta (3.97 s / 6..9 s) cuyo peso crece con el tiempo comprimiendo (se carga con 1.0 / 1.5 s hasta el 67 / 75 %)
        const double rel = std::max (1.0, s.releaseMs);
        const int tc = rel <= 150.0 ? 1 : rel <= 500.0 ? 2 : rel <= 950.0 ? 3 : rel <= 1300.0 ? 4 : rel <= 2000.0 ? 5 : 6;
        {   // ataque medido (TC1: 75 % a 0.3 ms + cola 3.5 ms; TC6: 90 % a 0.1 ms + cola 5 ms); entre medias se interpola
            const double u = (tc - 1) / 5.0;
            c.wA1 = 0.75 + 0.15 * u;
            c.aA1 = std::exp (-1.0 / ((0.3 - 0.2 * u) * 1.0e-3 * fs));
            c.aA2 = std::exp (-1.0 / ((3.5 + 1.5 * u) * 1.0e-3 * fs));
        }
        if (tc <= 4)
        {
            static const double tau[] = { 0.10, 0.30, 0.87, 1.70 };
            c.aR = std::exp (-1.0 / (tau[tc - 1] * fs));
        }
        else
        {
            c.vm = true;
            c.aRf = std::exp (-1.0 / ((tc == 5 ? 0.30 : 0.14) * fs));
            c.smax = tc == 5 ? 0.674 : 0.745;
            c.chg = 1.0 - std::exp (-1.0 / ((tc == 5 ? 1.0 : 1.5) * fs));
            c.tsMin = tc == 5 ? 3.97 : 6.0; c.tsMax = tc == 5 ? 3.97 : 9.0;
        }
    }
    if (s.mode == DXX)
    {   // compresor VCA RMS de referencia medido: la reduccion sigue al detector RMS (35 ms) sin balistica propia; el ataque (61/31/21/15/10 ms para 2/6/9/12/16 dB) y el release (~125 dB/s) salen del detector
        c.mu = true; c.wFast = 1.0;
        c.aA = std::exp (-1.0 / (0.0001 * fs)); c.aAs = c.aA;
        c.muRate = 1.0e5; c.holdSamples = 0;
        return c;
    }
    if (s.mode == MU)
    {   // TIT MU medido (tono -6 dBFS tras -60 dBFS, sin contaminar):
        // Attack: 5 posiciones medidas. Pos. 1-2: casi inmediatas (<1 ms); pos. 3-5: parte rapida (1-2 ms) + cola de ~46 ms (63 % a los ~25-30 ms)
        // Recovery: 5 posiciones, rampa lineal de 31 / 15.4 / 7.3 / 3.65 / 1.8 dB/s con una pequena espera inicial (30/45/15/100/400 ms)
        static const double wf[]   = { 1.0, 0.78, 0.36, 0.41, 0.37 },
                            tauF[] = { 0.4, 0.3,  1.7,  1.5,  1.2 },
                            tauS[] = { 1.0, 1.75, 46.6, 46.8, 45.3 };
        static const double rate[] = { 31.0, 15.4, 7.3, 3.65, 1.8 }, holdMs[] = { 30.0, 45.0, 15.0, 100.0, 400.0 };
        const int a = (int) std::clamp (std::round (s.muAtt), 0.0, 4.0), r = (int) std::clamp (std::round (s.muRec), 0.0, 4.0);
        c.mu = true; c.wFast = wf[a];
        c.aA = std::exp (-1.0 / (tauF[a] * 0.001 * fs));
        c.aAs = std::exp (-1.0 / (tauS[a] * 0.001 * fs));
        c.muRate = rate[r];
        c.holdSamples = (int) std::lround (holdMs[r] * 0.001 * fs);
    }
    return c;
}

// Un paso de balística sobre la reducción objetivo g (dB). Devuelve la reducción suavizada (dB).
inline double step (Ballistics& b, double g, const Coefs& c) noexcept
{
    if (c.vatk)
    {   // el ataque sube con dos exponenciales (la bajada es inmediata: el release lo da el resto de la balistica)
        b.p1 = g > b.p1 ? c.aA1 * b.p1 + (1.0 - c.aA1) * g : g;
        b.p2 = g > b.p2 ? c.aA2 * b.p2 + (1.0 - c.aA2) * g : g;
        g = c.wA1 * b.p1 + (1.0 - c.wA1) * b.p2;
    }
    if (c.vm)
    {
        if (std::abs (b.ef + b.es - b.e) > 0.05) { b.ef = b.e; b.es = 0.0; }   // cambio de modo o de posicion: toda la reduccion actual pasa a la parte rapida
        double tot = b.ef + b.es;
        if (g > tot) b.ef += (1.0 - c.aA) * (g - tot);                       // el ataque entra como parte rapida
        tot = b.ef + b.es;
        const double mv = (c.smax * tot - b.es) * c.chg;                      // la parte lenta se carga mientras se comprime
        if (mv > 0.0) { const double m = std::min (mv, b.ef); b.es += m; b.ef -= m; }
        if (g < tot)
        {
            const double share = tot > 1.0e-9 ? b.es / tot : 0.0;
            const double tauS = c.tsMin + (c.tsMax - c.tsMin) * std::clamp (share / c.smax, 0.0, 1.0);
            const double aRs = 1.0 - 1.0 / (tauS * c.fs);
            if (b.es > c.smax * g) b.es = aRs * b.es + (1.0 - aRs) * c.smax * g;
            const double gt = std::max (0.0, g - b.es);
            if (b.ef > gt) b.ef = c.aRf * b.ef + (1.0 - c.aRf) * gt;
        }
        b.e = b.ef + b.es;
        return b.e;
    }
    if (c.mu)
    {
        const double dn = c.muRate / c.fs;
        if (g >= b.e - 1.0e-6) b.hold = c.holdSamples;
        const bool waiting = (g < b.e - 1.0e-6) && b.hold > 0;
        if (waiting) --b.hold;
        if (g > b.ef) b.ef = c.aA * b.ef + (1.0 - c.aA) * g; else if (! waiting) b.ef = std::max (g, b.ef - dn);
        if (g > b.es) b.es = c.aAs * b.es + (1.0 - c.aAs) * g; else if (! waiting) b.es = std::max (g, b.es - dn);
        b.e = c.wFast * b.ef + (1.0 - c.wFast) * b.es;
        return b.e;
    }
    if (! c.dual)
    {
        if (g > b.e)            { b.e = c.aA * b.e + (1.0 - c.aA) * g; b.hold = c.holdSamples; }
        else if (b.hold > 0)    --b.hold;
        else                    b.e = c.aR * b.e + (1.0 - c.aR) * g;
        return b.e;
    }
    const double tauS = c.tauSlowBase * (c.k0 + c.k1 * b.mem);
    const double aRs = 1.0 - 1.0 / (std::max (1.0e-3, tauS) * c.fs);
    if (g > b.ef) b.ef = c.aA * b.ef + (1.0 - c.aA) * g; else b.ef = c.aRf * b.ef + (1.0 - c.aRf) * g;
    if (g > b.es) b.es = c.aAs * b.es + (1.0 - c.aAs) * g; else b.es = aRs   * b.es + (1.0 - aRs)   * g;
    b.e = c.wFast * b.ef + (1.0 - c.wFast) * b.es;
    b.mem += (std::clamp (b.e / 10.0, 0.0, 1.0) - b.mem) * c.memCoef;
    return b.e;
}
} // namespace titc
