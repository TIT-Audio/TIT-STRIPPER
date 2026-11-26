#include "PluginEditor.h"
#include <BinaryData.h>

using namespace titc;

static const juce::Colour kPink  (0xffff2e9a);
static const juce::Colour kPink2 (0xffff7ac0);
static const char* kKnobIds[]   = { "thr", "ratio", "knee", "attack", "release", "hold", "look", "range", "link", "mix", "out", "freq", "gain", "q", "slope" };
static const char* kKnobNames[] = { "Threshold", "Ratio", "Knee", "Attack", "Release", "Hold", "Lookahead", "Range", "Link", "Mix", "Out",
                                    "EQ Freq", "EQ Gain", "EQ Q", "EQ Slope" };

static const float kFreqLabels[] = { 20.f, 40.f, 60.f, 80.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                                     1000.f, 2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f, 21000.f };
static const int kNumFreqLabels = (int) (sizeof (kFreqLabels) / sizeof (float));
static const float kMaxFreq = 21000.f;

static juce::String fmtFreq (float f)
{
    return f >= 1000.f ? juce::String ((int) std::lround (f / 1000.f)) + "k" : juce::String ((int) std::lround (f));
}
static juce::String fmtDb (float db)
{
    const juce::String num = juce::String (std::abs (db), std::abs (db) == std::floor (std::abs (db)) ? 0 : 1);
    return db > 0.f ? "+" + num : (db < 0.f ? "-" + num : num);
}

// Rótulos y botones: caja casi transparente, borde rosa fino y letra clara con sombra suave (se ve el fondo y se lee bien)
void TitCompEditor::TitLaf::drawLabel (juce::Graphics& g, juce::Label& l)
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

void TitCompEditor::TitLaf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool highlighted, bool)
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

TitCompEditor::TitLaf::TitLaf()
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
    setColour (juce::TextButton::buttonColourId, dark);
    setColour (juce::TextButton::buttonOnColourId, kPink);
    setColour (juce::TextButton::textColourOffId, kPink2);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

TitCompEditor::TitCompEditor (TitCompProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    preDb.fill (-120.f); postDb.fill (-120.f); scratchDb.fill (-120.f);

    osBox.addItemList (juce::StringArray { "OS: Off", "OS: 2x", "OS: 4x", "OS: 8x" }, 1);
    modeBox.addItemList (modeNames(), 1);
    scSrcBox.addItemList (juce::StringArray { "SC: Internal", "SC: External" }, 1);
    msBox.addItemList (juce::StringArray { "Stereo (L/R)", "Mid/Side" }, 1);
    addAndMakeVisible (msBox);
    {
        const juce::String pm { juce::CharPointer_UTF8 ("\xc2\xb1") };
        for (int v = 3; v <= 12; ++v) dbBox.addItem (pm + juce::String (v) + " dB", v - 2);
    }
    for (auto* c : { &osBox, &modeBox, &scSrcBox, &dbBox, &typeBox, &presetBox }) addAndMakeVisible (*c);
    addAndMakeVisible (savePresetBtn);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (presetBox.getSelectedId() > 0) loadPreset (name);
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("", juce::dontSendNotification);
    };
    savePresetBtn.onClick = [this] { askSavePreset(); };
    addAndMakeVisible (presetMenuBtn);
    presetMenuBtn.onClick = [this] { showPresetMenu(); };
    refreshPresets();
    addAndMakeVisible (listenBtn); addAndMakeVisible (colorBtn); addAndMakeVisible (autoBtn); addAndMakeVisible (onBtn); addAndMakeVisible (deleteBtn);
    typeBox.addItemList (typeNames(), 1);

    osAtt     = std::make_unique<ComboAtt>  (proc.apvts, "os", osBox);
    modeAtt   = std::make_unique<ComboAtt>  (proc.apvts, "mode", modeBox);
    scSrcAtt  = std::make_unique<ComboAtt>  (proc.apvts, "scsrc", scSrcBox);
    msAtt     = std::make_unique<ComboAtt>  (proc.apvts, "ms", msBox);
    dbAtt     = std::make_unique<ComboAtt>  (proc.apvts, "dbrange", dbBox);
    listenAtt = std::make_unique<ButtonAtt> (proc.apvts, "sclisten", listenBtn);
    autoAtt   = std::make_unique<ButtonAtt> (proc.apvts, "autorel", autoBtn);
    colorAtt  = std::make_unique<ButtonAtt> (proc.apvts, "color", colorBtn);

    deleteBtn.onClick = [this] { deleteBand (selected); };

    scWarn.setText ("No external sidechain input connected", juce::dontSendNotification);
    scWarn.setColour (juce::Label::textColourId, juce::Colour (0xffffb040));
    scWarn.setJustificationType (juce::Justification::centred);
    addChildComponent (scWarn);

    for (int i = 0; i < titc::kNumBands; ++i)
    {
        auto& b = bandButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (b);
    }
    bandButtons[0].setToggleState (true, juce::dontSendNotification);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 16);
        addAndMakeVisible (k.s);
        k.l.setText (kKnobNames[i], juce::dontSendNotification);
        k.l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.l);
        if (i >= kLink && i <= kOut)   // link, mix y out se enlazan una vez; los 8 primeros cambian con el modo y los del EQ con la banda
            k.att = std::make_unique<SliderAtt> (proc.apvts, kKnobIds[i], k.s);
    }

    lastModeSeen = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
    configureSlots (lastModeSeen);
    selectBand (0);

    setResizable (true, true);
    setResizeLimits (880, 491, 1376, 768);
    getConstrainer()->setFixedAspectRatio (1376.0 / 768.0);
    setSize (1100, 614);
    startTimerHz (60);
}

