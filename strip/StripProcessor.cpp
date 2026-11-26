#include "StripProcessor.h"
#include "StripEditor.h"
#include "StripConfig.h"

const std::array<std::array<int, 3>, 6>& StripProcessor::orders()
{
    static const std::array<std::array<int, 3>, 6> o { { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
    return o;
}

// Archivo de configuracion: <datos de usuario>/TIT Audio/TIT STRIPPER/config.txt (se crea la primera vez con el orden por defecto)
juce::File StripProcessor::configFile() { return titc::configFile(); }

int StripProcessor::configuredOrderIndex()
{
    const auto file = configFile();
    if (! file.existsAsFile())
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::String (stripcfg::defaultText()));
        return 0;
    }
    return stripcfg::parseOrderIndex (file.loadFileAsString().toStdString());
}

void StripProcessor::saveConfiguredOrderIndex (int idx)
{
    const auto file = configFile();
    file.getParentDirectory().createDirectory();
    const std::string old = file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string();
    file.replaceWithText (juce::String (stripcfg::textForOrder (idx, old)));
}

static juce::AudioProcessorValueTreeState::ParameterLayout createStripLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray names;
    for (auto& o : StripProcessor::orders())
        names.add (juce::String (StripProcessor::moduleName (o[0])) + " > " + StripProcessor::moduleName (o[1]) + " > " + StripProcessor::moduleName (o[2]));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "order", 1 }, "Chain Order", names, StripProcessor::configuredOrderIndex()));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "gate_on", 1 }, "Gate On", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "comp_on", 1 }, "Comp On", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "eq_on", 1 },   "EQ On", true));
    return layout;
}

StripProcessor::StripProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "TITSTRIPPER", createStripLayout())
{
    gate = std::make_unique<GateProcessor>();
    comp = std::make_unique<TitCompProcessor>();
    eq   = std::make_unique<TitQProcessor>();
    mods = { gate.get(), comp.get(), eq.get() };

    static const char* idPrefix[]   = { "g_", "c_", "e_" };
    static const char* namePrefix[] = { "Gate ", "Comp ", "EQ " };
    for (int m = 0; m < NumModules; ++m)
        for (auto* p : mods[(size_t) m]->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* px = new ProxyParam (*r, idPrefix[m], namePrefix[m]);
                proxies.push_back (px);
                addParameter (px);   // el procesador se queda con la propiedad
            }
}

StripProcessor::~StripProcessor()
{
    for (auto* px : proxies) px->detach();   // los modulos internos se destruyen antes que los parametros del procesador
}

