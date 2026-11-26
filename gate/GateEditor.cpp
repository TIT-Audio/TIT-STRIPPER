#include "GateEditor.h"
#include <BinaryData.h>

static const juce::Colour kPink  (0xffff2e9a);
static const juce::Colour kPink2 (0xffff7ac0);
static const char* kIds[]   = { "thr", "range", "ratio", "attack", "hold", "release", "hyst", "look", "link", "schps", "sclps" };
static const char* kNames[] = { "Threshold", "Range", "Ratio", "Attack", "Hold", "Release", "Hysteresis", "Lookahead", "Stereo Link", "SC HP Slope", "SC LP Slope" };

void GateEditor::Laf::drawLabel (juce::Graphics& g, juce::Label& l)
{
    g.fillAll (l.findColour (juce::Label::backgroundColourId).withAlpha (0.f));
    if (l.isBeingEdited()) return;
    const auto b = l.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff10040b).withAlpha (0.30f));
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (kPink.withAlpha (0.55f));
    g.drawRoundedRectangle (b, 4.f, 1.f);
    const auto area = l.getLocalBounds().reduced (3, 0);
    g.setFont (getLabelFont (l).boldened());
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawFittedText (l.getText(), area.translated (1, 1), l.getJustificationType(), 1, l.getMinimumHorizontalScale());
    g.setColour (juce::Colour (0xfffff0f8));
    g.drawFittedText (l.getText(), area, l.getJustificationType(), 1, l.getMinimumHorizontalScale());
}

void GateEditor::Laf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool highlighted, bool)
{
    const auto b = btn.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = btn.getToggleState();
    g.setColour (juce::Colour (0xff10040b).withAlpha (highlighted ? 0.42f : 0.30f));
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (kPink.withAlpha (on ? 0.95f : 0.55f));
    g.drawRoundedRectangle (b, 4.f, 1.f);
    const float s = juce::jmin (16.f, b.getHeight() - 8.f);
    const juce::Rectangle<float> box (b.getX() + 6.f, b.getCentreY() - s * 0.5f, s, s);
    g.setColour (kPink2);
    g.drawRoundedRectangle (box, 3.f, 1.5f);
    if (on) { g.setColour (kPink); g.fillRoundedRectangle (box.reduced (3.f), 2.f); }
    const auto area = juce::Rectangle<int> ((int) (box.getRight() + 6.f), 0, juce::jmax (0, (int) (b.getRight() - box.getRight() - 8.f)), btn.getHeight());
    g.setFont (juce::Font (juce::FontOptions (b.getHeight() * 0.5f, juce::Font::bold)));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawFittedText (btn.getButtonText(), area.translated (1, 1), juce::Justification::centredLeft, 1);
    g.setColour (juce::Colour (0xfffff0f8));
    g.drawFittedText (btn.getButtonText(), area, juce::Justification::centredLeft, 1);
}

GateEditor::Laf::Laf()
{
    const auto dark = juce::Colour (0xff14060f);
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1b0a14));
    setColour (juce::Slider::rotarySliderFillColourId, kPink);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff4a1530));
    setColour (juce::Slider::thumbColourId, kPink2);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, juce::Colour (0xfffff0f8));
    setColour (juce::ComboBox::backgroundColourId, dark.withAlpha (0.62f));
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kPink);
    setColour (juce::ComboBox::arrowColourId, kPink);
    setColour (juce::PopupMenu::backgroundColourId, dark);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kPink);
}


