#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SpectrumAnalyzer.h"

class TitCompProcessor : public juce::AudioProcessor
{
public:
    TitCompProcessor();
    ~TitCompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TIT COMP"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    // Carga un estado guardado (proyecto o preset) migrando los formatos antiguos: version < 3 tenia el Threshold de TIT MU invertido
    void loadStateXml (const juce::XmlElement& xml);
    std::unique_ptr<juce::XmlElement> currentStateXml() { auto st = apvts.copyState(); st.setProperty ("titVersion", 3, nullptr); return st.createXml(); }
    void setStateInformation (const void*, int) override;

    titq::BandSettings readBandSettings (int band) const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<float> grDb { 0.f };              // reducción de ganancia actual (dB, >= 0)
    std::atomic<float> inDb { -120.f }, outDb { -120.f };   // picos de entrada y salida (dBFS)
    std::atomic<bool> extScActive { false };
    SpectrumAnalyzer analyzerPre, analyzerPost;

private:
    static constexpr int kChunk = 32;

    struct BandRt
    {
        titq::EqBand eq;
        titq::BandSettings last;
        double lastFs = 0.0;
        bool wasOn = false, active = false;
    };

    titc::Settings readSettings() const;
    void setOversampling (int index);
    void updateLatency();
    void updateBands (double osFs);
    void processCore (juce::dsp::AudioBlock<float>& m, juce::dsp::AudioBlock<float>& sc,
                      const titc::Settings& s, bool listen, float mix, double lookMs);

    std::array<BandRt, titc::kNumBands> bands;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> osMain, osSc;
    int currentOs = 0, osFactor = 1, lastLatency = -1;
    double osLatency = 0.0;

    std::array<std::vector<float>, 2> delayBuf;
    int delayPos = 0;
    std::array<titc::Ballistics, 2> bal;
    std::array<titc::Timbre, 2> colorTimbre;
    titq::Biquad muHp, muHp1;   // filtro de sidechain de TIT MU: 2o orden + 1er orden (medido en un compresor variable-mu de referencia)
    std::array<double, 2> det { 0.0, 0.0 };
    std::array<titc::PeakEst, 2> peakEst;

    juce::AudioBuffer<float> scBuf;
    float lastOutGain = 1.f;
    int lastMode = -1;   // para unificar el estado de la balistica al cambiar de modo (sin saltos)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitCompProcessor)
};
