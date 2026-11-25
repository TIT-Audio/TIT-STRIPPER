#include "PluginEditor.h"
#include <BinaryData.h>

using namespace titq;

static const juce::Colour kPink  (0xffff2e9a);
static const juce::Colour kPink2 (0xffff7ac0);
static const char* kKnobIds[]   = { "freq", "gain", "q", "slope", "boost", "atten", "range", "thr", "ratio", "attack", "release", "knee", "scfreq", "scq" };
static const char* kKnobNames[] = { "Freq", "Gain", "Q", "Slope", "BOOB", "ATTIT", "Range", "Threshold", "Ratio", "Attack", "Release", "Knee", "SC Freq", "SC Q" };

// escala de frecuencias hasta 21k: 20-100 de 20 en 20, 100-1k de 100 en 100, 1k-10k de 1k en 1k, luego 20k y 21k
static const float kFreqLabels[] = { 20.f, 40.f, 60.f, 80.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                                     1000.f, 2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f,
                                     20000.f, 21000.f };
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

// =====================================================================================================
//  Menú de configuración de clics / dobles clics + presets .json
// =====================================================================================================
namespace
{
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (TitQProcessor& p) : proc (p)
    {
        setSize (640, 640);

        auto label = [this] (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (l);
        };
        label (lblType,   "Type of a new band");
        label (lblSlope,  "Slope of a new band (dB/oct)");
        label (lblBand,   "Double-click on a band does");
        label (lblWheel,  "Mouse wheel speed (Q)");
        label (lblZones,  "Click zones: click here (Hz and dB) = get this band");

        addAndMakeVisible (presetBox);
        addAndMakeVisible (defaultBtn); addAndMakeVisible (saveBtn); addAndMakeVisible (deleteBtn); addAndMakeVisible (folderBtn);
        nameEd.setTextToShowWhenEmpty ("preset name", juce::Colours::grey);
        addAndMakeVisible (nameEd);

        newTypeBox.addItemList (titq::typeNames(), 1);
        bandBox.addItemList (ClickConfig::bandClickActions(), 1);
        for (auto* c : { &newTypeBox, &bandBox }) addAndMakeVisible (*c);

        for (auto* s : { &newSlope })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setRange (0.0, 96.0, 1.0);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
            addAndMakeVisible (*s);
        }
        wheelSpeed.setSliderStyle (juce::Slider::LinearHorizontal);
        wheelSpeed.setRange (0.2, 4.0, 0.1);
        wheelSpeed.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        addAndMakeVisible (wheelSpeed);
        wheelOn.setButtonText ("Mouse wheel over a band changes its Q");
        addAndMakeVisible (wheelOn);
        gainClick.setButtonText ("New band gets the gain where I click");
        addAndMakeVisible (gainClick);

        // ---- zonas ----
        for (int i = 0; i < ClickConfig::kNumZones; ++i)
        {
            auto& z = rows[(size_t) i];
            label (z.name, ClickConfig::zoneName (i));
            z.action.addItemList (ClickConfig::zoneActions(), 1);
            z.clicks.addItem ("1 click", 1);
            z.clicks.addItem ("2 clicks", 2);
            z.slope.setSliderStyle (juce::Slider::LinearHorizontal);
            z.slope.setRange (0.0, 96.0, 1.0);
            z.slope.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            for (auto* c : { &z.action, &z.clicks }) addAndMakeVisible (*c);
            addAndMakeVisible (z.slope);
            const char* t[] = { "Hz", "to", "dB", "to" };
            for (int k = 0; k < 4; ++k) { label (z.lbl[k], t[k]); }
            for (int k = 0; k < 4; ++k) { z.ed[k].setJustification (juce::Justification::centred); addAndMakeVisible (z.ed[k]); }

            auto& c = proc.clickConfig;
            z.action.onChange = [this, i, &c] { c.zones[i].action = ClickConfig::zoneActions()[rows[(size_t) i].action.getSelectedItemIndex()]; };
            z.clicks.onChange = [this, i, &c] { c.zones[i].clicks = rows[(size_t) i].clicks.getSelectedId(); };
            z.slope.onValueChange = [this, i, &c] { c.zones[i].slope = rows[(size_t) i].slope.getValue(); };
            auto commit = [this, i, &c]
            {
                auto& zz = rows[(size_t) i];
                auto& d = c.zones[i];
                const double a = juce::jlimit (10.0, 21000.0, zz.ed[0].getText().getDoubleValue());
                const double b = juce::jlimit (10.0, 21000.0, zz.ed[1].getText().getDoubleValue());
                const double e = juce::jlimit (-60.0, 60.0, zz.ed[2].getText().getDoubleValue());
                const double f = juce::jlimit (-60.0, 60.0, zz.ed[3].getText().getDoubleValue());
                if (a < b) { d.minHz = a; d.maxHz = b; }
                if (e < f) { d.minDb = e; d.maxDb = f; }
                refreshControls();
            };
            for (auto& ed : z.ed) { ed.onReturnKey = commit; ed.onFocusLost = commit; }
        }

        // ---- cambios: se aplican al momento sobre la configuración activa ----
        auto& c = proc.clickConfig;
        newTypeBox.onChange = [this, &c] { c.newBandType = titq::typeNames()[newTypeBox.getSelectedItemIndex()]; };
        newSlope.onValueChange  = [this, &c] { c.newBandSlope  = newSlope.getValue(); };
        bandBox.onChange = [this, &c] { c.bandDoubleClick = ClickConfig::bandClickActions()[bandBox.getSelectedItemIndex()]; };
        gainClick.onClick = [this, &c] { c.gainFromClick = gainClick.getToggleState(); };
        wheelOn.onClick = [this, &c] { c.wheelQ = wheelOn.getToggleState(); };
        wheelSpeed.onValueChange = [this, &c] { c.wheelQSpeed = wheelSpeed.getValue(); };

