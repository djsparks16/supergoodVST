#pragma once
#include <JuceHeader.h>

//==============================================================================
// Mod matrix definitions (shared by processor, voice and editor)
//==============================================================================
namespace Mod
{
    enum Source { sNone = 0, sEnv2, sLFO1, sLFO2, sVelocity, sModWheel, sNote, sRandom, numSources };
    enum Dest   { dNone = 0, dAMorph, dBMorph, dALevel, dBLevel, dPitch, dCutoff,
                  dReso, dPan, dUniDetune, dWarpA, dWarpB, numDests };

    inline const juce::StringArray sourceNames { "None", "Env 2", "LFO 1", "LFO 2",
                                                 "Velocity", "Mod Wheel", "Note", "Random" };

    inline const juce::StringArray destNames { "None", "Osc A Morph", "Osc B Morph",
                                               "Osc A Level", "Osc B Level", "Pitch",
                                               "Cutoff", "Resonance", "Pan",
                                               "Uni Detune", "Warp A Amt", "Warp B Amt" };

    constexpr int numSlots = 8;

    // Tempo-sync divisions, expressed in beats
    inline const juce::StringArray divNames { "1/1", "1/2", "1/4", "1/8", "1/16",
                                              "1/4T", "1/8T", "1/4.", "1/8." };
    inline constexpr double divBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25,
                                           2.0 / 3.0, 1.0 / 3.0, 1.5, 0.75 };
}

//==============================================================================
// LFO — 5 shapes, free-running Hz or host-tempo-synced, control-rate ticked
//==============================================================================
class LFO
{
public:
    enum Shape { sine = 0, triangle, saw, square, sampleHold };

    void reset (juce::Random& rng)
    {
        phase = 0.0;
        held  = rng.nextFloat() * 2.0f - 1.0f;
    }

    // Returns the current value, then advances the phase by dtSeconds.
    float tick (double dtSeconds, int shape, double rateHz, juce::Random& rng)
    {
        const float v = valueAt (shape);

        phase += rateHz * dtSeconds;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.nextFloat() * 2.0f - 1.0f; // new S&H value each cycle
        }
        return v;
    }

private:
    float valueAt (int shape) const
    {
        const auto p = (float) phase;
        switch (shape)
        {
            case triangle:   return 1.0f - 4.0f * std::abs (p - 0.5f);
            case saw:        return 2.0f * p - 1.0f;
            case square:     return p < 0.5f ? 1.0f : -1.0f;
            case sampleHold: return held;
            default:         return std::sin (juce::MathConstants<float>::twoPi * p);
        }
    }

    double phase = 0.0;
    float  held  = 0.0f;
};