// ---------------------------------------------------------------------------------------------
// EQ grafico del sidechain: analizador FFT de la senal de deteccion + curva + nodos arrastrables.
// Nodos: paso alto y paso bajo (solo frecuencia) y 3 bandas (low shelf, campana, high shelf: frecuencia y ganancia; rueda = Q).
// ---------------------------------------------------------------------------------------------
class ScEqView : public juce::Component, private juce::Timer
{
public:
    explicit ScEqView (GateProcessor& p) : proc (p) { startTimerHz (30); }
    ~ScEqView() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float s = (float) (r.getWidth() / 980.0);
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillRoundedRectangle (r, 8.f * s);
        g.setColour (kPink.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.6f), 8.f * s, 1.2f);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());
            juce::Path lp, fp, lq, fq;
            buildPath (preDb, lp, fp);
            buildPath (postDb, lq, fq);
            g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillPath (fp);
            g.setColour (juce::Colours::white.withAlpha (0.22f)); g.strokePath (lp, juce::PathStrokeType (1.f * s));
            g.setColour (kPink.withAlpha (0.16f)); g.fillPath (fq);
            g.setColour (kPink2.withAlpha (0.55f)); g.strokePath (lq, juce::PathStrokeType (1.2f * s));
        }

        for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        {
            const bool strong = (f == 100.f || f == 1000.f || f == 10000.f);
            g.setColour (juce::Colours::white.withAlpha (strong ? 0.14f : 0.06f));
            g.drawVerticalLine ((int) freqToX (f), r.getY(), r.getBottom());
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (float db : { -12.f, 12.f }) g.drawHorizontalLine ((int) dbToY (db), r.getX(), r.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawHorizontalLine ((int) dbToY (0.f), r.getX(), r.getRight());
        g.setFont (10.f * s);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        for (float db : { -12.f, 0.f, 12.f })
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String ((int) db), (int) r.getX() + 4, (int) dbToY (db) - 12, 30, 12, juce::Justification::left);
        for (auto f : { 100.f, 1000.f, 10000.f })
            g.drawText (f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f), (int) freqToX (f) - 15, (int) (r.getBottom() - 13.f * s), 30, 12, juce::Justification::centred);

        // curva (lo que "oye" el detector)
        const auto st = settings();
        const double fs = proc.currentSampleRate.load();
        titg::ScCurve sc; sc.set (st, fs);                 // los filtros se calculan una vez por dibujo, no por punto
        juce::Path curve;
        const int steps = juce::jmax (2, (int) (r.getWidth() / 2));
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float y = dbToY ((float) juce::jlimit (-kRange * 1.5, kRange * 1.5, sc.db ((double) xToFreq (x))));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (kPink.withAlpha (st.eqOn ? 0.25f : 0.12f)); g.strokePath (curve, juce::PathStrokeType (7.f * s));
        g.setColour (st.eqOn ? kPink2 : kPink2.withAlpha (0.55f)); g.strokePath (curve, juce::PathStrokeType (2.2f * s));

        // nodos
        for (int n = 0; n < kNodes; ++n)
        {
            const auto p = nodePos (n);
            const bool sel = (n == dragNode);
            const float rad = (sel ? 7.f : 5.5f) * s;
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (p.x - rad - 1.5f, p.y - rad - 1.5f, 2 * rad + 3.f, 2 * rad + 3.f);
            g.setColour (n < 2 ? kPink2.withAlpha (0.9f) : kPink); g.fillEllipse (p.x - rad, p.y - rad, 2 * rad, 2 * rad);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragNode = nodeAt (e.position);
        if (dragNode < 0) return;
        if (dragNode >= 2) { setBool ("sceq", true); }
        begin (dragNode);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragNode < 0) return;
        const float f = xToFreq (juce::jlimit (getLocalBounds().toFloat().getX(), getLocalBounds().toFloat().getRight(), e.position.x));
        if (dragNode == 0)      setParam ("schp", f);
        else if (dragNode == 1) setParam ("sclp", f);
        else
        {
            const int b = dragNode - 1;
            setParam (juce::String ("ef") + juce::String (b), f);
            setParam (juce::String ("eg") + juce::String (b), yToDb (e.position.y));
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragNode >= 0) end (dragNode);
        dragNode = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int n = nodeAt (e.position);
        if (n < 0) return;
        begin (n);
        if (n == 0) { setParam ("schp", 20.f); setParam ("schps", 12.f); }
        else if (n == 1) { setParam ("sclp", 20000.f); setParam ("sclps", 12.f); }
        else setParam (juce::String ("eg") + juce::String (n - 1), 0.f);
        end (n);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int n = nodeAt (e.position);
        if (n < 0) return;
        if (n < 2)      // rueda sobre HP / LP: cambia su pendiente de 6 en 6 dB/oct (como el mando de Slope de TIT Q)
        {
            if (w.deltaY == 0.f) return;
            const char* sid = n == 0 ? "schps" : "sclps";
            if (auto* p = proc.apvts.getParameter (sid))
            {
                const float v = proc.apvts.getRawParameterValue (sid)->load();
                const float nv = juce::jlimit (6.f, 96.f, (float) (std::round (v / 6.f) * 6.f) + (w.deltaY > 0.f ? 6.f : -6.f));
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (nv));
                p->endChangeGesture();
            }
            return;
        }
        const auto id = juce::String ("eq") + juce::String (n - 1);
        if (auto* p = proc.apvts.getParameter (id))
        {
            const float q = proc.apvts.getRawParameterValue (id)->load();
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (q * std::exp (w.deltaY * 1.5f)));
            p->endChangeGesture();
            setBool ("sceq", true);
        }
    }

