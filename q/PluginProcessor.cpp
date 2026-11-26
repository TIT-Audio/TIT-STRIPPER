#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace titq;

TitQProcessor::TitQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "TITQ", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto get = [&] (const char* n) { return apvts.getRawParameterValue (pid (b, n)); };
        auto& p = ptrs[(size_t) b];
        p.used = get ("used"); p.on = get ("on");       p.type = get ("type");   p.freq = get ("freq");   p.gain = get ("gain");
        p.q = get ("q");         p.slope = get ("slope"); p.boost = get ("boost"); p.atten = get ("atten");
        p.solo = get ("solo");   p.dyn = get ("dyn");     p.dyndir = get ("dyndir"); p.range = get ("range");
        p.thr = get ("thr");     p.ratio = get ("ratio");
        p.attack = get ("attack"); p.release = get ("release"); p.knee = get ("knee");
        p.scsrc = get ("scsrc"); p.scfree = get ("scfree"); p.scfreq = get ("scfreq"); p.scq = get ("scq"); p.chmode = get ("chmode");
    }
    for (auto& a : dynAmount) a.store (0.f);
    osParam  = apvts.getRawParameterValue ("os");
    outParam = apvts.getRawParameterValue ("out");
    phaseParam = apvts.getRawParameterValue ("phase");
    lpqParam   = apvts.getRawParameterValue ("lpq");

    // preset de clics marcado como Default (si algo falla, valores de fábrica)
    clickConfig = ClickConfig::loadDefault (clickPresetName);
}

BandSettings TitQProcessor::readBandSettings (int b) const
{
    const auto& p = ptrs[(size_t) b];
    BandSettings s;
    s.on = p.used->load() > 0.5f && p.on->load() > 0.5f;   // una banda no creada nunca procesa
    s.type = juce::jlimit (0, (int) NumBandTypes - 1, (int) std::lround (p.type->load()));
    s.freq = p.freq->load();  s.gain = p.gain->load();   s.q = p.q->load();
    s.slope = p.slope->load(); s.boost = p.boost->load(); s.atten = p.atten->load();
    return s;
}

bool TitQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (l.getMainInputChannelSet() != out) return false;
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

void TitQProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversamplers[i]->initProcessing ((size_t) kChunk);
    }

    dryChunk.setSize (2, kChunk);
    soloFilter.reset();
    soloF = soloQ = -1.0;
    lastOutGain = juce::Decibels::decibelsToGain (outParam->load());

    for (int i = 0; i < kNumBands; ++i)
    {
        auto& b = bands[(size_t) i];
        b.eq.reset(); b.detector.reset(); b.lastDyn = -1.0; b.detF = -1.0; b.amount = 0.f; b.ms = 0.0;
        b.fade = b.fadeStart = b.fadeEnd = readBandSettings (i).on ? 1.f : 0.f;   // sin fundido al cargar una sesión
        b.sFreq = -1.0; b.sType = -1;
    }
    linearActive = phaseParam->load() > 0.5f;
    setOversampling ((int) std::lround (osParam->load()), ! linearActive);

    // fase lineal: el motor reserva su memoria aquí; en modo Linear la latencia es la de su filtro
    lpEngine.prepare (sampleRate);
    for (auto& v : soloRing) v.assign ((size_t) lpLatency (3, sampleRate) + kSoloChunk + 8, 0.f);
    soloWrite = 0;
    if (linearActive) enterLinear (juce::jlimit (0, 3, (int) std::lround (lpqParam->load())));
    else tailSeconds.store (0.0);
}

void TitQProcessor::enterLinear (int quality)
{
    lpEngine.setQuality (quality);
    const int lat = lpEngine.latencySamples();
    setLatencySamples (lat);
    tailSeconds.store ((double) lat / std::max (1.0, currentSampleRate.load()));
}

void TitQProcessor::setOversampling (int idx, bool updateLatency)
{
    idx = juce::jlimit (0, 3, idx);
    currentOs = idx;
    osFactor = 1 << idx;

    if (idx > 0) oversamplers[(size_t) idx - 1]->reset();
    if (updateLatency)      // en fase lineal la latencia es la del filtro lineal: no se toca aquí (si no, cada prepareToPlay la cambiaría dos veces y el DAW se reinicia en bucle)
        setLatencySamples (idx > 0 ? juce::roundToInt (oversamplers[(size_t) idx - 1]->getLatencyInSamples()) : 0);

    for (auto& b : bands) { b.eq.reset(); b.lastDyn = -1.0; } // fuerza rediseño con la nueva frecuencia de muestreo
}