bool StripProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (l.getMainInputChannelSet() != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

void StripProcessor::configureModules (int numMain, bool sidechain)
{
    const auto mainSet = numMain == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    for (auto* m : mods)
    {
        auto l = m->getBusesLayout();
        l.getChannelSet (true, 0)  = mainSet;
        l.getChannelSet (false, 0) = mainSet;
        l.getChannelSet (true, 1)  = sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        m->setBusesLayout (l);
    }
    cfgMain = numMain;
    cfgSc = sidechain;
}

void StripProcessor::prepareToPlay (double fs, int bs)
{
    sampleRate = fs; blockSize = bs;
    const int numMain = juce::jlimit (1, 2, getMainBusNumInputChannels());
    auto* scBus = getBus (true, 1);
    const bool sc = scBus != nullptr && scBus->isEnabled();
    configureModules (numMain, sc);
    for (auto* m : mods)
    {
        m->setRateAndBufferSizeDetails (fs, bs);
        m->prepareToPlay (fs, bs);
    }
    work.setSize (numMain + (sc ? 2 : 0), bs);
    dry.setSize (numMain, bs);
    work.clear(); dry.clear();
    for (int m = 0; m < NumModules; ++m) mixState[(size_t) m] = isModuleOn (m) ? 1.f : 0.f;
    lastLatency = -1;
    prepared = true;
}

std::array<int, 3> StripProcessor::currentOrder() const
{
    const int idx = juce::jlimit (0, 5, (int) std::lround (apvts.getRawParameterValue ("order")->load()));
    return orders()[(size_t) idx];
}

void StripProcessor::setOrder (const std::array<int, 3>& o)
{
    for (size_t i = 0; i < orders().size(); ++i)
        if (orders()[i] == o)
            if (auto* p = apvts.getParameter ("order")) { p->setValueNotifyingHost (p->convertTo0to1 ((float) i)); return; }
}

void StripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int numMain = juce::jlimit (1, 2, main.getNumChannels());
    const int n = main.getNumSamples();
    if (n == 0) return;

    auto* scBus = getBus (true, 1);
    const bool sc = scBus != nullptr && scBus->isEnabled();
    if (! prepared || numMain != cfgMain || sc != cfgSc)
    {   // el DAW cambio la disposicion de canales sin volver a preparar (raro): se reconfiguran los modulos
        configureModules (numMain, sc);
        for (auto* m : mods) { m->setRateAndBufferSizeDetails (sampleRate, blockSize); m->prepareToPlay (sampleRate, blockSize); }
        prepared = true;
    }
    const int chs = numMain + (sc ? 2 : 0);
    if (work.getNumChannels() != chs || work.getNumSamples() < n) work.setSize (chs, std::max (n, blockSize), false, false, true);
    if (dry.getNumChannels() != numMain || dry.getNumSamples() < n) dry.setSize (numMain, std::max (n, blockSize), false, false, true);

    for (int c = 0; c < numMain; ++c) work.copyFrom (c, 0, main, c, 0, n);
    if (sc)
    {
        auto ext = getBusBuffer (buffer, true, 1);
        for (int k = 0; k < 2; ++k)
            if (ext.getNumChannels() > 0) work.copyFrom (numMain + k, 0, ext, std::min (k, ext.getNumChannels() - 1), 0, n);
            else work.clear (numMain + k, 0, n);
    }

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), chs, n);
    juce::MidiBuffer midi;
    int latency = 0;
    for (int m : currentOrder())
    {
        const float target = isModuleOn (m) ? 1.f : 0.f, from = mixState[(size_t) m];
        if (from <= 0.f && target <= 0.f) continue;
        const bool fade = from != target;
        if (fade)
            for (int c = 0; c < numMain; ++c) dry.copyFrom (c, 0, view, c, 0, n);
        mods[(size_t) m]->processBlock (view, midi);
        if (fade)
            for (int c = 0; c < numMain; ++c)
            {
                float* w = view.getWritePointer (c);
                const float* d = dry.getReadPointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float wt = from + (target - from) * (float) (i + 1) / (float) n;
                    w[i] = d[i] + wt * (w[i] - d[i]);
                }
            }
        mixState[(size_t) m] = target;
        if (target > 0.f) latency += mods[(size_t) m]->getLatencySamples();
    }
    if (latency != lastLatency) { lastLatency = latency; setLatencySamples (latency); }

    for (int c = 0; c < numMain; ++c) main.copyFrom (c, 0, view, c, 0, n);
}

juce::AudioProcessorEditor* StripProcessor::createEditor() { return new StripEditor (*this); }

std::unique_ptr<juce::XmlElement> StripProcessor::createStateXml()
{
    auto root = std::make_unique<juce::XmlElement> ("TITSTRIPPER");
    root->setAttribute ("version", 1);
    auto add = [&root] (const char* tag, std::unique_ptr<juce::XmlElement> x)
    {
        auto* w = root->createNewChildElement (tag);
        if (x != nullptr) w->addChildElement (x.release());
    };
    add ("strip", apvts.copyState().createXml());
    add ("gate",  gate->apvts.copyState().createXml());
    add ("comp",  comp->currentStateXml());
    add ("eq",    eq->apvts.copyState().createXml());
    return root;
}

void StripProcessor::loadStateXml (const juce::XmlElement& root)
{
    auto inner = [&root] (const char* tag) -> const juce::XmlElement*
    {
        if (auto* w = root.getChildByName (tag)) return w->getFirstChildElement();
        return nullptr;
    };
    if (auto* x = inner ("gate")) gate->apvts.replaceState (juce::ValueTree::fromXml (*x));
    if (auto* x = inner ("comp")) comp->loadStateXml (*x);
    if (auto* x = inner ("eq"))   eq->apvts.replaceState (juce::ValueTree::fromXml (*x));
    if (auto* x = inner ("strip")) apvts.replaceState (juce::ValueTree::fromXml (*x));
}

void StripProcessor::resetToDefaults()
{
    for (auto* px : proxies)
        if (auto* in = px->getInner()) in->setValueNotifyingHost (in->getDefaultValue());
    for (auto* id : { "order", "gate_on", "comp_on", "eq_on" })
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->getDefaultValue());
}

void StripProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createStateXml()) copyXmlToBinary (*xml, dest);
}

void StripProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("TITSTRIPPER"))
            loadStateXml (*xml);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StripProcessor(); }
