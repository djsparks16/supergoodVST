#pragma once
#include <JuceHeader.h>

//==============================================================================
// FXChain — global effects, processed after the synth:
//   distortion -> chorus -> phaser -> delay -> reverb -> compressor
//==============================================================================
class FXChain
{
public:
    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        auto p = [&] (const juce::String& id) { return apvts.getRawParameterValue (id); };

        pDistOn = p ("fxDistOn"); pDistDrive = p ("fxDistDrive"); pDistMix = p ("fxDistMix");
        pPhat = p ("phat");
        pWompOn = p ("fxWompOn"); pWompRate = p ("fxWompRate");
        pWompDepth = p ("fxWompDepth"); pWompMix = p ("fxWompMix");
        pCrushOn = p ("fxCrushOn"); pCrushBits = p ("fxCrushBits");
        pCrushRate = p ("fxCrushRate"); pCrushMix = p ("fxCrushMix");
        pChOn = p ("fxChorusOn"); pChRate = p ("fxChorusRate");
        pChDepth = p ("fxChorusDepth"); pChMix = p ("fxChorusMix");
        pPhOn = p ("fxPhaserOn"); pPhRate = p ("fxPhaserRate");
        pPhDepth = p ("fxPhaserDepth"); pPhMix = p ("fxPhaserMix");
        pDlOn = p ("fxDelayOn"); pDlTime = p ("fxDelayTime");
        pDlFb = p ("fxDelayFb"); pDlMix = p ("fxDelayMix");
        pRvOn = p ("fxRevOn"); pRvSize = p ("fxRevSize"); pRvDamp = p ("fxRevDamp");
        pRvWidth = p ("fxRevWidth"); pRvMix = p ("fxRevMix");
        pCpOn = p ("fxCompOn"); pCpThresh = p ("fxCompThresh"); pCpRatio = p ("fxCompRatio");
        pWidthOn = p ("fxWidthOn"); pWidthAmt = p ("fxWidthAmt");
        pEqOn = p ("fxEqOn"); pEqLow = p ("fxEqLow"); pEqHigh = p ("fxEqHigh");
        pHyperOn = p ("fxHyperOn"); pHyperAmt = p ("fxHyperAmt"); pHyperMix = p ("fxHyperMix");
        pLimitOn = p ("fxLimitOn"); pLimitDrive = p ("fxLimitDrive"); pLimitCeil = p ("fxLimitCeil");
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        chorus.prepare (spec);
        phaser.prepare (spec);
        reverb.prepare (spec);
        comp.prepare (spec);

        delay.setMaximumDelayInSamples ((int) (spec.sampleRate * 2.5));
        delay.prepare (spec);
        delay.reset();

        hyperDelay.setMaximumDelayInSamples ((int) (spec.sampleRate * 0.06));
        hyperDelay.prepare (spec);
        hyperDelay.reset();
        wompPhase = 0.0;
        crushPhase = 0.0;
        hyperPhase = 0.0;
        crushHold[0] = crushHold[1] = 0.0f;
        lowState[0] = lowState[1] = 0.0f;

