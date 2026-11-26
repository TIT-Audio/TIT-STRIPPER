#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class TitQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TitQEditor (TitQProcessor&);
    ~TitQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

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

    // Slider cuyo recorrido (de tope a tope) puede acotarse a [lo, hi]: el mando llega al tope justo en esos valores
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
    void openSettings();
    static juce::File eqPresetDir();
    void refreshEqPresets();
    void loadEqPreset (const juce::String& name);
    void askSaveEqPreset();
    void saveEqPreset (const juce::String& name);
    void askDeleteEqPreset();
    void showPresetMenu();
    std::unique_ptr<juce::XmlElement> makePresetXml() const;
    bool applyPresetXml (juce::XmlElement xml);

    // estado de las bandas
    bool isBandUsed (int band) const;      // la banda está creada
    bool isBandOn (int band) const;        // creada y encendida
    bool isDynActive (int band) const;
    bool isFreeScOn (int band) const;
    int  firstFreeBand() const;            // primera banda libre (creación secuencial 1..10)
    float dbRange() const;

    // crear / borrar bandas
    void setParamGesture (const juce::String& id, float realValue);
    void resetBandParams (int band);
    int  createNewBandAt (juce::Point<float> pos);
    int  zoneAt (juce::Point<float> pos) const;
    int  createZoneBand (juce::Point<float> pos, int zone);
    juce::uint32 lastCreateMs = 0;
    juce::uint32 lastZoneClickMs = 0;
    juce::Point<float> lastZoneClickPos;
    int  createBand (float freq, float gainDb, const juce::String& typeName, float slope);
    void deleteBand (int band);

    // geometría de la gráfica
    juce::Rectangle<float> displayArea() const;
    juce::Rectangle<float> scStrip() const;   // barra inferior del sidechain libre
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    bool nodeIsDraggableInGain (int type) const;
    juce::Point<float> nodePosition (int band) const;
    int findNode (juce::Point<float> p) const;
    void setParam (const juce::String& id, float realValue);
    void toggleParam (const juce::String& id);
    void buildSpectrumPaths (const std::array<float, SpectrumAnalyzer::kBins>& db,
                             juce::Path& line, juce::Path& fill) const;

    TitQProcessor& proc;
    TitLaf laf;
    juce::Image background;
    double scale = 1.0;
    double viewFs = 44100.0;     // frecuencia de muestreo real del filtrado (con oversampling) para dibujar la curva

    // globales
    juce::ComboBox osBox, dbBox, phaseBox, lpqBox;
    juce::TextButton settingsBtn { "Settings" };
    juce::ComboBox eqPresetBox;
    juce::TextButton eqSaveBtn { "Save" }, eqDelBtn { "Delete" }, eqMenuBtn { "..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Slider outSlider;
    juce::Label outLabel;
    std::unique_ptr<ComboAtt> osAtt, dbAtt, phaseAtt, lpqAtt;
    std::unique_ptr<SliderAtt> outAtt;

    // banda seleccionada
    int selected = 0;
    std::array<juce::TextButton, titq::kNumBands> bandButtons;
    juce::ComboBox typeBox, scSrcBox, dirBox, chBox;
    juce::ToggleButton onBtn { "On" }, soloBtn { "Solo" }, dynBtn { "Dynamic" }, freeBtn { "Free SC detect" };
    juce::TextButton deleteBtn { "Delete" };
    juce::Label scWarn;
    std::unique_ptr<ComboAtt> typeAtt, scSrcAtt, dirAtt, chAtt;
    std::unique_ptr<ButtonAtt> onAtt, soloAtt, dynAtt, freeAtt;

    // freq gain q slope boost atten | range thr ratio attack release knee scfreq scq
    enum { kFreq, kGain, kQ, kSlope, kBoost, kAtten, kRange, kThr, kRatio, kAttack, kRelease, kKnee, kScFreq, kScQ, kNumKnobs };
    std::array<Knob, kNumKnobs> knobs;

    // curva
    std::array<titq::EqBand, titq::kNumBands> viewBands;   // curva en vivo (con la dinámica actuando)
    std::array<titq::EqBand, titq::kNumBands> ghostBands;  // curva estática (dinámica en reposo)
    int lastTypeSeen = -1;
    int dragBand = -1;
    bool dragSc = false;

    // analizador
    std::array<float, SpectrumAnalyzer::kBins> preDb, postDb, scratchDb;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitQEditor)
};
