#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class TitCompEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TitCompEditor (TitCompProcessor&);
    ~TitCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct TitLaf : juce::LookAndFeel_V4
    {
        TitLaf();
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    // Slider cuyo recorrido (de tope a tope) se acota a [lo, hi]: el mando llega al tope justo en el primer y el ultimo valor reales del original
    struct LimitedSlider : juce::Slider
    {
        double lo = -1.0, hi = -1.0;    // hi <= lo: sin acotar
        bool limited() const noexcept { return hi > lo; }
        double valueToProportionOfLength (double v) override
        {
            if (! limited()) return juce::Slider::valueToProportionOfLength (v);
            const double pl = juce::Slider::valueToProportionOfLength (lo), ph = juce::Slider::valueToProportionOfLength (hi);
            return juce::jlimit (0.0, 1.0, (juce::Slider::valueToProportionOfLength (juce::jlimit (lo, hi, v)) - pl) / (ph - pl));
        }
        double proportionOfLengthToValue (double p) override
        {
            if (! limited()) return juce::Slider::proportionOfLengthToValue (p);
            const double pl = juce::Slider::valueToProportionOfLength (lo), ph = juce::Slider::valueToProportionOfLength (hi);
            return juce::jlimit (lo, hi, juce::Slider::proportionOfLengthToValue (pl + juce::jlimit (0.0, 1.0, p) * (ph - pl)));
        }
    };

    struct Knob
    {
        LimitedSlider s;
        juce::Label l;
        std::unique_ptr<SliderAtt> att;
    };

    void timerCallback() override;
    void selectBand (int b);
    void updateEnabledStates();
    void applyModePreset (int mode);
    void enforceModeLimits();
    void configureSlots (int mode);
    void layoutKnobs();
    static juce::File presetDir();
    void refreshPresets();
    void loadPreset (const juce::String& name);
    void askSavePreset();
    void savePreset (const juce::String& name);
    void showPresetMenu();

    bool isBandUsed (int b) const;
    bool isBandOn (int b) const;
    int  firstFreeBand() const;
    float dbRange() const;
    void setParam (const juce::String& id, float realValue);
    void setParamGesture (const juce::String& id, float realValue);
    void toggleParam (const juce::String& id);
    void resetBandParams (int b);
    int  createBand (float freq, float gainDb, int typeIndex, float slope);
    int  createBandAt (juce::Point<float> pos);
    void deleteBand (int b);

    juce::Rectangle<float> displayArea() const;
    juce::Rectangle<float> meterArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    bool nodeDraggableInGain (int type) const { return type != titq::LowCut && type != titq::HighCut; }
    juce::Point<float> nodePosition (int b) const;
    int findNode (juce::Point<float> p) const;
    void buildSpectrumPaths (const std::array<float, SpectrumAnalyzer::kBins>& db, juce::Path& line, juce::Path& fill) const;

    TitCompProcessor& proc;
    TitLaf laf;
    juce::Image background;
    double scale = 1.0;
    double viewFs = 44100.0;

    juce::ComboBox osBox, modeBox, scSrcBox, dbBox, typeBox, presetBox, msBox;
    juce::TextButton savePresetBtn { "Save" }, presetMenuBtn { "..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ToggleButton listenBtn { "SC Listen" }, onBtn { "On" }, autoBtn { "Auto" };
    juce::TextButton deleteBtn { "Delete" };
    juce::Label scWarn;
    std::unique_ptr<ComboAtt> osAtt, modeAtt, scSrcAtt, dbAtt, typeAtt, msAtt;
    std::unique_ptr<ButtonAtt> listenAtt, onAtt, autoAtt;

    int selected = 0;
    std::array<juce::TextButton, titc::kNumBands> bandButtons;

    enum { kThr, kRatio, kKnee, kAttack, kRelease, kHold, kLook, kRange, kLink, kMix, kOut, kFreq, kGain, kQ, kSlope, kNumKnobs };
    std::array<Knob, kNumKnobs> knobs;

    std::array<titq::EqBand, titc::kNumBands> viewBands;
    int dragBand = -1;
    int lastModeSeen = -1;
    juce::uint32 lastCreateMs = 0;
    float grShown = 0.f, inShown = -120.f, outShown = -120.f;
    int slotMode = -1, slotCount = 8;

    std::array<float, SpectrumAnalyzer::kBins> preDb, postDb, scratchDb;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitCompEditor)
};
