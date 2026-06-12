#pragma once
#include <JuceHeader.h>
#include "Wavetable.h"
#include "Modulation.h"

//==============================================================================
class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// SynthVoice
//
// Per-voice signal path:
//   Osc A (warp/FM) + Osc B (warp), each with up to 7 unison copies
//   + sub oscillator + noise
//     -> drive -> filter (SVF LP/BP/HP or Ladder 24dB)
//     -> amp envelope -> pan
//
// Modulation: Env2 + LFO1 hardwired to cutoff (amount knobs) plus an 8-slot
// mod matrix (Env2/LFO1/LFO2/velocity/mod wheel/note/random -> 11 dests),
// evaluated at control rate (every 32 samples).
//==============================================================================
class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int maxUnison = 7;
    static constexpr int controlInterval = 32;

    SynthVoice (juce::AudioProcessorValueTreeState& state,
                const WavetableBank& b,
                const std::atomic<double>& bpm)
        : apvts (state), bank (b), hostBpm (bpm)
    {
        auto p = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

        pATable = p ("oscATable"); pAMorph = p ("oscAMorph"); pALevel = p ("oscALevel");
        pAWarpMode = p ("oscAWarpMode"); pAWarpAmt = p ("oscAWarpAmt");
        pASemi = p ("oscASemi"); pAFine = p ("oscAFine");

        pBTable = p ("oscBTable"); pBMorph = p ("oscBMorph"); pBLevel = p ("oscBLevel");
        pBWarpMode = p ("oscBWarpMode"); pBWarpAmt = p ("oscBWarpAmt");
        pBSemi = p ("oscBSemi"); pBFine = p ("oscBFine");

        pSubLevel = p ("subLevel"); pSubOct = p ("subOct"); pSubWave = p ("subWave"); pNoiseLevel = p ("noiseLevel");

        pUniCount = p ("uniCount"); pUniDetune = p ("uniDetune"); pUniWidth = p ("uniWidth");
        pGlide = p ("glideTime"); pBendRange = p ("bendRange");

        pAmpA = p ("ampA"); pAmpD = p ("ampD"); pAmpS = p ("ampS"); pAmpR = p ("ampR");
        pEnv2A = p ("env2A"); pEnv2D = p ("env2D"); pEnv2S = p ("env2S"); pEnv2R = p ("env2R");

        pFltModel = p ("fltModel"); pCutoff = p ("cutoff"); pReso = p ("reso");
        pFltEnvAmt = p ("fltEnvAmt"); pFltDrive = p ("fltDrive");

        pLfo1Shape = p ("lfo1Shape"); pLfo1Rate = p ("lfo1Rate");
        pLfo1Sync = p ("lfo1Sync"); pLfo1Div = p ("lfo1Div"); pLfo1Cut = p ("lfo1Cut");
        pLfo2Shape = p ("lfo2Shape"); pLfo2Rate = p ("lfo2Rate");
        pLfo2Sync = p ("lfo2Sync"); pLfo2Div = p ("lfo2Div");

        for (int s = 0; s < Mod::numSlots; ++s)
        {
            const auto n = juce::String (s + 1);
            pModSrc[(size_t) s] = p ("mod" + n + "Src");
            pModDst[(size_t) s] = p ("mod" + n + "Dst");
            pModAmt[(size_t) s] = p ("mod" + n + "Amt");
        }
    }

    void prepare (double sr, int blockSize)
    {
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
        svf.prepare (spec);
        ladder.prepare (spec);
        ampEnv.setSampleRate (sr);
        env2.setSampleRate (sr);
        for (auto& o : oscA) o.setSampleRate (sr);
        for (auto& o : oscB) o.setSampleRate (sr);
        scratch.setSize (2, blockSize);
        glideHz.reset (sr, 0.0);
        glideHz.setCurrentAndTargetValue (440.0);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int wheelPos) override
    {
        const double targetHz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        const double glideSecs = pGlide->load();
        const double startHz   = (haveLastNote && glideSecs > 0.001) ? glideHz.getCurrentValue()
                                                                     : targetHz;
        glideHz.reset (getSampleRate(), glideSecs);
        glideHz.setCurrentAndTargetValue (startHz);
        glideHz.setTargetValue (targetHz);
        haveLastNote = true;

        vel       = velocity;
        noteVal   = juce::jlimit (-1.0f, 1.0f, (midiNote - 60) / 36.0f);
        randVal   = rng.nextFloat() * 2.0f - 1.0f;
        bendNorm  = (wheelPos - 8192) / 8192.0f;

        updateEnvelopes();
        ampEnv.noteOn();
        env2.noteOn();

        svf.reset();
        ladder.reset();
        lfo1.reset (rng);
        lfo2.reset (rng);
        subPhase = 0.0;

        const int count = juce::jlimit (1, maxUnison, (int) pUniCount->load());
        for (int u = 0; u < count; ++u)
        {
            const double ph = count == 1 ? 0.0 : rng.nextDouble();
            oscA[(size_t) u].resetPhase (ph);
            oscB[(size_t) u].resetPhase (count == 1 ? 0.0 : rng.nextDouble());
        }
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            env2.noteOff();
        }
        else
        {
            clearCurrentNote();
            ampEnv.reset();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        bendNorm = (newValue - 8192) / 8192.0f;
    }

    void controllerMoved (int controller, int newValue) override
    {
        if (controller == 1)
            modWheel = newValue / 127.0f;
    }

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        updateEnvelopes();

        const auto* tableA = &bank.get ((int) pATable->load());
        const auto* tableB = &bank.get ((int) pBTable->load());
        for (auto& o : oscA) o.setTable (tableA);
        for (auto& o : oscB) o.setTable (tableB);

        const int   warpModeA = (int) pAWarpMode->load();
        const int   warpModeB = (int) pBWarpMode->load();
        const float lvlSub    = pSubLevel->load();
        const float lvlNoise  = pNoiseLevel->load();
        const int   subOctave = (int) pSubOct->load() + 1;
        const int   subWave   = (int) pSubWave->load(); // 0 -> -1 oct, 1 -> -2 oct
        const float drive     = pFltDrive->load();
        const int   fltModel  = (int) pFltModel->load();
        const int   uniCount  = juce::jlimit (1, maxUnison, (int) pUniCount->load());
        const float uniNorm   = 1.0f / std::sqrt ((float) uniCount);
        const float uniWidth  = pUniWidth->load();
        const double sr       = getSampleRate();
        const double bpm      = juce::jmax (20.0, hostBpm.load());

        scratch.clear();

        int pos = 0;
        while (pos < numSamples)
        {
            const int len = juce::jmin (controlInterval, numSamples - pos);
            const double dt = len / sr;

            //==========================================================
            // 1) Control-rate: advance mod sources
            //==========================================================
            float env2v = 0.0f;
            for (int i = 0; i < len; ++i)
                env2v = env2.getNextSample();

            const double lfo1Rate = pLfo1Sync->load() > 0.5f
                ? (bpm / 60.0) / Mod::divBeats[(int) pLfo1Div->load()]
                : (double) pLfo1Rate->load();
            const double lfo2Rate = pLfo2Sync->load() > 0.5f
                ? (bpm / 60.0) / Mod::divBeats[(int) pLfo2Div->load()]
                : (double) pLfo2Rate->load();

            const float l1 = lfo1.tick (dt, (int) pLfo1Shape->load(), lfo1Rate, rng);
            const float l2 = lfo2.tick (dt, (int) pLfo2Shape->load(), lfo2Rate, rng);

            const float sources[Mod::numSources] =
                { 0.0f, env2v, l1, l2, vel, modWheel, noteVal, randVal };

            //==========================================================
            // 2) Evaluate the mod matrix
            //==========================================================
            float dest[Mod::numDests] = {};
            for (int s = 0; s < Mod::numSlots; ++s)
            {
                const int src = (int) pModSrc[(size_t) s]->load();
                const int dst = (int) pModDst[(size_t) s]->load();
                if (src > 0 && dst > 0)
                    dest[dst] += pModAmt[(size_t) s]->load() * sources[src];
            }

            //==========================================================
            // 3) Apply modulation to per-block targets
            //==========================================================
            const float morphA = juce::jlimit (0.0f, 1.0f, pAMorph->load() + dest[Mod::dAMorph]);
            const float morphB = juce::jlimit (0.0f, 1.0f, pBMorph->load() + dest[Mod::dBMorph]);
            const float lvlA   = juce::jlimit (0.0f, 1.0f, pALevel->load() + dest[Mod::dALevel]);
            const float lvlB   = juce::jlimit (0.0f, 1.0f, pBLevel->load() + dest[Mod::dBLevel]);
            const float warpA  = juce::jlimit (0.0f, 1.0f, pAWarpAmt->load() + dest[Mod::dWarpA]);
            const float warpB  = juce::jlimit (0.0f, 1.0f, pBWarpAmt->load() + dest[Mod::dWarpB]);

            const double baseHz    = glideHz.skip (len);
            const double pitchSemi = pBendRange->load() * bendNorm + 24.0 * dest[Mod::dPitch];
            const double noteHz    = baseHz * std::pow (2.0, pitchSemi / 12.0);

            const double semiA = pASemi->load() + pAFine->load() / 100.0;
            const double semiB = pBSemi->load() + pBFine->load() / 100.0;
            const double hzA   = noteHz * std::pow (2.0, semiA / 12.0);
            const double hzB   = noteHz * std::pow (2.0, semiB / 12.0);
            const double hzSub = noteHz / std::pow (2.0, (double) subOctave);

            const float detune = juce::jmax (0.0f, pUniDetune->load()
                                                     + 100.0f * dest[Mod::dUniDetune]);

            for (int u = 0; u < uniCount; ++u)
            {
                const float offset = uniCount == 1
                                       ? 0.0f
                                       : (u / float (uniCount - 1) - 0.5f) * 2.0f;
                const double ratio = std::pow (2.0, offset * detune / 1200.0);

                oscA[(size_t) u].setFrequency (hzA * ratio);
                oscB[(size_t) u].setFrequency (hzB * ratio);
                oscA[(size_t) u].setMorph (morphA);
                oscB[(size_t) u].setMorph (morphB);

                const float panPos = 0.5f + offset * 0.5f * uniWidth;
                gainL[(size_t) u] = std::cos (panPos * juce::MathConstants<float>::halfPi);
                gainR[(size_t) u] = std::sin (panPos * juce::MathConstants<float>::halfPi);
            }

            // Voice pan (mod only): simple balance law
            const float panMod = juce::jlimit (-1.0f, 1.0f, dest[Mod::dPan]);
            const float panL   = panMod <= 0.0f ? 1.0f : 1.0f - panMod;
            const float panR   = panMod >= 0.0f ? 1.0f : 1.0f + panMod;

            // Filter setup
            const float cutOct = pFltEnvAmt->load() * 5.0f * env2v
                               + pLfo1Cut->load()   * 3.0f * l1
                               + 5.0f * dest[Mod::dCutoff];
            const float cutoff = juce::jlimit (20.0f, 20000.0f,
                                               pCutoff->load() * std::pow (2.0f, cutOct));
            const float resoV  = juce::jlimit (0.1f, 8.0f, pReso->load() + 8.0f * dest[Mod::dReso]);

            //==========================================================
            // 4) Audio-rate rendering
            //==========================================================
            auto* L = scratch.getWritePointer (0, pos);
            auto* R = scratch.getWritePointer (1, pos);

            const double subInc   = hzSub / sr;
            const float  driveAmt = 1.0f + 9.0f * drive;

            for (int i = 0; i < len; ++i)
            {
                float sl = 0.0f, srgt = 0.0f;

                for (int u = 0; u < uniCount; ++u)
                {
                    const float bs = oscB[(size_t) u].getNextSample (warpModeB, warpB, 0.0f);
                    const float fm = warpModeA == WavetableOscillator::warpFM
                                       ? warpA * 0.5f * bs : 0.0f;
                    const int   modeA = warpModeA == WavetableOscillator::warpFM
                                          ? WavetableOscillator::warpOff : warpModeA;
                    const float as = oscA[(size_t) u].getNextSample (modeA, warpA, fm);

                    const float s = as * lvlA + bs * lvlB;
                    sl   += s * gainL[(size_t) u];
                    srgt += s * gainR[(size_t) u];
                }

                sl   *= uniNorm;
                srgt *= uniNorm;

                float subShape = 0.0f;
                if (subWave == 1)
                    subShape = 1.0f - 4.0f * std::abs ((float) subPhase - 0.5f);
                else if (subWave == 2)
                    subShape = subPhase < 0.5 ? 1.0f : -1.0f;
                else
                    subShape = (float) std::sin (juce::MathConstants<double>::twoPi * subPhase);

                const float sub = subShape * lvlSub;
                subPhase += subInc;
                subPhase -= std::floor (subPhase);

                const float noise = (rng.nextFloat() * 2.0f - 1.0f) * lvlNoise;

                float l = (sl + sub + noise) * vel;
                float r = (srgt + sub + noise) * vel;

                if (drive > 0.01f)
                {
                    l = std::tanh (l * driveAmt);
                    r = std::tanh (r * driveAmt);
                }

                L[i] = l * panL;
                R[i] = r * panR;
            }

            //==========================================================
            // 5) Filter + amp envelope for this sub-block
            //==========================================================
            auto block = juce::dsp::AudioBlock<float> (scratch)
                             .getSubBlock ((size_t) pos, (size_t) len);
            auto ctx = juce::dsp::ProcessContextReplacing<float> (block);

            if (fltModel == 3)
            {
                ladder.setMode (juce::dsp::LadderFilterMode::LPF24);
                ladder.setCutoffFrequencyHz (cutoff);
                ladder.setResonance (juce::jlimit (0.0f, 1.0f, (resoV - 0.1f) / 7.9f));
                ladder.process (ctx);
            }
            else
            {
                svf.setType (fltModel == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                           : fltModel == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                           : juce::dsp::StateVariableTPTFilterType::lowpass);
                svf.setCutoffFrequency (cutoff);
                svf.setResonance (resoV);
                svf.process (ctx);
            }

            ampEnv.applyEnvelopeToBuffer (scratch, pos, len);
            pos += len;
        }

        for (int ch = 0; ch < juce::jmin (2, output.getNumChannels()); ++ch)
            output.addFrom (ch, startSample, scratch, ch, 0, numSamples);

        if (! ampEnv.isActive())
            clearCurrentNote();
    }

