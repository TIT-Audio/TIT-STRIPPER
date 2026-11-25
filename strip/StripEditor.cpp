#include "StripEditor.h"
#include <BinaryData.h>

static const juce::Colour kPink  (0xffff2e9a);
static const juce::Colour kPink2 (0xffff7ac0);

void StripEditor::Laf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool highlighted, bool)
{
    const auto b = btn.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = btn.getToggleState();
    g.setColour (on ? kPink.withAlpha (highlighted ? 0.85f : 0.75f) : juce::Colour (0xff10040b).withAlpha (highlighted ? 0.55f : 0.40f));
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (kPink.withAlpha (on ? 1.0f : 0.6f));
    g.drawRoundedRectangle (b, 4.f, 1.f);
    g.setFont (juce::Font (juce::FontOptions (b.getHeight() * 0.5f, juce::Font::bold)));
    g.setColour (on ? juce::Colours::white : kPink2);
    g.drawFittedText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, 1);
}

StripEditor::Laf::Laf()
{
    const auto dark = juce::Colour (0xff14060f);
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1b0a14));
    setColour (juce::ComboBox::backgroundColourId, dark.withAlpha (0.62f));
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kPink);
    setColour (juce::ComboBox::arrowColourId, kPink);
    setColour (juce::PopupMenu::backgroundColourId, dark);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kPink);
    setColour (juce::TextButton::buttonColourId, dark.withAlpha (0.7f));
    setColour (juce::TextButton::buttonOnColourId, kPink);
    setColour (juce::TextButton::textColourOffId, kPink2);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId, dark);
    setColour (juce::AlertWindow::textColourId, juce::Colours::white);
    setColour (juce::AlertWindow::outlineColourId, kPink);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a0f1d));
    setColour (juce::TextEditor::textColourId, juce::Colours::white);
}

StripEditor::StripEditor (StripProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);

    for (int m = 0; m < 3; ++m)
    {
        auto& c = ctl[(size_t) m];
        c.name.setButtonText (StripProcessor::moduleName (m));
        c.name.setClickingTogglesState (false);
        c.name.onClick = [this, m] { select (m); };
        c.left.onClick  = [this, m] { moveModule (m, -1); };
        c.right.onClick = [this, m] { moveModule (m, +1); };
        c.onAtt = std::make_unique<ButtonAtt> (proc.apvts, StripProcessor::onId (m), c.on);
        for (auto* b : { (juce::Component*) &c.name, (juce::Component*) &c.left, (juce::Component*) &c.right, (juce::Component*) &c.on }) addAndMakeVisible (*b);

        panels[(size_t) m].reset (proc.module (m).createEditor());
        panels[(size_t) m]->setResizable (false, false);   // el tamano lo manda la cadena
        addChildComponent (*panels[(size_t) m]);
    }

    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0) loadPresetFile (presetDir().getChildFile (presetBox.getText() + ".titstrip"));
    };
    saveBtn.onClick = [this] { askSavePreset(); };
    menuBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetBox); addAndMakeVisible (saveBtn); addAndMakeVisible (menuBtn);
    refreshPresets();

    select (1);
    refreshOrder();

    setResizable (true, true);   // despues de anadir los modulos, para que el tirador quede por encima
    setResizeLimits (880, 530, 1376, 830);
    getConstrainer()->setFixedAspectRatio (1100.0 / (614.0 + kBar));
    setSize (1100, 614 + kBar);
    startTimerHz (30);
}

StripEditor::~StripEditor()
{
    stopTimer();
    for (auto& pn : panels) pn.reset();   // las ventanas de los modulos usan el LookAndFeel propio, no el nuestro
    setLookAndFeel (nullptr);
}

void StripEditor::select (int m)
{
    selected = juce::jlimit (0, 2, m);
    for (int i = 0; i < 3; ++i)
    {
        panels[(size_t) i]->setVisible (i == selected);
        ctl[(size_t) i].name.setToggleState (i == selected, juce::dontSendNotification);
        ctl[(size_t) i].name.setColour (juce::TextButton::buttonColourId, i == selected ? kPink : juce::Colour (0xff14060f).withAlpha (0.7f));
        ctl[(size_t) i].name.setColour (juce::TextButton::textColourOffId, i == selected ? juce::Colours::white : kPink2);
    }
    repaint();
}

void StripEditor::moveModule (int m, int dir)
{
    auto o = proc.currentOrder();
    int k = 0;
    while (k < 3 && o[(size_t) k] != m) ++k;
    const int k2 = k + dir;
    if (k >= 3 || k2 < 0 || k2 > 2) return;
    std::swap (o[(size_t) k], o[(size_t) k2]);
    proc.setOrder (o);
    refreshOrder();
}

