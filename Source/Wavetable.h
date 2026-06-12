#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <optional>
#include <cmath>

//==============================================================================
// Wavetable
//
// A wavetable is a set of frames (each 2048 samples) the oscillator can morph
// across. Every frame is rendered at 9 band-limited mipmap levels via FFT so
// high notes don't alias. Frames can come from built-in spectra or any WAV
// file (Serum-style: the file is sliced into consecutive 2048-sample frames).
//==============================================================================
class Wavetable
{
public:
    static constexpr int tableSize = 2048;
    static constexpr int numMips   = 9;

    static const std::array<int, numMips>& caps()
    {
        static const std::array<int, numMips> c { 512, 256, 128, 64, 32, 16, 8, 4, 2 };
        return c;
    }

    int getNumFrames() const noexcept { return numFrames; }

    const std::vector<float>& table (int frame, int mip) const noexcept
    {
        return data[(size_t) juce::jlimit (0, numFrames - 1, frame)][(size_t) mip];
    }

    // Build all mip levels from raw (full-bandwidth) frames using FFT:
    // forward transform -> zero bins above the mip's harmonic cap -> inverse.
    void buildFromFrames (const std::vector<std::vector<float>>& frames)
    {
        jassert (! frames.empty());
        numFrames = (int) frames.size();
        data.assign ((size_t) numFrames, {});

        juce::dsp::FFT fft (11); // 2^11 = 2048
        std::vector<float> spectrum (2 * tableSize, 0.0f);
        std::vector<float> work     (2 * tableSize, 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            const auto& src = frames[(size_t) f];
            for (int i = 0; i < tableSize; ++i)
                spectrum[(size_t) i] = i < (int) src.size() ? src[(size_t) i] : 0.0f;

            fft.performRealOnlyForwardTransform (spectrum.data());

            data[(size_t) f].resize (numMips);
            float refPeak = 0.0f;

            for (int m = 0; m < numMips; ++m)
            {
                work = spectrum;

                // Zero DC and every bin above this mip's harmonic cap
                work[0] = work[1] = 0.0f;
                for (int bin = caps()[(size_t) m] + 1; bin < tableSize; ++bin)
                {
                    work[(size_t) (2 * bin)]     = 0.0f;
                    work[(size_t) (2 * bin + 1)] = 0.0f;
                }

                fft.performRealOnlyInverseTransform (work.data());

                auto& t = data[(size_t) f][(size_t) m];
                t.assign (tableSize + 1, 0.0f);
                for (int i = 0; i < tableSize; ++i)
                    t[(size_t) i] = work[(size_t) i];

                // Normalise all mips of a frame by the full-band peak so the
                // level stays consistent when the mip switches with pitch.
                if (m == 0)
                {
                    for (int i = 0; i < tableSize; ++i)
                        refPeak = juce::jmax (refPeak, std::abs (t[(size_t) i]));
                    if (refPeak < 1.0e-9f)
                        refPeak = 1.0f;
                }

                for (auto& s : t)
                    s /= refPeak;

                t[(size_t) tableSize] = t[0]; // guard point for interpolation
            }
        }
    }

private:
    int numFrames = 1;
    std::vector<std::vector<std::vector<float>>> data; // [frame][mip][sample]
};

//==============================================================================
// WavetableBank — the factory tables plus one user slot (WAV import)
//==============================================================================
class WavetableBank
{
public:
    enum Slot { basic = 0, pulse = 1, growl = 2, formant = 3, metallic = 4, reese = 5, acid = 6, organ = 7, user = 8, numSlots };

    void init()
    {
        basicTable.buildFromFrames (makeBasicFrames());
        pulseTable.buildFromFrames (makePulseFrames());
        growlTable.buildFromFrames (makeSpectralFrames (0));
        formantTable.buildFromFrames (makeSpectralFrames (1));
        metallicTable.buildFromFrames (makeSpectralFrames (2));
        reeseTable.buildFromFrames (makeSpectralFrames (3));
        acidTable.buildFromFrames (makeSpectralFrames (4));
        organTable.buildFromFrames (makeSpectralFrames (5));
        userTable = growlTable; // until something is loaded
    }

