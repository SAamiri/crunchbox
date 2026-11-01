#pragma once

#include "PluginProcessor.h"

// Chunky, ugly 90s knob
struct CrunchLookAndFeel : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;

        g.setColour (juce::Colours::black);
        g.fillEllipse (b.expanded (3.0f));
        g.setColour (juce::Colour (0xffb4b4b4));
        g.fillEllipse (b);
        g.setColour (juce::Colours::white);
        g.drawEllipse (b.reduced (2.0f), 2.0f);
        g.setColour (juce::Colour (0xff555555));
        g.drawEllipse (b.reduced (6.0f), 1.0f);

        const float a = startAngle + pos * (endAngle - startAngle);
        juce::Point<float> tip (c.x + r * 0.85f * std::sin (a), c.y - r * 0.85f * std::cos (a));

        g.setColour (juce::Colours::black);
        g.drawLine (c.x, c.y, tip.x, tip.y, 7.0f);
        g.setColour (juce::Colour (0xffe00000));
        g.drawLine (c.x, c.y, tip.x, tip.y, 4.0f);
    }
};

class CrunchBoxEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit CrunchBoxEditor (CrunchBoxProcessor&);
    ~CrunchBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Drip
    {
        float x = 0, headY = 0, speed = 3, maxLen = 200, amp = 6, zig = 10, width = 3;
        juce::Colour colour;
    };

    void timerCallback() override;
    void renderScene();
    void drawEye (juce::Graphics&, const juce::Point<float>* pts, int n);
    void drawMouth (juce::Graphics&);
    void respawn (Drip&, bool anywhere);
    void drawDrip (juce::Graphics&, const Drip&);
    void drawOutlinedText (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                           juce::Colour fill, juce::Colour outline);

    CrunchBoxProcessor& proc;
    CrunchLookAndFeel lnf;

    juce::Image skull, frame;
    juce::Slider drive, tone, mix, crush;
    std::unique_ptr<Attachment> aDrive, aTone, aMix, aCrush;

    float time = 0.0f, eye = 0.5f, mouth = 0.5f, lvl = 0.0f;
    float driveN = 0.0f, crushN = 0.0f;
    int glitchSeed = 1;

    std::vector<Drip> drips;
    juce::Random rnd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrunchBoxEditor)
};
