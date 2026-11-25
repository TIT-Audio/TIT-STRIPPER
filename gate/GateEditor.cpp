#include "GateEditor.h"
#include <BinaryData.h>

static const juce::Colour kPink  (0xffff2e9a);
static const juce::Colour kPink2 (0xffff7ac0);
static const char* kIds[]   = { "thr", "range", "ratio", "attack", "hold", "release", "hyst", "look", "schp", "sclp", "link",
                              "ef1", "eg1", "eq1", "ef2", "eg2", "eq2", "ef3", "eg3", "eq3" };
static const char* kNames[] = { "Threshold", "Range", "Ratio", "Attack", "Hold", "Release", "Hysteresis", "Lookahead", "SC High-pass", "SC Low-pass", "Stereo Link",
                              "Low Freq", "Low Gain", "Low Q", "Mid Freq", "Mid Gain", "Mid Q", "High Freq", "High Gain", "High Q" };

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
        const int row = i < 6 ? 0 : i < 11 ? 1 : 2, col = i < 6 ? i : i < 11 ? i - 6 : i - 11;
        const int x0 = row == 0 ? 530 : row == 1 ? 600 : 250;    // la segunda fila va desplazada; la tercera es el EQ del sidechain
        const int x = S (x0 + col * (row == 2 ? 86 : 92));
        int y = S (row == 0 ? 130 : row == 1 ? 330 : 495);
        auto& k = knobs[(size_t) i];
        k.l.setFont (juce::Font (juce::FontOptions (12.f * (float) scale)));
        k.l.setBounds (x, y - S (20), S (88), S (18));
        k.s.setBounds (x, y, S (row == 2 ? 82 : 88), S (row == 2 ? 96 : 100));
    }
    scSrcBox.setBounds (S (60), S (400), S (150), S (28));
    listenBtn.setBounds (S (225), S (400), S (130), S (28));
    scEqBtn.setBounds (S (60), S (505), S (150), S (28));
    scWarn.setBounds (S (60), S (436), S (300), S (22));
}
