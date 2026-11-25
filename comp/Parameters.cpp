#include "Parameters.h"

namespace titc
{
static juce::String trimNum (float v, int maxDecimals)
{
    juce::String t (v, maxDecimals);
    if (t.containsChar ('.')) { t = t.trimCharactersAtEnd ("0"); t = t.trimCharactersAtEnd ("."); }
    if (t == "-0" || t.isEmpty()) t = "0";
    return t;
}

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
                         if (unit.isNotEmpty()) t += " " + unit;
                         return maxLen > 0 ? t.substring (0, maxLen) : t;
                     });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- globales ----
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "os", 1 }, "Oversampling",
                juce::StringArray { "Off", "2x", "4x", "8x" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", modeNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scsrc", 1 }, "Sidechain Source",
                juce::StringArray { "Internal", "External" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sclisten", 1 }, "SC Listen", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ms", 1 }, "Stereo Mode", juce::StringArray { "Stereo (L/R)", "Mid/Side" }, 0));

    {
        const juce::String pm { juce::CharPointer_UTF8 ("\xc2\xb1") };
        juce::StringArray names;
        for (int v = 3; v <= 12; ++v) names.add (pm + juce::String (v) + " dB");
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "dbrange", 1 }, "Display Range", names, 9));
    }

    // ---- compresor ----
    layout.add (makeFloat ("thr",     "Threshold", -60.f, 15.f,   -18.f, 0.f,   "dB",  3, 0.1f));
    layout.add (makeFloat ("ratio",   "Ratio",       1.f, 20.f,     4.f, 4.f,   ":1",  3, 0.1f));
    layout.add (makeFloat ("knee",    "Knee",        0.f, 30.f,     6.f, 0.f,   "dB",  3, 0.1f));
    layout.add (makeFloat ("dknee",   "Knee (TIT DIGITAL)", 0.f, 31.f, 6.f, 0.f, "dB", 3, 0.1f));   // 31 = rodilla infinita
    layout.add (makeFloat ("attack",  "Attack",     0.01f, 250.f,  10.f, 5.f,   "ms",  2, 0.f));
    layout.add (makeFloat ("release", "Release",    5.f, 5000.f,  120.f, 150.f, "ms",  2, 0.f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autorel", 1 }, "Auto Release", false));
    layout.add (makeFloat ("fin",     "FET Input",   0.f, 48.f,   12.f, 0.f,   "",    1, 0.1f));
    layout.add (makeFloat ("fout",    "FET Output",  0.f, 48.f,   29.f, 0.f,   "",    1, 0.1f));
    layout.add (makeFloat ("pr",      "Peak Reduction", 0.f, 100.f, 40.f, 0.f,  "",    1, 0.1f));
    layout.add (makeFloat ("gain",    "Opto Gain", 0.f, 100.f, 26.f, 0.f,  "",    1, 0.1f));
    layout.add (makeFloat ("vin",     "variable-mu Input Gain", 0.f, 20.f, 20.f, 0.f, "", 1, 0.1f));
    layout.add (makeFloat ("vthr",    "variable-mu Threshold",  0.f, 10.f, 8.f, 0.f, "", 2, 0.01f));
    layout.add (makeFloat ("dxcomp",  "DXX Compression", 1.5f, 100.f, 4.f, 4.f, "",   1, 0.1f));
    layout.add (makeFloat ("mutrim",  "MU Input Trim", -24.f, 24.f,  0.f, 0.f,  "dB", 1, 0.1f));
    layout.add (makeFloat ("muthr",   "MU Threshold",   0.f, 10.f,   2.f, 0.f,  "",   1, 0.1f));
    layout.add (makeFloat ("muatt",   "MU Attack",      0.f, 4.f,    1.f, 0.f,  "",   0, 1.f));
    layout.add (makeFloat ("murec",   "MU Recovery",    0.f, 4.f,    2.f, 0.f,  "",   0, 1.f));
    layout.add (makeFloat ("mumk",    "MU Make Up",     0.f, 19.f,   0.f, 0.f,  "dB", 1, 0.1f));
    layout.add (makeFloat ("mufilt",  "MU Filter",      0.f, 250.f,  0.f, 0.f,  "Hz", 0, 50.f));
    layout.add (makeFloat ("hold",    "Hold",       0.f, 500.f,     0.f, 0.f,   "ms",  3, 0.1f));
    layout.add (makeFloat ("look",    "Lookahead",  0.f, 20.f,      0.f, 0.f,   "ms",  3, 0.1f));
    layout.add (makeFloat ("range",   "Range",      0.f, 60.f,     60.f, 0.f,   "dB",  3, 0.1f));
    layout.add (makeFloat ("link",    "Stereo Link", 0.f, 100.f,  100.f, 0.f,   "%",   3, 1.f));
    layout.add (makeFloat ("mix",     "Mix",         0.f, 100.f,  100.f, 0.f,   "%",   3, 1.f));
    layout.add (makeFloat ("out",     "Output",    -24.f, 24.f,     0.f, 0.f,   "dB",  3, 0.01f));

    // ---- EQ del sidechain: 10 bandas, inicio limpio ----
    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::String n = "SC EQ " + juce::String (b + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { pid (b, "used"), 1 }, n + "Created", false));
        layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { pid (b, "on"), 1 },   n + "On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (b, "type"), 1 }, n + "Type", typeNames(), 0));
        layout.add (makeFloat (pid (b, "freq"),  n + "Freq",  10.f, 40000.f, 1000.f, 1000.f, "Hz", 2, 0.f));
        layout.add (makeFloat (pid (b, "gain"),  n + "Gain", -24.f, 24.f,    0.f,    0.f,    "dB", 3, 0.01f));
        layout.add (makeFloat (pid (b, "q"),     n + "Q",     0.025f, 40.f,  1.f,    1.f,    "", 3, 0.f));
        layout.add (makeFloat (pid (b, "slope"), n + "Slope", 0.f, 96.f,     12.f,   0.f,    "dB/oct", 3, 1.f));
    }
    return layout;
}
} // namespace titc