private:
    static constexpr int kNodes = 5;
    static constexpr float kRange = 18.f;
    GateProcessor& proc;
    std::array<float, SpectrumAnalyzer::kBins> preDb {}, postDb {}, scratch {};
    int dragNode = -1;

    float freqToX (float f) const { const auto r = getLocalBounds().toFloat(); return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (21000.0 / 20.0)); }
    float xToFreq (float x) const { const auto r = getLocalBounds().toFloat(); return (float) (20.0 * std::pow (21000.0 / 20.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())))); }
    float dbToY (float db) const { const auto r = getLocalBounds().toFloat(); return r.getCentreY() - db / (kRange * 1.5f) * (r.getHeight() * 0.5f); }
    float yToDb (float y) const { const auto r = getLocalBounds().toFloat(); return juce::jlimit (-kRange, kRange, (r.getCentreY() - y) / (r.getHeight() * 0.5f) * (kRange * 1.5f)); }

    float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    float raw (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    titg::Settings settings() const
    {
        titg::Settings s;
        s.hpHz = raw ("schp"); s.lpHz = raw ("sclp"); s.eqOn = raw ("sceq") > 0.5f;
        s.hpSlope = raw ("schps"); s.lpSlope = raw ("sclps");
        for (int i = 0; i < 3; ++i)
        {
            const auto k = juce::String (i + 1);
            s.eqF[i] = raw ("ef" + k); s.eqG[i] = raw ("eg" + k); s.eqQ[i] = raw ("eq" + k);
        }
        return s;
    }

    juce::Point<float> nodePos (int n) const
    {
        if (n == 0) return { freqToX (raw ("schp")), dbToY (0.f) };
        if (n == 1) return { freqToX (raw ("sclp")), dbToY (0.f) };
        const auto k = juce::String (n - 1);
        return { freqToX (raw ("ef" + k)), dbToY (raw ("eg" + k)) };
    }
    int nodeAt (juce::Point<float> p) const
    {
        int best = -1; float bd = 16.f * (float) (getWidth() / 980.0) + 4.f;
        for (int n = 0; n < kNodes; ++n)
        {
            const float d = nodePos (n).getDistanceFrom (p);
            if (d < bd) { bd = d; best = n; }
        }
        return best;
    }
    juce::StringArray idsOf (int n) const
    {
        if (n == 0) return { "schp", "schps" };
        if (n == 1) return { "sclp", "sclps" };
        const auto k = juce::String (n - 1);
        return { "ef" + k, "eg" + k };
    }
    void begin (int n) { for (auto& id : idsOf (n)) if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture(); }
    void end (int n)   { for (auto& id : idsOf (n)) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); }
    void setParam (const juce::String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    void setBool (const char* id, bool v)
    {
        if (auto* p = proc.apvts.getParameter (id)) if ((p->getValue() > 0.5f) != v) p->setValueNotifyingHost (v ? 1.f : 0.f);
    }

    void buildPath (const std::array<float, SpectrumAnalyzer::kBins>& db, juce::Path& line, juce::Path& fill) const
    {
        const auto r = getLocalBounds().toFloat();
        const float s = (float) (r.getWidth() / 980.0);
        const double fs = proc.currentSampleRate.load();
        const double binHz = fs / (double) SpectrumAnalyzer::kSize;
        const float bottom = r.getBottom();
        bool started = false;
        float lastX = r.getX();
        for (float x = r.getX(); x <= r.getRight(); x += 2.f * s)
        {
            const double f0 = xToFreq (x - s), f1 = xToFreq (x + s);
            if (f0 >= 0.5 * fs) break;
            const double b0 = f0 / binHz, b1 = f1 / binHz;
            float v;
            if (b1 - b0 < 1.0)
            {
                const double b = 0.5 * (b0 + b1);
                const int i = juce::jlimit (0, SpectrumAnalyzer::kBins - 2, (int) b);
                const float fr = (float) (b - i);
                v = db[(size_t) i] * (1.f - fr) + db[(size_t) i + 1] * fr;
            }
            else
            {
                const int i0 = juce::jmax (0, (int) b0), i1 = juce::jmin (SpectrumAnalyzer::kBins - 1, (int) std::ceil (b1));
                v = -200.f;
                for (int i = i0; i <= i1; ++i) v = juce::jmax (v, db[(size_t) i]);
            }
            v += 4.5f * (float) std::log2 (0.5 * (f0 + f1) / 1000.0);   // inclinacion de 4.5 dB/oct: ruido rosa se ve plano
            const float t = juce::jlimit (0.f, 1.f, (v + 100.f) / 85.f);
            const float y = bottom - t * r.getHeight();
            if (! started) { line.startNewSubPath (x, y); fill.startNewSubPath (x, bottom); fill.lineTo (x, y); started = true; }
            else           { line.lineTo (x, y); fill.lineTo (x, y); }
            lastX = x;
        }
        if (started) { fill.lineTo (lastX, bottom); fill.closeSubPath(); }
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        proc.analyzerPre.computeDb (scratch);
        for (size_t i = 0; i < preDb.size(); ++i) preDb[i] = scratch[i];
        proc.analyzerPost.computeDb (scratch);
        for (size_t i = 0; i < postDb.size(); ++i) postDb[i] = scratch[i];
        repaint();
    }
};