void StripEditor::refreshOrder()
{
    const auto o = proc.currentOrder();
    if (o == shownOrder) return;
    shownOrder = o;
    resized();
}

void StripEditor::timerCallback()
{
    refreshOrder();
}

void StripEditor::paint (juce::Graphics& g)
{
    if (background.isValid()) g.drawImage (background, getLocalBounds().toFloat());
    else g.fillAll (juce::Colour (0xff1b0a14));
    const float s = (float) scale, h = (float) kBar * s;
    g.setColour (juce::Colour (0xff10040b).withAlpha (0.55f));
    g.fillRect (0.f, 0.f, (float) getWidth(), h);
    g.setColour (kPink.withAlpha (0.7f));
    g.drawLine (0.f, h, (float) getWidth(), h, 1.5f);

    g.setFont (juce::Font (juce::FontOptions (24.f * s, juce::Font::bold)));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawText ("TIT STRIPPER", juce::Rectangle<float> (15.f * s, 1.f * s, 210.f * s, h), juce::Justification::centredLeft);
    g.setColour (kPink);
    g.drawText ("TIT STRIPPER", juce::Rectangle<float> (14.f * s, 0.f, 210.f * s, h), juce::Justification::centredLeft);

    // flechitas entre modulos (sentido de la senal)
    g.setColour (kPink2.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (18.f * s, juce::Font::bold)));
    for (int k = 0; k < 2; ++k)
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), juce::Rectangle<float> ((230.f + 200.f * (float) (k + 1) - 24.f) * s, 0.f, 24.f * s, h), juce::Justification::centred);
}

void StripEditor::resized()
{
    scale = getWidth() / 1100.0;
    auto S = [this] (double v) { return juce::roundToInt (v * scale); };
    const int barH = S (kBar);
    const int y = S (11), hh = S (30);
    const auto o = proc.currentOrder();
    for (int k = 0; k < 3; ++k)
    {
        auto& c = ctl[(size_t) o[(size_t) k]];
        const double x = 230.0 + 200.0 * k;
        c.left.setBounds  (S (x),          y, S (24), hh);
        c.name.setBounds  (S (x + 26),     y, S (84), hh);
        c.right.setBounds (S (x + 112),    y, S (24), hh);
        c.on.setBounds    (S (x + 140),    y, S (40), hh);
        c.left.setEnabled (k > 0);
        c.right.setEnabled (k < 2);
    }
    presetBox.setBounds (S (846), y, S (130), hh);
    saveBtn.setBounds   (S (982), y, S (52), hh);
    menuBtn.setBounds   (S (1040), y, S (40), hh);
    for (auto& pn : panels) pn->setBounds (0, barH, getWidth(), getHeight() - barH);
}

// ---------------------------------------------------------------- presets
juce::File StripEditor::presetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TIT STRIPPER").getChildFile ("Presets");
}

void StripEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    auto files = presetDir().findChildFiles (juce::File::findFiles, false, "*.titstrip");
    files.sort();
    int id = 1;
    for (const auto& f : files) presetBox.addItem (f.getFileNameWithoutExtension(), id++);
}

void StripEditor::loadPresetFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName ("TITSTRIPPER"))
            proc.loadStateXml (*xml);
    refreshOrder();
}

void StripEditor::savePreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return;
    auto dir = presetDir();
    dir.createDirectory();
    if (auto xml = proc.createStateXml())
        xml->writeTo (dir.getChildFile (name + ".titstrip"));
    refreshPresets();
}

void StripEditor::askSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "", "");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<StripEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result == 1 && safe != nullptr) safe->savePreset (aw->getTextEditorContents ("name"));
    }), true);
}

// Menu "...": guardar / cargar en cualquier sitio (pendrive, nube...), carpeta de presets y estado inicial
void StripEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save preset to a file...");
    m.addItem (2, "Load preset from a file...");
    m.addItem (3, "Open the presets folder");
    m.addSeparator();
    m.addItem (4, "Initialize (default settings)");
    juce::Component::SafePointer<StripEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 3) { presetDir().createDirectory(); presetDir().revealToUser(); return; }
        if (r == 4) { safe->proc.resetToDefaults(); safe->refreshOrder(); return; }
        const bool saving = (r == 1);
        safe->chooser = std::make_unique<juce::FileChooser> (saving ? "Save preset" : "Load preset", presetDir(), "*.titstrip");
        const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting)
                                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        safe->chooser->launchAsync (flags, [safe, saving] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe == nullptr || f == juce::File()) return;
            if (saving) { if (auto xml = safe->proc.createStateXml()) xml->writeTo (f.withFileExtension ("titstrip")); }
            else safe->loadPresetFile (f);
        });
    });
}
