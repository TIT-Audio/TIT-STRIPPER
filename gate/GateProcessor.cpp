#include "GateProcessor.h"
#include "GateEditor.h"

namespace titg
{
static juce::String trimNum (float v, int maxDecimals)
{
    juce::String t (v, maxDecimals);
    if (t.containsChar ('.')) { t = t.trimCharactersAtEnd ("0"); t = t.trimCharactersAtEnd ("."); }
    if (t == "-0" || t.isEmpty()) t = "0";
    return t;
}

static std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name, float lo, float hi, float def,
                                                             float centre, const juce::String& unit, int decimals, float step = 0.f,
                                                             float infAt = -1.f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > 0.f) r.setSkewForCentre (centre);
    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel (unit)
                     .withStringFromValueFunction ([decimals, unit, infAt] (float v, int maxLen)
                     {
                         if (infAt > 0.f && v >= infAt) return juce::String ("inf");
                         juce::String t = trimNum (v, decimals);
                         if (unit.isNotEmpty()) t += (unit.startsWithChar (':') ? "" : " ") + unit;
                         return maxLen > 0 ? t.substring (0, maxLen) : t;
                     });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat ("thr",     "Threshold",   -80.f, 0.f,    -50.f, 0.f,   "dB",    3, 0.1f));
    layout.add (makeFloat ("range",   "Range",         0.f, 90.f,    60.f, 0.f,   "dB",    3, 0.1f));
    layout.add (makeFloat ("ratio",   "Ratio",         1.f, 100.f,  100.f, 6.f,   ":1",    3, 0.1f, 99.f));   // 100 = gate duro
    layout.add (makeFloat ("attack",  "Attack",       0.01f, 100.f,  0.5f, 2.f,   "ms",    2));
    layout.add (makeFloat ("hold",    "Hold",           0.f, 2000.f, 50.f, 100.f, "ms",    2));
    layout.add (makeFloat ("release", "Release",        5.f, 5000.f, 150.f, 150.f, "ms",   2));
    layout.add (makeFloat ("hyst",    "Hysteresis",     0.f, 20.f,    3.f, 0.f,   "dB",    3, 0.1f));
    layout.add (makeFloat ("look",    "Lookahead",      0.f, 10.f,    0.f, 0.f,   "ms",    3, 0.1f));
    layout.add (makeFloat ("schp",    "SC High-pass",  20.f, 2000.f,  20.f, 200.f, "Hz",   2));
    layout.add (makeFloat ("sclp",    "SC Low-pass",  200.f, 20000.f, 20000.f, 3000.f, "Hz", 2));
    layout.add (makeFloat ("schps",   "SC HP Slope",    6.f, 96.f,     12.f, 0.f,   "dB/oct", 3, 1.f));
    layout.add (makeFloat ("sclps",   "SC LP Slope",    6.f, 96.f,     12.f, 0.f,   "dB/oct", 3, 1.f));
    layout.add (makeFloat ("link",    "Stereo Link",    0.f, 100.f,  100.f, 0.f,   "%",    3, 1.f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sceq", 1 }, "SC EQ On", false));
    {
        const char* nm[3] = { "Low", "Mid", "High" };
        const float df[3] = { 100.f, 1000.f, 6000.f }, dq[3] = { 0.7f, 1.0f, 0.7f };
        for (int i = 0; i < 3; ++i)
        {
            const juce::String n (nm[i]), k (i + 1);
            layout.add (makeFloat ("ef" + k, "SC EQ " + n + " Freq", 20.f, 20000.f, df[i], i == 0 ? 100.f : i == 1 ? 1000.f : 6000.f, "Hz", 2));
            layout.add (makeFloat ("eg" + k, "SC EQ " + n + " Gain", -18.f, 18.f, 0.f, 0.f, "dB", 3, 0.1f));
            layout.add (makeFloat ("eq" + k, "SC EQ " + n + " Q", 0.2f, 8.f, dq[i], 1.f, "", 3, 0.01f));
        }
    }
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scsrc", 1 }, "Sidechain Source",
                juce::StringArray { "Internal", "External" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sclisten", 1 }, "SC Listen", false));
    return layout;
}
} // namespace titg

static float P (const juce::AudioProcessorValueTreeState& a, const char* id) { return a.getRawParameterValue (id)->load(); }

GateProcessor::GateProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "TITGATE", titg::createLayout())
{
}

