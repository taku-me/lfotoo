#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
    constexpr auto rpmA      = "rpmA";
    constexpr auto rpmB      = "rpmB";
    constexpr auto mode      = "mode";
    constexpr auto threshold = "threshold";
    constexpr auto hyst      = "hyst";
    constexpr auto glide     = "glide";
    constexpr auto depth     = "depth";
    constexpr auto mix       = "mix";
    constexpr auto tapestop  = "tapestop";
    constexpr auto stopthr   = "stopthr";
}

juce::AudioProcessorValueTreeState::ParameterLayout TurntableLfoProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    const NormalisableRange<float> rpmRange { 10.0f, 100.0f, 0.001f };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::rpmA, 1 },
        "RPM A (slow)", rpmRange, 33.333f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::rpmB, 1 },
        "RPM B (fast)", rpmRange, 45.0f));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 },
        "Trigger", StringArray { "Internal (audio)", "Loaded (.json)" }, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::threshold, 1 },
        "Threshold", NormalisableRange<float> { 0.0f, 1.0f, 0.0001f }, 0.2f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::hyst, 1 },
        "Hysteresis", NormalisableRange<float> { 0.0f, 0.5f, 0.0001f }, 0.05f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::stopthr, 1 },
        "Tape-stop Threshold", NormalisableRange<float> { 0.0f, 1.0f, 0.0001f }, 0.03f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::tapestop, 1 },
        "Tape-stop (0 rpm)", false));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::glide, 1 },
        "Glide", NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 8.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::depth, 1 },
        "Depth", NormalisableRange<float> { 0.0f, 1.0f, 0.0001f }, 1.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, 1 },
        "Mix", NormalisableRange<float> { 0.0f, 1.0f, 0.0001f }, 1.0f));

    return { p.begin(), p.end() };
}

TurntableLfoProcessor::TurntableLfoProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

void TurntableLfoProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    engines_.resize ((size_t) numCh);
    for (auto& e : engines_)
        e.prepare (sampleRate);

    envCoeff_ = 1.0f - std::exp (-1.0f / (0.015f * (float) sampleRate)); // ~15 ms follower
    env_ = 0.0f;
    lfoLevel_ = 1;

    const double glideSec = 0.008;
    rateSmoothed_.reset (sampleRate, glideSec);
    rateSmoothed_.setCurrentAndTargetValue (1.0);
    gainSmoothed_.reset (sampleRate, glideSec);
    gainSmoothed_.setCurrentAndTargetValue (1.0f);
    lastGlideMs_ = -1.0f;
    currentRpm_.store ((float) kRefRpm);
}

bool TurntableLfoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

float TurntableLfoProcessor::classifyInternal (float env)
{
    const float thrHigh = apvts.getRawParameterValue (ids::threshold)->load();
    const float thrLow  = apvts.getRawParameterValue (ids::stopthr)->load();
    const float h       = apvts.getRawParameterValue (ids::hyst)->load();
    const bool  tape    = apvts.getRawParameterValue (ids::tapestop)->load() > 0.5f;

    int s = lfoLevel_;
    if (s == 2)
    {
        if (env < thrHigh - h)
            s = (tape && env < thrLow - h) ? 0 : 1;
    }
    else if (s == 1)
    {
        if (env >= thrHigh + h)        s = 2;
        else if (tape && env < thrLow - h) s = 0;
    }
    else // s == 0 (stop)
    {
        if (env >= thrHigh + h)      s = 2;
        else if (env >= thrLow + h)  s = 1;
    }
    if (! tape && s == 0) s = 1;
    lfoLevel_ = s;

    const float rpmA = apvts.getRawParameterValue (ids::rpmA)->load();
    const float rpmB = apvts.getRawParameterValue (ids::rpmB)->load();
    return s == 2 ? rpmB : (s == 0 ? 0.0f : rpmA);
}

void TurntableLfoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh    = buffer.getNumChannels();
    const int numSamps = buffer.getNumSamples();

    // Adjust smoothing length if the glide parameter changed (preserve current value).
    const float glideMs = apvts.getRawParameterValue (ids::glide)->load();
    if (glideMs != lastGlideMs_)
    {
        const double sec = juce::jmax (0.0001, (double) glideMs / 1000.0);
        const double curRate = rateSmoothed_.getCurrentValue();
        const float  curGain = gainSmoothed_.getCurrentValue();
        rateSmoothed_.reset (sampleRate_, sec);
        rateSmoothed_.setCurrentAndTargetValue (curRate);
        gainSmoothed_.reset (sampleRate_, sec);
        gainSmoothed_.setCurrentAndTargetValue (curGain);
        lastGlideMs_ = glideMs;
    }

    const int   mode  = (int) apvts.getRawParameterValue (ids::mode)->load();
    const float depth = apvts.getRawParameterValue (ids::depth)->load();
    const float mix   = apvts.getRawParameterValue (ids::mix)->load();

    // Host transport (for Loaded mode).
    double bpm = 120.0, ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())          bpm = *b;
            if (auto q = pos->getPpqPosition())  ppq = *q;
        }

    // Snapshot the loaded sequence for this block (RT-safe: no allocation, brief try-lock).
    const float* seq = nullptr;
    int seqLen = 0, seqGrid = 16;
    juce::SpinLock::ScopedTryLockType seqTry (seqLock_);
    const bool haveSeqLock = seqTry.isLocked();
    if (haveSeqLock && ! sequence_.empty())
    {
        seq     = sequence_.data();
        seqLen  = (int) sequence_.size();
        seqGrid = seqGrid_;
    }

    const double stepsPerQuarter = (double) seqGrid / 4.0;
    double stepPos = ppq * stepsPerQuarter;
    const double stepInc = (bpm / 60.0) / sampleRate_ * stepsPerQuarter;

    // Read inputs into a small stack copy per channel so we can write outputs in place.
    float lastRpm = (float) kRefRpm;

    for (int n = 0; n < numSamps; ++n)
    {
        // Mono magnitude for the envelope follower.
        float mag = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mag = juce::jmax (mag, std::fabs (buffer.getReadPointer (ch)[n]));
        env_ += (mag - env_) * envCoeff_;

        float targetRpm;
        if (mode == 1 && seq != nullptr)
        {
            int idx = (int) std::floor (stepPos);
            idx = juce::jlimit (0, seqLen - 1, idx);
            targetRpm = seq[idx];
        }
        else
        {
            targetRpm = classifyInternal (env_);
        }
        stepPos += stepInc;
        lastRpm = targetRpm;

        // Tape-stop (0 rpm) => mute via a smoothed gain; keep the engine running at unity so
        // resuming does not click. Otherwise apply depth-scaled varispeed.
        double effRate; float gainTarget;
        if (targetRpm <= 0.0f)
        {
            effRate = 1.0;
            gainTarget = 0.0f;
        }
        else
        {
            const double targetRate = (double) targetRpm / kRefRpm;
            effRate = 1.0 + (double) depth * (targetRate - 1.0);
            gainTarget = 1.0f;
        }
        rateSmoothed_.setTargetValue (effRate);
        gainSmoothed_.setTargetValue (gainTarget);
        const double rate = rateSmoothed_.getNextValue();
        const float  gain = gainSmoothed_.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = buffer.getReadPointer (ch)[n];
            const float wet = engines_[(size_t) ch].processSample (dry, rate) * gain;
            buffer.getWritePointer (ch)[n] = dry * (1.0f - mix) + wet * mix;
        }
    }

    currentRpm_.store (lastRpm);
}

juce::AudioProcessorEditor* TurntableLfoProcessor::createEditor()
{
    return new TurntableLfoEditor (*this);
}

void TurntableLfoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TurntableLfoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

bool TurntableLfoProcessor::loadSequenceFromFile (const juce::File& file, juce::String& error)
{
    const auto text = file.loadFileAsString();
    auto parsed = juce::JSON::parse (text);
    if (! parsed.isObject())
    {
        error = "not a JSON object";
        return false;
    }

    auto* obj = parsed.getDynamicObject();
    const auto rpmVar = obj->getProperty ("rpm");
    if (! rpmVar.isArray())
    {
        error = "missing 'rpm' array";
        return false;
    }

    std::vector<float> next;
    for (const auto& v : *rpmVar.getArray())
        next.push_back ((float) (double) v);

    int grid = 16;
    if (obj->hasProperty ("grid"))
        grid = (int) obj->getProperty ("grid");

    if (next.empty())
    {
        error = "'rpm' array is empty";
        return false;
    }

    {
        const juce::SpinLock::ScopedLockType sl (seqLock_);
        sequence_.swap (next);
        seqGrid_ = grid > 0 ? grid : 16;
    }
    loadedSteps_.store ((int) sequence_.size());
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TurntableLfoProcessor();
}