GateEditor::GateEditor (GateProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        k.s.setDoubleClickReturnValue (true, proc.apvts.getParameter (kIds[i])->convertFrom0to1 (proc.apvts.getParameter (kIds[i])->getDefaultValue()));
        k.l.setText (kNames[i], juce::dontSendNotification);
        k.l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.s); addAndMakeVisible (k.l);
        k.att = std::make_unique<SliderAtt> (proc.apvts, kIds[i], k.s);
    }
    scSrcBox.addItemList (juce::StringArray { "SC: Internal", "SC: External" }, 1);
    addAndMakeVisible (scSrcBox); addAndMakeVisible (listenBtn); addAndMakeVisible (scWarn);
    eqView = std::make_unique<ScEqView> (proc);
    addAndMakeVisible (*eqView);
    addAndMakeVisible (scEqBtn);
    scEqAtt = std::make_unique<ButtonAtt> (proc.apvts, "sceq", scEqBtn);
    scSrcAtt  = std::make_unique<ComboAtt>  (proc.apvts, "scsrc", scSrcBox);
    listenAtt = std::make_unique<ButtonAtt> (proc.apvts, "sclisten", listenBtn);
    scWarn.setJustificationType (juce::Justification::centred);

    setResizable (true, true);
    setResizeLimits (880, 491, 1376, 768);
    getConstrainer()->setFixedAspectRatio (1376.0 / 768.0);
    setSize (1100, 614);
    startTimerHz (30);
}

GateEditor::~GateEditor()
{
    stopTimer();
    eqView.reset();
    setLookAndFeel (nullptr);
}

juce::Rectangle<float> GateEditor::curveArea() const
{
    const float s = (float) scale;
    return { 60.f * s, 80.f * s, 330.f * s, 250.f * s };
}
juce::Rectangle<float> GateEditor::meterArea() const
{
    const float s = (float) scale;
    return { 420.f * s, 80.f * s, 26.f * s, 250.f * s };
}

void GateEditor::timerCallback()
{
    const float gr = proc.grDb.load(), in = proc.inDb.load();
    grShown = gr > grShown ? gr : grShown * 0.85f + gr * 0.15f;
    inShown = in > inShown ? in : inShown - 1.5f;
    const bool ext = proc.extScActive.load() && proc.apvts.getRawParameterValue ("scsrc")->load() > 0.5f;
    const bool noExt = ! proc.extScActive.load() && proc.apvts.getRawParameterValue ("scsrc")->load() > 0.5f;
    scWarn.setText (noExt ? "no external sidechain connected" : "", juce::dontSendNotification);
    juce::ignoreUnused (ext);
    repaint();
}