titg::Settings GateProcessor::readSettings() const
{
    titg::Settings s;
    s.thr = P (apvts, "thr");        s.range = P (apvts, "range");   s.ratio = P (apvts, "ratio");
    s.attackMs = P (apvts, "attack"); s.holdMs = P (apvts, "hold");  s.releaseMs = P (apvts, "release");
    s.hystDb = P (apvts, "hyst");    s.lookMs = P (apvts, "look");
    s.hpHz = P (apvts, "schp");      s.lpHz = P (apvts, "sclp");
    s.hpSlope = P (apvts, "schps");  s.lpSlope = P (apvts, "sclps");     s.link = P (apvts, "link") * 0.01;
    s.eqOn = P (apvts, "sceq") > 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        const juce::String k (i + 1);
        s.eqF[i] = P (apvts, ("ef" + k).toRawUTF8()); s.eqG[i] = P (apvts, ("eg" + k).toRawUTF8()); s.eqQ[i] = P (apvts, ("eq" + k).toRawUTF8());
    }
    return s;
}

bool GateProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (l.getMainInputChannelSet() != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

void GateProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    currentSampleRate.store (sampleRate);
    gate.prepare (sampleRate, 12.0);
    gate.setSettings (readSettings());
    lastLatency = -1;
}

void GateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = std::min (main.getNumChannels(), 2);
    const int n = main.getNumSamples();
    if (numCh == 0 || n == 0) return;

    auto* scBus = getBus (true, 1);
    const bool extAvailable = scBus != nullptr && scBus->isEnabled();
    extScActive.store (extAvailable, std::memory_order_relaxed);
    const bool useExt = extAvailable && P (apvts, "scsrc") > 0.5f;
    const bool listen = P (apvts, "sclisten") > 0.5f;
    juce::AudioBuffer<float> ext;
    if (useExt) ext = getBusBuffer (buffer, true, 1);

    gate.setSettings (readSettings());
    const int lat = gate.latencySamples();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples (lat); }

    float* ch[2] { main.getWritePointer (0), main.getWritePointer (numCh > 1 ? 1 : 0) };
    const float* kc[2] { ch[0], ch[1] };
    if (useExt && ext.getNumChannels() > 0)
    {
        kc[0] = ext.getReadPointer (0);
        kc[1] = ext.getReadPointer (ext.getNumChannels() > 1 ? 1 : 0);
    }

    float inPk = 0.f, keyPk = 0.f; double grMax = 0.0;
    for (int off = 0; off < n; off += kChunk)
    {
        const int m = std::min (kChunk, n - off);
        analyzerPre.push (kc[0] + off, kc[1] + off, m);   // antes de procesar: en modo interno kc apunta al mismo buffer que se sobrescribe
        for (int i = 0; i < m; ++i)
        {
            const int j = off + i;
            const float x[2] { ch[0][j], ch[1][j] }, k[2] { kc[0][j], kc[1][j] };
            float y[2], kf[2];
            grMax = std::max (grMax, gate.process (x, k, numCh, y, kf));
            for (int c = 0; c < numCh; ++c)
            {
                inPk = std::max (inPk, std::fabs (x[c]));
                keyPk = std::max (keyPk, std::fabs (kf[c]));
                ch[c][j] = listen ? kf[c] : y[c];
            }
            keyPost[0][i] = kf[0];
            keyPost[1][i] = numCh > 1 ? kf[1] : kf[0];
        }
        analyzerPost.push (keyPost[0], keyPost[1], m);
    }
    grDb.store ((float) grMax, std::memory_order_relaxed);
    inDb.store (juce::Decibels::gainToDecibels (inPk, -120.f), std::memory_order_relaxed);
    keyDb.store (juce::Decibels::gainToDecibels (keyPk, -120.f), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GateProcessor::createEditor() { return new GateEditor (*this); }

void GateProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void GateProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
