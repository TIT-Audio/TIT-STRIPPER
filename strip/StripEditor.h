#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "StripProcessor.h"

// Ventana de TIT STRIPPER: barra superior con la cadena (orden, On/Off, seleccion de modulo) y presets;
// debajo, la ventana propia del modulo seleccionado.
class StripEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StripEditor (StripProcessor&);
    ~StripEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Laf : juce::LookAndFeel_V4
    {
        Laf();
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    struct ModuleCtl
    {
        juce::TextButton name, left { "<" }, right { ">" };
        juce::ToggleButton on { "On" };
        std::unique_ptr<ButtonAtt> onAtt;
    };

    void timerCallback() override;
    void select (int m);
    void moveModule (int m, int dir);
    void refreshOrder();

    static juce::File presetDir();
    void refreshPresets();
    void loadPresetFile (const juce::File&);
    void savePreset (const juce::String& name);
    void askSavePreset();
    void showPresetMenu();

    StripProcessor& proc;
    Laf laf;
    juce::Image background;
    double scale = 1.0;
    int selected = 1;
    std::array<int, 3> shownOrder { -1, -1, -1 };

    std::array<ModuleCtl, 3> ctl;
    std::array<std::unique_ptr<juce::AudioProcessorEditor>, 3> panels;
    juce::ComboBox presetBox;
    juce::TextButton saveBtn { "Save" }, menuBtn { "..." };
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int kBar = 52;   // alto de la barra superior (unidades base, ventana de 1100 de ancho)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripEditor)
};
