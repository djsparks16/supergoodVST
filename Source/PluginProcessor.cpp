#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ObsidianAudioProcessor::ObsidianAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    bank.init();

    synth.addSound (new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (apvts, bank, hostBpm));

    fx.attach (apvts);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ObsidianAudioProcessor::createLayout()
{
    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;
    using BoolParam   = juce::AudioParameterBool;
    using Range       = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto add = [&] (const juce::String& id, const juce::String& name, Range range, float def)
    {
        params.push_back (std::make_unique<FloatParam> (juce::ParameterID { id, 1 }, name, range, def));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name,
                          const juce::StringArray& items, int def)
    {
        params.push_back (std::make_unique<ChoiceParam> (juce::ParameterID { id, 1 }, name, items, def));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        params.push_back (std::make_unique<BoolParam> (juce::ParameterID { id, 1 }, name, def));
    };

    const juce::StringArray tableNames { "Basic", "Pulse", "Growl", "Formant", "Metal", "Reese", "Acid", "Organ", "User" };
    const juce::StringArray warpANames { "Off", "Sync", "Bend", "Mirror", "Fold", "Asym", "Quant", "FM (Osc B)" };
    const juce::StringArray warpBNames { "Off", "Sync", "Bend", "Mirror", "Fold", "Asym", "Quant" };
    const juce::StringArray lfoShapes  { "Sine", "Triangle", "Saw", "Square", "S&H" };

    // ---------------- Oscillator A ----------------
    addChoice ("oscATable", "Osc A Table", tableNames, 0);
    add ("oscAMorph", "Osc A Morph", { 0.0f, 1.0f }, 0.3f);
    add ("oscALevel", "Osc A Level", { 0.0f, 1.0f }, 0.8f);
    addChoice ("oscAWarpMode", "Osc A Warp", warpANames, 0);
    add ("oscAWarpAmt", "Osc A Warp Amt", { 0.0f, 1.0f }, 0.0f);
    add ("oscASemi", "Osc A Semi", { -24.0f, 24.0f, 1.0f }, 0.0f);
    add ("oscAFine", "Osc A Fine", { -100.0f, 100.0f }, 0.0f);

    // ---------------- Oscillator B ----------------
    addChoice ("oscBTable", "Osc B Table", tableNames, 1);
    add ("oscBMorph", "Osc B Morph", { 0.0f, 1.0f }, 0.5f);
    add ("oscBLevel", "Osc B Level", { 0.0f, 1.0f }, 0.0f);
    addChoice ("oscBWarpMode", "Osc B Warp", warpBNames, 0);
    add ("oscBWarpAmt", "Osc B Warp Amt", { 0.0f, 1.0f }, 0.0f);
    add ("oscBSemi", "Osc B Semi", { -24.0f, 24.0f, 1.0f }, 0.0f);
    add ("oscBFine", "Osc B Fine", { -100.0f, 100.0f }, 0.0f);

    // ---------------- Sub / Noise ----------------
    add ("subLevel", "Sub Level", { 0.0f, 1.0f }, 0.0f);
    addChoice ("subOct", "Sub Octave", { "-1 Oct", "-2 Oct" }, 0);
    addChoice ("subWave", "Sub Wave", { "Sine", "Triangle", "Square" }, 0);
    add ("noiseLevel", "Noise Level", { 0.0f, 1.0f }, 0.0f);

    // ---------------- Unison / Voice ----------------
    add ("uniCount",  "Unison Voices", { 1.0f, 7.0f, 1.0f }, 1.0f);
    add ("uniDetune", "Unison Detune", { 0.0f, 100.0f }, 18.0f);
    add ("uniWidth",  "Unison Width",  { 0.0f, 1.0f }, 0.8f);
    add ("glideTime", "Glide", { 0.0f, 2.0f, 0.0f, 0.5f }, 0.0f);
    add ("bendRange", "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f);

    // ---------------- Envelopes ----------------
    add ("ampA", "Env1 Attack",  { 0.001f, 5.0f, 0.0f, 0.35f }, 0.005f);
    add ("ampD", "Env1 Decay",   { 0.001f, 5.0f, 0.0f, 0.35f }, 0.2f);
    add ("ampS", "Env1 Sustain", { 0.0f, 1.0f }, 0.8f);
    add ("ampR", "Env1 Release", { 0.001f, 8.0f, 0.0f, 0.35f }, 0.15f);

    add ("env2A", "Env2 Attack",  { 0.001f, 5.0f, 0.0f, 0.35f }, 0.005f);
    add ("env2D", "Env2 Decay",   { 0.001f, 5.0f, 0.0f, 0.35f }, 0.3f);
    add ("env2S", "Env2 Sustain", { 0.0f, 1.0f }, 0.2f);
    add ("env2R", "Env2 Release", { 0.001f, 8.0f, 0.0f, 0.35f }, 0.2f);

    // ---------------- Filter ----------------
    addChoice ("fltModel", "Filter Model", { "SVF LP", "SVF BP", "SVF HP", "Ladder 24" }, 0);
    add ("cutoff",    "Cutoff",     { 20.0f, 20000.0f, 0.0f, 0.25f }, 18000.0f);
    add ("reso",      "Resonance",  { 0.1f, 8.0f, 0.0f, 0.5f }, 0.707f);
    add ("fltEnvAmt", "Env2 > Cut", { -1.0f, 1.0f }, 0.0f);
    add ("fltDrive",  "Drive",      { 0.0f, 1.0f }, 0.0f);

    // ---------------- LFOs ----------------
    addChoice ("lfo1Shape", "LFO1 Shape", lfoShapes, 0);
    add ("lfo1Rate", "LFO1 Rate", { 0.01f, 20.0f, 0.0f, 0.4f }, 2.0f);
    addBool ("lfo1Sync", "LFO1 Sync", false);
    addChoice ("lfo1Div", "LFO1 Div", Mod::divNames, 2);
    add ("lfo1Cut", "LFO1 > Cut", { 0.0f, 1.0f }, 0.0f);

    addChoice ("lfo2Shape", "LFO2 Shape", lfoShapes, 0);
    add ("lfo2Rate", "LFO2 Rate", { 0.01f, 20.0f, 0.0f, 0.4f }, 1.0f);
    addBool ("lfo2Sync", "LFO2 Sync", false);
    addChoice ("lfo2Div", "LFO2 Div", Mod::divNames, 2);

    // ---------------- Mod matrix ----------------
    for (int s = 1; s <= Mod::numSlots; ++s)
    {
        const auto n = juce::String (s);
        addChoice ("mod" + n + "Src", "Mod " + n + " Source", Mod::sourceNames, 0);
        addChoice ("mod" + n + "Dst", "Mod " + n + " Dest",   Mod::destNames, 0);
        add ("mod" + n + "Amt", "Mod " + n + " Amount", { -1.0f, 1.0f }, 0.0f);
    }

    // ---------------- FX ----------------
    addBool ("fxDistOn", "Dist On", false);
    add ("fxDistDrive", "Dist Drive", { 0.0f, 1.0f }, 0.3f);
    add ("fxDistMix",   "Dist Mix",   { 0.0f, 1.0f }, 1.0f);

    add ("phat", "PHAT Macro", { 0.0f, 1.0f }, 0.0f);

    addBool ("fxWompOn", "Womp On", false);
    add ("fxWompRate",  "Womp Rate",  { 0.05f, 12.0f, 0.0f, 0.45f }, 2.0f);
    add ("fxWompDepth", "Womp Depth", { 0.0f, 1.0f }, 0.65f);
    add ("fxWompMix",   "Womp Mix",   { 0.0f, 1.0f }, 0.85f);

    addBool ("fxCrushOn", "Crush On", false);
    add ("fxCrushBits", "Crush Bits", { 4.0f, 16.0f, 1.0f }, 10.0f);
    add ("fxCrushRate", "Crush Rate", { 0.02f, 1.0f }, 1.0f);
    add ("fxCrushMix",  "Crush Mix",  { 0.0f, 1.0f }, 0.35f);

    addBool ("fxChorusOn", "Chorus On", false);
    add ("fxChorusRate",  "Chorus Rate",  { 0.05f, 8.0f, 0.0f, 0.5f }, 0.8f);
    add ("fxChorusDepth", "Chorus Depth", { 0.0f, 1.0f }, 0.3f);
    add ("fxChorusMix",   "Chorus Mix",   { 0.0f, 1.0f }, 0.5f);

    addBool ("fxPhaserOn", "Phaser On", false);
    add ("fxPhaserRate",  "Phaser Rate",  { 0.05f, 8.0f, 0.0f, 0.5f }, 0.5f);
    add ("fxPhaserDepth", "Phaser Depth", { 0.0f, 1.0f }, 0.6f);
    add ("fxPhaserMix",   "Phaser Mix",   { 0.0f, 1.0f }, 0.5f);

    addBool ("fxDelayOn", "Delay On", false);
    add ("fxDelayTime", "Delay Time", { 1.0f, 2000.0f, 0.0f, 0.4f }, 350.0f);
    add ("fxDelayFb",   "Delay FB",   { 0.0f, 0.95f }, 0.4f);
    add ("fxDelayMix",  "Delay Mix",  { 0.0f, 1.0f }, 0.3f);

    addBool ("fxRevOn", "Reverb On", false);
    add ("fxRevSize",  "Rev Size",  { 0.0f, 1.0f }, 0.6f);
    add ("fxRevDamp",  "Rev Damp",  { 0.0f, 1.0f }, 0.4f);
    add ("fxRevWidth", "Rev Width", { 0.0f, 1.0f }, 1.0f);
    add ("fxRevMix",   "Rev Mix",   { 0.0f, 1.0f }, 0.3f);

    addBool ("fxCompOn", "Comp On", false);
    add ("fxCompThresh", "Comp Thresh", { -48.0f, 0.0f }, -12.0f);
    add ("fxCompRatio",  "Comp Ratio",  { 1.0f, 20.0f, 0.0f, 0.5f }, 4.0f);

    addBool ("fxWidthOn", "Width On", false);
    add ("fxWidthAmt", "Stereo Width", { 0.0f, 2.0f }, 1.25f);

    addBool ("fxEqOn", "EQ On", false);
    add ("fxEqLow",  "EQ Low",  { -12.0f, 12.0f }, 0.0f);
    add ("fxEqHigh", "EQ High", { -12.0f, 12.0f }, 0.0f);

    addBool ("fxHyperOn", "Hyper On", false);
    add ("fxHyperAmt", "Hyper Amt", { 0.0f, 1.0f }, 0.35f);
    add ("fxHyperMix", "Hyper Mix", { 0.0f, 1.0f }, 0.55f);

    addBool ("fxLimitOn", "Limiter On", true);
    add ("fxLimitDrive", "Limiter Drive", { 0.0f, 1.0f }, 0.18f);
    add ("fxLimitCeil",  "Limiter Ceiling", { -12.0f, 0.0f }, -0.8f);

    // ---------------- Master ----------------
    add ("master", "Master", { -48.0f, 6.0f }, -6.0f);

    return { params.begin(), params.end() };
}

//==============================================================================
void ObsidianAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    fx.prepare (spec);

    masterGain.prepare (spec);
    masterGain.setRampDurationSeconds (0.02);
}

bool ObsidianAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ObsidianAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    fx.process (buffer);

    masterGain.setGainDecibels (apvts.getRawParameterValue ("master")->load());
    auto block = juce::dsp::AudioBlock<float> (buffer);
    auto ctx   = juce::dsp::ProcessContextReplacing<float> (block);
    masterGain.process (ctx);
}

//==============================================================================
bool ObsidianAudioProcessor::loadUserWavetable (const juce::File& file)
{
    auto wt = WavetableBank::loadFromWav (file);
    if (! wt.has_value())
        return false;

    suspendProcessing (true);
    bank.setUserTable (std::move (*wt));
    suspendProcessing (false);
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ObsidianAudioProcessor::createEditor()
{
    return new ObsidianAudioProcessorEditor (*this);
}

void ObsidianAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ObsidianAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ObsidianAudioProcessor();
}
