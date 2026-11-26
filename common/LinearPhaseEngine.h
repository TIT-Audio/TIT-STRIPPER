#pragma once
// Motor de fase lineal para el plugin: reparte el trabajo entre el hilo de audio (convolución, barata) y un hilo
// de diseño (calcular el filtro nuevo cuando cambia algo). El audio nunca espera al hilo de diseño: cuando el
// filtro nuevo está listo se cambia al empezar un bloque, con un fundido de un bloque entre el viejo y el nuevo.
// En un render fuera de tiempo real el filtro se diseña en el propio hilo de audio, para que el resultado
// no dependa de la velocidad del equipo.
#include <juce_core/juce_core.h>
#include "LinearPhaseCore.h"

namespace titq
{
class LinearPhaseEngine : private juce::Thread
{
public:
    LinearPhaseEngine() : juce::Thread ("TIT linear phase") {}
    ~LinearPhaseEngine() override { stopThread (3000); }

    // Reserva todo para esta frecuencia de muestreo (hilo de mensajes / prepareToPlay, con el audio parado).
    void prepare (double sampleRate)
    {
        stopThread (3000);
        fs = sampleRate;
        B = lpBlockSize (fs);
        asyncDesigner.prepare (fs);
        syncDesigner.prepare (fs);
        conv.prepare (fs);
        for (auto& s : sets) s.allocate (B);
        for (int c = 0; c < 2; ++c)
        {
            inBuf[(size_t) c].assign ((size_t) B, 0.f);
            outBuf[(size_t) c].assign ((size_t) B, 0.f);
        }
        pos = 0;
        minInterval = 1024 * lpScale (fs);
        busy = 0; cur = prev = ready = -1;
        quality.store (-1);
        setQuality (1);
        startThread (Priority::normal);
    }

    // Cambia la calidad (longitud del filtro). Empieza con un filtro "identidad" (solo retrasa) hasta que llega el diseño real.
    void setQuality (int q)
    {
        q = juce::jlimit (0, 3, q);
        if (q == quality.load()) return;
        const juce::SpinLock::ScopedLockType sl (setLock);
        quality.store (q);
        generation.fetch_add (1);
        const int idx = freeSetLocked (false);
        sets[(size_t) idx].generation = generation.load();
        syncDesigner.makeIdentity (sets[(size_t) idx], q);
        cur = idx; prev = -1; ready = -1;
        conv.configure (lpPartitions (q));
        havePosted = false;
        elapsed = 1 << 30;
    }

    int getQuality() const noexcept { return quality.load(); }
    int latencySamples() const noexcept { return lpLatency (std::max (0, quality.load()), fs); }

    void reset()
    {
        conv.reset();
        for (int c = 0; c < 2; ++c) { std::fill (inBuf[(size_t) c].begin(), inBuf[(size_t) c].end(), 0.f); std::fill (outBuf[(size_t) c].begin(), outBuf[(size_t) c].end(), 0.f); }
        pos = 0;
        havePosted = false;
        elapsed = 1 << 30;
    }

    // Una vez por bloque de audio: manda el estado actual de las bandas. Solo se pide un filtro nuevo si algo cambió
    // y ha pasado un tiempo mínimo desde el último.
    void update (const LpSnapshot& s, int numSamples, bool nonRealtime)
    {
        elapsed = std::min (elapsed + numSamples, 1 << 30);
        if (havePosted && s == lastPosted) return;
        if (! nonRealtime && havePosted && elapsed < minInterval) return;
        lastPosted = s; havePosted = true; elapsed = 0;

        if (nonRealtime)
        {
            const int idx = acquireFree();
            if (idx < 0) return;
            const int q = quality.load(), gen = generation.load();
            syncDesigner.design (s, q, sets[(size_t) idx]);
            sets[(size_t) idx].generation = gen;
            publish (idx, gen);
        }
        else
        {
            {
                const juce::SpinLock::ScopedLockType sl (jobLock);
                job.snap = s; job.quality = quality.load(); job.gen = generation.load(); hasJob = true;
            }
            wake.signal();
        }
    }

    // Procesa in situ (l y r, o solo l si es mono). Admite cualquier tamaño de bloque; añade B muestras de latencia de búfer.
    void process (float* l, float* r, int nch, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float oL = outBuf[0][(size_t) pos];
            inBuf[0][(size_t) pos] = l[i];
            l[i] = oL;
            if (nch > 1)
            {
                const float oR = outBuf[1][(size_t) pos];
                inBuf[1][(size_t) pos] = r[i];
                r[i] = oR;
            }
            if (++pos == B) { pos = 0; runBlock (nch); }
        }
    }

private:
    struct Job { LpSnapshot snap; int quality = 1; int gen = 0; };

    void runBlock (int nch)
    {
        int fadeFrom = -1;
        {
            const juce::SpinLock::ScopedTryLockType sl (setLock);
            if (sl.isLocked())
            {
                prev = -1;                                   // el fundido del bloque anterior ya terminó
                if (ready >= 0) { fadeFrom = cur; prev = cur; cur = ready; ready = -1; }
            }
        }
        if (cur < 0) return;
        conv.processBlock (inBuf[0].data(), inBuf[1].data(), outBuf[0].data(), outBuf[1].data(), nch,
                           sets[(size_t) cur], fadeFrom >= 0 ? &sets[(size_t) fadeFrom] : nullptr);
    }

    // busca un juego de filtros libre (ni en uso, ni en el fundido, ni a medio escribir); el "listo" pendiente se puede reutilizar
    int freeSetLocked (bool markBusy)
    {
        int pick = -1;
        for (int i = 0; i < kSets; ++i)
        {
            if (i == cur || i == prev || (busy & (1 << i))) continue;
            if (i == ready) { if (pick < 0) pick = i; continue; }
            pick = i; break;
        }
        if (pick < 0) return markBusy ? -1 : 0;
        if (pick == ready) ready = -1;
        if (markBusy) busy |= 1 << pick;
        return pick;
    }

    int acquireFree()
    {
        const juce::SpinLock::ScopedLockType sl (setLock);
        return freeSetLocked (true);
    }

    void publish (int idx, int gen)
    {
        const juce::SpinLock::ScopedLockType sl (setLock);
        busy &= ~(1 << idx);
        if (gen == generation.load()) ready = idx;           // si la calidad cambió mientras tanto, este filtro ya no vale
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wake.wait (200);
            if (threadShouldExit()) break;

            Job j;
            {
                const juce::SpinLock::ScopedLockType sl (jobLock);
                if (! hasJob) continue;
                j = job; hasJob = false;
            }
            const int idx = acquireFree();
            if (idx < 0) continue;
            asyncDesigner.design (j.snap, j.quality, sets[(size_t) idx]);
            sets[(size_t) idx].generation = j.gen;
            publish (idx, j.gen);
        }
    }

    static constexpr int kSets = 5;
    double fs = 48000.0;
    int B = 256;

    LpDesigner asyncDesigner, syncDesigner;
    LpConvolver conv;
    std::array<LpKernelSet, kSets> sets;
    juce::SpinLock setLock, jobLock;
    int cur = -1, prev = -1, ready = -1, busy = 0;           // se tocan siempre con setLock (el audio solo lee cur y prev sin él)
    std::atomic<int> quality { -1 }, generation { 0 };

    Job job; bool hasJob = false;
    juce::WaitableEvent wake;

    std::array<std::vector<float>, 2> inBuf, outBuf;
    int pos = 0;

    LpSnapshot lastPosted; bool havePosted = false;
    int elapsed = 1 << 30, minInterval = 1024;
};
} // namespace titq
