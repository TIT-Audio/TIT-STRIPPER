// Prueba "sin ventana" de TIT STRIPPER con JUCE: parametros, cadena, sidechain externo, latencia, estado y sincronia con los modulos.
// Se compila a mano (ver README, apartado Desarrollo); no forma parte del flujo de GitHub.
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cstdio>
#include "../../strip/StripProcessor.h"

static int failures = 0;
static void check (bool ok, const char* what, double a = 0, double b = 0)
{
    if (! ok) { ++failures; std::printf ("FALLO: %s (%.4f vs %.4f)\n", what, a, b); }
}

static juce::AudioProcessorParameter* findParam (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* q : p.getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (q))
            if (w->paramID == id) return q;
    return nullptr;
}
static void setReal (juce::AudioProcessor& p, const juce::String& id, float v)
{
    auto* q = findParam (p, id);
    if (q == nullptr) { check (false, "parametro no encontrado"); std::printf ("   %s\n", id.toRawUTF8()); return; }
    if (auto* px = dynamic_cast<ProxyParam*> (q)) q->setValueNotifyingHost (px->getInner()->convertTo0to1 (v));
    else if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q)) q->setValueNotifyingHost (r->convertTo0to1 (v));
}
static float getReal (juce::AudioProcessor& p, const juce::String& id)
{
    auto* q = findParam (p, id);
    if (auto* px = dynamic_cast<ProxyParam*> (q)) return px->getInner()->convertFrom0to1 (q->getValue());
    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q)) return r->convertFrom0to1 (q->getValue());
    return -9999.f;
}

static double rmsDb (const juce::AudioBuffer<float>& b, int ch, int from)
{
    double s = 0; int n = 0;
    for (int i = from; i < b.getNumSamples(); ++i) { s += (double) b.getSample (ch, i) * b.getSample (ch, i); ++n; }
    return 10.0 * std::log10 (s / std::max (1, n) + 1e-20);
}

