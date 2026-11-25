#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EqBand.h"
#include "CompCore.h"

namespace titc
{
constexpr int kNumBands = 10;   // bandas del EQ del sidechain

// curvas disponibles en el EQ del sidechain (coinciden con los primeros valores de titq::BandType)
inline const juce::StringArray& typeNames()
{
    static const juce::StringArray n { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Tilt Shelf" };
    return n;
}
inline const juce::StringArray& modeNames()
{
    static const juce::StringArray n { "Clean", "VCA TIT", "FET TIT", "FET TIT B", "OP TIT", "FAIR TIT", "TIT MU", "DXX TIT", "TIT DIGITAL" };
    return n;
}
inline juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace titc
