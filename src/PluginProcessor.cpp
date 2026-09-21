#include "PluginProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout DoublerProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> { 0.f, 1.f }, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "spread", 1 }, "Spread",
        juce::NormalisableRange<float> { 0.f, 1.f }, 0.7f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "humanize", 1 }, "Humanize",
        juce::NormalisableRange<float> { 0.f, 1.f }, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "width", 1 }, "Width",
        juce::NormalisableRange<float> { 0.f, 1.f }, 1.f));
    return layout;
}

DoublerProcessor::DoublerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

void DoublerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());
    mixSm.reset (sampleRate, 0.02);
    spreadSm.reset (sampleRate, 0.02);
    humanSm.reset (sampleRate, 0.02);
    widthSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load());
    spreadSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("spread")->load());
    humanSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("humanize")->load());
    widthSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("width")->load());
}

void DoublerProcessor::releaseResources() {}

bool DoublerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void DoublerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    mixSm.setTargetValue (apvts.getRawParameterValue ("mix")->load());
    spreadSm.setTargetValue (apvts.getRawParameterValue ("spread")->load());
    humanSm.setTargetValue (apvts.getRawParameterValue ("humanize")->load());
    widthSm.setTargetValue (apvts.getRawParameterValue ("width")->load());
    mixSm.skip (buffer.getNumSamples());
    spreadSm.skip (buffer.getNumSamples());
    humanSm.skip (buffer.getNumSamples());
    widthSm.skip (buffer.getNumSamples());
    engine.setParams (mixSm.getCurrentValue(), spreadSm.getCurrentValue(),
                      humanSm.getCurrentValue(), widthSm.getCurrentValue());

    const int n = buffer.getNumSamples();
    auto* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    engine.process (l, inR, l, r, n);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

juce::AudioProcessorEditor* DoublerProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void DoublerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DoublerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DoublerProcessor();
}
