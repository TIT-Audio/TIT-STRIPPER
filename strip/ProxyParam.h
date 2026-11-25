#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parametro del plugin (visible para el DAW y automatizable) que refleja un parametro de un modulo interno.
// Si el DAW lo cambia, se traslada al modulo; si el usuario mueve el mando en la ventana del modulo, se traslada al DAW.
class ProxyParam : public juce::AudioProcessorParameterWithID, private juce::AudioProcessorParameter::Listener
{
public:
    ProxyParam (juce::RangedAudioParameter& innerParam, const juce::String& idPrefix, const juce::String& namePrefix)
        : juce::AudioProcessorParameterWithID (juce::ParameterID { idPrefix + innerParam.paramID, 1 }, namePrefix + innerParam.getName (64)),
          inner (&innerParam)
    {
        inner->addListener (this);
    }
    ~ProxyParam() override { detach(); }

    // hay que llamarlo antes de destruir el modulo interno
    void detach()
    {
        if (inner != nullptr) { inner->removeListener (this); inner = nullptr; }
    }

    juce::RangedAudioParameter* getInner() const noexcept { return inner; }

    float getValue() const override                 { return inner != nullptr ? inner->getValue() : 0.f; }
    void setValue (float v) override
    {
        if (inner == nullptr || fromInner.load()) return;
        forwarding.store (true);
        inner->setValue (v);
        inner->sendValueChangedMessageToListeners (v);   // el valor "crudo" del modulo y sus mandos se enteran (el APVTS lo lee asi)
        forwarding.store (false);
    }
    float getDefaultValue() const override          { return inner != nullptr ? inner->getDefaultValue() : 0.f; }
    juce::String getText (float v, int len) const override { return inner != nullptr ? inner->getText (v, len) : juce::String(); }
    float getValueForText (const juce::String& t) const override { return inner != nullptr ? inner->getValueForText (t) : 0.f; }
    juce::String getLabel() const override          { return inner != nullptr ? inner->getLabel() : juce::String(); }
    int getNumSteps() const override                { return inner != nullptr ? inner->getNumSteps() : 2; }
    bool isDiscrete() const override                { return inner != nullptr && inner->isDiscrete(); }
    bool isBoolean() const override                 { return inner != nullptr && inner->isBoolean(); }

private:
    void parameterValueChanged (int, float newValue) override
    {
        if (forwarding.load() || fromInner.load()) return;
        fromInner.store (true);
        setValueNotifyingHost (newValue);
        fromInner.store (false);
    }
    void parameterGestureChanged (int, bool starting) override
    {
        if (forwarding.load()) return;
        if (starting) beginChangeGesture(); else endChangeGesture();
    }

    juce::RangedAudioParameter* inner = nullptr;
    std::atomic<bool> fromInner { false }, forwarding { false };
};