    const Wavetable& get (int slot) const noexcept
    {
        switch (juce::jlimit (0, numSlots - 1, slot))
        {
            case pulse:    return pulseTable;
            case growl:    return growlTable;
            case formant:  return formantTable;
            case metallic: return metallicTable;
            case reese:    return reeseTable;
            case acid:     return acidTable;
            case organ:    return organTable;
            case user:     return userTable;
            default:       return basicTable;
        }
    }

    void setUserTable (Wavetable wt) { userTable = std::move (wt); }

    // Serum-style import: slice the file into consecutive 2048-sample frames.
    static std::optional<Wavetable> loadFromWav (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 64)
            return std::nullopt;

        const int totalLen = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                            256 * (juce::int64) Wavetable::tableSize);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, totalLen);
        reader->read (&buf, 0, totalLen, 0, true, true);

        // Mono mix
        std::vector<float> mono ((size_t) totalLen, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < totalLen; ++i)
                mono[(size_t) i] += d[i] / (float) buf.getNumChannels();
        }

        const int numFrames = juce::jmax (1, totalLen / Wavetable::tableSize);
        std::vector<std::vector<float>> frames ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            auto& frame = frames[(size_t) f];
            frame.resize (Wavetable::tableSize);
            for (int i = 0; i < Wavetable::tableSize; ++i)
            {
                const int idx = f * Wavetable::tableSize + i;
                frame[(size_t) i] = idx < totalLen
                                      ? mono[(size_t) idx]
                                      : mono[(size_t) (idx % totalLen)]; // loop short files
            }
        }

        Wavetable wt;
        wt.buildFromFrames (frames);
        return wt;
    }