        // ---- presets ----
        presetBox.onChange = [this]
        {
            const int i = presetBox.getSelectedItemIndex();
            if (! juce::isPositiveAndBelow (i, files.size())) return;
            proc.clickConfig = ClickConfig::load (files[i]);
            proc.clickPresetName = files[i].getFileNameWithoutExtension();
            nameEd.setText (proc.clickPresetName, juce::dontSendNotification);
            refreshControls();
        };
        saveBtn.onClick = [this]
        {
            const auto name = nameEd.getText().trim();
            if (name.isEmpty()) return;
            const auto f = ClickConfig::fileFor (name);
            ClickConfig::save (f, proc.clickConfig);
            proc.clickPresetName = f.getFileNameWithoutExtension();
            refreshPresetList();
        };
        defaultBtn.onClick = [this]
        {
            auto name = nameEd.getText().trim();
            if (name.isEmpty()) name = proc.clickPresetName;
            const auto f = ClickConfig::fileFor (name);
            if (! f.existsAsFile()) ClickConfig::save (f, proc.clickConfig);
            proc.clickPresetName = f.getFileNameWithoutExtension();
            ClickConfig::setDefaultName (proc.clickPresetName);
            refreshPresetList();
        };
        deleteBtn.onClick = [this]
        {
            auto name = nameEd.getText().trim();
            if (name.isEmpty()) name = proc.clickPresetName;
            const auto f = ClickConfig::fileFor (name);
            if (f.existsAsFile())
            {
                f.deleteFile();
                if (ClickConfig::defaultName().equalsIgnoreCase (f.getFileNameWithoutExtension()))
                    ClickConfig::setDefaultName ({});
            }
            refreshPresetList();
        };
        folderBtn.onClick = []
        {
            ClickConfig::presetsFolder().createDirectory();
            ClickConfig::presetsFolder().revealToUser();
        };

        defaultBtn.setButtonText ("Set as Default");
        saveBtn.setButtonText ("Save");
        deleteBtn.setButtonText ("Delete");
        folderBtn.setButtonText ("Open settings folder");

        nameEd.setText (proc.clickPresetName, juce::dontSendNotification);
        refreshPresetList();
        refreshControls();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (kPink.withAlpha (0.5f));
        g.drawHorizontalLine (118, 12.f, (float) getWidth() - 12.f);
    }

    void resized() override
    {
        const int lx = 12, lw = 260, cx = 280, cw = 348;
        presetBox.setBounds (12, 12, 330, 26);   defaultBtn.setBounds (350, 12, 278, 26);
        nameEd.setBounds (12, 46, 330, 26);      saveBtn.setBounds (350, 46, 130, 26);   deleteBtn.setBounds (486, 46, 142, 26);
        folderBtn.setBounds (12, 80, 616, 26);

        int y = 130;
        auto row = [&] (juce::Label& l, juce::Component& c) { l.setBounds (lx, y, lw, 26); c.setBounds (cx, y, cw, 26); y += 36; };
        row (lblType,  newTypeBox);
        row (lblSlope, newSlope);
        row (lblBand,  bandBox);
        gainClick.setBounds (lx, y, 616, 26); y += 36;
        wheelOn.setBounds (lx, y, 616, 26); y += 36;
        row (lblWheel, wheelSpeed);

        lblZones.setBounds (lx, y, 616, 26); y += 32;
        for (auto& z : rows)
        {
            z.name.setBounds (12, y, 140, 24);
            z.action.setBounds (158, y, 130, 24);
            z.clicks.setBounds (296, y, 100, 24);
            z.slope.setBounds (404, y, 224, 24);
            int x = 158; const int yy = y + 28;
            z.lbl[0].setBounds (x, yy, 26, 24);  x += 28;
            z.ed[0].setBounds (x, yy, 64, 24);   x += 68;
            z.lbl[1].setBounds (x, yy, 22, 24);  x += 24;
            z.ed[1].setBounds (x, yy, 64, 24);   x += 90;
            z.lbl[2].setBounds (x, yy, 26, 24);  x += 28;
            z.ed[2].setBounds (x, yy, 54, 24);   x += 58;
            z.lbl[3].setBounds (x, yy, 22, 24);  x += 24;
            z.ed[3].setBounds (x, yy, 54, 24);
            y += 62;
        }
    }

private:
    void refreshPresetList()
    {
        files = ClickConfig::listPresets();
        const auto def = ClickConfig::defaultName();
        presetBox.clear (juce::dontSendNotification);
        int selectedIdx = -1;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto stem = files[i].getFileNameWithoutExtension();
            presetBox.addItem (stem + (stem.equalsIgnoreCase (def) ? "   (Default)" : ""), i + 1);
            if (stem.equalsIgnoreCase (proc.clickPresetName)) selectedIdx = i;
        }
        if (selectedIdx >= 0) presetBox.setSelectedItemIndex (selectedIdx, juce::dontSendNotification);
    }

    void refreshControls()
    {
        const auto& c = proc.clickConfig;
        newTypeBox.setSelectedItemIndex (juce::jmax (0, titq::typeNames().indexOf (c.newBandType)), juce::dontSendNotification);
        newSlope.setValue (c.newBandSlope, juce::dontSendNotification);
        bandBox.setSelectedItemIndex (juce::jmax (0, ClickConfig::bandClickActions().indexOf (c.bandDoubleClick)), juce::dontSendNotification);
        gainClick.setToggleState (c.gainFromClick, juce::dontSendNotification);
        wheelOn.setToggleState (c.wheelQ, juce::dontSendNotification);
        wheelSpeed.setValue (c.wheelQSpeed, juce::dontSendNotification);
        for (int i = 0; i < ClickConfig::kNumZones; ++i)
        {
            auto& z = rows[(size_t) i];
            const auto& d = c.zones[i];
            z.action.setSelectedItemIndex (juce::jmax (0, ClickConfig::zoneActions().indexOf (d.action)), juce::dontSendNotification);
            z.clicks.setSelectedId (d.clicks == 2 ? 2 : 1, juce::dontSendNotification);
            z.slope.setValue (d.slope, juce::dontSendNotification);
            z.ed[0].setText (juce::String (d.minHz, 0), false);
            z.ed[1].setText (juce::String (d.maxHz, 0), false);
            z.ed[2].setText (juce::String (d.minDb, 1), false);
            z.ed[3].setText (juce::String (d.maxDb, 1), false);
        }
    }

    struct ZoneRow
    {
        juce::Label name;
        juce::ComboBox action, clicks;
        juce::Slider slope;
        juce::Label lbl[4];
        juce::TextEditor ed[4];
    };

    TitQProcessor& proc;
    juce::Array<juce::File> files;

    juce::ComboBox presetBox, newTypeBox, bandBox;
    juce::TextButton defaultBtn, saveBtn, deleteBtn, folderBtn;
    juce::TextEditor nameEd;
    juce::Slider newSlope, wheelSpeed;
    juce::ToggleButton gainClick, wheelOn;
    juce::Label lblType, lblSlope, lblBand, lblWheel, lblZones;
    std::array<ZoneRow, ClickConfig::kNumZones> rows;
};
} // namespace

// =====================================================================================================

