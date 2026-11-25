#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace titc;

TitCompProcessor::TitCompProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "TITCOMP", createLayout())
{
}

static float P (const juce::AudioProcessorValueTreeState& a, const char* id)
{
    return a.getRawParameterValue (id)->load();
}

titq::BandSettings TitCompProcessor::readBandSettings (int b) const
{
    titq::BandSettings s;
    s.on = apvts.getRawParameterValue (pid (b, "used"))->load() > 0.5f && apvts.getRawParameterValue (pid (b, "on"))->load() > 0.5f;
    s.type = juce::jlimit (0, (int) typeNames().size() - 1, (int) std::lround (apvts.getRawParameterValue (pid (b, "type"))->load()));
    s.freq = apvts.getRawParameterValue (pid (b, "freq"))->load();
    s.gain = apvts.getRawParameterValue (pid (b, "gain"))->load();
    s.q    = apvts.getRawParameterValue (pid (b, "q"))->load();
    s.slope = apvts.getRawParameterValue (pid (b, "slope"))->load();
    return s;
}

Settings TitCompProcessor::readSettings() const
{
    Settings s;
    s.mode = juce::jlimit (0, (int) NumModes - 1, (int) std::lround (P (apvts, "mode")));
    s.thr = P (apvts, "thr");        s.ratio = P (apvts, "ratio");   s.knee = P (apvts, "knee");
    s.attackMs = P (apvts, "attack"); s.releaseMs = P (apvts, "release"); s.holdMs = P (apvts, "hold");
    s.range = P (apvts, "range");    s.link = P (apvts, "link") * 0.01;
    s.autoRel = P (apvts, "autorel") > 0.5f;
    s.fin = P (apvts, "fin"); s.fout = P (apvts, "fout"); s.pr = P (apvts, "pr"); s.gain = P (apvts, "gain");
    s.muTrim = P (apvts, "mutrim"); s.muThr = P (apvts, "muthr"); s.muAtt = P (apvts, "muatt"); s.muRec = P (apvts, "murec");
    s.dxComp = P (apvts, "dxcomp"); s.muMk = P (apvts, "mumk"); s.muFilt = P (apvts, "mufilt");
    s.vin = P (apvts, "vin"); s.vthr = P (apvts, "vthr"); s.outParam = P (apvts, "out");
    if (s.mode == DIG) s.knee = P (apvts, "dknee");
    if (s.mode != Clean && s.mode != DIG) s.range = 60.0;
    constrain (s);
    derive (s);   // cada modo solo admite los valores del compresor original
    return s;
}

bool TitCompProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (l.getMainInputChannelSet() != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

void TitCompProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (size_t i = 0; i < osMain.size(); ++i)
    {
        osMain[i] = std::make_unique<juce::dsp::Oversampling<float>> (2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        osSc[i]   = std::make_unique<juce::dsp::Oversampling<float>> (2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        osMain[i]->initProcessing ((size_t) kChunk);
        osSc[i]->initProcessing ((size_t) kChunk);
    }

    const int maxDelay = (int) std::ceil (0.02 * sampleRate * 8.0) + 16;
    for (auto& d : delayBuf) d.assign ((size_t) maxDelay, 0.f);
    delayPos = 0;
    for (auto& b : bal) b.reset();
    det = { 0.0, 0.0 };
    scBuf.setSize (2, kChunk);
    lastOutGain = juce::Decibels::decibelsToGain (P (apvts, "out"));
    for (auto& b : bands) { b.eq.reset(); b.wasOn = false; b.lastFs = 0.0; }
    lastLatency = -1;
    setOversampling ((int) std::lround (P (apvts, "os")));
}

void TitCompProcessor::setOversampling (int idx)
{
    idx = juce::jlimit (0, 3, idx);
    currentOs = idx;
    osFactor = 1 << idx;
    osLatency = 0.0;
    if (idx > 0)
    {
        osMain[(size_t) idx - 1]->reset();
        osSc[(size_t) idx - 1]->reset();
        osLatency = (double) osMain[(size_t) idx - 1]->getLatencyInSamples();
    }
    for (auto& b : bands) { b.eq.reset(); b.lastFs = 0.0; }
    for (auto& d : bal) d.reset();
    updateLatency();
}

void TitCompProcessor::updateLatency()
{
    const int md = (int) std::lround (P (apvts, "mode"));
    const int look = (md == Clean || md == DIG) ? juce::roundToInt (P (apvts, "look") * 0.001 * currentSampleRate.load()) : 0;
    const int total = juce::roundToInt (osLatency) + look;
    if (total != lastLatency) { lastLatency = total; setLatencySamples (total); }
}

void TitCompProcessor::updateBands (double osFs)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& rt = bands[(size_t) b];
        const titq::BandSettings s = readBandSettings (b);
        if (! s.on) { if (rt.wasOn) rt.eq.reset(); rt.wasOn = false; rt.active = false; continue; }
        if (! rt.wasOn) { rt.eq.reset(); rt.lastFs = 0.0; }
        rt.wasOn = true;
        if (rt.lastFs != osFs || s != rt.last)
        {
            rt.eq.design (s, osFs, 1.0);
            rt.last = s;
            rt.lastFs = osFs;
        }
        rt.active = true;
    }
}