void TitQProcessor::updateBands (const float* intL, const float* intR, const float* extL, const float* extR, int n)
{
    const double fs = currentSampleRate.load();
    const double osFs = fs * osFactor;
    const double chunkSec = (double) n / fs;

    // señales de detección en mono: interna (la entrada) y externa (la única entrada de sidechain, si está conectada)
    float monoInt[kChunk], sideInt[kChunk], monoExt[kChunk];
    for (int i = 0; i < n; ++i) { monoInt[i] = 0.5f * (intL[i] + intR[i]); sideInt[i] = 0.5f * (intL[i] - intR[i]); }
    const bool haveExt = (extL != nullptr && extR != nullptr);
    if (haveExt)
        for (int i = 0; i < n; ++i) monoExt[i] = 0.5f * (extL[i] + extR[i]);

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& rt = bands[(size_t) b];
        auto& p = ptrs[(size_t) b];
        const BandSettings s = readBandSettings (b);
        rt.chmode = juce::jlimit (0, 4, (int) std::lround (p.chmode->load()));

        // fundido de ~8 ms al crear / encender / apagar / borrar una banda (evita clics)
        rt.fadeStart = rt.fade;
        rt.fade = s.on ? (float) std::min (1.0, (double) rt.fade + chunkSec / 0.008)
                       : (float) std::max (0.0, (double) rt.fade - chunkSec / 0.008);
        rt.fadeEnd = rt.fade;

        const bool dynOn = s.on && p.dyn->load() > 0.5f && s.type != LowCut && s.type != HighCut;
        const double rangeDb = (double) p.range->load();

        if (dynOn)
        {
            // ---- detector ----
            // frecuencia de escucha: la de la banda, o libre (desacoplada) si "Free SC" está activado
            double f, q;
            if (p.scfree->load() > 0.5f)
            {
                f = juce::jlimit (20.0, 0.49 * fs, (double) p.scfreq->load());
                q = juce::jlimit (0.1, 20.0, (double) p.scq->load());
            }
            else
            {
                f = juce::jlimit (20.0, 0.49 * fs, (double) s.freq);
                if (s.type == APTIT)
                    q = juce::jlimit (0.3, 10.0, apTitQ ((double) s.gain));
                else
                    q = (s.type == Bell || s.type == SLTIT || s.type == TIT250) ? juce::jlimit (0.3, 10.0, (double) s.q) : 0.7;
            }
            if (f != rt.detF || q != rt.detQ) { rt.detector.set (Biquad::bandpass (fs, f, q)); rt.detF = f; rt.detQ = q; }

            // fuente: Internal o External (si no hay entrada externa conectada, usa la interna)
            const bool useExt = haveExt && p.scsrc->load() > 0.5f;
            // la banda escucha la parte de la senal en la que actua (Mid, Side, L o R); el sidechain externo es siempre mono
            const float* internalSrc = rt.chmode == 2 ? sideInt : (rt.chmode == 3 ? intL : (rt.chmode == 4 ? intR : monoInt));
            const float* src = useExt ? monoExt : internalSrc;
            float tmp[kChunk];
            std::copy (src, src + n, tmp);
            rt.detector.process (tmp, n, 0);

            // RMS suavizado (ventana ~3 ciclos de la frecuencia de escucha, entre 5 y 50 ms)
            const double tau = juce::jlimit (0.005, 0.05, 3.0 / f);
            const double k = 1.0 - std::exp (-1.0 / (fs * tau));
            for (int i = 0; i < n; ++i) rt.ms += k * ((double) tmp[i] * tmp[i] - rt.ms);
            const double levelDb = 10.0 * std::log10 (rt.ms + 1.0e-12);

            // ---- compresor / expansor con soft knee ----
            // Above: actúa cuando el nivel SUPERA el umbral (estándar).
            // Below: actúa cuando el nivel CAE POR DEBAJO del umbral (modo upward).
            // Con el signo de Range salen las cuatro combinaciones: compresión, expansión, compresión upward y expansión upward.
            const bool below = p.dyndir->load() > 0.5f;
            const double thr = p.thr->load(), ratio = std::max (1.0f, p.ratio->load()), knee = p.knee->load();
            const double over = below ? (thr - levelDb) : (levelDb - thr);
            double gr = 0.0;
            if (knee > 0.01 && 2.0 * std::abs (over) <= knee)
            {
                const double x = over + knee * 0.5;
                gr = (1.0 - 1.0 / ratio) * x * x / (2.0 * knee);
            }
            else if (over > 0.0)
                gr = (1.0 - 1.0 / ratio) * over;

            // la banda llega a su Range completo cuando la reducción alcanza |Range| dB
            const double span = (s.type == BOOBTEC) ? std::max (s.boost, s.atten) * kBoobtecDbPerStep
                                                   : std::abs (rangeDb);
            const double target = span > 0.01 ? juce::jlimit (0.0, 1.0, gr / span) : 0.0;

            const double aT = std::max (0.1, (double) p.attack->load()) * 0.001;
            const double rT = std::max (1.0, (double) p.release->load()) * 0.001;
            const double coef = std::exp (-chunkSec / (target > rt.amount ? aT : rT));
            rt.amount = (float) (coef * rt.amount + (1.0 - coef) * target);
        }
        else
        {
            rt.amount = 0.f;
            rt.ms = 0.0;
        }

        dynAmount[(size_t) b].store (rt.amount, std::memory_order_relaxed);

        // filtros de corte: la frecuencia (en escala logarítmica) y la pendiente se acercan al valor pedido en ~20 ms,
        // así mover el corte o cambiar el slope no produce clics
        BandSettings sm = s;
        if (s.type == LowCut || s.type == HighCut)
        {
            if (rt.sType != s.type || rt.sFreq < 0.0)
            {
                rt.sFreq = (double) s.freq;
                rt.sSlope = (double) s.slope;
            }
            else
            {
                const double a = 1.0 - std::exp (-chunkSec / 0.02);
                rt.sFreq = std::exp (std::log (rt.sFreq) + a * (std::log ((double) s.freq) - std::log (rt.sFreq)));
                rt.sSlope += a * ((double) s.slope - rt.sSlope);
                if (std::abs (std::log (rt.sFreq / (double) s.freq)) < 1.0e-4) rt.sFreq = (double) s.freq;
                if (std::abs (rt.sSlope - (double) s.slope) < 0.01) rt.sSlope = (double) s.slope;
            }
            sm.freq = (float) rt.sFreq;
            sm.slope = (float) rt.sSlope;
        }
        else
            rt.sFreq = -1.0;
        rt.sType = s.type;

        BandSettings eff;
        const double scale = applyDynamics (sm, dynOn, rangeDb, (double) rt.amount, eff);
        rt.effNow = eff; rt.scaleNow = scale;
        if (! linearActive && (rt.lastDyn < 0.0 || eff != rt.last || scale != rt.lastDyn))
        {
            rt.eq.design (eff, osFs, scale, n * osFactor);   // rampa de coeficientes muestra a muestra durante el trozo
            rt.last = eff;
            rt.lastDyn = scale;
        }
    }
}