// Rótulos: caja casi transparente (se ve el fondo), borde rosa fino y letra clara con una sombra suave para que se lea cómodo.
void TitQEditor::TitLaf::drawLabel (juce::Graphics& g, juce::Label& l)
{
    g.fillAll (l.findColour (juce::Label::backgroundColourId).withAlpha (0.f));
    if (l.isBeingEdited()) return;

    const auto b = l.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff10040b).withAlpha (0.30f));
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (kPink.withAlpha (0.55f));
    g.drawRoundedRectangle (b, 4.f, 1.f);

    const auto area = l.getLocalBounds().reduced (3, 0);
    const juce::Font font = getLabelFont (l).boldened();
    g.setFont (font);
    g.setColour (juce::Colours::black.withAlpha (0.85f));                 // sombra
    g.drawFittedText (l.getText(), area.translated (1, 1), l.getJustificationType(), 1, l.getMinimumHorizontalScale());
    g.setColour (juce::Colour (0xfffff0f8));                              // letra clara
    g.drawFittedText (l.getText(), area, l.getJustificationType(), 1, l.getMinimumHorizontalScale());
}

void TitQEditor::TitLaf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool highlighted, bool)
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
    if (on)
    {
        g.setColour (kPink);
        g.fillRoundedRectangle (box.reduced (3.f), 2.f);
    }

    const auto area = juce::Rectangle<int> ((int) (box.getRight() + 6.f), 0, juce::jmax (0, (int) (b.getRight() - box.getRight() - 8.f)), btn.getHeight());
    g.setFont (juce::Font (juce::FontOptions (b.getHeight() * 0.5f, juce::Font::bold)));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawFittedText (btn.getButtonText(), area.translated (1, 1), juce::Justification::centredLeft, 1);
    g.setColour (juce::Colour (0xfffff0f8));
    g.drawFittedText (btn.getButtonText(), area, juce::Justification::centredLeft, 1);
}

TitQEditor::TitLaf::TitLaf()
{
    const auto dark = juce::Colour (0xff14060f);
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1b0a14));
    setColour (juce::Slider::rotarySliderFillColourId, kPink);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff4a1530));
    setColour (juce::Slider::thumbColourId, kPink2);
    setColour (juce::Slider::trackColourId, kPink);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxBackgroundColourId, dark.withAlpha (0.6f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kPink2);
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
    setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    setColour (juce::ToggleButton::tickColourId, kPink);
    setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff6a2a4a));
    setColour (juce::TextEditor::backgroundColourId, dark);
    setColour (juce::TextEditor::textColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId, kPink);
    setColour (juce::TextEditor::focusedOutlineColourId, kPink2);
}

TitQEditor::TitQEditor (TitQProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    preDb.fill (-120.f); postDb.fill (-120.f); scratchDb.fill (-120.f);

    // ---- globales ----
    osBox.addItemList (juce::StringArray { "OS: Off", "OS: 2x", "OS: 4x", "OS: 8x" }, 1);
    addAndMakeVisible (osBox);
    osAtt = std::make_unique<ComboAtt> (proc.apvts, "os", osBox);

    {   // selector de escala de dB (esquina superior derecha): ±3 ... ±12 dB
        const juce::String pm { juce::CharPointer_UTF8 ("\xc2\xb1") };
        for (int v = 3; v <= 12; ++v) dbBox.addItem (pm + juce::String (v) + " dB", v - 2);
        addAndMakeVisible (dbBox);
        dbAtt = std::make_unique<ComboAtt> (proc.apvts, "dbrange", dbBox);
    }

    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
    addAndMakeVisible (outSlider);
    outLabel.setText ("Out", juce::dontSendNotification);
    addAndMakeVisible (outLabel);
    outAtt = std::make_unique<SliderAtt> (proc.apvts, "out", outSlider);

    settingsBtn.onClick = [this] { openSettings(); };
    addAndMakeVisible (settingsBtn);

    // ---- presets del ecualizador (estado completo de las 10 bandas) ----
    addAndMakeVisible (eqPresetBox); addAndMakeVisible (eqSaveBtn); addAndMakeVisible (eqDelBtn);
    eqPresetBox.setTextWhenNothingSelected ("EQ presets");
    eqPresetBox.onChange = [this] { if (eqPresetBox.getSelectedId() > 0) loadEqPreset (eqPresetBox.getText()); };
    eqSaveBtn.onClick = [this] { askSaveEqPreset(); };
    eqDelBtn.onClick  = [this] { askDeleteEqPreset(); };
    addAndMakeVisible (eqMenuBtn);
    eqMenuBtn.onClick = [this] { showPresetMenu(); };
    refreshEqPresets();

    // ---- selector de banda ----
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& b = bandButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (b);
    }
    bandButtons[0].setToggleState (true, juce::dontSendNotification);

    typeBox.addItemList (typeNames(), 1);
    scSrcBox.addItemList (juce::StringArray { "SC: Internal", "SC: External" }, 1);
    dirBox.addItemList (juce::StringArray { "Above threshold", "Below threshold" }, 1);
    chBox.addItemList (juce::StringArray { "Stereo (L+R)", "Mid", "Side", "Left only", "Right only" }, 1);
    addAndMakeVisible (chBox);
    addAndMakeVisible (typeBox); addAndMakeVisible (onBtn); addAndMakeVisible (soloBtn); addAndMakeVisible (dynBtn);
    addAndMakeVisible (scSrcBox); addAndMakeVisible (dirBox); addAndMakeVisible (freeBtn);
    deleteBtn.onClick = [this] { deleteBand (selected); };
    addAndMakeVisible (deleteBtn);

    scWarn.setText ("No external sidechain input connected", juce::dontSendNotification);
    scWarn.setColour (juce::Label::textColourId, juce::Colour (0xffffb040));
    scWarn.setJustificationType (juce::Justification::centred);
    scWarn.setMinimumHorizontalScale (1.0f);
    addChildComponent (scWarn);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 16);
        addAndMakeVisible (k.s);
        k.l.setText (kKnobNames[i], juce::dontSendNotification);
        k.l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.l);
    }

    selectBand (0);

    setResizable (true, true);
    setResizeLimits (880, 491, 1376, 768);
    getConstrainer()->setFixedAspectRatio (1376.0 / 768.0);
    setSize (1100, 614);
    startTimerHz (60);
}

TitQEditor::~TitQEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::File TitQEditor::eqPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TIT Q").getChildFile ("EQ Presets");
}

// Un preset es un archivo de texto (.titq) con TODO: las 10 bandas, el nivel de salida y los ajustes de Settings.
// No guarda rutas del PC: se puede copiar a otro ordenador y cargar igual.
std::unique_ptr<juce::XmlElement> TitQEditor::makePresetXml() const
{
    auto xml = proc.apvts.copyState().createXml();
    if (xml == nullptr) return nullptr;
    xml->createNewChildElement ("ClickSettings")->addTextElement (juce::JSON::toString (proc.clickConfig.toVar()));
    return xml;
}

