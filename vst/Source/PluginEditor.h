#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class TurntableLfoEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit TurntableLfoEditor (TurntableLfoProcessor&);
    ~TurntableLfoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openFileChooser();

    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> attach;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text);

    TurntableLfoProcessor& processor_;

    Knob rpmA_, rpmB_, threshold_, hyst_, stopthr_, glide_, depth_, mix_;

    juce::ComboBox modeBox_;
    std::unique_ptr<APVTS::ComboBoxAttachment> modeAttach_;

    juce::ToggleButton tapeButton_ { "Tape-stop (0 rpm)" };
    std::unique_ptr<APVTS::ButtonAttachment> tapeAttach_;

    juce::TextButton loadButton_ { "Load LFO (.json)" };
    juce::Label      titleLabel_, statusLabel_, rpmLabel_;

    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TurntableLfoEditor)
};