private:
    // sine -> triangle -> saw -> square -> growl
    static std::vector<std::vector<float>> makeBasicFrames()
    {
        auto shapeAmp = [] (int shape, int n) -> double
        {
            switch (shape)
            {
                case 0: return n == 1 ? 1.0 : 0.0;
                case 1: return (n % 2 == 1) ? ((n % 4 == 1 ? 1.0 : -1.0) / double (n * n)) : 0.0;
                case 2: return 1.0 / n;
                case 3: return (n % 2 == 1) ? 1.0 / n : 0.0;
                case 4:
                {
                    const double saw  = 1.0 / n;
                    const double peak = 0.9 * std::exp (-0.5 * std::pow ((n - 12.0) / 3.0, 2.0));
                    return saw + peak;
                }
                default: return 0.0;
            }
        };

        constexpr int numFrames = 8, maxHarm = 512;
        std::vector<std::vector<float>> frames ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const double pos = f / double (numFrames - 1) * 4.0;
            const int    s0  = juce::jlimit (0, 4, (int) std::floor (pos));
            const int    s1  = juce::jlimit (0, 4, s0 + 1);
            const double t   = pos - s0;

            auto& frame = frames[(size_t) f];
            frame.assign (Wavetable::tableSize, 0.0f);

            for (int n = 1; n <= maxHarm; ++n)
            {
                const double a = shapeAmp (s0, n) * (1.0 - t) + shapeAmp (s1, n) * t;
                if (a == 0.0)
                    continue;
                for (int i = 0; i < Wavetable::tableSize; ++i)
                    frame[(size_t) i] += (float) (a * std::sin (juce::MathConstants<double>::twoPi
                                                                * n * i / Wavetable::tableSize));
            }
        }
        return frames;
    }

    // Band-limited PWM sweep: duty 0.5 -> 0.06
    static std::vector<std::vector<float>> makePulseFrames()
    {
        constexpr int numFrames = 8, maxHarm = 512;
        std::vector<std::vector<float>> frames ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const double duty = 0.5 - 0.44 * (f / double (numFrames - 1));
            auto& frame = frames[(size_t) f];
            frame.assign (Wavetable::tableSize, 0.0f);

            for (int n = 1; n <= maxHarm; ++n)
            {
                const double a = (2.0 / (n * juce::MathConstants<double>::pi))
                                 * std::sin (juce::MathConstants<double>::pi * n * duty);
                for (int i = 0; i < Wavetable::tableSize; ++i)
                    frame[(size_t) i] += (float) (a * std::cos (juce::MathConstants<double>::twoPi
                                                                * n * i / Wavetable::tableSize));
            }
        }
        return frames;
    }



    // Bass-oriented factory tables: growl/formant/metallic/reese/acid/organ.
    static std::vector<std::vector<float>> makeSpectralFrames (int flavour)
    {
        constexpr int numFrames = 12, maxHarm = 512;
        std::vector<std::vector<float>> frames ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const double t = f / double (numFrames - 1);
            auto& frame = frames[(size_t) f];
            frame.assign (Wavetable::tableSize, 0.0f);

            for (int n = 1; n <= maxHarm; ++n)
            {
                double a = 0.0, phase = 0.0;

                switch (flavour)
                {
                    case 0: // Growl: sweeping twin spectral peaks over a saw base
                    {
                        const double p1 = 4.0 + 26.0 * t;
                        const double p2 = 18.0 + 38.0 * (1.0 - t);
                        a = 0.45 / n
                          + 1.35 * std::exp (-0.5 * std::pow ((n - p1) / (2.0 + 3.0 * t), 2.0))
                          + 0.95 * std::exp (-0.5 * std::pow ((n - p2) / 5.0, 2.0));
                        phase = 0.35 * std::sin (n * (0.11 + t));
                        break;
                    }
                    case 1: // Formant: vowel-like resonant bands
                    {
                        const double f1 = 3.0 + 10.0 * t;
                        const double f2 = 18.0 + 22.0 * std::sin (t * juce::MathConstants<double>::pi);
                        const double f3 = 48.0 + 44.0 * t;
                        a = 0.10 / n
                          + 1.80 * std::exp (-0.5 * std::pow ((n - f1) / 1.6, 2.0))
                          + 1.10 * std::exp (-0.5 * std::pow ((n - f2) / 3.8, 2.0))
                          + 0.65 * std::exp (-0.5 * std::pow ((n - f3) / 6.5, 2.0));
                        phase = (n % 2 == 0 ? 0.35 : -0.18);
                        break;
                    }
                    case 2: // Metallic: sparse inharmonic-ish partial clusters
                    {
                        const double bend = 1.0 + 0.015 * n * t;
                        const double gate = (std::fmod (n * (1.0 + t * 2.7), 5.0) < 1.9) ? 1.0 : 0.12;
                        a = gate * std::pow (n, -0.62) * (0.35 + 0.65 * std::sin (0.17 * n * bend) * std::sin (0.17 * n * bend));
                        phase = 0.73 * std::sin (n * 0.31 + t * 4.0);
                        break;
                    }
                    case 3: // Reese: detuned beating baked into the frame evolution
                    {
                        const double odd = (n % 2 == 1) ? 1.0 : 0.28;
                        const double comb = 0.62 + 0.38 * std::cos (n * (0.18 + 0.08 * t));
                        a = odd * comb / std::pow (n, 0.82);
                        phase = 0.45 * std::sin (n * (0.09 + 0.13 * t));
                        break;
                    }
                    case 4: // Acid: nasal square-to-saw harmonic tilt
                    {
                        const double odd = (n % 2 == 1) ? 1.0 : 0.14 + 0.65 * t;
                        const double peak = std::exp (-0.5 * std::pow ((n - (8.0 + 46.0 * t)) / 3.0, 2.0));
                        a = odd * (0.50 / n + 0.95 * peak);
                        phase = 0.15 * n * t;
                        break;
                    }
                    default: // Organ: additive drawbars morphing into buzzy bass
                    {
                        const double draw = (n == 1 || n == 2 || n == 3 || n == 4 || n == 6 || n == 8) ? 1.0 : 0.0;
                        a = draw * (1.0 - 0.55 * t) / std::sqrt ((double) n)
                          + t * ((n % 3 == 0) ? 0.55 / n : 0.18 / n);
                        phase = 0.0;
                        break;
                    }
                }

                if (std::abs (a) < 1.0e-9)
                    continue;

                for (int i = 0; i < Wavetable::tableSize; ++i)
                    frame[(size_t) i] += (float) (a * std::sin (juce::MathConstants<double>::twoPi
                                                                * n * i / Wavetable::tableSize + phase));
            }
        }
        return frames;
    }


    Wavetable basicTable, pulseTable, growlTable, formantTable, metallicTable, reeseTable, acidTable, organTable, userTable;
};

