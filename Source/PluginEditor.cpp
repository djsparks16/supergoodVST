#include "PluginEditor.h"
#include "Modulation.h"

//==============================================================================
// Palette + shared glass painting
//==============================================================================
namespace
{
    constexpr auto bg0     = 0xff070810u;
    constexpr auto bg1     = 0xff101220u;
    constexpr auto glass   = 0xaa171a2cu;
    constexpr auto cyan    = 0xff20f7ffu;
    constexpr auto magenta = 0xffff2bd6u;
    constexpr auto violet  = 0xff8a5cffu;
    constexpr auto lime    = 0xff7cff4fu;
    constexpr auto amber   = 0xffffb020u;

    const juce::Colour textHi  (0xfff4fdff);
    const juce::Colour textLo  (0xffaccbdd);

    using APVTS = juce::AudioProcessorValueTreeState;

    void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent,
                         float corner = 12.0f, int shadow = 12)
    {
        if (shadow > 0)
            juce::DropShadow (accent.withAlpha (0.22f), shadow, { 0, 0 }).drawForRectangle (g, r.toNearestInt());

        juce::ColourGradient fill (juce::Colour (0x55264ffa).withAlpha (0.33f), r.getTopLeft(),
                                   juce::Colour (glass), r.getBottomRight(), false);
        fill.addColour (0.45, juce::Colour (0x9920253a));
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, corner);

        g.setColour (juce::Colour (0x18ffffff));
        g.drawRoundedRectangle (r.reduced (1.0f), corner, 1.0f);

        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);

        // Neon edge light along the top
        auto top = r.withHeight (3.0f).reduced (12.0f, 0.0f);
        juce::ColourGradient glow (accent.withAlpha (0.0f), top.getTopLeft(),
                                   accent.withAlpha (0.85f), top.getCentre(), false);
        glow.addColour (1.0, accent.withAlpha (0.0f));
        g.setGradientFill (glow);
        g.fillRoundedRectangle (top, 2.0f);

        // Stained-glass leading lines on larger panels only
        if (r.getHeight() > 60.0f)
        {
            juce::Path shards;
            shards.startNewSubPath (r.getX() + r.getWidth() * 0.08f, r.getBottom() - 1.0f);
            shards.lineTo (r.getX() + r.getWidth() * 0.34f, r.getY() + 1.0f);
            shards.startNewSubPath (r.getX() + r.getWidth() * 0.67f, r.getY() + 1.0f);
            shards.lineTo (r.getRight() - r.getWidth() * 0.12f, r.getBottom() - 1.0f);
            g.setColour (accent.withAlpha (0.09f));
            g.strokePath (shards, juce::PathStrokeType (1.0f));
        }
    }

    // Dark inset "screen" used behind waveform / envelope / LFO / filter displays
    void drawDisplayScreen (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xcc05060c));
        g.fillRoundedRectangle (r, 7.0f);

        juce::ColourGradient sheen (juce::Colour (0x10ffffff), r.getTopLeft(),
                                    juce::Colour (0x00000000), r.getBottomLeft(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f), 7.0f);

        g.setColour (accent.withAlpha (0.30f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

        // Faint centre grid line
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4.0f, r.getRight() - 4.0f);
    }

    void strokeNeon (juce::Graphics& g, const juce::Path& p, juce::Colour accent, float coreWidth = 1.6f)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (coreWidth + 3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (coreWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

//==============================================================================
// NeonObsidianLookAndFeel
//==============================================================================
NeonObsidianLookAndFeel::NeonObsidianLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (cyan));
    setColour (juce::Slider::thumbColourId,             juce::Colour (magenta));
    setColour (juce::Slider::trackColourId,             juce::Colour (cyan));
    setColour (juce::Slider::textBoxTextColourId,       textHi);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x55111322));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0x6634d2eb));
    setColour (juce::ComboBox::backgroundColourId,      juce::Colour (0x88101624));
    setColour (juce::ComboBox::textColourId,            textHi);
    setColour (juce::ComboBox::outlineColourId,         juce::Colour (cyan).withAlpha (0.65f));
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff0c0f1a));
    setColour (juce::PopupMenu::textColourId,           textHi);
    setColour (juce::TextButton::buttonColourId,        juce::Colour (0x88101624));
    setColour (juce::TextButton::textColourOffId,       textHi);
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf00c0f1a));
    setColour (juce::BubbleComponent::outlineColourId,    juce::Colour (cyan).withAlpha (0.7f));
}

void NeonObsidianLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPosProportional, float rotaryStartAngle,
                                                float rotaryEndAngle, juce::Slider& slider)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const auto size = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);

    const auto radius = size * 0.5f;
    const auto centre = b.getCentre();
    const auto angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // Glow scaled to knob size (keeps small FX knobs crisp)
    juce::Path shadowPath;
    shadowPath.addEllipse (b);
    juce::DropShadow (accent.withAlpha (0.40f), juce::jlimit (5, 14, (int) (radius * 0.5f)), { 0, 0 })
        .drawForPath (g, shadowPath);

    juce::ColourGradient body (juce::Colour (0xff232842), b.getTopLeft(),
                               juce::Colour (0xff080a12), b.getBottomRight(), false);
    body.addColour (0.25, juce::Colour (0xff303754));
    g.setGradientFill (body);
    g.fillEllipse (b);

    g.setColour (juce::Colour (0x30ffffff));
    g.drawEllipse (b.reduced (1.0f), 1.0f);

    juce::Path bgArc, valueArc;
    const auto arcRadius = radius - 3.0f;
    const auto arcWidth  = juce::jlimit (2.5f, 4.0f, radius * 0.14f);
    bgArc.addCentredArc    (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (juce::Colour (0x55252b45));
    g.strokePath (bgArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withAlpha (0.92f));
    g.strokePath (valueArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.4f, -radius + 7.0f, 2.8f, radius * 0.42f, 1.4f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (textHi);
    g.fillPath (pointer);

    auto hi = b.reduced (b.getWidth() * 0.22f, b.getHeight() * 0.18f)
               .translated (-b.getWidth() * 0.08f, -b.getHeight() * 0.12f);
    g.setColour (juce::Colour (0x14ffffff));
    g.fillEllipse (hi);
}

void NeonObsidianLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPos, float minSliderPos, float maxSliderPos,
                                                const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    auto track  = juce::Rectangle<float> ((float) x, (float) y + height * 0.5f - 2.5f, (float) width, 5.0f).reduced (2.0f, 0.0f);
    auto accent = slider.findColour (juce::Slider::trackColourId);

    g.setColour (juce::Colour (0x7722283e));
    g.fillRoundedRectangle (track, 3.0f);

    // Bipolar parameters (e.g. mod amounts) fill out from the centre
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
    {
        const float zeroX = track.getX() + track.getWidth()
                              * (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()));
        auto fill = sliderPos >= zeroX ? juce::Rectangle<float> (zeroX, track.getY(), sliderPos - zeroX, track.getHeight())
                                       : juce::Rectangle<float> (sliderPos, track.getY(), zeroX - sliderPos, track.getHeight());
        g.setColour (accent.withAlpha (0.88f));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colour (0x66ffffff));
        g.fillRect (juce::Rectangle<float> (zeroX - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f));
    }
    else
    {
        g.setColour (accent.withAlpha (0.88f));
        g.fillRoundedRectangle (track.withRight (sliderPos), 3.0f);
    }

    g.setColour (textHi);
    g.fillEllipse (sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void NeonObsidianLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                    const juce::Colour&, bool over, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (1.0f);
    auto accent = down ? juce::Colour (magenta) : (over ? juce::Colour (lime) : juce::Colour (cyan));
    drawGlassPanel (g, b, accent, 8.0f, 6);
}

void NeonObsidianLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                            int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    auto accent = box.findColour (juce::ComboBox::outlineColourId);
    drawGlassPanel (g, b, isButtonDown ? juce::Colour (magenta) : accent, 7.0f, 0);

    juce::Path arrow;
    arrow.addTriangle ((float) width - 16.0f, height * 0.42f,
                       (float) width - 8.0f,  height * 0.42f,
                       (float) width - 12.0f, height * 0.62f);
    g.setColour (textHi);
    g.fillPath (arrow);
}

void NeonObsidianLookAndFeel::positionComboBoxText (juce::ComboBox&, juce::Label& label)
{
    label.setBounds (6, 1, label.getParentWidth() - 22, label.getParentHeight() - 2);
    label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    label.setJustificationType (juce::Justification::centredLeft);
}

void NeonObsidianLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                                bool over, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (2.0f);
    auto accent = button.getToggleState() ? juce::Colour (lime) : juce::Colour (0xff626b86);
    if (over || down) accent = accent.brighter (0.25f);

    const auto pillW = juce::jmin (b.getWidth(), 30.0f);
    const auto pillH = juce::jmin (b.getHeight(), 14.0f);

    juce::Rectangle<float> pill;
    if (button.getButtonText().isNotEmpty())
    {
        pill = juce::Rectangle<float> (b.getRight() - pillW, b.getCentreY() - pillH * 0.5f, pillW, pillH);
        g.setColour (button.getToggleState() ? textHi : textLo);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (button.getButtonText().toUpperCase(),
                    b.withTrimmedRight (pillW + 3.0f).toNearestInt(), juce::Justification::centredLeft);
    }
    else
    {
        pill = b.withSizeKeepingCentre (pillW, pillH);
    }

    g.setColour (juce::Colour (0xcc0a0c16));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);

    const auto d = pill.getHeight() - 4.0f;
    const auto knobX = button.getToggleState() ? pill.getRight() - d - 2.0f : pill.getX() + 2.0f;
    g.setColour (button.getToggleState() ? textHi : juce::Colour (0xff626b86));
    g.fillEllipse (knobX, pill.getY() + 2.0f, d, d);

    if (button.getToggleState())
    {
        juce::Path p;
        p.addEllipse (knobX, pill.getY() + 2.0f, d, d);
        juce::DropShadow (juce::Colour (lime).withAlpha (0.6f), 7, { 0, 0 }).drawForPath (g, p);
    }
}

//==============================================================================
// Building blocks
//==============================================================================
namespace
{
    //--------------------------------------------------------------------------
    // Knob: rotary slider + uppercase caption, value shown in a popup on drag.
    //--------------------------------------------------------------------------
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> att;

        void init (juce::Component& parent, APVTS& apvts, const juce::String& paramID,
                   const juce::String& caption, juce::Colour accent)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setPopupDisplayEnabled (true, false, nullptr);
            slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            slider.setColour (juce::Slider::thumbColourId, accent);
            parent.addAndMakeVisible (slider);
            att = std::make_unique<APVTS::SliderAttachment> (apvts, paramID, slider);