bool TitQEditor::applyPresetXml (juce::XmlElement xml)
{
    if (! xml.hasTagName (proc.apvts.state.getType())) return false;
    if (auto* e = xml.getChildByName ("ClickSettings"))
    {
        proc.clickConfig = ClickConfig::fromVar (juce::JSON::parse (e->getAllSubText()));
        xml.removeChildElement (e, true);
    }
    proc.apvts.replaceState (juce::ValueTree::fromXml (xml));
    selectBand (0);
    return true;
}

void TitQEditor::refreshEqPresets()
{
    eqPresetBox.clear (juce::dontSendNotification);
    auto files = eqPresetDir().findChildFiles (juce::File::findFiles, false, "*.titq");
    files.sort();
    int id = 1;
    for (const auto& f : files) eqPresetBox.addItem (f.getFileNameWithoutExtension(), id++);
}

void TitQEditor::loadEqPreset (const juce::String& name)
{
    if (auto xml = juce::XmlDocument::parse (eqPresetDir().getChildFile (name + ".titq")))
        applyPresetXml (*xml);
}

void TitQEditor::saveEqPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return;
    auto dir = eqPresetDir();
    dir.createDirectory();
    if (auto xml = makePresetXml())
        xml->writeTo (dir.getChildFile (name + ".titq"));
    refreshEqPresets();
    for (int i = 0; i < eqPresetBox.getNumItems(); ++i)
        if (eqPresetBox.getItemText (i).equalsIgnoreCase (name)) eqPresetBox.setSelectedItemIndex (i, juce::dontSendNotification);
}

void TitQEditor::askSaveEqPreset()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", eqPresetBox.getText(), "");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TitQEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result == 1 && safe != nullptr) safe->saveEqPreset (aw->getTextEditorContents ("name"));
    }), true);
}

void TitQEditor::askDeleteEqPreset()
{
    const auto name = eqPresetBox.getText();
    if (eqPresetBox.getSelectedId() <= 0 || name.isEmpty()) return;
    juce::Component::SafePointer<TitQEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Delete preset").withMessage ("Delete the preset \"" + name + "\"?")
                                      .withButton ("Delete").withButton ("Cancel"),
                                  [safe, name] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        eqPresetDir().getChildFile (name + ".titq").deleteFile();
        safe->refreshEqPresets();
        safe->eqPresetBox.setSelectedId (0, juce::dontSendNotification);
    });
}

// Menu "...": guardar el preset en cualquier sitio (pendrive, nube...) o cargar uno de otro PC
void TitQEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save preset to a file...");
    m.addItem (2, "Load preset from a file...");
    m.addItem (3, "Open the presets folder");
    juce::Component::SafePointer<TitQEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (eqMenuBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 3) { eqPresetDir().createDirectory(); eqPresetDir().revealToUser(); return; }
        const bool saving = (r == 1);
        safe->chooser = std::make_unique<juce::FileChooser> (saving ? "Save preset" : "Load preset", eqPresetDir(), "*.titq");
        const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting)
                                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        safe->chooser->launchAsync (flags, [safe, saving] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe == nullptr || f == juce::File()) return;
            if (saving)
            {
                if (auto xml = safe->makePresetXml()) xml->writeTo (f.withFileExtension ("titq"));
            }
            else if (auto xml = juce::XmlDocument::parse (f))
                safe->applyPresetXml (*xml);
        });
    });
}

void TitQEditor::openSettings()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<SettingsPanel> (proc), settingsBtn.getBounds(), this);
}

void TitQEditor::selectBand (int b)
{
    selected = b;
    bandButtons[(size_t) b].setToggleState (true, juce::dontSendNotification);

    typeAtt.reset(); onAtt.reset(); soloAtt.reset(); dynAtt.reset(); scSrcAtt.reset(); dirAtt.reset(); freeAtt.reset(); chAtt.reset();
    typeAtt  = std::make_unique<ComboAtt>  (proc.apvts, pid (b, "type"),   typeBox);
    onAtt    = std::make_unique<ButtonAtt> (proc.apvts, pid (b, "on"),     onBtn);
    soloAtt  = std::make_unique<ButtonAtt> (proc.apvts, pid (b, "solo"),   soloBtn);
    dynAtt   = std::make_unique<ButtonAtt> (proc.apvts, pid (b, "dyn"),    dynBtn);
    scSrcAtt = std::make_unique<ComboAtt>  (proc.apvts, pid (b, "scsrc"),  scSrcBox);
    dirAtt   = std::make_unique<ComboAtt>  (proc.apvts, pid (b, "dyndir"), dirBox);
    freeAtt  = std::make_unique<ButtonAtt> (proc.apvts, pid (b, "scfree"), freeBtn);
    chAtt    = std::make_unique<ComboAtt>  (proc.apvts, pid (b, "chmode"), chBox);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        knobs[(size_t) i].att.reset();
        knobs[(size_t) i].att = std::make_unique<SliderAtt> (proc.apvts, pid (b, kKnobIds[i]), knobs[(size_t) i].s);
    }
    lastTypeSeen = (int) std::lround (proc.apvts.getRawParameterValue (pid (b, "type"))->load());
    updateEnabledStates();
    repaint();
}

void TitQEditor::updateEnabledStates()
{
    const bool used = isBandUsed (selected);
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (selected, "type"))->load());
    const bool dynOn = proc.apvts.getRawParameterValue (pid (selected, "dyn"))->load() > 0.5f;
    const bool isCut = (type == LowCut || type == HighCut);
    const bool freeOn = isFreeScOn (selected);

    auto en = [] (juce::Component& c, bool e) { c.setEnabled (e); c.setAlpha (e ? 1.0f : 0.28f); };

    // una banda no creada no se edita: se crea con doble clic en la gráfica
    en (typeBox, used); en (onBtn, used); en (soloBtn, used); en (deleteBtn, used);
    en (knobs[kFreq].s,   used);
    en (knobs[kGain].s,   used && type != BOOBTEC && ! isCut);
    // Q manual solo en las curvas que la usan; AP TIT (Q automática), BIG TIT y TIT SKY (Q fija) quedan bloqueadas; SL TIT va acotada
    {   // SL TIT: la ruleta de Q recorre exactamente el rango permitido de la consola (tope izquierdo = Q minima, tope derecho = Q maxima)
        auto& q = knobs[kQ].s;
        if (type == SLTIT) { q.lo = titq::kSltitQMin; q.hi = titq::kSltitQMax; }
        else               { q.lo = -1.0; q.hi = -1.0; }
        if (q.limited() && (q.getValue() < q.lo || q.getValue() > q.hi))
            q.setValue (juce::jlimit (q.lo, q.hi, q.getValue()), juce::sendNotificationSync);
        q.repaint();
    }
    en (knobs[kQ].s,      used && (type == Bell || type == LowShelf || type == HighShelf || type == TiltShelf || type == SLTIT));
    en (knobs[kSlope].s,  used && (type == LowShelf || type == HighShelf || isCut));
    en (knobs[kBoost].s,  used && type == BOOBTEC);
    en (knobs[kAtten].s,  used && type == BOOBTEC);
    en (dynBtn, used && ! isCut);
    en (knobs[kRange].s, used && dynOn && ! isCut && type != BOOBTEC);
    for (int i : { kThr, kRatio, kAttack, kRelease, kKnee })
        en (knobs[(size_t) i].s, used && dynOn && ! isCut);
    en (scSrcBox, used && dynOn && ! isCut);
    en (dirBox,   used && dynOn && ! isCut);
    en (chBox,    used);
    en (freeBtn,  used && dynOn && ! isCut);
    en (knobs[kScFreq].s, used && dynOn && ! isCut && freeOn);
    en (knobs[kScQ].s,    used && dynOn && ! isCut && freeOn);

    const bool extSel = proc.apvts.getRawParameterValue (pid (selected, "scsrc"))->load() > 0.5f;
    scWarn.setVisible (used && dynOn && ! isCut && extSel && ! proc.extScActive.load());

    for (int b = 0; b < kNumBands; ++b)
        bandButtons[(size_t) b].setAlpha (isBandUsed (b) ? 1.0f : 0.45f);
}

