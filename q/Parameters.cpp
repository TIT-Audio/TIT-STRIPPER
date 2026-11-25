#include "Parameters.h"

namespace titq
{
// Número con como máximo 'maxDecimals' decimales, sin ceros sobrantes (1000.50 -> "1000.5", 0.7070 -> "0.707")
static juce::String trimNum (float v, int maxDecimals)
{
    juce::String t (v, maxDecimals);
    if (t.containsChar ('.'))
    {
        t = t.trimCharactersAtEnd ("0");
        t = t.trimCharactersAtEnd (".");
    }
    if (t == "-0" || t.isEmpty()) t = "0";
    return t;
}

// decimals = máximo de decimales que se muestran en las lecturas (Freq 2, Q 3, Attack 2, Release 2, resto 3)
static std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                             float lo, float hi, float def, float centre,
                                                             const juce::String& unit, int decimals, float step = 0.f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > 0.f) r.setSkewForCentre (centre);

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel (unit)
                     .withStringFromValueFunction ([decimals, unit] (float v, int maxLen)
                     {
                         juce::String t = trimNum (v, decimals);
                         if (unit.isNotEmpty()) t += (unit.startsWithChar (':') ? "" : " ") + unit;
                         return maxLen > 0 ? t.substring (0, maxLen) : t;
                     });

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Globales ----
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "os", 1 }, "Oversampling",
                juce::StringArray { "Off", "2x", "4x", "8x" }, 0));
    layout.add (makeFloat ("out", "Output", -24.f, 24.f, 0.f, 0.f, "dB", 3, 0.01f));

    // escala de dB simétrica de la gráfica: ±3 ... ±12 dB (por defecto ±6)
    {
        const juce::String pm { juce::CharPointer_UTF8 ("\xc2\xb1") };
        juce::StringArray names;
        for (int v = 3; v <= 12; ++v) names.add (pm + juce::String (v) + " dB");
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "dbrange", 1 }, "Display Range", names, 3));
    }

    // ---- 10 bandas: inicio limpio, ninguna banda creada ("used" = false) ----
    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";

        layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { pid (b, "used"), 1 }, n + "Created", false));
        layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { pid (b, "on"), 1 },   n + "On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (b, "type"), 1 }, n + "Type", typeNames(), Bell));
        layout.add (makeFloat (pid (b, "freq"),   n + "Freq",    10.f, 40000.f, 1000.f,     1000.f, "Hz", 2, 0.f));
        layout.add (makeFloat (pid (b, "gain"),   n + "Gain",   -24.f, 24.f,    0.f,        0.f,    "dB", 3, 0.01f));
        layout.add (makeFloat (pid (b, "q"),      n + "Q",       0.025f, 40.f,  1.f,        1.f,    "", 3, 0.f));
        layout.add (makeFloat (pid (b, "slope"),  n + "Slope",   0.f, 96.f,     12.f,       0.f,    "dB/oct", 3, 1.f));   // de 1 dB en 1 dB
        layout.add (makeFloat (pid (b, "boost"),  n + "BOOB",   0.f, 10.f,     0.f,        0.f,    "", 3, 0.1f));
        layout.add (makeFloat (pid (b, "atten"),  n + "ATTIT",   0.f, 10.f,     0.f,        0.f,    "", 3, 0.1f));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (b, "solo"), 1 }, n + "Solo", false));

        // dinámica
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (b, "dyn"), 1 }, n + "Dynamic", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (b, "dyndir"), 1 }, n + "Dynamic Direction",
                    juce::StringArray { "Above", "Below" }, 0));
        layout.add (makeFloat (pid (b, "range"),   n + "Range",     -24.f, 24.f, -6.f,  0.f,   "dB", 3, 0.1f));
        layout.add (makeFloat (pid (b, "thr"),     n + "Threshold", -80.f, 0.f,   -30.f, 0.f,   "dB", 3, 0.1f));
        layout.add (makeFloat (pid (b, "ratio"),   n + "Ratio",      1.f,  20.f,   4.f,  4.f,   ":1", 3, 0.1f));
        layout.add (makeFloat (pid (b, "attack"),  n + "Attack",     0.1f, 300.f,  10.f, 20.f,  "ms", 2, 0.f));
        layout.add (makeFloat (pid (b, "release"), n + "Release",    5.f,  3000.f, 150.f, 150.f, "ms", 2, 0.f));
        layout.add (makeFloat (pid (b, "knee"),    n + "Knee",       0.f,  30.f,   6.f,  0.f,   "dB", 3, 0.1f));

        // a que parte de la senal estereo actua la banda
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (b, "chmode"), 1 }, n + "Stereo Mode",
                    juce::StringArray { "Stereo", "Mid", "Side", "Left", "Right" }, 0));

        // sidechain por banda (una sola entrada externa por instancia, compartida por las bandas que la usen)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (b, "scsrc"), 1 }, n + "Sidechain Source",
                    juce::StringArray { "Internal", "External" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (b, "scfree"), 1 }, n + "Free SC Detect", false));
        layout.add (makeFloat (pid (b, "scfreq"), n + "SC Freq", 20.f, 30000.f, 1000.f, 1000.f, "Hz", 2, 0.f));
        layout.add (makeFloat (pid (b, "scq"),    n + "SC Q",    0.1f, 20.f,    1.f,    1.f,    "", 3, 0.f));
    }
    return layout;
}
} // namespace titq
