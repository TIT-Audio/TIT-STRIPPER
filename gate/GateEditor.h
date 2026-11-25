#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GateProcessor.h"

class GateEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GateEditor (GateProcessor&);
    ~GateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Laf : juce::LookAndFeel_V4
    {
        Laf();
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    struct Knob
    {
        juce::Slider s;
        juce::Label l;
        std::unique_ptr<SliderAtt> att;
    };

    void timerCallback() override;
    juce::Rectangle<float> curveArea() const;
    juce::Rectangle<float> meterArea() const;

    GateProcessor& proc;
    Laf laf;
    juce::Image background;
    double scale = 1.0;

    static constexpr int kNumKnobs = 20;
    std::array<Knob, kNumKnobs> knobs;
    juce::ComboBox scSrcBox;
    juce::ToggleButton listenBtn { "SC Listen" };
    juce::ToggleButton scEqBtn { "SC EQ On" };
    std::unique_ptr<ButtonAtt> scEqAtt;
    juce::Label scWarn;
    std::unique_ptr<ComboAtt> scSrcAtt;
    std::unique_ptr<ButtonAtt> listenAtt;

    float grShown = 0.f, inShown = -120.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateEditor)
};