private:
    void updateEnvelopes()
    {
        ampEnv.setParameters ({ pAmpA->load(), pAmpD->load(), pAmpS->load(), pAmpR->load() });
        env2.setParameters ({ pEnv2A->load(), pEnv2D->load(), pEnv2S->load(), pEnv2R->load() });
    }

    juce::AudioProcessorValueTreeState& apvts;
    const WavetableBank& bank;
    const std::atomic<double>& hostBpm;

    std::array<WavetableOscillator, maxUnison> oscA, oscB;
    std::array<float, maxUnison> gainL {}, gainR {};

    juce::ADSR ampEnv, env2;
    LFO lfo1, lfo2;
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::LadderFilter<float> ladder;
    juce::AudioBuffer<float> scratch;
    juce::Random rng;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> glideHz;
    bool haveLastNote = false;

    double subPhase = 0.0;
    float  vel = 1.0f, modWheel = 0.0f, noteVal = 0.0f, randVal = 0.0f, bendNorm = 0.0f;

    // Cached parameter pointers
    std::atomic<float> *pATable {}, *pAMorph {}, *pALevel {}, *pAWarpMode {}, *pAWarpAmt {},
                       *pASemi {}, *pAFine {},
                       *pBTable {}, *pBMorph {}, *pBLevel {}, *pBWarpMode {}, *pBWarpAmt {},
                       *pBSemi {}, *pBFine {},
                       *pSubLevel {}, *pSubOct {}, *pSubWave {}, *pNoiseLevel {},
                       *pUniCount {}, *pUniDetune {}, *pUniWidth {},
                       *pGlide {}, *pBendRange {},
                       *pAmpA {}, *pAmpD {}, *pAmpS {}, *pAmpR {},
                       *pEnv2A {}, *pEnv2D {}, *pEnv2S {}, *pEnv2R {},
                       *pFltModel {}, *pCutoff {}, *pReso {}, *pFltEnvAmt {}, *pFltDrive {},
                       *pLfo1Shape {}, *pLfo1Rate {}, *pLfo1Sync {}, *pLfo1Div {}, *pLfo1Cut {},
                       *pLfo2Shape {}, *pLfo2Rate {}, *pLfo2Sync {}, *pLfo2Div {};

    std::array<std::atomic<float>*, Mod::numSlots> pModSrc {}, pModDst {}, pModAmt {};
};