TitCompEditor::~TitCompEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TitCompEditor::selectBand (int b)
{
    selected = b;
    bandButtons[(size_t) b].setToggleState (true, juce::dontSendNotification);
    typeAtt.reset(); onAtt.reset();
    typeAtt = std::make_unique<ComboAtt>  (proc.apvts, pid (b, "type"), typeBox);
    onAtt   = std::make_unique<ButtonAtt> (proc.apvts, pid (b, "on"), onBtn);
    for (int i = kFreq; i < kNumKnobs; ++i)
    {
        knobs[(size_t) i].att.reset();
        knobs[(size_t) i].att = std::make_unique<SliderAtt> (proc.apvts, pid (b, kKnobIds[i]), knobs[(size_t) i].s);
    }
    updateEnabledStates();
    repaint();
}

void TitCompEditor::updateEnabledStates()
{
    const bool used = isBandUsed (selected);
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (selected, "type"))->load());
    const bool isCut = (type == titq::LowCut || type == titq::HighCut);
    auto en = [] (juce::Component& c, bool e) { c.setEnabled (e); c.setAlpha (e ? 1.0f : 0.28f); };

    en (typeBox, used); en (onBtn, used); en (deleteBtn, used);
    en (knobs[kFreq].s, used);
    en (knobs[kGain].s, used && ! isCut);
    en (knobs[kQ].s,    used && (type == titq::Bell || type == titq::LowShelf || type == titq::HighShelf || type == titq::TiltShelf));
    en (knobs[kSlope].s, used && (type == titq::LowShelf || type == titq::HighShelf || isCut));

    const bool extSel = proc.apvts.getRawParameterValue ("scsrc")->load() > 0.5f;
    scWarn.setVisible (extSel && ! proc.extScActive.load());
    for (int b = 0; b < titc::kNumBands; ++b)
        bandButtons[(size_t) b].setAlpha (isBandUsed (b) ? 1.0f : 0.45f);
}

bool TitCompEditor::isBandUsed (int b) const { return proc.apvts.getRawParameterValue (pid (b, "used"))->load() > 0.5f; }
bool TitCompEditor::isBandOn (int b) const   { return isBandUsed (b) && proc.apvts.getRawParameterValue (pid (b, "on"))->load() > 0.5f; }
int TitCompEditor::firstFreeBand() const
{
    for (int b = 0; b < titc::kNumBands; ++b) if (! isBandUsed (b)) return b;
    return -1;
}
float TitCompEditor::dbRange() const { return 3.f + (float) std::lround (proc.apvts.getRawParameterValue ("dbrange")->load()); }

void TitCompEditor::setParam (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
void TitCompEditor::setParamGesture (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}
void TitCompEditor::toggleParam (const juce::String& id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.f : 1.f);
        p->endChangeGesture();
    }
}

