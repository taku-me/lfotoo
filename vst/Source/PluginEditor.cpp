#include "PluginEditor.h"

TurntableLfoEditor::TurntableLfoEditor (TurntableLfoProcessor& p)
    : juce::AudioProcessorEditor (&p), processor_ (p)
{
    titleLabel_.setText ("TURNTABLE LFO", juce::dontSendNotification);
    titleLabel_.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel_);

    addKnob (rpmA_,      "rpmA",      "RPM A");
    addKnob (rpmB_,      "rpmB",      "RPM B");
    addKnob (threshold_, "threshold", "Threshold");
    addKnob (hyst_,      "hyst",      "Hysteresis");
    addKnob (stopthr_,   "stopthr",   "Stop Thr");
    addKnob (glide_,     "glide",     "Glide");
    addKnob (depth_,     "depth",     "Depth");
    addKnob (mix_,       "mix",       "Mix");

    modeBox_.addItem ("Internal (audio)", 1);
    modeBox_.addItem ("Loaded (.json)",   2);
    addAndMakeVisible (modeBox_);
    modeAttach_ = std::make_unique<APVTS::ComboBoxAttachment> (processor_.apvts, "mode", modeBox_);

    addAndMakeVisible (tapeButton_);
    tapeAttach_ = std::make_unique<APVTS::ButtonAttachment> (processor_.apvts, "tapestop", tapeButton_);

    loadButton_.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton_);

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    rpmLabel_.setJustificationType (juce::Justification::centredRight);
    rpmLabel_.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    addAndMakeVisible (statusLabel_);
    addAndMakeVisible (rpmLabel_);

    setSize (560, 360);
    startTimerHz (30);
}

TurntableLfoEditor::~TurntableLfoEditor()
{
    stopTimer();
}

void TurntableLfoEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<APVTS::SliderAttachment> (processor_.apvts, paramId, k.slider);
}

void TurntableLfoEditor::openFileChooser()
{
    chooser_ = std::make_unique<juce::FileChooser> (
        "Select a turntable-lfo .json", juce::File{}, "*.json");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser_->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File{})
            return;

        juce::String error;
        if (processor_.loadSequenceFromFile (file, error))
            statusLabel_.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
        else
            statusLabel_.setText ("Load failed: " + error, juce::dontSendNotification);
    });
}

void TurntableLfoEditor::timerCallback()
{
    rpmLabel_.setText (juce::String (processor_.getCurrentRpm(), 1) + " rpm",
                       juce::dontSendNotification);

    const int steps = processor_.getLoadedStepCount();
    if (statusLabel_.getText().isEmpty())
        statusLabel_.setText (steps > 0 ? juce::String (steps) + " steps loaded"
                                        : "no sequence loaded",
                              juce::dontSendNotification);
}

void TurntableLfoEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1d));
    g.setColour (juce::Colour (0xff2a2a30));
    g.fillRect (getLocalBounds().removeFromTop (44));
}

void TurntableLfoEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto header = area.removeFromTop (32);
    titleLabel_.setBounds (header.removeFromLeft (220));
    rpmLabel_.setBounds (header);
    area.removeFromTop (8);

    auto controls = area.removeFromTop (28);
    modeBox_.setBounds (controls.removeFromLeft (180));
    controls.removeFromLeft (10);
    tapeButton_.setBounds (controls.removeFromLeft (170));
    controls.removeFromLeft (10);
    loadButton_.setBounds (controls.removeFromLeft (160));
    area.removeFromTop (10);

    statusLabel_.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (8);

    // Two rows of four knobs.
    Knob* row1[] = { &rpmA_, &rpmB_, &threshold_, &hyst_ };
    Knob* row2[] = { &stopthr_, &glide_, &depth_, &mix_ };

    auto layoutRow = [] (juce::Rectangle<int> r, Knob** knobs, int count)
    {
        const int w = r.getWidth() / count;
        for (int i = 0; i < count; ++i)
        {
            auto cell = r.removeFromLeft (w).reduced (6);
            knobs[i]->label.setBounds (cell.removeFromTop (18));
            knobs[i]->slider.setBounds (cell);
        }
    };

    const int rowH = area.getHeight() / 2;
    layoutRow (area.removeFromTop (rowH), row1, 4);
    layoutRow (area, row2, 4);
}
