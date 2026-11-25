#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../gate/GateProcessor.h"
#include "../comp/PluginProcessor.h"
#include "../q/PluginProcessor.h"
#include "ProxyParam.h"

// TIT STRIPPER: cadena de tres modulos (Gate/Expander, TIT COMP y TIT Q) con orden intercambiable, On/Off por modulo,
// sidechain externo compartido y presets del conjunto.
class StripProcessor : public juce::AudioProcessor
{
public:
    enum Module { Gate = 0, Comp = 1, Eq = 2, NumModules = 3 };

    StripProcessor();
    ~StripProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TIT STRIPPER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // estado completo (cadena + los tres modulos) para proyectos y presets
    std::unique_ptr<juce::XmlElement> createStateXml();
    void loadStateXml (const juce::XmlElement&);
    void resetToDefaults();

    // orden de la cadena: permutacion de {Gate, Comp, Eq}
    static const std::array<std::array<int, 3>, 6>& orders();
    std::array<int, 3> currentOrder() const;
    void setOrder (const std::array<int, 3>& o);
    bool isModuleOn (int m) const { return apvts.getRawParameterValue (onId (m))->load() > 0.5f; }
    static const char* onId (int m) { static const char* ids[] = { "gate_on", "comp_on", "eq_on" }; return ids[m]; }
    static const char* moduleName (int m) { static const char* n[] = { "GATE", "COMP", "EQ" }; return n[m]; }

    juce::AudioProcessor& module (int m) { return *mods[(size_t) m]; }

    juce::AudioProcessorValueTreeState apvts;

private:
    void configureModules (int numMain, bool sidechain);

    std::unique_ptr<GateProcessor> gate;
    std::unique_ptr<TitCompProcessor> comp;
    std::unique_ptr<TitQProcessor> eq;
    std::array<juce::AudioProcessor*, 3> mods {};
    std::vector<ProxyParam*> proxies;

    juce::AudioBuffer<float> work, dry;
    std::array<float, 3> mixState { 0.f, 0.f, 0.f };   // 0 = modulo fuera, 1 = dentro (para el fundido de On/Off)
    double sampleRate = 44100.0;
    int blockSize = 512, cfgMain = 0, lastLatency = -1;
    bool cfgSc = false, prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripProcessor)
};