bool TitQEditor::isBandUsed (int b) const
{
    return proc.apvts.getRawParameterValue (pid (b, "used"))->load() > 0.5f;
}
bool TitQEditor::isBandOn (int b) const
{
    return isBandUsed (b) && proc.apvts.getRawParameterValue (pid (b, "on"))->load() > 0.5f;
}
bool TitQEditor::isFreeScOn (int b) const
{
    return proc.apvts.getRawParameterValue (pid (b, "scfree"))->load() > 0.5f;
}
bool TitQEditor::isDynActive (int b) const
{
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (b, "type"))->load());
    return isBandOn (b)
        && proc.apvts.getRawParameterValue (pid (b, "dyn"))->load() > 0.5f
        && type != LowCut && type != HighCut;
}
int TitQEditor::firstFreeBand() const
{
    for (int b = 0; b < kNumBands; ++b)
        if (! isBandUsed (b)) return b;
    return -1;
}
float TitQEditor::dbRange() const
{
    return 3.f + (float) std::lround (proc.apvts.getRawParameterValue ("dbrange")->load());   // ±3 ... ±12
}

// ---------- crear / borrar bandas ----------
void TitQEditor::setParamGesture (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void TitQEditor::resetBandParams (int b)
{
    static const char* ids[] = { "on", "type", "freq", "gain", "q", "slope", "boost", "atten", "solo", "dyn", "dyndir", "range",
                                 "thr", "ratio", "attack", "release", "knee", "scsrc", "scfree", "scfreq", "scq", "used" };
    for (auto* id : ids)
        if (auto* p = proc.apvts.getParameter (pid (b, id)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

int TitQEditor::createBand (float freq, float gainDb, const juce::String& typeName, float slope)
{
    const int b = firstFreeBand();                 // asignación secuencial: la primera banda libre
    if (b < 0) return -1;                          // las 10 bandas están en uso

    resetBandParams (b);
    const int ti = juce::jmax (0, typeNames().indexOf (typeName));
    setParamGesture (pid (b, "type"), (float) ti);
    setParamGesture (pid (b, "freq"), freq);
    setParamGesture (pid (b, "gain"), gainDb);
    setParamGesture (pid (b, "slope"), slope);
    setParamGesture (pid (b, "q"), (float) proc.clickConfig.newBandQ);
    setParamGesture (pid (b, "on"), 1.f);
    setParamGesture (pid (b, "used"), 1.f);        // al final: el audio nunca ve una banda a medio configurar
    selectBand (b);
    return b;
}

void TitQEditor::deleteBand (int b)
{
    if (! isBandUsed (b)) return;
    resetBandParams (b);                           // vuelve a quedar libre (used = false)
    updateEnabledStates();
}

void TitQEditor::timerCallback()
{
    // al elegir TIT SKY (9107.48 Hz) o BOOBTEC (10.3 Hz) en una banda, la frecuencia arranca en ese punto (después se puede mover)
    if (isBandUsed (selected))
    {
        const int tNow = (int) std::lround (proc.apvts.getRawParameterValue (pid (selected, "type"))->load());
        if (lastTypeSeen >= 0 && tNow != lastTypeSeen && tNow == TITSKY)
            setParamGesture (pid (selected, "freq"), (float) kTitSkyStartHz);
        else if (lastTypeSeen >= 0 && tNow != lastTypeSeen && tNow == BOOBTEC)
            setParamGesture (pid (selected, "freq"), (float) kBoobtecStartHz);
        lastTypeSeen = tNow;
    }

    // la curva se evalúa a la frecuencia de muestreo real del filtrado (con oversampling), igual que el audio
    const int osIdx = juce::jlimit (0, 3, (int) std::lround (proc.apvts.getRawParameterValue ("os")->load()));
    viewFs = proc.currentSampleRate.load() * (double) (1 << osIdx);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto s = proc.readBandSettings (b);
        const bool dynOn = isDynActive (b);
        const double range = (double) proc.apvts.getRawParameterValue (pid (b, "range"))->load();
        const double amount = (double) proc.dynAmount[(size_t) b].load();

        BandSettings live, still;
        const double scLive  = applyDynamics (s, dynOn, range, amount, live);
        const double scStill = applyDynamics (s, dynOn, range, 0.0,    still);
        viewBands[(size_t) b].design (live,  viewFs, scLive);
        ghostBands[(size_t) b].design (still, viewFs, scStill);
    }

    // analizador FFT a 30 Hz con suavizado (sube rápido, baja más despacio)
    if ((++tick & 1) == 0)
    {
        proc.analyzerPre.computeDb (scratchDb);
        for (size_t i = 0; i < preDb.size(); ++i)
            preDb[i] += (scratchDb[i] > preDb[i] ? 0.6f : 0.15f) * (scratchDb[i] - preDb[i]);
        proc.analyzerPost.computeDb (scratchDb);
        for (size_t i = 0; i < postDb.size(); ++i)
            postDb[i] += (scratchDb[i] > postDb[i] ? 0.6f : 0.15f) * (scratchDb[i] - postDb[i]);
    }

    updateEnabledStates();
    repaint (displayArea().toNearestInt().expanded (24));
}

// ---------- geometría de la gráfica ----------
juce::Rectangle<float> TitQEditor::displayArea() const
{
    const float s = (float) scale;
    return { 40.f * s, 50.f * s, 1020.f * s, 240.f * s };
}
juce::Rectangle<float> TitQEditor::scStrip() const
{
    const float s = (float) scale;
    const auto d = displayArea();
    return { d.getX(), d.getBottom() - 36.f * s, d.getWidth(), 16.f * s };
}
float TitQEditor::freqToX (float f) const   // 20 Hz ... 21 kHz, escala logarítmica
{
    const auto a = displayArea();
    return a.getX() + a.getWidth() * std::log (juce::jlimit (20.f, kMaxFreq, f) / 20.f) / std::log (kMaxFreq / 20.f);
}
float TitQEditor::xToFreq (float x) const
{
    const auto a = displayArea();
    return 20.f * std::pow (kMaxFreq / 20.f, juce::jlimit (0.f, 1.f, (x - a.getX()) / a.getWidth()));
}
float TitQEditor::dbToY (float db) const    // simétrico: +R arriba, -R abajo
{
    const auto a = displayArea();
    const float r = dbRange();
    return a.getCentreY() - juce::jlimit (-r, r, db) / r * (a.getHeight() * 0.5f);
}
float TitQEditor::yToDb (float y) const     // el tope de la escala limita el arrastre
{
    const auto a = displayArea();
    const float r = dbRange();
    return juce::jlimit (-r, r, (a.getCentreY() - y) / (a.getHeight() * 0.5f) * r);
}
bool TitQEditor::nodeIsDraggableInGain (int type) const
{
    return type != LowCut && type != HighCut && type != BOOBTEC;
}
juce::Point<float> TitQEditor::nodePosition (int b) const
{
    const auto s = proc.readBandSettings (b);
    float db = 0.f;
    if (s.type == BOOBTEC)                     db = (s.boost - s.atten) * (float) kBoobtecDbPerStep;
    else if (nodeIsDraggableInGain (s.type))  db = s.gain;
    return { freqToX (s.freq), dbToY (db) };
}
int TitQEditor::findNode (juce::Point<float> p) const    // solo bandas creadas
{
    int hit = -1;
    float best = 14.f * (float) scale;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! isBandUsed (b)) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < best) { best = d; hit = b; }
    }
    return hit;
}
void TitQEditor::setParam (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}
void TitQEditor::toggleParam (const juce::String& id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.f : 1.f);
        p->endChangeGesture();
    }
}