void TitQProcessor::processBands (juce::dsp::AudioBlock<float>& block)
{
    // Todas las bandas se encadenan en 64 bits (double) y solo se redondea a float al final del trozo: sin redondeos intermedios entre bandas
    const int n = (int) block.getNumSamples();
    const int numCh = (int) block.getNumChannels();
    double buf[2][kChunk * 8];
    double dry[kChunk * 8];                      // un trozo como maximo (32 muestras x oversampling x8)
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = block.getChannelPointer ((size_t) ch);
        for (int i = 0; i < n; ++i) buf[ch][i] = (double) src[i];
    }

    for (auto& b : bands)
    {
        if (b.fadeStart <= 0.f && b.fadeEnd <= 0.f)   // banda no creada / apagada del todo: no se procesa
        {
            b.eq.reset();
            continue;
        }

        const bool steady = (b.fadeStart >= 1.f && b.fadeEnd >= 1.f);

        // aplica el filtro de la banda a un trozo de senal (con fundido si la banda se esta encendiendo o apagando)
        auto run = [&] (double* data, int stateCh)
        {
            if (steady) { b.eq.process (data, n, stateCh); return; }
            std::copy (data, data + n, dry);
            b.eq.process (data, n, stateCh);
            for (int i = 0; i < n; ++i)
            {
                const double w = (double) b.fadeStart + ((double) b.fadeEnd - (double) b.fadeStart) * (double) (i + 1) / (double) n;
                data[i] = dry[i] + w * (data[i] - dry[i]);
            }
        };

        const int mode = numCh == 2 ? b.chmode : 0;
        if (mode == 0)                                   // Stereo: L y R
        {
            for (int ch = 0; ch < numCh; ++ch) run (buf[ch], ch);
        }
        else if (mode == 3 || mode == 4)                 // solo Left o solo Right
        {
            run (buf[mode - 3], mode - 3);
        }
        else                                             // Mid o Side: se codifica, se filtra una sola parte y se decodifica
        {
            double m[kChunk * 8], sd[kChunk * 8];
            for (int i = 0; i < n; ++i) { m[i] = 0.5 * (buf[0][i] + buf[1][i]); sd[i] = 0.5 * (buf[0][i] - buf[1][i]); }
            if (mode == 1) run (m, 0); else run (sd, 1);
            for (int i = 0; i < n; ++i) { buf[0][i] = m[i] + sd[i]; buf[1][i] = m[i] - sd[i]; }
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* dst = block.getChannelPointer ((size_t) ch);
        for (int i = 0; i < n; ++i) dst[i] = (float) buf[ch][i];
    }
}

void TitQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int numCh = std::min (main.getNumChannels(), 2);
    const int total = main.getNumSamples();
    if (numCh == 0 || total == 0) return;

    const int osIdx = juce::jlimit (0, 3, (int) std::lround (osParam->load()));
    const int lpq = juce::jlimit (0, 3, (int) std::lround (lpqParam->load()));
    if (phaseParam->load() > 0.5f)
    {
        if (! linearActive)
        {
            linearActive = true;
            lpEngine.reset();
            for (auto& v : soloRing) std::fill (v.begin(), v.end(), 0.f);
            enterLinear (lpq);
        }
        else if (lpq != lpEngine.getQuality())
            enterLinear (lpq);
    }
    else if (linearActive)
    {
        linearActive = false;
        tailSeconds.store (0.0);
        setOversampling (osIdx);                    // vuelve a los filtros normales (y a su latencia)
    }
    else if (osIdx != currentOs)
        setOversampling (osIdx);

    // sidechain externo: una sola entrada por instancia, compartida por todas las bandas que la elijan
    auto* scBus = getBus (true, 1);
    const bool extAvailable = scBus != nullptr && scBus->isEnabled();
    extScActive.store (extAvailable, std::memory_order_relaxed);
    juce::AudioBuffer<float> sc;
    if (extAvailable) sc = getBusBuffer (buffer, true, 1);

    // analizador: señal de entrada (pre)
    analyzerPre.push (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0), total);

    // solo (la banda con índice menor que esté en solo)
    int soloBand = -1;
    for (int b = 0; b < kNumBands; ++b)
        if (ptrs[(size_t) b].solo->load() > 0.5f && ptrs[(size_t) b].used->load() > 0.5f && ptrs[(size_t) b].on->load() > 0.5f) { soloBand = b; break; }

    const double fs = currentSampleRate.load();
    const float newOutGain = juce::Decibels::decibelsToGain (outParam->load());

    // filtro de paso de banda del solo (frecuencia y Q de la banda en solo)
    auto prepareSolo = [&]
    {
        const auto s = readBandSettings (soloBand);
        const double f = juce::jlimit (20.0, 0.49 * fs, (double) s.freq);
        double q = 0.5;
        if (s.type == APTIT)
            q = juce::jlimit (0.3, 10.0, apTitQ ((double) s.gain));
        else if (s.type == Bell || s.type == SLTIT || s.type == TIT250)
            q = juce::jlimit (0.3, 10.0, (double) s.q);
        if (f != soloF || q != soloQ) { soloFilter.set (Biquad::bandpass (fs, f, q)); soloF = f; soloQ = q; }
    };

    if (linearActive)
    {
        // 1) dinámica y detectores sobre la entrada, trozo a trozo (la señal aún no se ha tocado)
        for (int pos = 0; pos < total; pos += kChunk)
        {
            const int n = std::min (kChunk, total - pos);
            const float* extL = nullptr; const float* extR = nullptr;
            if (extAvailable && sc.getNumChannels() > 0)
            {
                extL = sc.getReadPointer (0) + pos;
                extR = (sc.getNumChannels() > 1 ? sc.getReadPointer (1) : sc.getReadPointer (0)) + pos;
            }
            updateBands (main.getReadPointer (0) + pos, (numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0)) + pos, extL, extR, n);
        }

        // 2) estado de las bandas -> filtro nuevo (se diseña en otro hilo y entra con un fundido)
        LpSnapshot snap;
        snap.mixed = phaseParam->load() > 1.5f;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& rt = bands[(size_t) b];
            if (rt.fade <= 0.f) continue;
            auto& e = snap.band[(size_t) b];
            e.active = true; e.eff = rt.effNow; e.scale = rt.scaleNow; e.fade = rt.fade; e.chmode = numCh == 2 ? rt.chmode : 0;
        }
        lpEngine.update (snap, total, isNonRealtime());

        // 3) convolución (en trozos para el solo, que necesita la entrada retrasada)
        const int delay = lpEngine.latencySamples();
        const int ring = (int) soloRing[0].size();
        float* wl = main.getWritePointer (0);
        float* wr = numCh > 1 ? main.getWritePointer (1) : nullptr;
        if (soloBand >= 0) prepareSolo();

        for (int off = 0; off < total; off += kSoloChunk)
        {
            const int n = std::min (kSoloChunk, total - off);
            for (int i = 0; i < n; ++i)
            {
                const int w = (soloWrite + i) % ring;
                soloRing[0][(size_t) w] = wl[off + i];
                if (numCh > 1) soloRing[1][(size_t) w] = wr[off + i];
            }
            lpEngine.process (wl + off, wr != nullptr ? wr + off : nullptr, numCh, n);
            if (soloBand >= 0)
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float* d = main.getWritePointer (ch) + off;
                    for (int i = 0; i < n; ++i)
                        d[i] = soloRing[(size_t) ch][(size_t) (((soloWrite + i - delay) % ring + ring) % ring)];
                    soloFilter.process (d, n, ch);
                }
            soloWrite = (soloWrite + n) % ring;
        }

        main.applyGainRamp (0, total, lastOutGain, newOutGain);
        lastOutGain = newOutGain;
        analyzerPost.push (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0), total);
        return;
    }

    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (kChunk, total - pos);

        const float* extL = nullptr; const float* extR = nullptr;
        if (extAvailable && sc.getNumChannels() > 0)
        {
            extL = sc.getReadPointer (0) + pos;
            extR = (sc.getNumChannels() > 1 ? sc.getReadPointer (1) : sc.getReadPointer (0)) + pos;
        }
        const float* intL = main.getReadPointer (0) + pos;
        const float* intR = (numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0)) + pos;

        updateBands (intL, intR, extL, extR, n);

        if (soloBand >= 0)
            for (int ch = 0; ch < numCh; ++ch)
                dryChunk.copyFrom (ch, 0, main, ch, pos, n);

        juce::dsp::AudioBlock<float> block (main.getArrayOfWritePointers(), (size_t) numCh, (size_t) pos, (size_t) n);

        if (osIdx == 0)
            processBands (block);
        else
        {
            auto& os = *oversamplers[(size_t) osIdx - 1];
            auto up = os.processSamplesUp (block);
            processBands (up);
            os.processSamplesDown (block);
        }

        if (soloBand >= 0)
        {
            prepareSolo();
            for (int ch = 0; ch < numCh; ++ch)
            {
                main.copyFrom (ch, pos, dryChunk, ch, 0, n);
                soloFilter.process (main.getWritePointer (ch) + pos, n, ch);
            }
        }

        // ganancia de salida con rampa
        const float g0 = lastOutGain + (newOutGain - lastOutGain) * (float) pos / (float) total;
        const float g1 = lastOutGain + (newOutGain - lastOutGain) * (float) (pos + n) / (float) total;
        main.applyGainRamp (pos, n, g0, g1);

        pos += n;
    }
    lastOutGain = newOutGain;

    // analizador: señal de salida (post)
    analyzerPost.push (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : main.getReadPointer (0), total);
}

juce::AudioProcessorEditor* TitQProcessor::createEditor() { return new TitQEditor (*this); }

void TitQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("titVersion", 2, nullptr);     // version del formato del estado (para que los presets antiguos sigan abriendo)
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TitQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

