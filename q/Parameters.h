#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EqBand.h"

namespace titq
{
constexpr int kNumBands = 10;

inline const juce::StringArray& typeNames()
{
    static const juce::StringArray n { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                       "Tilt Shelf", "Tilt Flat", "SL TIT", "BOOBTEC", "TIT SKY", "TIT 250", "AP TIT" };
    return n;
}

inline juce::String pid (int band, const char* name)
{
    return "b" + juce::String (band + 1) + "_" + name;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace titq