// pasa 'secs' segundos de una senal por el plugin; devuelve el ultimo bloque
static juce::AudioBuffer<float> run (StripProcessor& p, double fs, int bs, double secs, float mainDb, float scDb, bool sc, double hz = 1000.0, bool noise = false)
{
    juce::AudioBuffer<float> buf (sc ? 4 : 2, bs), last (2, bs);
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    double ph = 0;
    const float am = std::pow (10.f, mainDb / 20.f), as = std::pow (10.f, scDb / 20.f);
    const int blocks = (int) (secs * fs / bs);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < bs; ++i)
        {
            const float v = noise ? (rnd.nextFloat() * 2.f - 1.f) : (float) std::sin (ph);
            ph += 2.0 * juce::MathConstants<double>::pi * hz / fs;
            buf.setSample (0, i, am * v); buf.setSample (1, i, am * v);
            if (sc) { buf.setSample (2, i, as * v); buf.setSample (3, i, as * v); }
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) last.copyFrom (c, 0, buf, c, 0, bs);
        for (int c = 0; c < buf.getNumChannels(); ++c) for (int i = 0; i < bs; ++i) if (! std::isfinite (buf.getSample (c, i))) { check (false, "salida no finita"); return last; }
    }
    return last;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0; const int bs = 256;

    auto make = [&] (bool sc)
    {
        auto p = std::make_unique<StripProcessor>();
        auto l = p->getBusesLayout();
        l.getChannelSet (true, 1) = sc ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        check (p->setBusesLayout (l), "layout del strip");
        p->setRateAndBufferSizeDetails (fs, bs);
        p->prepareToPlay (fs, bs);
        return p;
    };

    // 1) parametros
    {
        auto p = make (false);
        const int n = p->getParameters().size();
        std::printf ("parametros expuestos: %d\n", n);
        check (n > 250, "numero de parametros", n, 250);
        juce::StringArray ids; bool dup = false;
        for (auto* q : p->getParameters()) if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (q)) { if (ids.contains (w->paramID)) dup = true; ids.add (w->paramID); }
        check (! dup, "ids de parametro unicos");
        check (findParam (*p, "c_thr") && findParam (*p, "g_thr") && findParam (*p, "e_b1_freq") && findParam (*p, "order"), "ids esperados");
    }

    // 2) EQ sin bandas = transparente; todo lo demas apagado
    {
        auto p = make (false);
        auto o = run (*p, fs, bs, 0.5, -10.f, -120.f, false);
        check (std::abs (rmsDb (o, 0, 0) - (-13.0)) < 0.15, "EQ vacio transparente", rmsDb (o, 0, 0), -13.0);
    }

    // 3) compresor: -6 dB con umbral -18 dB baja el nivel
    {
        auto p = make (false);
        setReal (*p, "comp_on", 1.f); setReal (*p, "eq_on", 0.f);
        setReal (*p, "c_thr", -18.f); setReal (*p, "c_ratio", 4.f);
        auto o = run (*p, fs, bs, 1.5, -6.f, -120.f, false);
        const double r = rmsDb (o, 0, 0), ref = -6.0 - 3.01;
        check (r < ref - 5.0, "el compresor atenua", r, ref);
        std::printf("   compresor: %.2f dB (sin comprimir %.2f)\n", r, ref);
    }

    // 4) gate interno: -50 dB cerrado, -10 dB abierto
    {
        auto p = make (false);
        setReal (*p, "gate_on", 1.f); setReal (*p, "eq_on", 0.f); setReal (*p, "g_thr", -30.f);
        auto lo = run (*p, fs, bs, 1.0, -50.f, -120.f, false);
        check (rmsDb (lo, 0, 0) < -50.0 - 3.01 - 40.0, "gate cerrado", rmsDb (lo, 0, 0), -90.0);
        auto hi = run (*p, fs, bs, 1.0, -10.f, -120.f, false);
        check (std::abs (rmsDb (hi, 0, 0) - (-13.01)) < 0.3, "gate abierto", rmsDb (hi, 0, 0), -13.01);
    }

    // 5) gate con sidechain externo: la senal principal (-10 dB) se abre solo si el sidechain pasa el umbral
    {
        auto p = make (true);
        setReal (*p, "gate_on", 1.f); setReal (*p, "eq_on", 0.f); setReal (*p, "g_thr", -30.f); setReal (*p, "g_scsrc", 1.f);
        auto closed = run (*p, fs, bs, 1.0, -10.f, -60.f, true);
        check (rmsDb (closed, 0, 0) < -13.0 - 40.0, "sidechain externo flojo mantiene cerrado", rmsDb (closed, 0, 0), -60.0);
        auto open = run (*p, fs, bs, 1.0, -10.f, -10.f, true);
        check (std::abs (rmsDb (open, 0, 0) - (-13.01)) < 0.3, "sidechain externo fuerte abre", rmsDb (open, 0, 0), -13.01);
    }

    // 6) latencia = suma de los modulos activos
    {
        auto p = make (false);
        setReal (*p, "gate_on", 1.f); setReal (*p, "g_look", 5.f);
        run (*p, fs, bs, 0.2, -10.f, -120.f, false);
        check (p->getLatencySamples() == 240, "latencia del gate (5 ms)", p->getLatencySamples(), 240);
        setReal (*p, "gate_on", 0.f);
        run (*p, fs, bs, 0.2, -10.f, -120.f, false);
        check (p->getLatencySamples() == 0, "latencia vuelve a 0", p->getLatencySamples(), 0);
    }

    // 7) sincronia parametros <-> modulos
    {
        auto p = make (false);
        setReal (*p, "c_thr", -33.f);
        { auto& c0 = static_cast<TitCompProcessor&> (p->module (StripProcessor::Comp)); const float v = c0.apvts.getRawParameterValue ("thr")->load(); check (std::abs (v + 33.f) < 0.2f, "DAW -> modulo", v, -33.0); }
        auto& comp = static_cast<TitCompProcessor&> (p->module (StripProcessor::Comp));
        if (auto* q = comp.apvts.getParameter ("ratio")) q->setValueNotifyingHost (q->convertTo0to1 (7.f));
        check (std::abs (getReal (*p, "c_ratio") - 7.f) < 0.2f, "modulo -> DAW", getReal (*p, "c_ratio"), 7.0);
    }

    // 8) estado: ida y vuelta + orden
    {
        auto a = make (false);
        setReal (*a, "c_thr", -27.f); setReal (*a, "g_hold", 123.f); setReal (*a, "e_b3_used", 1.f); setReal (*a, "e_b3_gain", 4.5f); setReal (*a, "gate_on", 1.f);
        a->setOrder ({ 2, 0, 1 });
        juce::MemoryBlock mb; a->getStateInformation (mb);
        auto b = make (false);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (getReal (*b, "c_thr") + 27.f) < 0.2f, "estado: c_thr", getReal (*b, "c_thr"), -27.0);
        check (std::abs (getReal (*b, "g_hold") - 123.f) < 1.0f, "estado: g_hold", getReal (*b, "g_hold"), 123.0);
        check (std::abs (getReal (*b, "e_b3_gain") - 4.5f) < 0.05f, "estado: banda 3", getReal (*b, "e_b3_gain"), 4.5);
        check (b->isModuleOn (StripProcessor::Gate), "estado: gate on");
        const auto o = b->currentOrder();
        check (o[0] == 2 && o[1] == 0 && o[2] == 1, "estado: orden", o[0], 2);
        // y los modulos internos reciben el estado
        check (std::abs (static_cast<TitCompProcessor&> (b->module (StripProcessor::Comp)).apvts.getRawParameterValue ("thr")->load() + 27.f) < 0.2f, "estado en el modulo");
    }

    // 9) orden intercambiable: con EQ de +12 dB antes o despues del compresor el resultado cambia
    {
        auto run1 = [&] (std::array<int, 3> ord)
        {
            auto p = make (false);
            setReal (*p, "comp_on", 1.f); setReal (*p, "eq_on", 1.f);
            setReal (*p, "c_thr", -20.f); setReal (*p, "c_ratio", 8.f);
            setReal (*p, "e_b1_used", 1.f); setReal (*p, "e_b1_freq", 1000.f); setReal (*p, "e_b1_gain", 12.f);
            p->setOrder (ord);
            return rmsDb (run (*p, fs, bs, 1.5, -24.f, -120.f, false), 0, 0);
        };
        const double eqFirst = run1 ({ 2, 1, 0 }), compFirst = run1 ({ 1, 2, 0 });
        std::printf ("   EQ antes: %.2f dB, EQ despues: %.2f dB\n", eqFirst, compFirst);
        check (std::abs (eqFirst - compFirst) > 2.0, "el orden cambia el resultado", eqFirst, compFirst);
    }

    // 10) mono
    {
        auto p = std::make_unique<StripProcessor>();
        auto l = p->getBusesLayout();
        l.getChannelSet (true, 0) = juce::AudioChannelSet::mono(); l.getChannelSet (false, 0) = juce::AudioChannelSet::mono();
        check (p->setBusesLayout (l), "layout mono");
        p->setRateAndBufferSizeDetails (fs, bs); p->prepareToPlay (fs, bs);
        setReal (*p, "comp_on", 1.f); setReal (*p, "gate_on", 1.f);
        juce::AudioBuffer<float> buf (1, bs); juce::MidiBuffer midi;
        for (int b = 0; b < 100; ++b) { for (int i = 0; i < bs; ++i) buf.setSample (0, i, 0.3f * std::sin ((float) (b * bs + i) * 0.13f)); p->processBlock (buf, midi); }
        check (std::isfinite (buf.getSample (0, 10)), "mono finito");
    }

    // 11) coste de CPU con los tres modulos activos
    {
        auto p = make (true);
        setReal (*p, "gate_on", 1.f); setReal (*p, "comp_on", 1.f); setReal (*p, "eq_on", 1.f); setReal (*p, "g_thr", -60.f);
        for (int b = 1; b <= 6; ++b) { setReal (*p, "e_b" + juce::String (b) + "_used", 1.f); setReal (*p, "e_b" + juce::String (b) + "_gain", 3.f); setReal (*p, "e_b" + juce::String (b) + "_freq", 100.f * (float) b * b); }
        juce::AudioBuffer<float> buf (4, bs); juce::MidiBuffer midi; juce::Random rnd (3);
        const auto t0 = std::chrono::steady_clock::now();
        const int blocks = (int) (fs * 4 / bs);
        for (int b = 0; b < blocks; ++b) { for (int c = 0; c < 4; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, 0.2f * (rnd.nextFloat() * 2.f - 1.f)); p->processBlock (buf, midi); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("   CPU (sin optimizar, 4 s de audio): %.1f %% de tiempo real\n", 100.0 * sec / 4.0);
    }

    if (failures == 0) std::printf ("Todas las pruebas del strip han pasado.\n");
    return failures != 0;
}