// ---------- ratón ----------
void TitQEditor::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;
    dragSc = false;

    // clic derecho sobre una banda: la borra
    if (e.mods.isPopupMenu())
    {
        const int hit = findNode (e.position);
        if (hit >= 0) deleteBand (hit);
        return;
    }

    // barra inferior del sidechain libre: botón On/Off y arrastre de la frecuencia de escucha
    if (isBandUsed (selected))
    {
        if (scStrip().contains (e.position) && isFreeScOn (selected))
        {
            dragSc = true;
            proc.apvts.getParameter (pid (selected, "scfreq"))->beginChangeGesture();
            setParam (pid (selected, "scfreq"), xToFreq (e.position.x));
            return;
        }
    }

    // nodos de las bandas creadas (también las apagadas, para poder seleccionarlas)
    const int hit = findNode (e.position);
    if (hit < 0 && displayArea().contains (e.position))
    {
        // UN SOLO CLIC en una zona vacía: crea una banda en la primera posición libre (1..10) y la deja lista para arrastrar
        const int nb = createNewBandAt (e.position);
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
        if (isBandOn (hit))   // una banda apagada se selecciona pero no se arrastra
        {
            dragBand = hit;
            proc.apvts.getParameter (pid (hit, "freq"))->beginChangeGesture();
            proc.apvts.getParameter (pid (hit, "gain"))->beginChangeGesture();
        }
    }
}

void TitQEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSc) { setParam (pid (selected, "scfreq"), xToFreq (e.position.x)); return; }
    if (dragBand < 0) return;
    setParam (pid (dragBand, "freq"), xToFreq (e.position.x));
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (dragBand, "type"))->load());
    if (nodeIsDraggableInGain (type))
        setParam (pid (dragBand, "gain"), yToDb (e.position.y));
}

void TitQEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragSc)
        proc.apvts.getParameter (pid (selected, "scfreq"))->endChangeGesture();
    if (dragBand >= 0)
    {
        proc.apvts.getParameter (pid (dragBand, "freq"))->endChangeGesture();
        proc.apvts.getParameter (pid (dragBand, "gain"))->endChangeGesture();
    }
    dragBand = -1;
    dragSc = false;
}

void TitQEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    // el segundo clic de un doble clic que acaba de crear una banda no debe apagarla
    if (juce::Time::getMillisecondCounter() - lastCreateMs < 600) return;
    const auto& cfg = proc.clickConfig;
    const int hit = findNode (e.position);
    if (hit < 0)
    {
        if (displayArea().contains (e.position))
        {
            const int zi = zoneAt (e.position);
            if (zi >= 0 && cfg.zones[zi].clicks == 2 && createZoneBand (e.position, zi) >= 0)
                lastCreateMs = juce::Time::getMillisecondCounter();
        }
        return;
    }
    if (cfg.bandDoubleClick == "ToggleOnOff")  toggleParam (pid (hit, "on"));
    else if (cfg.bandDoubleClick == "Delete")  deleteBand (hit);
}

int TitQEditor::zoneAt (juce::Point<float> pos) const
{
    const auto& cfg = proc.clickConfig;
    const float f = xToFreq (pos.x), db = yToDb (pos.y);
    for (int i = 0; i < ClickConfig::kNumZones; ++i)
    {
        const auto& z = cfg.zones[i];
        if (z.action != "None" && f >= z.minHz && f <= z.maxHz && db >= z.minDb && db <= z.maxDb) return i;
    }
    return -1;
}

int TitQEditor::createNewBandAt (juce::Point<float> pos)
{
    const auto& cfg = proc.clickConfig;
    const int zi = zoneAt (pos);
    if (zi >= 0 && cfg.zones[zi].clicks == 2) return -1;      // zona de doble clic: no se crea con un solo clic
    return createZoneBand (pos, zi);
}

int TitQEditor::createZoneBand (juce::Point<float> pos, int zi)
{
    const auto& cfg = proc.clickConfig;
    const float f = xToFreq (pos.x);
    juce::String typeName = cfg.newBandType;
    float slope = (float) cfg.newBandSlope;

    if (zi >= 0)
    {
        typeName = ClickConfig::typeForAction (cfg.zones[zi].action);
        slope = (float) cfg.zones[zi].slope;
    }

    const int ti = typeNames().indexOf (typeName);
    const bool isCut = (ti == LowCut || ti == HighCut);
    const float gain = (cfg.gainFromClick && ! isCut && ti != BOOBTEC) ? yToDb (pos.y) : 0.f;
    return createBand (f, gain, typeName, slope);
}

void TitQEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // rueda sobre la barra del sidechain libre: ajusta la Q (anchura) de la banda de escucha
    if (isBandUsed (selected) && scStrip().contains (e.position) && isFreeScOn (selected))
    {
        const float q = proc.apvts.getRawParameterValue (pid (selected, "scq"))->load();
        setParam (pid (selected, "scq"), q * std::exp (w.deltaY * 1.5f));
        return;
    }

    // rueda sobre un nodo de banda: cambia su Q
    const auto& cfg = proc.clickConfig;
    if (! cfg.wheelQ) return;
    const int hit = findNode (e.position);
    if (hit < 0 || ! isBandOn (hit)) return;
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (pid (hit, "type"))->load());
    if (! (type == Bell || type == LowShelf || type == HighShelf || type == TiltShelf || type == SLTIT)) return;

    auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (pid (hit, "q")));
    if (p == nullptr) return;
    auto r = p->getNormalisableRange();
    double lo = r.start, hi = r.end;
    if (type == SLTIT) { lo = titq::kSltitQMin; hi = titq::kSltitQMax; }
    const float q = proc.apvts.getRawParameterValue (pid (hit, "q"))->load();
    const float nq = (float) juce::jlimit (lo, hi, (double) q * std::exp (w.deltaY * 1.5 * cfg.wheelQSpeed));
    if (hit != selected) selectBand (hit);
    setParamGesture (pid (hit, "q"), nq);
}

// ---------- analizador ----------
void TitQEditor::buildSpectrumPaths (const std::array<float, SpectrumAnalyzer::kBins>& db,
                                     juce::Path& line, juce::Path& fill) const
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
        if (f0 >= 0.5 * fs) break;                         // por encima de Nyquist no hay datos
        const double b0 = f0 / binHz, b1 = f1 / binHz;

        float v;
        if (b1 - b0 < 1.0)                                 // graves: interpolar entre bins
        {
            const double b = 0.5 * (b0 + b1);
            const int i = juce::jlimit (0, SpectrumAnalyzer::kBins - 2, (int) b);
            const float fr = (float) (b - i);
            v = db[(size_t) i] * (1.f - fr) + db[(size_t) i + 1] * fr;
        }
        else                                               // agudos: máximo de los bins del píxel
        {
            const int i0 = juce::jmax (0, (int) b0);
            const int i1 = juce::jmin (SpectrumAnalyzer::kBins - 1, (int) std::ceil (b1));
            v = -200.f;
            for (int i = i0; i <= i1; ++i) v = juce::jmax (v, db[(size_t) i]);
        }

        v += 4.5f * (float) std::log2 (0.5 * (f0 + f1) / 1000.0);     // inclinación 4.5 dB/oct
        const float t = juce::jlimit (0.f, 1.f, (v + 100.f) / 85.f);  // -100 dB ... -15 dB
        const float y = bottom - t * disp.getHeight();

        if (! started)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, bottom);
            fill.lineTo (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }
        lastX = x;
    }
    if (started)
    {
        fill.lineTo (lastX, bottom);
        fill.closeSubPath();
    }
}

