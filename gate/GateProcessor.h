#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GateCore.h"

namespace titg
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

// Modulo Gate / Expander de TIT STRIPPER (procesador interno: la cadena lo llama directamente y le presta la ventana)
class GateProcessor : public juce::AudioProcessor
{
public:
    GateProcessor();
    ~GateProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TIT GATE"; }
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

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> grDb { 0.f };                       // atenuacion actual (dB, >= 0)
    std::atomic<float> inDb { -120.f }, keyDb { -120.f };  // picos de entrada y de la senal de deteccion (dBFS)
    std::atomic<bool> extScActive { false };

private:
    titg::Settings readSettings() const;

    titg::Gate gate;
    double fs = 44100.0;
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateProcessor)
};