void TitCompEditor::resetBandParams (int b)
{
    static const char* ids[] = { "on", "type", "freq", "gain", "q", "slope", "used" };
    for (auto* id : ids)
        if (auto* p = proc.apvts.getParameter (pid (b, id)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

int TitCompEditor::createBand (float freq, float gainDb, int typeIndex, float slope)
{
    const int b = firstFreeBand();
    if (b < 0) return -1;
    resetBandParams (b);
    setParamGesture (pid (b, "type"), (float) typeIndex);
    setParamGesture (pid (b, "freq"), freq);
    setParamGesture (pid (b, "gain"), gainDb);
    setParamGesture (pid (b, "slope"), slope);
    setParamGesture (pid (b, "on"), 1.f);
    setParamGesture (pid (b, "used"), 1.f);
    selectBand (b);
    return b;
}

int TitCompEditor::createBandAt (juce::Point<float> pos)
{
    const float f = xToFreq (pos.x);
    if (f >= 20.f && f <= 35.f)        return createBand (f, 0.f, titq::LowCut, 12.f);     // graves -> Low Cut
    if (f >= 17000.f && f <= 21000.f)  return createBand (f, 0.f, titq::HighCut, 12.f);    // agudos -> High Cut
    return createBand (f, yToDb (pos.y), titq::Bell, 12.f);
}

void TitCompEditor::deleteBand (int b)
{
    if (! isBandUsed (b)) return;
    resetBandParams (b);
    updateEnabledStates();
}

// Al elegir un modo se cargan sus valores de partida (después se pueden mover)
void TitCompEditor::applyModePreset (int mode)
{
    struct P { double ratio, knee, attack, release, hold; };
    static const P presets[NumModes] = {
        { 4.0,  6.0, 10.0,  120.0, 0.0 },   // Clean (no se toca)
        { 4.0,  4.0,  3.0,  300.0, 0.0 },   // VCA TIT (rodilla ~4 dB medida)
        { 4.0,  3.0,  0.2,  200.0, 0.0 },   // FET TIT
        { 4.0,  3.0,  0.15, 180.0, 0.0 },   // FET TIT B
        { 3.0, 12.0, 10.0, 1500.0, 0.0 },   // OP TIT
        { 6.5, 9.0,  1.5,  800.0, 0.0 },   // FAIR TIT (variable-mu medido)
        { 2.0, 20.0, 1.0,  500.0, 0.0 },   // TIT MU
        { 4.0,  0.0, 16.0, 100.0, 0.0 },   // DXX TIT
        { 4.0,  0.0, 10.0, 120.0, 0.0 }    // TIT DIGITAL (rodilla infinita fija)
    };
    if (mode < Clean || mode >= NumModes) return;
    setParamGesture ("out", 0.f);
    if (mode == Clean || mode == DIG) { setParamGesture ("thr", -18.f); if (mode == DIG) { setParamGesture ("dknee", 6.f); setParamGesture ("ratio", 4.f); setParamGesture ("attack", 10.f); setParamGesture ("release", 120.f); } return; }
    if (mode == VCA)  setParamGesture ("thr", -10.f);
    if (mode == FET || mode == FETB) { setParamGesture ("fin", 12.f); setParamGesture ("fout", 29.f); }
    if (mode == OPTO) { setParamGesture ("pr", 40.f); setParamGesture ("gain", 26.f); }
    if (mode == MU) { setParamGesture ("mutrim", 0.f); setParamGesture ("muthr", 2.f); setParamGesture ("muatt", 1.f); setParamGesture ("murec", 2.f);
                      setParamGesture ("mumk", 0.f); setParamGesture ("mufilt", 0.f); setParamGesture ("mix", 100.f); setParamGesture ("link", 100.f); }
    if (mode == DXX) { setParamGesture ("thr", -18.26f); setParamGesture ("dxcomp", 4.f); setParamGesture ("mix", 100.f); }
    if (mode == VARI) { setParamGesture ("vin", 20.f); setParamGesture ("vthr", 8.f); setParamGesture ("mix", 100.f); }
    const auto& p = presets[mode];
    setParamGesture ("ratio",   (float) p.ratio);
    setParamGesture ("knee",    (float) p.knee);
    setParamGesture ("attack",  (float) p.attack);
    setParamGesture ("release", (float) p.release);
    setParamGesture ("hold",    (float) p.hold);
}

// Mandos visibles de cada modo (los de cada compresor original) y su texto
struct SlotSpec { const char* id; const char* label; int fmt; };
static std::vector<SlotSpec> slotSpecs (int mode)
{
    switch (mode)
    {
        case VCA:  return { { "thr", "Threshold", 0 }, { "ratio", "Ratio", 6 }, { "attack", "Attack", 0 }, { "release", "Release", 5 }, { "out", "Make-up", 0 }, { "mix", "Mix", 0 } };
        case FET:
        case FETB: return { { "fin", "Input", 0 }, { "fout", "Output", 0 }, { "attack", "Attack", 1 }, { "release", "Release", 2 }, { "ratio", "Ratio", 6 } };
        case OPTO: return { { "pr", "Peak Reduction", 0 }, { "gain", "Gain", 0 } };
        case VARI: return { { "vin", "Input Gain", 0 }, { "vthr", "Threshold", 0 }, { "release", "Time Constant", 3 }, { "out", "Output", 0 }, { "mix", "Mix", 0 } };
        case MU:   return { { "mutrim", "Input Trim", 0 }, { "muthr", "Threshold", 0 }, { "muatt", "Attack", 7 }, { "murec", "Recovery", 8 },
                            { "mumk", "Make Up", 0 }, { "mufilt", "Filter", 0 }, { "mix", "Mix", 0 }, { "link", "Stereo Link", 0 } };
        case DXX:  return { { "thr", "Threshold", 0 }, { "dxcomp", "Compression", 9 }, { "out", "Output Gain", 0 }, { "mix", "Mix", 0 } };
        case DIG:  return { { "thr", "Threshold", 0 }, { "ratio", "Ratio", 0 }, { "dknee", "Knee", 10 }, { "attack", "Attack", 0 }, { "release", "Release", 0 }, { "hold", "Hold", 0 },
                            { "look", "Lookahead", 0 }, { "range", "Range", 0 } };
        default:   return { { "thr", "Threshold", 0 }, { "ratio", "Ratio", 0 }, { "knee", "Knee", 0 }, { "attack", "Attack", 0 },
                            { "release", "Release", 0 }, { "hold", "Hold", 0 }, { "look", "Lookahead", 0 }, { "range", "Range", 0 } };
    }
}

void TitCompEditor::configureSlots (int mode)
{
    slotMode = mode;
    const auto specs = slotSpecs (mode);
    slotCount = (int) specs.size();
    static const double tc[] = { 100.0, 200.0, 800.0, 1100.0, 1500.0, 2500.0 };
    for (int i = 0; i < 8; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.att.reset();
        k.s.textFromValueFunction = nullptr;
        k.s.valueFromTextFunction = nullptr;
        if (i >= slotCount) { k.s.setVisible (false); k.l.setVisible (false); continue; }
        const auto& sp = specs[(size_t) i];
        k.att = std::make_unique<SliderAtt> (proc.apvts, sp.id, k.s);
        k.l.setText (sp.label, juce::dontSendNotification);
        k.s.setVisible (true); k.l.setVisible (true);
        k.s.setTextBoxIsEditable (sp.fmt == 0 || sp.fmt == 10);
        {   // recorrido de la ruleta = rango real del original en cada modo
            const juce::String id (sp.id);
            k.s.lo = k.s.hi = -1.0;
            if (mode == VCA)       { if (id == "attack") { k.s.lo = 0.1;  k.s.hi = 30.0; }  if (id == "release") { k.s.lo = 100.0; k.s.hi = 1200.0; }  if (id == "ratio") { k.s.lo = 2.0; k.s.hi = 10.0; } }
            else if (mode == FET || mode == FETB) { if (id == "attack") { k.s.lo = 0.02; k.s.hi = 0.8; }  if (id == "release") { k.s.lo = 50.0; k.s.hi = 1100.0; }  if (id == "ratio") { k.s.lo = 4.0; k.s.hi = 20.0; } }
            else if (mode == DXX) { if (id == "thr") { k.s.lo = -58.3; k.s.hi = -8.7; }  if (id == "out") { k.s.lo = -20.0; k.s.hi = 20.0; } }
            else if (mode == VARI) { if (id == "release") { k.s.lo = 100.0; k.s.hi = 2500.0; } }
            if (k.s.limited() && (k.s.getValue() < k.s.lo || k.s.getValue() > k.s.hi))
                k.s.setValue (juce::jlimit (k.s.lo, k.s.hi, k.s.getValue()), juce::sendNotificationSync);
        }
        switch (sp.fmt)
        {
            case 1: k.s.textFromValueFunction = [] (double ms) { return juce::String (juce::roundToInt (juce::jlimit (0.02, 0.8, ms) * 1000.0)) + " us"; }; break;
            case 2: k.s.textFromValueFunction = [] (double ms) { return juce::String (juce::roundToInt (juce::jlimit (50.0, 1100.0, ms))) + " ms"; }; break;
            case 3: k.s.textFromValueFunction = [] (double ms)
                    { int best = 0; for (int j = 1; j < 6; ++j) if (std::abs (std::log (ms / tc[j])) < std::abs (std::log (ms / tc[best]))) best = j; return juce::String (best + 1); }; break;
            case 4: k.s.textFromValueFunction = [] (double db) { return juce::String (juce::jlimit (0.0, 100.0, (db + 24.0) / 48.0 * 100.0), 0); }; break;
            case 5: k.s.textFromValueFunction = [] (double ms) { return juce::String (ms / 1000.0, 1) + " s"; }; break;
            case 7: k.s.textFromValueFunction = [] (double a)
                    {   // Attack del MU (5 posiciones medidas)
                        static const char* txt[] = { "1 (0.3 ms)", "2 (0.5 ms)", "3 (25 ms)", "4 (24 ms)", "5 (30 ms)" };
                        return juce::String (txt[juce::jlimit (0, 4, juce::roundToInt (a))]); }; break;
            case 8: k.s.textFromValueFunction = [] (double r)
                    {   // Recovery del MU (5 posiciones medidas, rampa lineal)
                        static const char* txt[] = { "1 (31 dB/s)", "2 (15 dB/s)", "3 (7.3 dB/s)", "4 (3.6 dB/s)", "5 (1.8 dB/s)" };
                        return juce::String (txt[juce::jlimit (0, 4, juce::roundToInt (r))]); }; break;
            case 10: k.s.textFromValueFunction = [] (double w) { return w >= 30.95 ? juce::String ("inf") : juce::String (w, 1) + " dB"; }; break;
            case 9: k.s.textFromValueFunction = [] (double r) { return r >= 99.0 ? juce::String ("inf") : juce::String (r, 1) + ":1"; }; break;
            case 6: k.s.textFromValueFunction = [] (double r) { return juce::String (r, 0) + ":1"; }; break;
            default: break;
        }
        k.s.updateText();
    }
    for (int i = kLink; i <= kOut; ++i)
    {
        const bool v = (mode == Clean || mode == DIG);
        knobs[(size_t) i].s.setVisible (v); knobs[(size_t) i].l.setVisible (v);
    }
    layoutKnobs();
}

void TitCompEditor::layoutKnobs()
{
    auto S = [this] (int v) { return juce::roundToInt ((double) v * scale); };
    for (int i = 0; i < 8; ++i)
    {
        const int x = S (50 + (8 - slotCount) * 60 + i * 120);
        const int y = S (362);
        knobs[(size_t) i].l.setFont (juce::Font (juce::FontOptions (13.f * (float) scale)));
        knobs[(size_t) i].l.setBounds (x, y - S (20), S (90), S (18));
        knobs[(size_t) i].s.setBounds (x, y, S (90), S (100));
    }
    // botón Auto Release justo debajo del mando de release
    const auto specs = slotSpecs (slotMode);
    for (int i = 0; i < (int) specs.size() && i < 8; ++i)
        if (juce::String (specs[(size_t) i].id) == "release")
            autoBtn.setBounds (S (50 + (8 - slotCount) * 60 + i * 120), S (466), S (90), S (22));
}

// Refleja en los mandos lo que el modo permite (el procesador ya lo aplica siempre) y bloquea los que el original no tiene
void TitCompEditor::enforceModeLimits()
{
    auto raw = [this] (const char* id) { return (double) proc.apvts.getRawParameterValue (id)->load(); };
    titc::Settings s;
    s.mode = (int) std::lround (raw ("mode"));
    s.ratio = raw ("ratio"); s.knee = raw ("knee"); s.attackMs = raw ("attack"); s.releaseMs = raw ("release");
    s.holdMs = raw ("hold"); s.autoRel = raw ("autorel") > 0.5;
    titc::Settings c = s;
    titc::constrain (c);
    auto fix = [this] (const char* id, double a, double b) { if (std::abs (a - b) > 1.0e-3) setParamGesture (id, (float) b); };
    fix ("ratio", s.ratio, c.ratio); fix ("knee", s.knee, c.knee); fix ("attack", s.attackMs, c.attackMs);
    fix ("release", s.releaseMs, c.releaseMs); fix ("hold", s.holdMs, c.holdMs);
    if (s.autoRel != c.autoRel) setParamGesture ("autorel", c.autoRel ? 1.f : 0.f);
    if (s.mode == VCA)  { const double o = raw ("out"); fix ("out", o, juce::jlimit (-5.0, 15.0, o)); }
    if (s.mode == DXX)  { const double o = raw ("out"); fix ("out", o, juce::jlimit (-20.0, 20.0, o)); const double t = raw ("thr"); fix ("thr", t, juce::jlimit (-58.3, -8.7, t)); }
    if (s.mode == VARI) { const double o = raw ("out"); fix ("out", o, juce::jlimit (-20.0, 20.0, o)); }

    const auto lk = titc::locksFor (s.mode);
    auto en = [] (juce::Component& comp, bool e) { comp.setEnabled (e); comp.setAlpha (e ? 1.0f : 0.28f); };
    const auto specs = slotSpecs (s.mode);
    for (int i = 0; i < slotCount && i < (int) specs.size(); ++i)
    {
        const juce::String id (specs[(size_t) i].id);
        bool e = true;
        if (id == "attack") e = ! lk.attack;
        else if (id == "release") e = ! lk.release && ! c.autoRel;
        else if (id == "ratio") e = ! lk.ratio;
        else if (id == "knee") e = ! lk.knee;
        else if (id == "hold") e = ! lk.hold;
        en (knobs[(size_t) i].s, e);
    }
    autoBtn.setVisible (lk.autoRel);
    en (autoBtn, lk.autoRel);
}

void TitCompEditor::timerCallback()
{
    const int modeNow = (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load());
    if (modeNow != lastModeSeen) { lastModeSeen = modeNow; configureSlots (modeNow); applyModePreset (modeNow); }

    const int osIdx = juce::jlimit (0, 3, (int) std::lround (proc.apvts.getRawParameterValue ("os")->load()));
    viewFs = proc.currentSampleRate.load() * (double) (1 << osIdx);
    for (int b = 0; b < titc::kNumBands; ++b)
        viewBands[(size_t) b].design (proc.readBandSettings (b), viewFs, 1.0);

    if ((++tick & 1) == 0)
    {
        proc.analyzerPre.computeDb (scratchDb);
        for (size_t i = 0; i < preDb.size(); ++i)
            preDb[i] += (scratchDb[i] > preDb[i] ? 0.6f : 0.15f) * (scratchDb[i] - preDb[i]);
        proc.analyzerPost.computeDb (scratchDb);
        for (size_t i = 0; i < postDb.size(); ++i)
            postDb[i] += (scratchDb[i] > postDb[i] ? 0.6f : 0.15f) * (scratchDb[i] - postDb[i]);
    }

    const float gr = proc.grDb.load();
    grShown += (gr > grShown ? 0.5f : 0.12f) * (gr - grShown);
    const float inNow = juce::jmax (-90.f, proc.inDb.load()), outNow = juce::jmax (-90.f, proc.outDb.load());
    inShown  += (inNow  > inShown  ? 0.6f : 0.08f) * (inNow  - inShown);
    outShown += (outNow > outShown ? 0.6f : 0.08f) * (outNow - outShown);

    enforceModeLimits();
    updateEnabledStates();
    repaint (displayArea().getUnion (meterArea()).toNearestInt().expanded (24));
}

// ---------- geometría ----------
juce::Rectangle<float> TitCompEditor::displayArea() const
{
    const float s = (float) scale;
    return { 40.f * s, 184.f * s, 1020.f * s, 106.f * s };   // EQ del sidechain, más baja
}
juce::Rectangle<float> TitCompEditor::meterArea() const   // medidor horizontal IN / GR / OUT, arriba
{
    const float s = (float) scale;
    return { 40.f * s, 46.f * s, 1020.f * s, 132.f * s };
}
float TitCompEditor::freqToX (float f) const
{
    const auto a = displayArea();
    return a.getX() + a.getWidth() * std::log (juce::jlimit (20.f, kMaxFreq, f) / 20.f) / std::log (kMaxFreq / 20.f);
}
float TitCompEditor::xToFreq (float x) const
{
    const auto a = displayArea();
    return 20.f * std::pow (kMaxFreq / 20.f, juce::jlimit (0.f, 1.f, (x - a.getX()) / a.getWidth()));
}
float TitCompEditor::dbToY (float db) const
{
    const auto a = displayArea();
    const float r = dbRange();
    return a.getCentreY() - juce::jlimit (-r, r, db) / r * (a.getHeight() * 0.5f);
}
float TitCompEditor::yToDb (float y) const
{
    const auto a = displayArea();
    const float r = dbRange();
    return juce::jlimit (-r, r, (a.getCentreY() - y) / (a.getHeight() * 0.5f) * r);
}
juce::Point<float> TitCompEditor::nodePosition (int b) const
{
    const auto s = proc.readBandSettings (b);
    return { freqToX (s.freq), dbToY (nodeDraggableInGain (s.type) ? s.gain : 0.f) };
}
int TitCompEditor::findNode (juce::Point<float> p) const
{
    int hit = -1;
    float best = 14.f * (float) scale;
    for (int b = 0; b < titc::kNumBands; ++b)
    {
        if (! isBandUsed (b)) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < best) { best = d; hit = b; }
    }
    return hit;
}

// ---------- ratón ----------
void TitCompEditor::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;
    if (e.mods.isPopupMenu())
    {
        const int hit = findNode (e.position);
        if (hit >= 0) deleteBand (hit);
        return;
    }
    if (meterArea().expanded (8.f * (float) scale).contains (e.position)) return;

    const int hit = findNode (e.position);
    if (hit < 0 && displayArea().contains (e.position))
    {   // UN SOLO CLIC en una zona vacía crea una banda del EQ del sidechain y se puede arrastrar al momento
        const int nb = createBandAt (e.position);
        if (nb >= 0)
        {
            lastCreateMs = juce::Time::getMillisecondCounter();
            dragBand = nb;
            proc.apvts.getParameter (pid (nb, "freq"))->beginChangeGesture();
            proc.apvts.getParameter (pid (nb, "gain"))->beginChangeGesture();
        }
        return;
    }
    if (hit >= 0)
    {
        selectBand (hit);
        if (isBandOn (hit))
        {
            dragBand = hit;
            proc.apvts.getParameter (pid (hit, "freq"))->beginChangeGesture();
            proc.apvts.getParameter (pid (hit, "gain"))->beginChangeGesture();
        }
    }
}

void TitCompEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    setParam (pid (dragBand, "freq"), xToFreq (e.position.x));
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (dragBand, "type"))->load());
    if (nodeDraggableInGain (type)) setParam (pid (dragBand, "gain"), yToDb (e.position.y));
}

void TitCompEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        proc.apvts.getParameter (pid (dragBand, "freq"))->endChangeGesture();
        proc.apvts.getParameter (pid (dragBand, "gain"))->endChangeGesture();
    }
    dragBand = -1;
}

void TitCompEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (juce::Time::getMillisecondCounter() - lastCreateMs < 600) return;   // el 2º clic de la creación no apaga la banda
    const int hit = findNode (e.position);
    if (hit >= 0) toggleParam (pid (hit, "on"));
}

// ---------- analizador ----------
void TitCompEditor::buildSpectrumPaths (const std::array<float, SpectrumAnalyzer::kBins>& db, juce::Path& line, juce::Path& fill) const
{
    const float s = (float) scale;
    const auto disp = displayArea();
    const double fs = proc.currentSampleRate.load();
    const double binHz = fs / (double) SpectrumAnalyzer::kSize;
    const float bottom = disp.getBottom();
    bool started = false;
    float lastX = disp.getX();
    for (float x = disp.getX(); x <= disp.getRight(); x += 2.f * s)
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
            const int i0 = juce::jmax (0, (int) b0);
            const int i1 = juce::jmin (SpectrumAnalyzer::kBins - 1, (int) std::ceil (b1));
            v = -200.f;
            for (int i = i0; i <= i1; ++i) v = juce::jmax (v, db[(size_t) i]);
        }
        v += 4.5f * (float) std::log2 (0.5 * (f0 + f1) / 1000.0);
        const float t = juce::jlimit (0.f, 1.f, (v + 100.f) / 85.f);
        const float y = bottom - t * disp.getHeight();
        if (! started) { line.startNewSubPath (x, y); fill.startNewSubPath (x, bottom); fill.lineTo (x, y); started = true; }
        else           { line.lineTo (x, y); fill.lineTo (x, y); }
        lastX = x;
    }
    if (started) { fill.lineTo (lastX, bottom); fill.closeSubPath(); }
}