            label.setText (caption.toUpperCase(), juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, textLo);
            label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            label.setInterceptsMouseClicks (false, false);
            parent.addAndMakeVisible (label);
        }

        void setBounds (juce::Rectangle<int> cell)
        {
            label.setBounds (cell.removeFromBottom (13));
            slider.setBounds (cell);
        }
    };

    //--------------------------------------------------------------------------
    // Choice: combo box bound to an AudioParameterChoice.
    //--------------------------------------------------------------------------
    struct Choice
    {
        juce::ComboBox box;
        std::unique_ptr<APVTS::ComboBoxAttachment> att;

        void init (juce::Component& parent, APVTS& apvts, const juce::String& paramID, juce::Colour accent)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
                box.addItemList (choice->choices, 1);
            box.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.55f));
            parent.addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (apvts, paramID, box);
        }
    };

    //--------------------------------------------------------------------------
    // GlassCard: panel with a neon header strip + title; children fill content()
    //--------------------------------------------------------------------------
    class GlassCard : public juce::Component
    {
    public:
        GlassCard (juce::String titleIn, juce::Colour accentIn)
            : title (std::move (titleIn)), accent (accentIn) {}

        juce::Colour getAccent() const noexcept { return accent; }

        juce::Rectangle<int> content() const
        {
            return getLocalBounds().reduced (9).withTrimmedTop (headerHeight + 2);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, accent, 12.0f, 9);

            auto strip = getLocalBounds().reduced (7).removeFromTop (headerHeight).toFloat();
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (strip, 6.0f);

            g.setColour (textHi);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (title, strip.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }

        static constexpr int headerHeight = 19;

    private:
        juce::String title;
        juce::Colour accent;
    };

    //--------------------------------------------------------------------------
    // WavetableDisplay: live view of the oscillator's current morph frame.
    //--------------------------------------------------------------------------
    class WavetableDisplay : public juce::Component, private juce::Timer
    {
    public:
        WavetableDisplay (ObsidianAudioProcessor& p, juce::String tableID, juce::String morphID,
                          juce::Colour accentIn)
            : processor (p), accent (accentIn)
        {
            tableParam = p.apvts.getRawParameterValue (tableID);
            morphParam = p.apvts.getRawParameterValue (morphID);
            startTimerHz (12);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto inner = r.reduced (5.0f, 6.0f);
            const int  n     = juce::jmax (16, (int) inner.getWidth());

            const int   tableIdx = (int) tableParam->load();
            const float morph    = morphParam->load();

            const auto& wt        = processor.getBank().get (tableIdx);
            const int   numFrames = wt.getNumFrames();
            const float framePos  = morph * (float) (numFrames - 1);
            const int   f0        = juce::jlimit (0, numFrames - 1, (int) framePos);
            const int   f1        = juce::jmin (f0 + 1, numFrames - 1);
            const float ft        = framePos - (float) f0;

            const auto& tA = wt.table (f0, 0);
            const auto& tB = wt.table (f1, 0);

            juce::Path wave;
            for (int i = 0; i < n; ++i)
            {
                const float pos = (float) i / (float) (n - 1);
                const int   idx = juce::jlimit (0, Wavetable::tableSize - 1,
                                                (int) (pos * (float) (Wavetable::tableSize - 1)));
                const float s   = tA[(size_t) idx] + ft * (tB[(size_t) idx] - tA[(size_t) idx]);
                const float xx  = inner.getX() + pos * inner.getWidth();
                const float yy  = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, s) * inner.getHeight() * 0.46f;

                if (i == 0) wave.startNewSubPath (xx, yy);
                else        wave.lineTo (xx, yy);
            }

            // Soft fill under the curve
            juce::Path fill (wave);
            fill.lineTo (inner.getRight(), inner.getCentreY());
            fill.lineTo (inner.getX(), inner.getCentreY());
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.08f));
            g.fillPath (fill);

            strokeNeon (g, wave, accent);

            // Frame position readout, Serum-style
            g.setColour (textLo.withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (f0 + 1) + " / " + juce::String (numFrames),
                        getLocalBounds().reduced (8, 4), juce::Justification::topRight);
        }

    private:
        void timerCallback() override
        {
            // Cheap change signature: table choice, morph, frame count and a few spot samples
            const int   tableIdx = (int) tableParam->load();
            const auto& wt       = processor.getBank().get (tableIdx);

            float sig = (float) tableIdx * 1000.0f + morphParam->load() * 97.0f
                        + (float) wt.getNumFrames();
            const auto& t0 = wt.table (0, 0);
            for (int i = 0; i < Wavetable::tableSize; i += Wavetable::tableSize / 7)
                sig += t0[(size_t) i];

            if (std::abs (sig - lastSig) > 1.0e-6f)
            {
                lastSig = sig;
                repaint();
            }
        }

        ObsidianAudioProcessor& processor;
        juce::Colour accent;
        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* morphParam = nullptr;
        float lastSig = -1.0e9f;
    };

    //--------------------------------------------------------------------------
    // ADSRDisplay
    //--------------------------------------------------------------------------
    class ADSRDisplay : public juce::Component, private juce::Timer
    {
    public:
        ADSRDisplay (APVTS& apvts, const juce::String& prefix, juce::Colour accentIn,
                     float maxAD = 5.0f, float maxR = 8.0f)
            : accent (accentIn), maxAttackDecay (maxAD), maxRelease (maxR)
        {
            a = apvts.getRawParameterValue (prefix + "A");
            d = apvts.getRawParameterValue (prefix + "D");
            s = apvts.getRawParameterValue (prefix + "S");
            r = apvts.getRawParameterValue (prefix + "R");
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto rect = getLocalBounds().toFloat();
            drawDisplayScreen (g, rect, accent);

            const auto in   = rect.reduced (7.0f, 8.0f);
            const float top = in.getY(), bot = in.getBottom();

            auto seg = [] (float v, float maxV) { return 0.02f + 0.28f * std::pow (v / maxV, 0.45f); };

            const float wa = seg (a->load(), maxAttackDecay);
            const float wd = seg (d->load(), maxAttackDecay);
            const float wr = seg (r->load(), maxRelease);
            const float ws = juce::jmax (0.08f, 1.0f - wa - wd - wr);
            const float total = wa + wd + ws + wr;

            const float xA = in.getX() + in.getWidth() * (wa / total);
            const float xD = xA        + in.getWidth() * (wd / total);
            const float xS = xD        + in.getWidth() * (ws / total);
            const float yS = bot - s->load() * (bot - top);

            juce::Path p;
            p.startNewSubPath (in.getX(), bot);
            p.quadraticTo (in.getX() + (xA - in.getX()) * 0.4f, top, xA, top);          // attack
            p.quadraticTo (xA + (xD - xA) * 0.3f, yS, xD, yS);                          // decay
            p.lineTo (xS, yS);                                                          // sustain
            p.quadraticTo (xS + (in.getRight() - xS) * 0.3f, bot, in.getRight(), bot);  // release

            juce::Path fill (p);
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.10f));
            g.fillPath (fill);

            strokeNeon (g, p, accent, 1.5f);

            g.setColour (textHi);
            for (auto pt : { juce::Point<float> (xA, top),
                             juce::Point<float> (xD, yS),
                             juce::Point<float> (xS, yS) })
                g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
        }

    private:
        void timerCallback() override
        {
            const float sig = a->load() * 7.1f + d->load() * 3.3f + s->load() * 11.7f + r->load();
            if (std::abs (sig - lastSig) > 1.0e-6f) { lastSig = sig; repaint(); }
        }

        juce::Colour accent;
        float maxAttackDecay, maxRelease;
        std::atomic<float>* a = nullptr; std::atomic<float>* d = nullptr;
        std::atomic<float>* s = nullptr; std::atomic<float>* r = nullptr;
        float lastSig = -1.0e9f;
    };

    //--------------------------------------------------------------------------
    // LFODisplay: one cycle of the selected shape.
    //--------------------------------------------------------------------------
    class LFODisplay : public juce::Component, private juce::Timer
    {
    public:
        LFODisplay (APVTS& apvts, const juce::String& shapeID, juce::Colour accentIn)
            : accent (accentIn)
        {
            shape = apvts.getRawParameterValue (shapeID);
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto in = r.reduced (7.0f, 8.0f);
            auto yOf = [&] (float v) { return in.getCentreY() - v * in.getHeight() * 0.45f; };

            juce::Path p;
            const int sh = (int) shape->load();

            if (sh == 4) // S&H: deterministic preview steps
            {
                static const float steps[] = { 0.35f, -0.6f, 0.85f, -0.2f, 0.55f, -0.9f, 0.1f, -0.45f };
                const int n = (int) (sizeof (steps) / sizeof (steps[0]));
                for (int i = 0; i < n; ++i)
                {
                    const float x0 = in.getX() + in.getWidth() * (float) i / (float) n;
                    const float x1 = in.getX() + in.getWidth() * (float) (i + 1) / (float) n;
                    if (i == 0) p.startNewSubPath (x0, yOf (steps[i]));
                    else        p.lineTo (x0, yOf (steps[i]));
                    p.lineTo (x1, yOf (steps[i]));
                }
            }
            else if (sh == 3) // square
            {
                p.startNewSubPath (in.getX(), yOf (1.0f));
                p.lineTo (in.getCentreX(), yOf (1.0f));
                p.lineTo (in.getCentreX(), yOf (-1.0f));
                p.lineTo (in.getRight(),  yOf (-1.0f));
            }
            else if (sh == 2) // saw
            {
                p.startNewSubPath (in.getX(), yOf (-1.0f));
                p.lineTo (in.getRight(), yOf (1.0f));
            }
            else if (sh == 1) // triangle
            {
                p.startNewSubPath (in.getX(), yOf (-1.0f));
                p.lineTo (in.getCentreX(), yOf (1.0f));
                p.lineTo (in.getRight(), yOf (-1.0f));
            }
            else // sine
            {
                const int n = juce::jmax (16, (int) in.getWidth());
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / (float) (n - 1);
                    const float x = in.getX() + t * in.getWidth();
                    const float y = yOf (std::sin (juce::MathConstants<float>::twoPi * t));
                    if (i == 0) p.startNewSubPath (x, y);
                    else        p.lineTo (x, y);
                }
            }

            strokeNeon (g, p, accent, 1.5f);
        }

    private:
        void timerCallback() override
        {
            const float v = shape->load();
            if (v != last) { last = v; repaint(); }
        }

        juce::Colour accent;
        std::atomic<float>* shape = nullptr;
        float last = -1.0f;
    };

    //--------------------------------------------------------------------------
    // FilterDisplay: stylised response curve from model / cutoff / resonance.
    //--------------------------------------------------------------------------
    class FilterDisplay : public juce::Component, private juce::Timer
    {
    public:
        FilterDisplay (APVTS& apvts, juce::Colour accentIn) : accent (accentIn)
        {
            model  = apvts.getRawParameterValue ("fltModel");
            cutoff = apvts.getRawParameterValue ("cutoff");
            reso   = apvts.getRawParameterValue ("reso");
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto in = r.reduced (6.0f, 7.0f);
            const int  m  = (int) model->load();
            const float fc = juce::jlimit (20.0f, 20000.0f, cutoff->load());
            const float resoDb = juce::jlimit (0.0f, 18.0f,
                                               20.0f * std::log10 (juce::jmax (0.1f, reso->load()) / 0.707f));
            const float slope = (m == 3 ? 24.0f : 12.0f);

            auto dbToY = [&] (float db)
            {
                const float norm = (24.0f - juce::jlimit (-36.0f, 24.0f, db)) / 60.0f;
                return in.getY() + norm * in.getHeight();
            };

            juce::Path p;
            const int n = juce::jmax (24, (int) in.getWidth() / 2);

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) (n - 1);
                const float f = 20.0f * std::pow (1000.0f, t);           // 20 Hz .. 20 kHz, log axis
                const float octs = std::log2 (f / fc);

                float db = 0.0f;
                if (m == 1)       db = -slope * 0.75f * std::abs (octs); // band pass
                else if (m == 2)  db = octs < 0.0f ? slope * octs : 0.0f; // high pass
                else              db = octs > 0.0f ? -slope * octs : 0.0f; // low pass (SVF or ladder)

                db += resoDb * std::exp (-octs * octs * 7.0f);           // resonance bump at cutoff

                const float x = in.getX() + t * in.getWidth();
                const float y = dbToY (db);
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }

            // 0 dB reference
            g.setColour (juce::Colour (0x12ffffff));
            g.drawHorizontalLine ((int) dbToY (0.0f), in.getX(), in.getRight());

            juce::Path fill (p);
            fill.lineTo (in.getRight(), in.getBottom());
            fill.lineTo (in.getX(), in.getBottom());
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.08f));
            g.fillPath (fill);

            strokeNeon (g, p, accent, 1.5f);
        }

    private:
        void timerCallback() override
        {
            const float sig = model->load() * 100.0f + cutoff->load() * 0.013f + reso->load() * 3.0f;
            if (std::abs (sig - lastSig) > 1.0e-5f) { lastSig = sig; repaint(); }
        }

        juce::Colour accent;
        std::atomic<float>* model = nullptr;
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* reso = nullptr;
        float lastSig = -1.0e9f;
    };

    //==========================================================================
    // Panels
    //==========================================================================
    class OscPanel : public GlassCard
    {
    public:
        OscPanel (ObsidianAudioProcessor& p, const juce::String& prefix, // "oscA" / "oscB"
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent),
              display (p, prefix + "Table", prefix + "Morph", accent)
        {
            auto& apvts = p.apvts;
            table.init (*this, apvts, prefix + "Table", accent);
            warp.init  (*this, apvts, prefix + "WarpMode", accent);
            addAndMakeVisible (display);

            morph.init   (*this, apvts, prefix + "Morph",   "Morph",  accent);
            warpAmt.init (*this, apvts, prefix + "WarpAmt", "Warp",   accent);
            semi.init    (*this, apvts, prefix + "Semi",    "Semi",   accent);
            fine.init    (*this, apvts, prefix + "Fine",    "Fine",   accent);
            level.init   (*this, apvts, prefix + "Level",   "Level",  accent);
        }

        void resized() override
        {
            auto r = content();

            auto comboRow = r.removeFromTop (22);
            table.box.setBounds (comboRow.removeFromLeft (comboRow.getWidth() / 2).reduced (1, 0));
            warp.box.setBounds  (comboRow.reduced (1, 0));
            r.removeFromTop (5);

            auto knobRow = r.removeFromBottom (juce::jmin (74, r.getHeight() / 2));
            const int cellW = knobRow.getWidth() / 5;
            morph.setBounds   (knobRow.removeFromLeft (cellW));
            warpAmt.setBounds (knobRow.removeFromLeft (cellW));
            semi.setBounds    (knobRow.removeFromLeft (cellW));
            fine.setBounds    (knobRow.removeFromLeft (cellW));
            level.setBounds   (knobRow);

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice table, warp;
        WavetableDisplay display;
        Knob morph, warpAmt, semi, fine, level;
    };

    class SubNoisePanel : public GlassCard
    {
    public:
        SubNoisePanel (APVTS& apvts, juce::Colour accent) : GlassCard ("SUB / NOISE", accent)
        {
            oct.init   (*this, apvts, "subOct", accent);
            wave.init  (*this, apvts, "subWave", accent);
            sub.init   (*this, apvts, "subLevel",   "Sub",   accent);
            noise.init (*this, apvts, "noiseLevel", "Noise", accent);
        }

        void resized() override
        {
            auto r = content();
            oct.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);
            wave.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);

            const int half = r.getHeight() / 2;
            sub.setBounds   (r.removeFromTop (half).reduced (4, 2));
            noise.setBounds (r.reduced (4, 2));
        }

    private:
        Choice oct, wave;
        Knob sub, noise;
    };

    class FilterPanel : public GlassCard
    {
    public:
        FilterPanel (APVTS& apvts, juce::Colour accent)
            : GlassCard ("FILTER", accent), display (apvts, accent)
        {
            model.init  (*this, apvts, "fltModel", accent);
            addAndMakeVisible (display);
            cutoff.init (*this, apvts, "cutoff",    "Cutoff", accent);
            reso.init   (*this, apvts, "reso",      "Reso",   accent);
            drive.init  (*this, apvts, "fltDrive",  "Drive",  accent);
            envAmt.init (*this, apvts, "fltEnvAmt", "Env 2",  accent);
        }

        void resized() override
        {
            auto r = content();
            model.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (5);

            auto knobRow = r.removeFromBottom (juce::jmin (74, r.getHeight() / 2));
            const int cellW = knobRow.getWidth() / 4;
            cutoff.setBounds (knobRow.removeFromLeft (cellW));
            reso.setBounds   (knobRow.removeFromLeft (cellW));
            drive.setBounds  (knobRow.removeFromLeft (cellW));
            envAmt.setBounds (knobRow);

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice model;
        FilterDisplay display;
        Knob cutoff, reso, drive, envAmt;
    };

    class EnvPanel : public GlassCard
    {
    public:
        EnvPanel (APVTS& apvts, const juce::String& prefix, // "amp" / "env2"
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent), display (apvts, prefix, accent)
        {
            addAndMakeVisible (display);
            a.init (*this, apvts, prefix + "A", "A", accent);
            d.init (*this, apvts, prefix + "D", "D", accent);
            s.init (*this, apvts, prefix + "S", "S", accent);
            r.init (*this, apvts, prefix + "R", "R", accent);
        }

        void resized() override
        {
            auto rect = content();
            auto knobRow = rect.removeFromBottom (juce::jmin (66, rect.getHeight() / 2));
            const int cellW = knobRow.getWidth() / 4;
            a.setBounds (knobRow.removeFromLeft (cellW));
            d.setBounds (knobRow.removeFromLeft (cellW));
            s.setBounds (knobRow.removeFromLeft (cellW));
            r.setBounds (knobRow);

            rect.removeFromBottom (4);
            display.setBounds (rect);
        }

    private:
        ADSRDisplay display;
        Knob a, d, s, r;
    };

    class LfoPanel : public GlassCard
    {
    public:
        LfoPanel (APVTS& apvts, int index, bool withCutoffSend,
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent),
              display (apvts, "lfo" + juce::String (index) + "Shape", accent)
        {
            const auto n = juce::String (index);
            shape.init (*this, apvts, "lfo" + n + "Shape", accent);
            div.init   (*this, apvts, "lfo" + n + "Div",   accent);

            sync.setButtonText ("Sync");
            addAndMakeVisible (sync);
            syncAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, "lfo" + n + "Sync", sync);

            addAndMakeVisible (display);
            rate.init (*this, apvts, "lfo" + n + "Rate", "Rate", accent);

            if (withCutoffSend)
            {
                cut = std::make_unique<Knob>();
                cut->init (*this, apvts, "lfo1Cut", "> Cutoff", accent);
            }
        }

        void resized() override
        {
            auto r = content();

            auto row = r.removeFromTop (22);
            shape.box.setBounds (row.removeFromLeft (juce::roundToInt (row.getWidth() * 0.40f)).reduced (1, 0));
            sync.setBounds (row.removeFromLeft (62).reduced (2, 0));
            div.box.setBounds (row.reduced (1, 0));
            r.removeFromTop (4);

            auto knobRow = r.removeFromBottom (juce::jmin (66, r.getHeight() / 2));
            if (cut != nullptr)
            {
                const int cellW = knobRow.getWidth() / 2;
                rate.setBounds (knobRow.removeFromLeft (cellW));
                cut->setBounds (knobRow);
            }
            else
            {
                rate.setBounds (knobRow.withSizeKeepingCentre (knobRow.getWidth() / 2, knobRow.getHeight()));
            }

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice shape, div;
        juce::ToggleButton sync;
        std::unique_ptr<APVTS::ButtonAttachment> syncAtt;
        LFODisplay display;
        Knob rate;
        std::unique_ptr<Knob> cut;
    };

    class VoicePanel : public GlassCard
    {
    public:
        VoicePanel (APVTS& apvts, juce::Colour accent) : GlassCard ("VOICE", accent)
        {
            voices.init (*this, apvts, "uniCount",  "Unison", accent);
            detune.init (*this, apvts, "uniDetune", "Detune", accent);
            width.init  (*this, apvts, "uniWidth",  "Width",  accent);
            glide.init  (*this, apvts, "glideTime", "Glide",  accent);
            bend.init   (*this, apvts, "bendRange", "Bend",   accent);
        }

        void resized() override
        {
            auto r = content();
            auto topRow = r.removeFromTop (r.getHeight() / 2);

            const int w3 = topRow.getWidth() / 3;
            voices.setBounds (topRow.removeFromLeft (w3));
            detune.setBounds (topRow.removeFromLeft (w3));
            width.setBounds  (topRow);

            const int w2 = r.getWidth() / 2;
            glide.setBounds (r.removeFromLeft (w2).reduced (8, 0));
            bend.setBounds  (r.reduced (8, 0));
        }

    private:
        Knob voices, detune, width, glide, bend;
    };

    //--------------------------------------------------------------------------
    // MatrixPanel: 8 compact rows of source -> destination -> amount
    //--------------------------------------------------------------------------
    class MatrixPanel : public GlassCard
    {
    public:
        MatrixPanel (APVTS& apvts, juce::Colour accent) : GlassCard ("MOD MATRIX", accent)
        {
            for (int s = 1; s <= Mod::numSlots; ++s)
            {
                auto row = std::make_unique<Row>();
                const auto n = juce::String (s);

                row->index.setText (n, juce::dontSendNotification);
                row->index.setJustificationType (juce::Justification::centred);
                row->index.setColour (juce::Label::textColourId, accent.withAlpha (0.8f));
                row->index.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                row->index.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (row->index);

                row->src.addItemList (Mod::sourceNames, 1);
                row->dst.addItemList (Mod::destNames, 1);
                row->src.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
                row->dst.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
                addAndMakeVisible (row->src);
                addAndMakeVisible (row->dst);

                row->amt.setSliderStyle (juce::Slider::LinearHorizontal);
                row->amt.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                row->amt.setPopupDisplayEnabled (true, false, nullptr);
                row->amt.setColour (juce::Slider::trackColourId, accent);
                addAndMakeVisible (row->amt);

                row->srcAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, "mod" + n + "Src", row->src);
                row->dstAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, "mod" + n + "Dst", row->dst);
                row->amtAtt = std::make_unique<APVTS::SliderAttachment>   (apvts, "mod" + n + "Amt", row->amt);

                rows.push_back (std::move (row));
            }
        }

        void resized() override
        {
            auto r = content();
            const int rowH = r.getHeight() / Mod::numSlots;

            for (auto& row : rows)
            {
                auto line = r.removeFromTop (rowH).reduced (0, juce::jmax (1, (rowH - 22) / 2));
                row->index.setBounds (line.removeFromLeft (16));
                row->src.setBounds   (line.removeFromLeft (juce::roundToInt (line.getWidth() * 0.30f)).reduced (2, 0));
                row->dst.setBounds   (line.removeFromLeft (juce::roundToInt (line.getWidth() * 0.48f)).reduced (2, 0));
                row->amt.setBounds   (line.reduced (2, 0));
            }
        }

    private:
        struct Row
        {
            juce::Label index;
            juce::ComboBox src, dst;
            juce::Slider amt;
            std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt, dstAtt;
            std::unique_ptr<APVTS::SliderAttachment> amtAtt;
        };
        std::vector<std::unique_ptr<Row>> rows;
    };

    //--------------------------------------------------------------------------
    // FxPanel: six mini cards in a 3x2 grid, each with a power switch.
    //--------------------------------------------------------------------------
    class FxPanel : public GlassCard
    {
    public:
        FxPanel (APVTS& apvts, juce::Colour accent) : GlassCard ("FX RACK", accent)
        {
            struct Def { const char* name; const char* onID;
                         std::vector<std::pair<const char*, const char*>> params; };

            const std::vector<Def> defs =
            {
                { "WOMP",   "fxWompOn",   { { "fxWompRate",  "Rate" }, { "fxWompDepth", "Depth" }, { "fxWompMix", "Mix" } } },
                { "CRUSH",  "fxCrushOn",  { { "fxCrushBits", "Bits" }, { "fxCrushRate",  "Rate"  }, { "fxCrushMix", "Mix" } } },
                { "DIST",   "fxDistOn",   { { "fxDistDrive",   "Drive" }, { "fxDistMix",   "Mix"   } } },
                { "CHORUS", "fxChorusOn", { { "fxChorusRate",  "Rate"  }, { "fxChorusDepth","Depth"}, { "fxChorusMix", "Mix" } } },
                { "PHASER", "fxPhaserOn", { { "fxPhaserRate",  "Rate"  }, { "fxPhaserDepth","Depth"}, { "fxPhaserMix", "Mix" } } },
                { "DELAY",  "fxDelayOn",  { { "fxDelayTime",   "Time"  }, { "fxDelayFb",   "FB"    }, { "fxDelayMix",  "Mix" } } },
                { "REVERB", "fxRevOn",    { { "fxRevSize",     "Size"  }, { "fxRevDamp",   "Damp"  }, { "fxRevWidth",  "Width" }, { "fxRevMix", "Mix" } } },
                { "WIDTH",  "fxWidthOn",  { { "fxWidthAmt", "Wide" } } },
                { "HYPER",  "fxHyperOn",  { { "fxHyperAmt", "Amt" }, { "fxHyperMix", "Mix" } } },
                { "EQ",     "fxEqOn",     { { "fxEqLow", "Low" }, { "fxEqHigh", "High" } } },
                { "COMP",   "fxCompOn",   { { "fxCompThresh",  "Thresh"}, { "fxCompRatio", "Ratio" } } },
                { "LIMIT",  "fxLimitOn",  { { "fxLimitDrive", "Drive" }, { "fxLimitCeil", "Ceil" } } },
            };

            for (const auto& def : defs)
            {
                auto unit = std::make_unique<Unit>();
                unit->name = def.name;
                unit->accent = accent;

                unit->on.setButtonText ("");
                addAndMakeVisible (unit->on);
                unit->onAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, def.onID, unit->on);

                for (const auto& [id, caption] : def.params)
                {
                    auto k = std::make_unique<Knob>();
                    k->init (*this, apvts, id, caption, accent);
                    unit->knobs.push_back (std::move (k));
                }
                units.push_back (std::move (unit));
            }
        }

        void resized() override
        {
            auto r = content();
            const int cols = 3;
            const int rowsN = juce::jmax (1, (int) std::ceil (units.size() / (float) cols));
            const int cw = r.getWidth() / cols, ch = r.getHeight() / rowsN;

            for (size_t i = 0; i < units.size(); ++i)
            {
                auto cell = juce::Rectangle<int> (r.getX() + (int) (i % cols) * cw,
                                                  r.getY() + (int) (i / cols) * ch, cw, ch).reduced (3);
                units[i]->bounds = cell;

                auto header = cell.removeFromTop (16);
                units[i]->on.setBounds (header.removeFromRight (32));

                const int n = (int) units[i]->knobs.size();
                const int kw = cell.getWidth() / juce::jmax (1, n);
                for (auto& k : units[i]->knobs)
                    k->setBounds (cell.removeFromLeft (kw));
            }
        }

        void paint (juce::Graphics& g) override
        {
            GlassCard::paint (g);

            for (auto& u : units)
            {
                auto cell = u->bounds.toFloat();
                g.setColour (juce::Colour (0x4408090f));
                g.fillRoundedRectangle (cell, 7.0f);
                g.setColour (u->accent.withAlpha (u->on.getToggleState() ? 0.45f : 0.18f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 7.0f, 1.0f);

                g.setColour (u->on.getToggleState() ? textHi : textLo.withAlpha (0.7f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (u->name, u->bounds.reduced (8, 2).removeFromTop (14),
                            juce::Justification::centredLeft);
            }
        }

    private:
        struct Unit
        {
            juce::String name;
            juce::Colour accent;
            juce::Rectangle<int> bounds;
            juce::ToggleButton on;
            std::unique_ptr<APVTS::ButtonAttachment> onAtt;
            std::vector<std::unique_ptr<Knob>> knobs;
        };
        std::vector<std::unique_ptr<Unit>> units;
    };


    //--------------------------------------------------------------------------
    // Visual chrome: Serum-style top tabs, mod source rail, and keyboard footer.
    //--------------------------------------------------------------------------
    class TopTabStrip : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 8.0f, 4);

            static const char* names[] { "OSC", "FX", "MATRIX", "GLOBAL" };
            auto cell = getLocalBounds().reduced (5, 4).removeFromLeft (juce::jmin (420, getWidth() / 2));
            const int w = cell.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
            {
                auto tab = cell.removeFromLeft (w).reduced (2, 0).toFloat();
                const bool active = i == 0;
                g.setColour ((active ? juce::Colour (cyan) : juce::Colour (0xff20263a)).withAlpha (active ? 0.26f : 0.50f));
                g.fillRoundedRectangle (tab, 6.0f);
                g.setColour ((active ? juce::Colour (lime) : textLo).withAlpha (active ? 0.95f : 0.65f));
                g.drawRoundedRectangle (tab.reduced (0.5f), 6.0f, 1.0f);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (names[i], tab.toNearestInt(), juce::Justification::centred);
            }

            auto preset = getLocalBounds().reduced (5, 4).withTrimmedLeft (430).withTrimmedRight (110);
            g.setColour (juce::Colour (0xaa0b1220));
            g.fillRoundedRectangle (preset.toFloat(), 5.0f);
            g.setColour (juce::Colour (cyan).withAlpha (0.55f));
            g.drawRoundedRectangle (preset.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (textHi);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("MONSTER BASS BANK | HYPER / EQ / LIMIT | WT+", preset.reduced (12, 0), juce::Justification::centredLeft);
        }
    };

    class ModRail : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (violet), 10.0f, 5);
            auto slot = getLocalBounds().reduced (8, 10);
            static const char* labels[] { "MOD", "ENV", "LFO", "VEL" };
            for (int i = 0; i < 4; ++i)
            {
                auto s = slot.removeFromTop (juce::jmax (54, (getHeight() - 24) / 4)).reduced (0, 4);
                g.setColour (juce::Colour (0x66090c16));
                g.fillRoundedRectangle (s.toFloat(), 8.0f);
                g.setColour ((i == 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.5f));
                g.drawRoundedRectangle (s.toFloat().reduced (0.5f), 8.0f, 1.0f);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.setColour (textLo);
                g.drawText (labels[i], s.removeFromTop (14), juce::Justification::centred);

                auto k = s.withSizeKeepingCentre (26, 26).toFloat();
                g.setColour (juce::Colour (0xff151a2c));
                g.fillEllipse (k);
                g.setColour ((i == 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.85f));
                g.drawEllipse (k.reduced (1.0f), 2.0f);
                g.drawLine (k.getCentreX(), k.getCentreY(), k.getCentreX() + 7.0f, k.getY() + 5.0f, 2.0f);
            }
        }
    };

    class KeyboardFooter : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 8.0f, 3);

            auto controls = getLocalBounds().reduced (8, 7).removeFromLeft (132);
            for (int i = 0; i < 3; ++i)
            {
                auto f = controls.removeFromLeft (36).reduced (5, 0).toFloat();
                g.setColour (juce::Colour (0xdd101624));
                g.fillRoundedRectangle (f, 5.0f);
                g.setColour ((i == 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.5f));
                g.drawRoundedRectangle (f.reduced (0.5f), 5.0f, 1.0f);
                auto meter = f.reduced (10.0f, 8.0f);
                g.setColour (juce::Colour (lime).withAlpha (0.55f));
                g.fillRoundedRectangle (meter.withTrimmedTop (meter.getHeight() * (0.25f + 0.2f * i)), 3.0f);
            }

            auto keys = getLocalBounds().reduced (150, 9);
            const int whiteCount = 36;
            const float whiteW = keys.getWidth() / (float) whiteCount;
            for (int i = 0; i < whiteCount; ++i)
            {
                auto wk = juce::Rectangle<float> (keys.getX() + i * whiteW, (float) keys.getY(), whiteW - 1.0f, (float) keys.getHeight());
                g.setColour (juce::Colour (0xffbde7ea));
                g.fillRoundedRectangle (wk, 3.0f);
                g.setColour (juce::Colour (0x5536f8ff));
                g.drawRoundedRectangle (wk.reduced (0.5f), 3.0f, 1.0f);
            }

            const int pattern[5] { 1, 1, 0, 1, 1 };
            for (int i = 0; i < whiteCount - 1; ++i)
            {
                if (pattern[i % 5] == 0) continue;
                auto bk = juce::Rectangle<float> (keys.getX() + (i + 0.68f) * whiteW, (float) keys.getY(), whiteW * 0.54f, keys.getHeight() * 0.62f);
                g.setColour (juce::Colour (0xff0a1220));
                g.fillRoundedRectangle (bk, 3.0f);
                g.setColour (juce::Colour (cyan).withAlpha (0.22f));
                g.drawRoundedRectangle (bk.reduced (0.5f), 3.0f, 1.0f);
            }
        }
    };

    //--------------------------------------------------------------------------
    // HeaderBar: logo, preset actions, wavetable import, master volume.
    //--------------------------------------------------------------------------
    class HeaderBar : public juce::Component
    {
    public:
        explicit HeaderBar (ObsidianAudioProcessor& p) : processor (p)
        {
            for (auto* b : { &initBtn, &loadBtn, &saveBtn, &wtBtn })
                addAndMakeVisible (*b);

            initBtn.onClick = [this] { initPatch(); };
            loadBtn.onClick = [this] { loadPreset(); };
            saveBtn.onClick = [this] { savePreset(); };
            wtBtn.onClick   = [this] { loadWavetable(); };

            phat.init (*this, processor.apvts, "phat", "", juce::Colour (lime));
            phat.label.setVisible (false);
            master.init (*this, processor.apvts, "master", "", juce::Colour (amber));
            master.label.setVisible (false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 11.0f, 10);

            g.setColour (textHi);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText ("OBSIDIAN", 16, 0, 150, getHeight(), juce::Justification::centredLeft);

            g.setColour (juce::Colour (cyan).withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("WAVETABLE SYNTH", 17, getHeight() / 2 + 6, 150, 12, juce::Justification::topLeft);

            g.setColour (juce::Colour (cyan).withAlpha (0.4f));
            g.fillRect (16, getHeight() / 2 + 4, 118, 1);

            g.setColour (textLo);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("PHAT", phatLabelArea, juce::Justification::centredRight);
            g.drawText ("MASTER", masterLabelArea, juce::Justification::centredRight);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 5);

            auto knobArea = r.removeFromRight (r.getHeight() + 6);
            master.slider.setBounds (knobArea.reduced (1));
            masterLabelArea = r.removeFromRight (54);
            auto phatArea = r.removeFromRight (r.getHeight() + 6);
            phat.slider.setBounds (phatArea.reduced (1));
            phatLabelArea = r.removeFromRight (42);

            r.removeFromRight (8);
            wtBtn.setBounds   (r.removeFromRight (118).reduced (2, 1));
            saveBtn.setBounds (r.removeFromRight (72).reduced (2, 1));
            loadBtn.setBounds (r.removeFromRight (72).reduced (2, 1));
            initBtn.setBounds (r.removeFromRight (64).reduced (2, 1));
        }

    private:
        void loadWavetable()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load wavetable (WAV, 2048-sample frames)",
                                                           juce::File(), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file.existsAsFile() && ! processor.loadUserWavetable (file))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Obsidian", "Couldn't read that file.");
                });
        }

        void savePreset()
        {
            chooser = std::make_unique<juce::FileChooser> ("Save preset", juce::File(), "*.obsn");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File())
                        return;
                    if (auto xml = processor.apvts.copyState().createXml())
                        xml->writeTo (file.withFileExtension ("obsn"));
                });
        }

        void loadPreset()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load preset", juce::File(), "*.obsn");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (! file.existsAsFile())
                        return;
                    if (auto xml = juce::parseXML (file))
                        processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
                });
        }

        void initPatch()
        {
            for (auto* param : processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                {
                    ranged->beginChangeGesture();
                    ranged->setValueNotifyingHost (ranged->getDefaultValue());
                    ranged->endChangeGesture();
                }
        }

        ObsidianAudioProcessor& processor;
        juce::TextButton initBtn { "INIT" }, loadBtn { "OPEN" }, saveBtn { "SAVE" }, wtBtn { "LOAD WT" };
        Knob phat, master;
        juce::Rectangle<int> phatLabelArea, masterLabelArea;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==========================================================================
    // MainView: Serum-style fixed bands.
    //   Row 1: OSC A | OSC B | SUB/NOISE | FILTER
    //   Row 2: ENV 1 | ENV 2 | LFO 1 | LFO 2 | VOICE
    //   Row 3: MOD MATRIX | FX RACK
    //==========================================================================
    class MainView : public juce::Component, private juce::Timer
    {
    public:
        explicit MainView (ObsidianAudioProcessor& p)
            : header (p),
              oscA (p, "oscA", "OSC A", juce::Colour (cyan)),
              oscB (p, "oscB", "OSC B", juce::Colour (magenta)),
              subNoise (p.apvts, juce::Colour (amber)),
              filter (p.apvts, juce::Colour (lime)),
              env1 (p.apvts, "amp",  "ENV 1 - AMP", juce::Colour (amber)),
              env2 (p.apvts, "env2", "ENV 2 - MOD", juce::Colour (amber)),
              lfo1 (p.apvts, 1, true,  "LFO 1", juce::Colour (cyan)),
              lfo2 (p.apvts, 2, false, "LFO 2", juce::Colour (magenta)),
              voice (p.apvts, juce::Colour (amber)),
              matrix (p.apvts, juce::Colour (lime)),
              fx (p.apvts, juce::Colour (violet))
        {
            startTimerHz (24);
            for (auto* c : std::initializer_list<juce::Component*> {
                     &topTabs, &header, &rail, &oscA, &oscB, &subNoise, &filter,
                     &env1, &env2, &lfo1, &lfo2, &voice, &matrix, &fx, &keyboard })
                addAndMakeVisible (*c);
        }

        void paint (juce::Graphics& g) override
        {
            juce::ColourGradient bg (juce::Colour (bg0), 0.0f, 0.0f,
                                     juce::Colour (bg1), (float) getWidth(), (float) getHeight(), false);
            bg.addColour (0.52, juce::Colour (0xff12142a));
            g.setGradientFill (bg);
            g.fillAll();

            // Animated bloom field behind the cards
            const float pulse = 0.5f + 0.5f * std::sin (animPhase);
            juce::ColourGradient orb1 (juce::Colour (cyan).withAlpha (0.20f + 0.08f * pulse),
                                       getWidth() * (0.18f + 0.03f * pulse), getHeight() * 0.16f,
                                       juce::Colour (0x00000000), getWidth() * 0.78f, getHeight() * 0.92f, true);
            g.setGradientFill (orb1);
            g.fillAll();

            juce::ColourGradient orb2 (juce::Colour (magenta).withAlpha (0.12f + 0.07f * (1.0f - pulse)),
                                       getWidth() * 0.82f, getHeight() * (0.18f + 0.04f * pulse),
                                       juce::Colour (0x00000000), getWidth() * 0.22f, getHeight() * 0.75f, true);
            g.setGradientFill (orb2);
            g.fillAll();

            // Faint neon shards behind the cards
            g.setColour (juce::Colour (cyan).withAlpha (0.045f));
            for (int i = 0; i < getWidth(); i += 190)
                g.drawLine ((float) i, 0.0f, (float) i + 110.0f, (float) getHeight(), 1.0f);
            g.setColour (juce::Colour (magenta).withAlpha (0.035f));
            for (int i = 90; i < getWidth(); i += 230)
                g.drawLine ((float) i, (float) getHeight(), (float) i + 80.0f, 0.0f, 1.0f);
        }

        void timerCallback() override
        {
            animPhase += 0.018f;
            if (animPhase > juce::MathConstants<float>::twoPi)
                animPhase -= juce::MathConstants<float>::twoPi;
            repaint();
        }

        void resized() override
        {
            constexpr int gap = 6;
            auto r = getLocalBounds().reduced (8);

            topTabs.setBounds (r.removeFromTop (34));
            r.removeFromTop (gap);
            header.setBounds (r.removeFromTop (48));
            r.removeFromTop (gap);

            keyboard.setBounds (r.removeFromBottom (66));
            r.removeFromBottom (gap);

            rail.setBounds (r.removeFromLeft (72));
            r.removeFromLeft (gap);

            const int h = r.getHeight();
            auto oscBand = r.removeFromTop (juce::roundToInt (h * 0.40f));
            r.removeFromTop (gap);
            auto modBand = r.removeFromTop (juce::roundToInt (h * 0.265f));
            r.removeFromTop (gap);
            auto botBand = r;

            // --- Oscillator band
            {
                const int w = oscBand.getWidth();
                oscA.setBounds     (oscBand.removeFromLeft (juce::roundToInt (w * 0.305f)));
                oscBand.removeFromLeft (gap);
                oscB.setBounds     (oscBand.removeFromLeft (juce::roundToInt (w * 0.305f)));
                oscBand.removeFromLeft (gap);
                subNoise.setBounds (oscBand.removeFromLeft (juce::roundToInt (w * 0.115f)));
                oscBand.removeFromLeft (gap);
                filter.setBounds   (oscBand);
            }

            // --- Modulation band
            {
                const int w = modBand.getWidth();
                env1.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                env2.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                lfo1.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.215f)));
                modBand.removeFromLeft (gap);
                lfo2.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                voice.setBounds (modBand);
            }

            // --- Bottom band
            {
                const int w = botBand.getWidth();
                matrix.setBounds (botBand.removeFromLeft (juce::roundToInt (w * 0.44f)));
                botBand.removeFromLeft (gap);
                fx.setBounds (botBand);
            }
        }

    private:
        TopTabStrip topTabs;
        HeaderBar header;
        ModRail rail;
        OscPanel oscA, oscB;
        SubNoisePanel subNoise;
        FilterPanel filter;
        EnvPanel env1, env2;
        LfoPanel lfo1, lfo2;
        VoicePanel voice;
        MatrixPanel matrix;
        FxPanel fx;
        KeyboardFooter keyboard;
        float animPhase = 0.0f;
    };
}

//==============================================================================
// Editor
//==============================================================================
ObsidianAudioProcessorEditor::ObsidianAudioProcessorEditor (ObsidianAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    mainView = std::make_unique<MainView> (processor);
    addAndMakeVisible (*mainView);

    setSize (1280, 920);
    setResizable (true, true);
    setResizeLimits (1180, 820, 2200, 1400);
}

ObsidianAudioProcessorEditor::~ObsidianAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ObsidianAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (bg0));
}

void ObsidianAudioProcessorEditor::resized()
{
    mainView->setBounds (getLocalBounds());
}