        comp.setAttack (5.0f);
        comp.setRelease (120.0f);
    }

    void reset()
    {
        chorus.reset();
        phaser.reset();
        reverb.reset();
        comp.reset();
        delay.reset();
        hyperDelay.reset();
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin (2, buffer.getNumChannels());

        auto block = juce::dsp::AudioBlock<float> (buffer);
        auto ctx   = juce::dsp::ProcessContextReplacing<float> (block);

        // --- PHAT macro: one-knob bass soft clip / density stage -----------
        if (pPhat != nullptr && pPhat->load() > 0.001f)
        {
            const float amt = pPhat->load();
            const float drive = 1.0f + 10.0f * amt;
            const float outTrim = 1.0f / (1.0f + 0.65f * amt);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = d[i];
                    const float sat = std::tanh (x * drive) * outTrim;
                    d[i] = x + amt * (sat - x);
                }
            }
        }

        // --- Womp: tempo-free post amp wobble for bass movement -------------
        if (pWompOn != nullptr && pWompOn->load() > 0.5f)
        {
            const float rate = juce::jlimit (0.05f, 12.0f, pWompRate->load());
            const float depth = juce::jlimit (0.0f, 1.0f, pWompDepth->load());
            const float mix = juce::jlimit (0.0f, 1.0f, pWompMix->load());
            for (int i = 0; i < numSamples; ++i)
            {
                const float wob = 0.5f + 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * wompPhase));
                const float gain = 1.0f - mix * depth * (1.0f - wob);
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.getWritePointer (ch)[i] *= gain;
                wompPhase += rate / sampleRate;
                if (wompPhase >= 1.0) wompPhase -= 1.0;
            }
        }

        // --- Bit crusher / sample reducer ----------------------------------
        if (pCrushOn != nullptr && pCrushOn->load() > 0.5f)
        {
            const int steps = 1 << juce::jlimit (4, 16, (int) pCrushBits->load());
            const float rate = juce::jlimit (0.02f, 1.0f, pCrushRate->load());
            const float mix = juce::jlimit (0.0f, 1.0f, pCrushMix->load());
            const double holdSamples = (sampleRate / 800.0) + ((double) rate - 0.02) * (1.0 - sampleRate / 800.0) / (1.0 - 0.02);
            for (int i = 0; i < numSamples; ++i)
            {
                if (crushPhase <= 0.0)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        const float x = buffer.getReadPointer (ch)[i];
                        crushHold[ch] = std::round (x * (float) steps) / (float) steps;
                    }
                    crushPhase += holdSamples;
                }

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    d[i] = d[i] + mix * (crushHold[ch] - d[i]);
                }
                crushPhase -= 1.0;
            }
        }

        // --- Distortion -------------------------------------------------
        if (pDistOn->load() > 0.5f)
        {
            const float drive = 1.0f + 19.0f * pDistDrive->load();
            const float mix   = pDistMix->load();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    d[i] = mix * std::tanh (d[i] * drive) + (1.0f - mix) * d[i];
            }
        }

        // --- Chorus -----------------------------------------------------
        if (pChOn->load() > 0.5f)
        {
            chorus.setRate (pChRate->load());
            chorus.setDepth (pChDepth->load());
            chorus.setCentreDelay (7.0f);
            chorus.setFeedback (0.0f);
            chorus.setMix (pChMix->load());
            chorus.process (ctx);
        }

        // --- Phaser -----------------------------------------------------
        if (pPhOn->load() > 0.5f)
        {
            phaser.setRate (pPhRate->load());
            phaser.setDepth (pPhDepth->load());
            phaser.setCentreFrequency (900.0f);
            phaser.setFeedback (0.3f);
            phaser.setMix (pPhMix->load());
            phaser.process (ctx);
        }

        // --- Delay ------------------------------------------------------
        if (pDlOn->load() > 0.5f)
        {
            const float delaySamples = juce::jlimit (1.0f, (float) (sampleRate * 2.4),
                                                     pDlTime->load() * 0.001f * (float) sampleRate);
            const float fb  = juce::jlimit (0.0f, 0.95f, pDlFb->load());
            const float mix = pDlMix->load();

            delay.setDelay (delaySamples);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float in  = d[i];
                    const float wet = delay.popSample (ch);
                    delay.pushSample (ch, in + wet * fb);
                    d[i] = in + wet * mix;
                }
            }
        }

        // --- Reverb -----------------------------------------------------
        if (pRvOn->load() > 0.5f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize   = pRvSize->load();
            rp.damping    = pRvDamp->load();
            rp.width      = pRvWidth->load();
            rp.wetLevel   = pRvMix->load();
            rp.dryLevel   = 1.0f - pRvMix->load() * 0.5f;
            rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
            reverb.process (ctx);
        }

        // --- Two-band tone EQ: bass weight + air bite -----------------------
        if (pEqOn != nullptr && pEqOn->load() > 0.5f)
        {
            const float lowGain  = juce::Decibels::decibelsToGain (pEqLow->load());
            const float highGain = juce::Decibels::decibelsToGain (pEqHigh->load());
            const float lowA = std::exp (-2.0f * juce::MathConstants<float>::pi * 180.0f / (float) sampleRate);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = d[i];
                    lowState[ch] = lowA * lowState[ch] + (1.0f - lowA) * x;
                    const float low = lowState[ch];
                    const float high = x - low;
                    d[i] = low * lowGain + high * highGain;
                }
            }
        }

        // --- Hyper: micro-delay side enhancer for Serum-style width ---------
        if (pHyperOn != nullptr && pHyperOn->load() > 0.5f && numChannels >= 2)
        {
            const float amt = juce::jlimit (0.0f, 1.0f, pHyperAmt->load());
            const float mix = juce::jlimit (0.0f, 1.0f, pHyperMix->load());
            const float baseDelay = (4.0f + 18.0f * amt) * 0.001f * (float) sampleRate;
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const float mod = 0.5f + 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * hyperPhase));
                hyperDelay.setDelay (baseDelay + mod * 12.0f * amt);

                const float inL = l[i], inR = r[i];
                const float wetL = hyperDelay.popSample (0);
                const float wetR = hyperDelay.popSample (1);
                hyperDelay.pushSample (0, inR);
                hyperDelay.pushSample (1, inL);

                l[i] = inL + mix * (wetL - inL) * 0.55f;
                r[i] = inR + mix * (wetR - inR) * 0.55f;

                hyperPhase += (0.11 + 0.66 * amt) / sampleRate;
                if (hyperPhase >= 1.0) hyperPhase -= 1.0;
            }
        }

        // --- Stereo width --------------------------------------------------
        if (pWidthOn != nullptr && pWidthOn->load() > 0.5f && numChannels >= 2)
        {
            const float width = juce::jlimit (0.0f, 2.0f, pWidthAmt->load());
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = 0.5f * (l[i] + r[i]);
                const float side = 0.5f * (l[i] - r[i]) * width;
                l[i] = mid + side;
                r[i] = mid - side;
            }
        }

        // --- Compressor ---------------------------------------------------
        if (pCpOn->load() > 0.5f)
        {
            comp.setThreshold (pCpThresh->load());
            comp.setRatio (juce::jmax (1.0f, pCpRatio->load()));
            comp.process (ctx);
        }

        // --- Safety/attitude limiter: final soft ceiling --------------------
        if (pLimitOn != nullptr && pLimitOn->load() > 0.5f)
        {
            const float drive = 1.0f + 6.0f * juce::jlimit (0.0f, 1.0f, pLimitDrive->load());
            const float ceil  = juce::Decibels::decibelsToGain (pLimitCeil->load());
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    d[i] = std::tanh (d[i] * drive) * ceil;
            }
        }
    }