//==============================================================================
// WavetableOscillator — normalised phase (0..1), warp modes, FM input,
// 2D interpolation (sample position x morph frame), automatic mip selection.
//==============================================================================
class WavetableOscillator
{
public:
    enum WarpMode { warpOff = 0, warpSync, warpBend, warpMirror, warpFold, warpAsym, warpQuant, warpFM };

    void setTable (const Wavetable* t) noexcept   { table = t; }
    void setSampleRate (double sr) noexcept       { sampleRate = sr; }

    void setFrequency (double hz) noexcept
    {
        increment = hz / sampleRate;

        const double allowed = (sampleRate * 0.5) / juce::jmax (1.0, hz);
        mip = 0;
        while (mip < Wavetable::numMips - 1 && Wavetable::caps()[(size_t) mip] > allowed)
            ++mip;
    }

    void setMorph (float m) noexcept    { morph = juce::jlimit (0.0f, 0.99999f, m); }
    void resetPhase (double p) noexcept { phase = p - std::floor (p); }

    float getNextSample (int warpMode, float warpAmt, float fm) noexcept
    {
        double p = phase;

        switch (warpMode)
        {
            case warpSync:
                p = p * (1.0 + 15.0 * warpAmt);
                p -= std::floor (p);
                break;

            case warpBend:
            {
                const double k = std::pow (4.0, (double) warpAmt * 2.0 - 1.0);
                p = std::pow (p, k);
                break;
            }

            case warpMirror:
            {
                const double mirrored = 1.0 - std::abs (1.0 - 2.0 * p);
                p = p * (1.0 - warpAmt) + mirrored * warpAmt;
                break;
            }

            case warpFold:
            {
                const double folded = std::abs (std::fmod (p * (1.0 + 7.0 * warpAmt), 2.0) - 1.0);
                p = folded;
                break;
            }

            case warpAsym:
            {
                const double skew = 0.5 + 0.46 * (warpAmt * 2.0 - 1.0);
                p = p < skew ? (0.5 * p / juce::jmax (0.001, skew))
                             : (0.5 + 0.5 * (p - skew) / juce::jmax (0.001, 1.0 - skew));
                break;
            }

            case warpQuant:
            {
                const double steps = 2.0 + std::round (62.0 * (1.0 - warpAmt));
                p = std::floor (p * steps) / steps;
                break;
            }

            default: break; // off / FM (FM arrives via 'fm')
        }

        p += fm;
        p -= std::floor (p);

        const int   numFrames = table->getNumFrames();
        const float framePos  = morph * (float) (numFrames - 1);
        const int   f0        = (int) framePos;
        const int   f1        = juce::jmin (f0 + 1, numFrames - 1);
        const float ft        = framePos - (float) f0;

        const double sp = p * Wavetable::tableSize;
        const int    i0 = (int) sp;
        const float  st = (float) (sp - i0);

        const auto& tA = table->table (f0, mip);
        const auto& tB = table->table (f1, mip);

        const float sA = tA[(size_t) i0] + st * (tA[(size_t) i0 + 1] - tA[(size_t) i0]);
        const float sB = tB[(size_t) i0] + st * (tB[(size_t) i0 + 1] - tB[(size_t) i0]);

        phase += increment;
        phase -= std::floor (phase);

        return sA + ft * (sB - sA);
    }

private:
    const Wavetable* table = nullptr;
    double sampleRate = 44100.0, phase = 0.0, increment = 0.0;
    float  morph = 0.0f;
    int    mip = 0;
};