// ---------- dibujo ----------
void TitCompEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);

    const float s = (float) scale;
    const auto disp = displayArea();
    const float range = dbRange();

    g.setColour (juce::Colours::black.withAlpha (0.26f));
    g.fillRoundedRectangle (disp, 10.f * s);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (juce::Rectangle<float> (30.f * s, 296.f * s, 1040.f * s, 310.f * s), 10.f * s);
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    g.fillRoundedRectangle (meterArea(), 10.f * s);
    g.setColour (kPink.withAlpha (0.6f));
    g.drawRoundedRectangle (meterArea(), 10.f * s, 1.2f);
    g.setColour (kPink.withAlpha (0.6f));
    g.drawRoundedRectangle (disp, 10.f * s, 1.2f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (disp.toNearestInt());
        juce::Path linePre, fillPre, linePost, fillPost;
        buildSpectrumPaths (preDb, linePre, fillPre);
        buildSpectrumPaths (postDb, linePost, fillPost);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillPath (fillPre);
        g.setColour (juce::Colours::white.withAlpha (0.22f)); g.strokePath (linePre, juce::PathStrokeType (1.f * s));
        g.setColour (kPink.withAlpha (0.16f)); g.fillPath (fillPost);
        g.setColour (kPink2.withAlpha (0.55f)); g.strokePath (linePost, juce::PathStrokeType (1.2f * s));
    }

    for (int i = 0; i < kNumFreqLabels; ++i)
    {
        const float f = kFreqLabels[i];
        const bool strong = (f == 100.f || f == 1000.f || f == 10000.f);
        g.setColour (juce::Colours::white.withAlpha (strong ? 0.14f : 0.06f));
        g.drawVerticalLine ((int) freqToX (f), disp.getY(), disp.getBottom());
    }
    const float dbLines[] = { -range, -range * 0.5f, range * 0.5f, range };
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (float db : dbLines) g.drawHorizontalLine ((int) dbToY (db), disp.getX(), disp.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.drawHorizontalLine ((int) dbToY (0.f), disp.getX(), disp.getRight());

    g.setFont (10.f * s);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    for (float db : { -range, -range * 0.5f, 0.f, range * 0.5f, range })
        g.drawText (fmtDb (db), (int) disp.getX() + 4, (int) dbToY (db) - 13, 40, 12, juce::Justification::left);
    for (int i = 0; i < kNumFreqLabels; ++i)
    {
        const float f = kFreqLabels[i];
        const float x = freqToX (f);
        const float w = 30.f * s;
        const int y = (int) (disp.getBottom() - 15.f * s), h = (int) (13.f * s);
        if (f == 20000.f)      g.drawText (fmtFreq (f), (int) (x - w - 1.f * s), y, (int) w, h, juce::Justification::centredRight);
        else if (f == 21000.f) g.drawText (fmtFreq (f), (int) (x + 2.f * s), y, (int) w, h, juce::Justification::centredLeft);
        else                   g.drawText (fmtFreq (f), (int) (x - w * 0.5f), y, (int) w, h, juce::Justification::centred);
    }

    // curva del EQ del sidechain (lo que "oye" el detector)
    juce::Path curve;
    const int steps = juce::jmax (2, (int) (disp.getWidth() / 2));
    for (int i = 0; i <= steps; ++i)
    {
        const float x = disp.getX() + disp.getWidth() * (float) i / (float) steps;
        const double f = xToFreq (x);
        double db = 0.0;
        for (int b = 0; b < titc::kNumBands; ++b) db += viewBands[(size_t) b].magnitudeDb (f, viewFs);
        const float y = dbToY ((float) db);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (kPink.withAlpha (0.25f)); g.strokePath (curve, juce::PathStrokeType (7.f * s));
    g.setColour (kPink2);                  g.strokePath (curve, juce::PathStrokeType (2.2f * s));

    for (int b = 0; b < titc::kNumBands; ++b)
    {
        if (! isBandUsed (b)) continue;
        const bool on = isBandOn (b);
        const auto p = nodePosition (b);
        const float r = (b == selected ? 8.f : 6.f) * s;
        g.setColour ((b == selected && on) ? kPink : juce::Colours::black.withAlpha (on ? 0.7f : 0.35f));
        g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
        g.setColour (kPink2.withAlpha (on ? 1.f : 0.45f));
        g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.f : 0.5f));
        g.setFont (10.f * s);
        g.drawText (juce::String (b + 1), (int) (p.x - r), (int) (p.y - r), (int) (2 * r), (int) (2 * r), juce::Justification::centred);
    }

    // medidor horizontal: IN, GR y OUT
    {
        const auto m = meterArea();
        struct Bar { const char* name; float v, lo, hi; bool isGr; };
        const Bar bars[3] = { { "IN", inShown, -60.f, 0.f, false }, { "GR", grShown, 0.f, 20.f, true }, { "OUT", outShown, -60.f, 0.f, false } };
        const float rowH = 38.f * s, gap = 3.f * s;
        for (int i = 0; i < 3; ++i)
        {
            const auto& b = bars[i];
            const float y = m.getY() + 4.f * s + (float) i * (rowH + gap);
            const juce::Rectangle<float> r (m.getX() + 70.f * s, y, m.getWidth() - 70.f * s - 120.f * s, rowH);
            g.setFont (juce::Font (juce::FontOptions (17.f * s, juce::Font::bold)));
            g.setColour (juce::Colours::white);
            g.drawText (b.name, (int) (m.getX() + 12.f * s), (int) y, (int) (54.f * s), (int) rowH, juce::Justification::centredLeft);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 4.f * s);
            const float frac = juce::jlimit (0.f, 1.f, (b.v - b.lo) / (b.hi - b.lo));
            {
                const auto fr = r.withWidth (juce::jmax (0.f, r.getWidth() * frac));
                if (b.isGr) g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc01626), r.getX(), 0.f, juce::Colour (0xffff3b3b), r.getRight(), 0.f, false));
                else        g.setGradientFill (juce::ColourGradient (kPink, r.getX(), 0.f, kPink2, r.getRight(), 0.f, false));
                g.fillRoundedRectangle (fr, 4.f * s);
            }
            g.setColour (kPink2.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 4.f * s, 1.f);
            g.setFont (9.f * s);
            const float marks[] = { 0.f, 2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f, 18.f, 20.f };
            const float marksDb[] = { -60.f, -48.f, -36.f, -24.f, -18.f, -12.f, -6.f, -3.f, 0.f };
            for (int k = 0; k < (b.isGr ? 11 : 9); ++k)
            {
                const float v = b.isGr ? marks[k] : marksDb[k];
                const float x = r.getX() + r.getWidth() * (v - b.lo) / (b.hi - b.lo);
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.drawVerticalLine ((int) x, r.getY() + 2.f * s, r.getBottom() - 2.f * s);
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawText (juce::String ((int) v), (int) (x - 14.f * s), (int) (r.getBottom() - 12.f * s), (int) (28.f * s), (int) (11.f * s), juce::Justification::centred);
            }
            g.setFont (juce::Font (juce::FontOptions (16.f * s, juce::Font::bold)));
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (b.v, 1) + " dB", (int) (r.getRight() + 8.f * s), (int) y, (int) (104.f * s), (int) rowH, juce::Justification::centredRight);
        }
    }

    // logo TIT COMP, esquina inferior derecha
    {
        const float fh = 30.f * s;
        g.setColour (kPink);
        g.setFont (juce::Font (juce::FontOptions (fh, juce::Font::bold)));
        g.drawText ("TIT COMP", juce::Rectangle<float> (700.f * s, 596.f * s - fh, 356.f * s, fh * 1.2f).toNearestInt(), juce::Justification::centredRight);
    }
}