// ---------- dibujo ----------
void TitQEditor::paint (juce::Graphics& g)
{
    // FONDO: la imagen del plugin (background.jpg)
    g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);

    const float s = (float) scale;
    const auto disp = displayArea();
    const float range = dbRange();

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (disp, 10.f * s);
    g.fillRoundedRectangle (juce::Rectangle<float> (30.f * s, 296.f * s, 1040.f * s, 310.f * s), 10.f * s);
    g.setColour (kPink.withAlpha (0.6f));
    g.drawRoundedRectangle (disp, 10.f * s, 1.2f);

    // analizador FFT transparente: entrada (blanco tenue) y salida (rosa tenue)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (disp.toNearestInt());

        juce::Path linePre, fillPre, linePost, fillPost;
        buildSpectrumPaths (preDb, linePre, fillPre);
        buildSpectrumPaths (postDb, linePost, fillPost);

        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillPath (fillPre);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (linePre, juce::PathStrokeType (1.f * s));

        g.setColour (kPink.withAlpha (0.16f));
        g.fillPath (fillPost);
        g.setColour (kPink2.withAlpha (0.55f));
        g.strokePath (linePost, juce::PathStrokeType (1.2f * s));
    }

    // rejilla de frecuencia (en cada número de la escala) y de dB (simétrica)
    for (int i = 0; i < kNumFreqLabels; ++i)
    {
        const float f = kFreqLabels[i];
        const bool strong = (f == 100.f || f == 1000.f || f == 10000.f);
        g.setColour (juce::Colours::white.withAlpha (strong ? 0.14f : 0.06f));
        g.drawVerticalLine ((int) freqToX (f), disp.getY(), disp.getBottom());
    }
    const float dbLines[] = { -range, -range * 0.5f, range * 0.5f, range };
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (float db : dbLines)
        g.drawHorizontalLine ((int) dbToY (db), disp.getX(), disp.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.drawHorizontalLine ((int) dbToY (0.f), disp.getX(), disp.getRight());

    g.setFont (10.f * s);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    for (float db : { -range, -range * 0.5f, 0.f, range * 0.5f, range })
        g.drawText (fmtDb (db), (int) disp.getX() + 4, (int) dbToY (db) - 13, 40, 12, juce::Justification::left);

    // escala de frecuencias inferior (hasta 21k, notación k desde 1k). 20k y 21k están muy juntos:
    // 20k se escribe a la izquierda de su línea y 21k a la derecha de la suya para que no se pisen.
    for (int i = 0; i < kNumFreqLabels; ++i)
    {
        const float f = kFreqLabels[i];
        const float x = freqToX (f);
        const float w = 30.f * s;
        const int y = (int) (disp.getBottom() - 15.f * s), h = (int) (13.f * s);
        if (f == 20000.f)
            g.drawText (fmtFreq (f), (int) (x - w - 1.f * s), y, (int) w, h, juce::Justification::centredRight);
        else if (f == 21000.f)
            g.drawText (fmtFreq (f), (int) (x + 2.f * s), y, (int) w, h, juce::Justification::centredLeft);
        else
            g.drawText (fmtFreq (f), (int) (x - w * 0.5f), y, (int) w, h, juce::Justification::centred);
    }

    // barra del sidechain libre
    if (isBandUsed (selected))
    {
        const auto strip = scStrip();
        const bool freeOn = isFreeScOn (selected);

        if (freeOn)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (strip, 3.f * s);
        }

        if (freeOn)
        {
            const double f  = (double) proc.apvts.getRawParameterValue (pid (selected, "scfreq"))->load();
            const double q  = (double) proc.apvts.getRawParameterValue (pid (selected, "scq"))->load();
            const double bwOct = 2.0 * std::asinh (1.0 / (2.0 * q)) / std::log (2.0);
            const float xLo = freqToX ((float) (f * std::pow (2.0, -bwOct * 0.5)));
            const float xHi = freqToX ((float) (f * std::pow (2.0, bwOct * 0.5)));
            const float xC  = freqToX ((float) f);

            g.setColour (kPink.withAlpha (0.5f));
            g.fillRoundedRectangle (juce::Rectangle<float> (xLo, strip.getY(), juce::jmax (4.f * s, xHi - xLo), strip.getHeight()), 3.f * s);
            g.setColour (kPink2);
            g.drawVerticalLine ((int) xC, strip.getY(), strip.getBottom());
            g.setColour (kPink2.withAlpha (0.35f));
            g.drawVerticalLine ((int) xC, disp.getY(), strip.getY());
        }
    }

    // curva total EN VIVO + curva "fantasma" con la dinámica en reposo (evaluadas a la frecuencia de muestreo real del filtrado)
    juce::Path curve, ghost;
    bool anyDyn = false;
    for (int b = 0; b < kNumBands; ++b) anyDyn = anyDyn || isDynActive (b);

    const int steps = juce::jmax (2, (int) (disp.getWidth() / 2));
    for (int i = 0; i <= steps; ++i)
    {
        const float x = disp.getX() + disp.getWidth() * (float) i / (float) steps;
        const double f = xToFreq (x);
        double db = 0.0, dbG = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            db  += viewBands[(size_t) b].magnitudeDb (f, viewFs);
            dbG += ghostBands[(size_t) b].magnitudeDb (f, viewFs);
        }
        const float y  = dbToY ((float) db);
        const float yG = dbToY ((float) dbG);
        if (i == 0) { curve.startNewSubPath (x, y); ghost.startNewSubPath (x, yG); }
        else        { curve.lineTo (x, y);          ghost.lineTo (x, yG); }
    }
    if (anyDyn)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (ghost, juce::PathStrokeType (1.2f * s));
    }
    g.setColour (kPink.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (7.f * s));
    g.setColour (kPink2);
    g.strokePath (curve, juce::PathStrokeType (2.2f * s));

    // marcadores de dinámica: del nodo estático a la ganancia que tiene ahora mismo la banda
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! isDynActive (b)) continue;
        const auto st = proc.readBandSettings (b);
        if (st.type == BOOBTEC) continue;
        const float amount = proc.dynAmount[(size_t) b].load();
        const float rangeDyn = proc.apvts.getRawParameterValue (pid (b, "range"))->load();
        const auto node = nodePosition (b);
        const float y1 = dbToY (st.gain + rangeDyn * amount);
        g.setColour (kPink2.withAlpha (0.85f));
        g.drawLine (node.x, node.y, node.x, y1, 2.f * s);
        g.fillEllipse (node.x - 3.5f * s, y1 - 3.5f * s, 7.f * s, 7.f * s);
    }

    // nodos: solo las bandas creadas (las apagadas se ven huecas y tenues; doble clic las enciende)
    for (int b = 0; b < kNumBands; ++b)
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

    // logo TIT Q en la esquina inferior derecha. La Q se dibuja a mano (anillo + cola) para que no se lea como una O.
    {
        const float fh = 30.f * s;
        const juce::Font font (juce::FontOptions (fh, juce::Font::bold));
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, "TIT", 0.f, 0.f);
        const float tw = ga.getBoundingBox (0, -1, true).getWidth();

        const float capH = fh * 0.72f;              // altura de las mayúsculas
        const float th = fh * 0.13f;                // grosor del trazo
        const float qW = capH * 0.92f, gap = 5.f * s;
        const float baseline = 596.f * s;           // más arriba que antes
        const float qR = 1056.f * s;                // mismo borde derecho que antes (la cola llega hasta ~1062)
        const float qL = qR - qW;
        const float top = baseline - capH;

        g.setColour (kPink);
        g.setFont (font);
        g.drawSingleLineText ("TIT", (int) (qL - gap - tw), (int) baseline);
        g.drawEllipse (qL + th * 0.5f, top + th * 0.5f, qW - th, capH - th, th);                       // anillo de la Q
        g.drawLine (qL + qW * 0.52f, top + capH * 0.66f, qR + 5.f * s, baseline + capH * 0.20f, th);   // cola de la Q
    }
}

void TitQEditor::resized()
{
    scale = getWidth() / 1100.0;
    auto S = [this] (int v) { return juce::roundToInt ((double) v * scale); };

    osBox.setBounds (S (40), S (12), S (100), S (26));
    outLabel.setBounds (S (160), S (12), S (36), S (26));
    outSlider.setBounds (S (196), S (12), S (260), S (26));
    settingsBtn.setBounds (S (470), S (12), S (90), S (26));
    eqPresetBox.setBounds (S (572), S (12), S (150), S (26));
    eqSaveBtn.setBounds   (S (726), S (12), S (62),  S (26));
    eqDelBtn.setBounds    (S (792), S (12), S (72),  S (26));
    eqMenuBtn.setBounds   (S (868), S (12), S (40),  S (26));
    dbBox.setBounds (S (950), S (12), S (110), S (26));   // selector de escala de dB: esquina superior derecha

    for (int i = 0; i < kNumBands; ++i)
        bandButtons[(size_t) i].setBounds (S (44 + i * 46), S (304), S (42), S (26));
    typeBox.setBounds (S (520), S (304), S (120), S (26));
    onBtn.setBounds   (S (652), S (304), S (56),  S (26));
    chBox.setBounds   (S (716), S (304), S (150), S (26));
    soloBtn.setBounds (S (712), S (304), S (62),  S (26));
    dynBtn.setBounds  (S (780), S (304), S (90),  S (26));
    deleteBtn.setBounds (S (880), S (304), S (80), S (26));

    // sidechain por banda
    scSrcBox.setBounds (S (790), S (364), S (140), S (26));
    dirBox.setBounds   (S (790), S (396), S (140), S (26));
    freeBtn.setBounds  (S (790), S (428), S (140), S (26));
    scWarn.setBounds   (S (940), S (364), S (120), S (60));

    // fila 1: freq gain q slope boost atten  — fila 2: range thr ratio attack release knee scfreq scq
    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int row = i < 6 ? 0 : 1;
        const int col = i < 6 ? i : i - 6;
        const int x = S (50 + col * 120);
        const int y = row == 0 ? S (362) : S (490);
        knobs[(size_t) i].l.setFont (juce::Font (juce::FontOptions (13.f * (float) scale)));
        knobs[(size_t) i].l.setBounds (x, y - S (20), S (90), S (18));
        knobs[(size_t) i].s.setBounds (x, y, S (90), S (100));
    }
}
