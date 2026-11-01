#include "PluginProcessor.h"
#include "PluginEditor.h"

CrunchBoxProcessor::CrunchBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CrunchBoxProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "drive", 1 }, "Drive",
        NormalisableRange<float> (0.0f, 36.0f, 0.01f), 12.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone",
        NormalisableRange<float> (200.0f, 20000.0f, 1.0f, 0.3f), 12000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "crush", 1 }, "Crush",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

void CrunchBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) getTotalNumOutputChannels();

    toneFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.reset();

    held[0] = held[1] = 0.0f;
    holdCounter = 0;

    auto setup = [sampleRate] (juce::SmoothedValue<float>& s, float v)
    {
        s.reset (sampleRate, 0.02);
        s.setCurrentAndTargetValue (v);
    };

    setup (driveSmooth, juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("drive")->load()));
    setup (mixSmooth,   apvts.getRawParameterValue ("mix")->load());
    setup (outSmooth,   1.0f);
    setup (crushSmooth, apvts.getRawParameterValue ("crush")->load());
}

bool CrunchBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void CrunchBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (2, getTotalNumInputChannels());
    const int numSamples  = buffer.getNumSamples();

    for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    driveSmooth.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("drive")->load()));
    mixSmooth.setTargetValue   (apvts.getRawParameterValue ("mix")->load());
    crushSmooth.setTargetValue (apvts.getRawParameterValue ("crush")->load());
    toneFilter.setCutoffFrequency (apvts.getRawParameterValue ("tone")->load());

    float peak = 0.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        const float drive = driveSmooth.getNextValue();
        const float mix   = mixSmooth.getNextValue();
        const float crush = crushSmooth.getNextValue();

        const bool  doCrush = crush > 0.001f;
        const int   hold    = 1 + (int) (crush * 14.0f);
        const bool  refresh = (holdCounter == 0);
        const float step    = doCrush ? 2.0f / std::pow (2.0f, 16.0f - crush * 11.0f) : 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            const float dry = data[s];

            float wet = std::tanh (dry * drive);

            if (doCrush)
            {
                if (refresh)
                    held[ch] = wet;            // sample-rate reduction
                wet = held[ch];
                wet = std::round (wet / step) * step;   // bit reduction
            }

            wet = toneFilter.processSample (ch, wet);

            data[s] = dry * (1.0f - mix) + wet * mix;
            peak = juce::jmax (peak, std::abs (data[s]));
        }

        holdCounter = doCrush ? (holdCounter + 1) % hold : 0;
    }

    outputLevel.store (juce::jmax (juce::jmin (peak, 1.0f), outputLevel.load() * 0.85f));
}

juce::AudioProcessorEditor* CrunchBoxProcessor::createEditor()
{
    return new CrunchBoxEditor (*this);
}

void CrunchBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CrunchBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrunchBoxProcessor();
}
