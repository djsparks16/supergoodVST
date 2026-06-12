#pragma once
#include <JuceHeader.h>
#include "SynthVoice.h"
#include "FXChain.h"

class ObsidianAudioProcessor : public juce::AudioProcessor
{
public:
    ObsidianAudioProcessor();
    ~ObsidianAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 3.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the editor (message thread)
    bool loadUserWavetable (const juce::File& file);

    // Read-only access for the editor's wavetable displays (message thread)
    const WavetableBank& getBank() const noexcept { return bank; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    WavetableBank bank;
    juce::Synthesiser synth;
    FXChain fx;
    juce::dsp::Gain<float> masterGain;
    std::atomic<double> hostBpm { 120.0 };

    static constexpr int numVoices = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObsidianAudioProcessor)
};
