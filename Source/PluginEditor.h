#pragma once
#include "PluginProcessor.h"

class RedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RedLookAndFeel()
    {
        const juce::Colour dark (0xFF2A0508), mid (0xFF5A0A12), red (0xFFE01B24), light (0xFFFFB3B3);
        setColour (juce::Slider::textBoxTextColourId, light);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, light);
        setColour (juce::TextEditor::textColourId, light);
        setColour (juce::TextEditor::backgroundColourId, dark);
        setColour (juce::ComboBox::backgroundColourId, dark);
        setColour (juce::ComboBox::textColourId, light);
        setColour (juce::ComboBox::outlineColourId, mid);
        setColour (juce::ComboBox::arrowColourId, red);
        setColour (juce::PopupMenu::backgroundColourId, dark);
        setColour (juce::PopupMenu::textColourId, light);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, red);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Knopf-Koerper
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xFF4A0C12), c.x, c.y - r,
                                                 juce::Colour (0xFF120305), c.x, c.y + r, false));
        g.fillEllipse (c.x - r * 0.74f, c.y - r * 0.74f, r * 1.48f, r * 1.48f);
        g.setColour (juce::Colour (0xFF7A0F18));
        g.drawEllipse (c.x - r * 0.74f, c.y - r * 0.74f, r * 1.48f, r * 1.48f, 1.5f);

        // Bogen
        juce::Path track;
        track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xFF3A0A0E));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, startAngle, angle, true);
        g.setColour (juce::Colour (0xFFE01B24));
        g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Zeiger
        juce::Path p;
        p.addRoundedRectangle (-2.0f, -r * 0.70f, 4.0f, r * 0.32f, 2.0f);
        g.setColour (juce::Colour (0xFFFF6B6B));
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

class RedTuneAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RedTuneAudioProcessorEditor (RedTuneAudioProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p)
    {
        setLookAndFeel (&laf);

        addKnob (speed,   "speed",   "RETUNE SPEED", " ms", 1);
        addKnob (amount,  "amount",  "AMOUNT",       " %",  0);
        addKnob (pitch,   "pitch",   "PITCH",        " st", 2);
        addKnob (mix,     "mix",     "DRY / WET",    " %",  0);
        addKnob (inGain,  "inGain",  "INPUT",        " dB", 1);
        addKnob (outGain, "outGain", "OUTPUT",       " dB", 1);

        setupCombo (keyBox,   keyLabel,   "KEY",   RedTuneAudioProcessor::getKeyNames());
        setupCombo (scaleBox, scaleLabel, "SCALE", RedTuneAudioProcessor::getScaleNames());
        keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key",   keyBox);
        scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);

        setSize (780, 440);
        startTimerHz (30);
    }

    ~RedTuneAudioProcessorEditor() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xFF1E0509), 0.0f, 0.0f,
                                                 juce::Colour (0xFF050102), 0.0f, (float) getHeight(), false));
        g.fillAll();
        g.setColour (juce::Colour (0xFF5A0A12));
        g.drawRect (getLocalBounds(), 2);

        g.setColour (juce::Colour (0xFFE01B24));
        g.setFont (34.0f);
        g.drawText ("RED TUNE", 0, 10, getWidth(), 44, juce::Justification::centred);

        drawMeter (g, inMeterArea,  inDb,  "IN");
        drawMeter (g, outMeterArea, outDb, "OUT");

        g.setColour (juce::Colour (0xFFFF3B3B));
        g.setFont (60.0f);
        g.drawText (noteMain, noteArea.removeFromTop (66), juce::Justification::centred);
        g.setColour (juce::Colour (0xFFFFB3B3));
        g.setFont (16.0f);
        g.drawText (noteSub, noteArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        inMeterArea  = b.removeFromLeft (90).reduced (10, 0).withTrimmedTop (60).withTrimmedBottom (20);
        outMeterArea = b.removeFromRight (90).reduced (10, 0).withTrimmedTop (60).withTrimmedBottom (20);
        b.removeFromTop (60);

        noteArea = b.removeFromTop (90);

        auto combos = b.removeFromTop (36);
        const int half = combos.getWidth() / 2;
        auto left = combos.removeFromLeft (half).reduced (10, 4);
        keyLabel.setBounds (left.removeFromLeft (50));
        keyBox.setBounds (left);
        auto right = combos.reduced (10, 4);
        scaleLabel.setBounds (right.removeFromLeft (60));
        scaleBox.setBounds (right);

        b.removeFromTop (10);
        b.removeFromBottom (15);

        Knob* ks[6] = { &speed, &amount, &pitch, &mix, &inGain, &outGain };
        const int w = b.getWidth() / 6;
        for (auto* k : ks)
        {
            auto col = b.removeFromLeft (w).reduced (4, 0);
            k->label.setBounds (col.removeFromTop (22));
            k->slider.setBounds (col);
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void addKnob (Knob& k, const juce::String& id, const juce::String& text,
                  const juce::String& suffix, int decimals)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        addAndMakeVisible (k.slider);

        k.label.setText (text, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (k.label);

        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
        k.slider.textFromValueFunction = [suffix, decimals] (double v)
        {
            return juce::String (v, decimals) + suffix;
        };
        k.slider.updateText();
    }

    void setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& text,
                     const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (label);
    }

    static juce::String midiName (int m)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int pc = ((m % 12) + 12) % 12;
        const int oct = (m - pc) / 12 - 1;
        return juce::String (names[pc]) + juce::String (oct);
    }

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, float db, const juce::String& name)
    {
        auto top = r.removeFromTop (22);
        auto bottom = r.removeFromBottom (26);
        g.setColour (juce::Colour (0xFFFFB3B3));
        g.setFont (14.0f);
        g.drawText (name, top, juce::Justification::centred);

        auto bar = r.reduced (18, 2).toFloat();
        g.setColour (juce::Colour (0xFF2A0508));
        g.fillRoundedRectangle (bar, 4.0f);

        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = bar;
        fill.removeFromTop (fill.getHeight() * (1.0f - frac));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xFFFF3B3B), 0.0f, bar.getY(),
                                                 juce::Colour (0xFF5A0008), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (fill, 4.0f);

        g.setColour (juce::Colour (0x66FFB3B3));
        for (int t = 0; t <= 60; t += 12)
        {
            const float yy = bar.getY() + bar.getHeight() * ((float) t / 60.0f);
            g.drawHorizontalLine ((int) yy, bar.getX() - 5.0f, bar.getX());
        }

        g.setColour (db > -0.5f ? juce::Colours::white : juce::Colour (0xFFFFB3B3));
        g.setFont (13.0f);
        g.drawText (db <= -99.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB",
                    bottom, juce::Justification::centred);
    }

    void timerCallback() override
    {
        auto upd = [] (float& shown, float peak)
        {
            const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
            shown = db > shown ? db : juce::jmax (db, shown - 2.0f);
        };
        upd (inDb,  proc.inPeak.exchange (0.0f));
        upd (outDb, proc.outPeak.exchange (0.0f));

        const float dm = proc.detectedMidi.load(), tm = proc.targetMidi.load();
        if (dm < 0.0f || tm < 0.0f)
        {
            noteMain = "--";
            noteSub = "no pitch detected";
        }
        else
        {
            const int rounded = (int) std::lround (dm);
            const float cents = (dm - (float) rounded) * 100.0f;
            noteMain = midiName ((int) std::lround (tm));
            noteSub = "in: " + midiName (rounded) + "  (" + juce::String (cents, 0) + " cents)";
        }
        repaint();
    }

    RedTuneAudioProcessor& proc;
    RedLookAndFeel laf;

    Knob speed, amount, pitch, mix, inGain, outGain;
    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;

    juce::Rectangle<int> inMeterArea, outMeterArea, noteArea;
    float inDb = -100.0f, outDb = -100.0f;
    juce::String noteMain = "--", noteSub = "no pitch detected";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedTuneAudioProcessorEditor)
};