juce::File TitCompEditor::presetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TIT COMP").getChildFile ("Presets");
}

void TitCompEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    auto files = presetDir().findChildFiles (juce::File::findFiles, false, "*.titc");
    files.sort();
    int id = 1;
    for (const auto& f : files) presetBox.addItem (f.getFileNameWithoutExtension(), id++);
}

void TitCompEditor::loadPreset (const juce::String& name)
{
    if (auto xml = juce::XmlDocument::parse (presetDir().getChildFile (name + ".titc")))
        if (xml->hasTagName (proc.apvts.state.getType()))
            proc.loadStateXml (*xml);
}

void TitCompEditor::savePreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return;
    auto dir = presetDir();
    dir.createDirectory();
    if (auto xml = proc.currentStateXml())
        xml->writeTo (dir.getChildFile (name + ".titc"));
    refreshPresets();
}

// Menu "...": guardar el preset en cualquier sitio (pendrive, nube...) o cargar uno de otro PC
void TitCompEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save preset to a file...");
    m.addItem (2, "Load preset from a file...");
    m.addItem (3, "Open the presets folder");
    m.addSeparator();
    m.addItem (4, "Color on at start (new instances)", true, colorDefault());
    m.addItem (5, "Open the settings file (config.txt)");
    juce::Component::SafePointer<TitCompEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetMenuBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 3) { presetDir().createDirectory(); presetDir().revealToUser(); return; }
        if (r == 4) { saveColorDefault (! colorDefault()); return; }
        if (r == 5) { if (! configFile().existsAsFile()) saveColorDefault (colorDefault()); configFile().revealToUser(); return; }
        const bool saving = (r == 1);
        safe->chooser = std::make_unique<juce::FileChooser> (saving ? "Save preset" : "Load preset", presetDir(), "*.titc");
        const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting)
                                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        safe->chooser->launchAsync (flags, [safe, saving] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe == nullptr || f == juce::File()) return;
            if (saving)
            {
                if (auto xml = safe->proc.currentStateXml()) xml->writeTo (f.withFileExtension ("titc"));
            }
            else if (auto xml = juce::XmlDocument::parse (f))
                if (xml->hasTagName (safe->proc.apvts.state.getType()))
                    safe->proc.loadStateXml (*xml);
        });
    });
}