private:
    double sampleRate = 44100.0;
    double wompPhase = 0.0, crushPhase = 0.0, hyperPhase = 0.0;
    float crushHold[2] { 0.0f, 0.0f };
    float lowState[2] { 0.0f, 0.0f };

    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Reverb reverb;
    juce::dsp::Compressor<float> comp;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hyperDelay;

    std::atomic<float> *pDistOn {}, *pDistDrive {}, *pDistMix {}, *pPhat {},
                       *pWompOn {}, *pWompRate {}, *pWompDepth {}, *pWompMix {},
                       *pCrushOn {}, *pCrushBits {}, *pCrushRate {}, *pCrushMix {},
                       *pChOn {}, *pChRate {}, *pChDepth {}, *pChMix {},
                       *pPhOn {}, *pPhRate {}, *pPhDepth {}, *pPhMix {},
                       *pDlOn {}, *pDlTime {}, *pDlFb {}, *pDlMix {},
                       *pRvOn {}, *pRvSize {}, *pRvDamp {}, *pRvWidth {}, *pRvMix {},
                       *pCpOn {}, *pCpThresh {}, *pCpRatio {},
                       *pWidthOn {}, *pWidthAmt {},
                       *pEqOn {}, *pEqLow {}, *pEqHigh {},
                       *pHyperOn {}, *pHyperAmt {}, *pHyperMix {},
                       *pLimitOn {}, *pLimitDrive {}, *pLimitCeil {};
};