void TitCompProcessor::processCore (juce::dsp::AudioBlock<float>& m, juce::dsp::AudioBlock<float>& sc,
                                    const Settings& s, bool listen, float mix, double lookMs)
{
    const int n = (int) m.getNumSamples();
    const int numCh = (int) m.getNumChannels();
    const double osFs = currentSampleRate.load() * osFactor;

    // EQ del sidechain (solo afecta a la detección)
    for (auto& rt : bands)
        if (rt.active)
            for (int ch = 0; ch < numCh; ++ch)
                rt.eq.process (sc.getChannelPointer ((size_t) ch), n, ch);

    if (s.mode == MU && s.muFilt >= 25.0)
    {   // filtro de sidechain de TIT MU (paso alto de 12 dB/oct)
        // medido en un compresor variable-mu de referencia (Filter 100 y 250): ~15 dB/oct, equivalente a un 2o orden Butterworth con corte ~3.4 x el valor del mando (potencia 0.76) mas un 1er orden a 100 Hz
        const double fc = std::min (0.45 * osFs, 340.0 * std::pow (s.muFilt / 100.0, 0.76));
        muHp.set (titq::Biquad::highpass (osFs, fc, 0.7071));
        muHp1.set (titq::Biquad::firstOrderHP (osFs, 100.0));
        for (int ch = 0; ch < numCh; ++ch) { muHp.process (sc.getChannelPointer ((size_t) ch), n, ch); muHp1.process (sc.getChannelPointer ((size_t) ch), n, ch); }
    }

    const auto dt = detectorFor (s.mode);
    const double aDA = std::exp (-1.0 / (dt.atkMs * 0.001 * osFs));
    const double aDR = std::exp (-1.0 / (dt.relMs * 0.001 * osFs));
    const Coefs cc = makeCoefs (s, osFs);

    const int dlen = (int) delayBuf[0].size();
    const int L = juce::jlimit (0, dlen - 2, juce::roundToInt (lookMs * 0.001 * osFs));

    double lev[2] = { -200.0, -200.0 };
    double grLast = 0.0;

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const double x = (double) sc.getSample (ch, i);
            const double p = x * x;
            det[(size_t) ch] = (p > det[(size_t) ch]) ? aDA * det[(size_t) ch] + (1.0 - aDA) * p
                                                      : aDR * det[(size_t) ch] + (1.0 - aDR) * p;
            lev[ch] = 10.0 * std::log10 (det[(size_t) ch] + 1.0e-12) + dt.offsetDb;
        }
        const double mx = numCh > 1 ? std::max (lev[0], lev[1]) : lev[0];

        for (int ch = 0; ch < numCh; ++ch)
        {
            const double own = lev[ch];
            const double linked = s.link * mx + (1.0 - s.link) * own;
            const double gr = gainReductionDb (linked, s);
            const double e = step (bal[(size_t) ch], gr, cc);
            grLast = std::max (grLast, e);

            auto& d = delayBuf[(size_t) ch];
            d[(size_t) delayPos] = m.getSample (ch, i);
            int rp = delayPos - L; if (rp < 0) rp += dlen;
            const float delayed = d[(size_t) rp];

            const float g = (float) std::exp (-e * 0.11512925464970229);
            float out = delayed + mix * (delayed * g - delayed);
            if (listen) out = sc.getSample (ch, i);
            m.setSample (ch, i, out);
        }
        if (++delayPos >= dlen) delayPos = 0;
    }
    grDb.store ((float) grLast, std::memory_order_relaxed);
}

void TitCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = std::min (main.getNumChannels(), 2);
    const int total = main.getNumSamples();
    if (numCh == 0 || total == 0) return;

    const int osIdx = juce::jlimit (0, 3, (int) std::lround (P (apvts, "os")));
    if (osIdx != currentOs) setOversampling (osIdx);
    updateLatency();

    auto* scBus = getBus (true, 1);
    const bool extAvailable = scBus != nullptr && scBus->isEnabled();
    extScActive.store (extAvailable, std::memory_order_relaxed);
    const bool useExt = extAvailable && P (apvts, "scsrc") > 0.5f;
    juce::AudioBuffer<float> ext;
    if (extAvailable) ext = getBusBuffer (buffer, true, 1);

    {
        float pk = 0.f;
        for (int ch = 0; ch < numCh; ++ch) pk = std::max (pk, main.getMagnitude (ch, 0, total));
        inDb.store (juce::Decibels::gainToDecibels (pk, -120.f), std::memory_order_relaxed);
    }
    analyzerPre.push (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0), total);

    const Settings s = readSettings();
    if (s.mode != lastMode)
    {   // al cambiar de modo, las dos ramas de la balistica parten del mismo valor y la memoria se reinicia: la reduccion no salta
        for (auto& b : bal) { b.ef = b.es = b.e; b.mem = 0.0; b.hold = 0; }
        lastMode = s.mode;
    }
    const bool msMode = P (apvts, "ms") > 0.5f;
    const bool listen = P (apvts, "sclisten") > 0.5f;
    const bool freeMode = (s.mode == Clean || s.mode == DIG), mixMode = (s.mode == Clean || s.mode == DIG || s.mode == VARI || s.mode == VCA || s.mode == MU || s.mode == DXX);
    const float mix = mixMode ? juce::jlimit (0.f, 1.f, P (apvts, "mix") * 0.01f) : 1.f;
    const double lookMs = freeMode ? (double) P (apvts, "look") : 0.0;
    const float newOutGain = juce::Decibels::decibelsToGain ((float) s.makeupDb);
    const double osFs = currentSampleRate.load() * osFactor;

    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (kChunk, total - pos);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = (useExt && ext.getNumChannels() > 0)
                                   ? ext.getReadPointer (std::min (ch, ext.getNumChannels() - 1)) + pos
                                   : main.getReadPointer (ch) + pos;
            scBuf.copyFrom (ch, 0, src, n);
        }

        updateBands (osFs);

        // Mid/Side: se codifica la senal y el sidechain, se comprime en M y S, y se decodifica al final
        const bool midSide = msMode && numCh == 2;
        if (midSide)
        {
            auto enc = [n] (float* l, float* r)
            {
                for (int i = 0; i < n; ++i) { const float a = l[i], b = r[i]; l[i] = 0.5f * (a + b); r[i] = 0.5f * (a - b); }
            };
            enc (main.getWritePointer (0) + pos, main.getWritePointer (1) + pos);
            enc (scBuf.getWritePointer (0), scBuf.getWritePointer (1));
        }

        juce::dsp::AudioBlock<float> mainBlk (main.getArrayOfWritePointers(), (size_t) numCh, (size_t) pos, (size_t) n);
        juce::dsp::AudioBlock<float> scBlk (scBuf.getArrayOfWritePointers(), (size_t) numCh, 0, (size_t) n);

        if (osIdx == 0)
            processCore (mainBlk, scBlk, s, listen, mix, lookMs);
        else
        {
            auto up   = osMain[(size_t) osIdx - 1]->processSamplesUp (mainBlk);
            auto upSc = osSc  [(size_t) osIdx - 1]->processSamplesUp (scBlk);
            processCore (up, upSc, s, listen, mix, lookMs);
            osMain[(size_t) osIdx - 1]->processSamplesDown (mainBlk);
        }

        if (midSide)
        {
            float* l = main.getWritePointer (0) + pos; float* r = main.getWritePointer (1) + pos;
            for (int i = 0; i < n; ++i) { const float m = l[i], sd = r[i]; l[i] = m + sd; r[i] = m - sd; }
        }

        const float g0 = lastOutGain + (newOutGain - lastOutGain) * (float) pos / (float) total;
        const float g1 = lastOutGain + (newOutGain - lastOutGain) * (float) (pos + n) / (float) total;
        main.applyGainRamp (pos, n, g0, g1);
        pos += n;
    }
    lastOutGain = newOutGain;

    {
        float pk = 0.f;
        for (int ch = 0; ch < numCh; ++ch) pk = std::max (pk, main.getMagnitude (ch, 0, total));
        outDb.store (juce::Decibels::gainToDecibels (pk, -120.f), std::memory_order_relaxed);
    }
    analyzerPost.push (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0), total);
}

juce::AudioProcessorEditor* TitCompProcessor::createEditor() { return new TitCompEditor (*this); }

void TitCompProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("titVersion", 3, nullptr);     // version del formato del estado (para que los presets antiguos sigan abriendo)
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TitCompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            loadStateXml (*xml);
}

void TitCompProcessor::loadStateXml (const juce::XmlElement& xml)
{
    auto vt = juce::ValueTree::fromXml (xml);
    if ((int) vt.getProperty ("titVersion", 0) < 3)
        for (auto c : vt)
            if (c.getProperty ("id").toString() == "muthr")
                c.setProperty ("value", 10.0 - (double) c.getProperty ("value", 8.0), nullptr);   // antes 10 = maxima compresion; ahora 0 (MIN) = maxima compresion
    vt.setProperty ("titVersion", 3, nullptr);
    apvts.replaceState (vt);
}