void TitCompEditor::askSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "", "");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TitCompEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result == 1 && safe != nullptr) safe->savePreset (aw->getTextEditorContents ("name"));
    }), true);
}

void TitCompEditor::resized()
{
    scale = getWidth() / 1100.0;
    auto S = [this] (int v) { return juce::roundToInt ((double) v * scale); };

    osBox.setBounds     (S (40),  S (12), S (85),  S (26));
    modeBox.setBounds   (S (130), S (12), S (120), S (26));
    scSrcBox.setBounds  (S (255), S (12), S (110), S (26));
    listenBtn.setBounds (S (370), S (12), S (100), S (26));
    msBox.setBounds     (S (475), S (12), S (110), S (26));
    scWarn.setBounds    (S (590), S (12), S (130), S (26));
    presetBox.setBounds (S (725), S (12), S (100), S (26));
    savePresetBtn.setBounds (S (829), S (12), S (62), S (26));
    presetMenuBtn.setBounds (S (895), S (12), S (44), S (26));
    dbBox.setBounds     (S (950), S (12), S (80),  S (26));
    colorBtn.setBounds  (S (1036), S (12), S (60), S (26));

    for (int i = 0; i < titc::kNumBands; ++i)
        bandButtons[(size_t) i].setBounds (S (44 + i * 46), S (304), S (42), S (26));
    typeBox.setBounds   (S (520), S (304), S (120), S (26));
    onBtn.setBounds     (S (652), S (304), S (56),  S (26));
    deleteBtn.setBounds (S (880), S (304), S (80),  S (26));

    // fila 1: 8 mandos del compresor — fila 2: link, mix, out y los 4 del EQ del sidechain
    layoutKnobs();
    for (int i = 8; i < kNumKnobs; ++i)
    {
        const int row = i < 8 ? 0 : 1;
        const int col = i < 8 ? i : i - 8;
        const int x = S (50 + col * 120);
        const int y = row == 0 ? S (362) : S (512);
        knobs[(size_t) i].l.setFont (juce::Font (juce::FontOptions (13.f * (float) scale)));
        knobs[(size_t) i].l.setBounds (x, y - S (20), S (90), S (18));
        knobs[(size_t) i].s.setBounds (x, y, S (90), S (90));
    }
}