void GateEditor::paint (juce::Graphics& g)
{
    if (background.isValid()) g.drawImage (background, getLocalBounds().toFloat());
    else g.fillAll (juce::Colour (0xff1b0a14));
    const float s = (float) scale;

    g.setFont (juce::Font (juce::FontOptions (26.f * s, juce::Font::bold)));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawText ("GATE / EXPANDER", juce::Rectangle<float> (61.f * s, 21.f * s, 400.f * s, 36.f * s), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfffff0f8));
    g.drawText ("GATE / EXPANDER", juce::Rectangle<float> (60.f * s, 20.f * s, 400.f * s, 36.f * s), juce::Justification::centredLeft);

    // curva de transferencia
    const auto ca = curveArea();
    g.setColour (juce::Colour (0xff10040b).withAlpha (0.55f));
    g.fillRoundedRectangle (ca, 6.f);
    g.setColour (kPink.withAlpha (0.55f));
    g.drawRoundedRectangle (ca, 6.f, 1.f);
    auto X = [&] (double db) { return ca.getX() + (float) ((db + 90.0) / 90.0) * ca.getWidth(); };
    auto Y = [&] (double db) { return ca.getBottom() - (float) ((db + 90.0) / 90.0) * ca.getHeight(); };
    g.setColour (kPink.withAlpha (0.16f));
    for (int d = -80; d <= -10; d += 10) { g.drawLine (X (d), ca.getY(), X (d), ca.getBottom()); g.drawLine (ca.getX(), Y (d), ca.getRight(), Y (d)); }
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawLine (X (-90), Y (-90), X (0), Y (0));   // 1:1

    titg::Settings st;
    auto P = [this] (const char* id) { return (double) proc.apvts.getRawParameterValue (id)->load(); };
    st.thr = P ("thr"); st.range = P ("range"); st.ratio = P ("ratio"); st.hystDb = 0.0;
    juce::Path curve;
    for (int d = -90; d <= 0; ++d)
    {
        const double out = (double) d - titg::staticGr ((double) d, st, false);
        const float x = X (d), y = Y (juce::jmax (-90.0, out));
        if (d == -90) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (kPink);
    g.strokePath (curve, juce::PathStrokeType (2.2f * s));
    if (inShown > -90.f)
    {
        const double out = (double) inShown - (double) grShown;
        g.setColour (kPink2);
        g.fillEllipse (X (inShown) - 4.f * s, Y (juce::jmax (-90.0, out)) - 4.f * s, 8.f * s, 8.f * s);
    }
    g.setFont (juce::Font (juce::FontOptions (11.f * s)));
    g.setColour (juce::Colour (0xfffff0f8).withAlpha (0.8f));
    g.drawText ("in", juce::Rectangle<float> (ca.getRight() - 24.f * s, ca.getBottom() + 1.f * s, 24.f * s, 14.f * s), juce::Justification::centredRight);
    g.drawText ("out", juce::Rectangle<float> (ca.getX() + 2.f * s, ca.getY() + 1.f * s, 30.f * s, 14.f * s), juce::Justification::centredLeft);

    // medidor de reduccion (rojo, baja desde arriba)
    const auto ma = meterArea();
    g.setColour (juce::Colour (0xff10040b).withAlpha (0.55f));
    g.fillRoundedRectangle (ma, 4.f);
    const float frac = juce::jlimit (0.f, 1.f, grShown / 60.f);
    g.setColour (juce::Colour (0xffe02020));
    g.fillRect (juce::Rectangle<float> (ma.getX() + 2.f, ma.getY() + 2.f, ma.getWidth() - 4.f, (ma.getHeight() - 4.f) * frac));
    g.setColour (kPink.withAlpha (0.55f));
    g.drawRoundedRectangle (ma, 4.f, 1.f);
    g.setColour (juce::Colour (0xfffff0f8));
    g.drawText ("GR " + juce::String (grShown, 1) + " dB", juce::Rectangle<float> (ma.getX() - 14.f * s, ma.getBottom() + 2.f * s, ma.getWidth() + 28.f * s, 14.f * s), juce::Justification::centred);
}

void GateEditor::resized()
{
    scale = getWidth() / 1100.0;
    auto S = [this] (double v) { return juce::roundToInt (v * scale); };
    laf.setDefaultSansSerifTypefaceName ("Arial");
    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int row = i < 6 ? 0 : 1, col = i < 6 ? i : i - 6;
        const int x0 = row == 0 ? 530 : 600;                  // la segunda fila va desplazada
        const int x = S (x0 + col * 92);
        const int y = S (row == 0 ? 130 : 330);
        auto& k = knobs[(size_t) i];
        k.l.setFont (juce::Font (juce::FontOptions (12.f * (float) scale)));
        k.l.setBounds (x, y - S (20), S (88), S (18));
        k.s.setBounds (x, y, S (88), S (100));
    }
    eqView->setBounds (S (60), S (466), S (980), S (138));
    scSrcBox.setBounds (S (60), S (400), S (150), S (28));
    listenBtn.setBounds (S (225), S (400), S (130), S (28));
    scEqBtn.setBounds (S (370), S (400), S (130), S (28));
    scWarn.setBounds (S (60), S (436), S (300), S (22));
}
