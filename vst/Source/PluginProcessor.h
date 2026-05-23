#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "Varispeed.h"

// Turntable LFO: a vinyl varispeed effect whose speed hard-snaps between turntable RPM
// states (33 1/3 / 45 / optional 0). The snap source is either an internal envelope follower
// (audio energy as a stand-in for motion) or a step-LFO sequence imported from the
// turntable-lfo-extractor (.json), read in sync with the host transport.

class TurntableLfoProcessor : public juce::AudioProcessor
{
public:
    TurntableLfoProcessor();
    ~TurntableLfoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- LFO sequence import (called from the editor) ---
    bool loadSequenceFromFile (const juce::File& file, juce::String& error);
    int  getLoadedStepCount() const { return loadedSteps_.load(); }

    // Live display for the editor.
    float getCurrentRpm() const { return currentRpm_.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Schmitt-trigger classification mirroring extractor/turntable_lfo/quantize.py.
    float classifyInternal (float env);

    static constexpr double kRefRpm = 45.0; // unity reference; rate = rpm / kRefRpm

    double sampleRate_ = 44100.0;
    std::vector<Varispeed> engines_;        // one per channel

    // internal-mode state
    float env_      = 0.0f;
    int   lfoLevel_ = 1;                    // 0=stop, 1=33, 2=45
    float envCoeff_ = 0.0f;

    // loaded-sequence state (read on the audio thread, swapped under a lock by the editor)
    juce::SpinLock seqLock_;
    std::vector<float> sequence_;           // rpm per step
    int   seqGrid_ = 16;
    std::atomic<int> loadedSteps_ { 0 };

    // smoothing + display
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> rateSmoothed_;
    juce::SmoothedValue<float,  juce::ValueSmoothingTypes::Linear> gainSmoothed_;
    float lastGlideMs_ = -1.0f;
    std::atomic<float> currentRpm_ { kRefRpm };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TurntableLfoProcessor)
};
