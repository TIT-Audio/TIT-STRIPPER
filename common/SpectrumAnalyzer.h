#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>

// Analizador FFT: el hilo de audio escribe muestras en un anillo; la interfaz calcula la FFT.
class SpectrumAnalyzer
{
public:
    static constexpr int kOrder = 12;
    static constexpr int kSize  = 1 << kOrder;   // 4096 puntos
    static constexpr int kBins  = kSize / 2;

    SpectrumAnalyzer()
        : fft (kOrder),
          window ((size_t) kSize, juce::dsp::WindowingFunction<float>::hann, false)
    {
        ring.fill (0.f);
        scratch.fill (0.f);
    }

    // hilo de audio: mezcla a mono y guarda
    void push (const float* l, const float* r, int n) noexcept
    {
        int pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) pos] = 0.5f * (l[i] + r[i]);
            pos = (pos + 1) & (kSize - 1);
        }
        writePos.store (pos, std::memory_order_release);
    }

    // hilo de la interfaz: magnitudes en dB (0 dB = seno a fondo de escala)
    void computeDb (std::array<float, kBins>& out)
    {
        const int pos = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < kSize; ++i)
            scratch[(size_t) i] = ring[(size_t) ((pos + i) & (kSize - 1))];
        std::fill (scratch.begin() + kSize, scratch.end(), 0.f);

        window.multiplyWithWindowingTable (scratch.data(), (size_t) kSize);
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        for (int i = 0; i < kBins; ++i)
            out[(size_t) i] = 20.0f * std::log10 (scratch[(size_t) i] * 4.0f / (float) kSize + 1.0e-9f);
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, kSize> ring;
    std::array<float, 2 * kSize> scratch;
    std::atomic<int> writePos { 0 };
};
