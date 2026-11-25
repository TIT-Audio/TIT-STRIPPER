#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SpectrumAnalyzer.h"
#include "ClickConfig.h"

class TitQProcessor : public juce::AudioProcessor
{
public:
    TitQProcessor();
    ~TitQProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TIT Q"; }
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

    titq::BandSettings readBandSettings (int band) const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<double> currentSampleRate { 44100.0 };
    // cuánto está actuando la dinámica en cada banda (0..1); la interfaz lo lee para mover la curva
    std::array<std::atomic<float>, titq::kNumBands> dynAmount {};
    // analizador FFT (entrada y salida) y estado de la entrada de sidechain externa
    SpectrumAnalyzer analyzerPre, analyzerPost;
    std::atomic<bool> extScActive { false };

    // comportamiento configurable de clics (se carga del preset .json marcado como Default)
    titq::ClickConfig clickConfig;
    juce::String clickPresetName;

private:
    static constexpr int kChunk = 32; // el EQ dinámico actualiza coeficientes cada 32 muestras

    struct ParamPtrs
    {
        std::atomic<float> *used, *on, *type, *freq, *gain, *q, *slope, *boost, *atten, *solo,
                           *dyn, *dyndir, *range, *thr, *ratio, *attack, *release, *knee,
                           *scsrc, *scfree, *scfreq, *scq, *chmode;
    };

    struct BandRuntime
    {
        titq::EqBand eq;
        titq::BandSettings last;
        double lastDyn = -1.0;
        titq::Biquad detector;
        double detF = -1.0, detQ = -1.0;
        float amount = 0.f; // 0..1 cuánto actúa la banda dinámica
        double ms = 0.0;    // energía media (RMS al cuadrado) del detector
        float fade = 0.f, fadeStart = 0.f, fadeEnd = 0.f;   // fundido de encendido/apagado de la banda (sin clics)
        double sFreq = -1.0, sSlope = 0.0;                  // frecuencia y pendiente suavizadas (filtros de corte)
        int sType = -1;
        int chmode = 0;                                     // 0 Stereo, 1 Mid, 2 Side, 3 Left, 4 Right
    };

    void setOversampling (int index);
    void updateBands (const float* intL, const float* intR, const float* extL, const float* extR, int n);
    void processBands (juce::dsp::AudioBlock<float>& block);

    std::array<ParamPtrs, titq::kNumBands> ptrs {};
    std::array<BandRuntime, titq::kNumBands> bands;
    std::atomic<float> *osParam = nullptr, *outParam = nullptr;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    int currentOs = 0, osFactor = 1;

    titq::Biquad soloFilter;
    double soloF = -1.0, soloQ = -1.0;
    juce::AudioBuffer<float> dryChunk;
    float lastOutGain = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitQProcessor)
};
