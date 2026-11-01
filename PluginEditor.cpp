#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    // All coordinates are in pixels of the original 750x720 skull image.
    const Point<float> kLeftEye[]  = { { 282, 424 }, { 340, 428 }, { 369, 438 }, { 368, 452 },
                                       { 355, 457 }, { 310, 462 }, { 288, 454 }, { 282, 440 } };

    const Point<float> kRightEye[] = { { 386, 430 }, { 420, 426 }, { 474, 422 }, { 472, 440 },
                                       { 463, 450 }, { 448, 459 }, { 400, 460 }, { 386, 446 } };

    const Point<float> kJaw[]      = { { 322, 502 }, { 440, 502 }, { 445, 540 },
                                       { 425, 578 }, { 335, 578 }, { 318, 540 } };

    const Colour kBone (0xfff8f8f8);
    const float  kMaxJawDrop = 26.0f;   // image pixels
    const int    kBarHeight  = 98;

    Path makePath (const Point<float>* pts, int n)
    {
        Path p;
        p.startNewSubPath (pts[0]);
        for (int i = 1; i < n; ++i)
            p.lineTo (pts[i]);
        p.closeSubPath();
        return p;
    }
}

CrunchBoxEditor::CrunchBoxEditor (CrunchBoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    skull = ImageCache::getFromMemory (BinaryData::skull_jpg, BinaryData::skull_jpgSize);

    const int w = 520;
    const int artH = roundToInt (w * 720.0 / 750.0);
    setSize (w, artH + kBarHeight);
    frame = Image (Image::ARGB, w, artH, true);

    auto setup = [this] (Slider& s)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel (&lnf);
        s.setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (s);
    };

    setup (drive); setup (tone); setup (mix); setup (crush);

    aDrive = std::make_unique<Attachment> (proc.apvts, "drive", drive);
    aTone  = std::make_unique<Attachment> (proc.apvts, "tone",  tone);
    aMix   = std::make_unique<Attachment> (proc.apvts, "mix",   mix);
    aCrush = std::make_unique<Attachment> (proc.apvts, "crush", crush);

    startTimerHz (30);
}

CrunchBoxEditor::~CrunchBoxEditor()
{
    stopTimer();
    for (auto* s : { &drive, &tone, &mix, &crush })
        s->setLookAndFeel (nullptr);
}

void CrunchBoxEditor::resized()
{
    const int barY = getHeight() - kBarHeight;
    Slider* knobs[] = { &drive, &tone, &mix, &crush };

    for (int i = 0; i < 4; ++i)
    {
        const int cx = (int) (getWidth() * (i + 0.5f) / 4.0f);
        knobs[i]->setBounds (cx - 30, barY + 8, 60, 60);
    }
}

void CrunchBoxEditor::respawn (Drip& d, bool anywhere)
{
    d.x      = rnd.nextFloat() * (float) getWidth();
    d.headY  = anywhere ? rnd.nextFloat() * (float) getHeight() : 0.0f;
    d.speed  = 2.0f + rnd.nextFloat() * 7.0f;
    d.maxLen = 120.0f + rnd.nextFloat() * 320.0f;
    d.amp    = 4.0f + rnd.nextFloat() * 8.0f;
    d.zig    = 9.0f + rnd.nextFloat() * 8.0f;
    d.width  = 2.5f + rnd.nextFloat() * 3.0f;

    static const uint32 cols[] = { 0xffd10000, 0xffff0000, 0xff8f0000, 0xffb30000 };
    d.colour = Colour (cols[rnd.nextInt (4)]);
}

void CrunchBoxEditor::timerCallback()
{
    time += 1.0f / 30.0f;

    auto norm = [this] (const char* id) { return proc.apvts.getParameter (id)->getValue(); };

    driveN = norm ("drive");
    crushN = norm ("crush");

    eye   += (norm ("tone") - eye)   * 0.25f;   // tone  -> eyes open
    mouth += (norm ("mix")  - mouth) * 0.25f;   // mix   -> mouth open
    lvl   += (jlimit (0.0f, 1.0f, proc.outputLevel.load()) - lvl) * 0.4f;

    // crush -> amount of blood zigzags
    const int target = (int) (crushN * 30.0f);
    while ((int) drips.size() < target)
    {
        Drip d;
        respawn (d, true);
        drips.push_back (d);
    }
    while ((int) drips.size() > target)
        drips.pop_back();

    for (auto& d : drips)
    {
        d.headY += d.speed;
        if (d.headY - d.maxLen > (float) getHeight())
            respawn (d, false);
    }

    if ((int) (time * 30.0f) % 2 == 0)
        glitchSeed = rnd.nextInt();

    repaint();
}

void CrunchBoxEditor::drawEye (Graphics& g, const Point<float>* pts, int n)
{
    Path p = makePath (pts, n);
    auto bb = p.getBounds();

    g.saveState();
    g.reduceClipRegion (p, AffineTransform());

    const float lidBottom = bb.getY() + (1.0f - eye) * bb.getHeight();

    // glowing red pupil, visible once the lid lifts
    if (eye > 0.35f)
    {
        g.setColour (Colour (0xffff1010).withAlpha (jmin (1.0f, (eye - 0.35f) * 1.4f)));
        auto c = bb.getCentre();
        g.fillEllipse (c.x - 6.0f, c.y - 5.0f, 12.0f, 10.0f);
    }

    // eyelid
    g.setColour (kBone);
    g.fillRect (bb.getX() - 2.0f, bb.getY() - 2.0f, bb.getWidth() + 4.0f, lidBottom - bb.getY() + 2.0f);
    g.setColour (Colours::black);
    g.fillRect (bb.getX() - 2.0f, lidBottom - 1.5f, bb.getWidth() + 4.0f, 3.0f);

    g.restoreState();
}

void CrunchBoxEditor::drawMouth (Graphics& g)
{
    const float drop = mouth * kMaxJawDrop;
    if (drop < 0.5f)
        return;

    Path jaw = makePath (kJaw, (int) std::size (kJaw));

    // dark mouth interior where the jaw used to be
    g.saveState();
    g.reduceClipRegion (jaw, AffineTransform());
    g.fillAll (Colours::black);
    g.restoreState();

    // a few upper teeth
    g.setColour (kBone);
    for (float x : { 334.0f, 352.0f, 370.0f, 388.0f, 406.0f, 424.0f })
        g.fillRect (x, 502.0f, 11.0f, 7.0f + drop * 0.2f);

    // the lower jaw, shifted down
    g.saveState();
    g.reduceClipRegion (jaw, AffineTransform::translation (0.0f, drop));
    g.drawImageAt (skull, 0, roundToInt (drop));
    g.restoreState();
}

void CrunchBoxEditor::renderScene()
{
    frame.clear (frame.getBounds());
    Graphics g (frame);
    g.fillAll (Colours::black);
    g.setImageResamplingQuality (Graphics::lowResamplingQuality);   // crunchy pixels

    const float W = (float) frame.getWidth();
    const float H = (float) frame.getHeight();

    // The whole picture breathes, sways and shakes with the audio level
    const float pulse = 1.07f + 0.025f * std::sin (time * 1.3f) + lvl * 0.08f;
    const float angle = 0.03f * std::sin (time * 0.7f);
    const float shake = driveN * 3.0f + lvl * 2.0f;

    g.saveState();
    auto t = AffineTransform::scale (W / 750.0f)
                 .scaled (pulse, pulse, W * 0.5f, H * 0.5f)
                 .rotated (angle, W * 0.5f, H * 0.5f)
                 .translated ((rnd.nextFloat() - 0.5f) * shake, (rnd.nextFloat() - 0.5f) * shake);
    g.addTransform (t);

    g.drawImageAt (skull, 0, 0);
    drawEye (g, kLeftEye,  (int) std::size (kLeftEye));
    drawEye (g, kRightEye, (int) std::size (kRightEye));
    drawMouth (g);
    g.restoreState();

    // drive -> everything goes red
    g.setColour (Colour (0xffff0000).withAlpha (driveN * 0.55f));
    g.fillAll();

    // drive -> junk glitch blocks
    if (driveN > 0.3f)
    {
        const int blocks = (int) (driveN * 8.0f);
        for (int i = 0; i < blocks; ++i)
        {
            const uint32 cols[] = { 0xffff0000, 0xff000000, 0xffffffff };
            g.setColour (Colour (cols[rnd.nextInt (3)]).withAlpha (0.35f));
            g.fillRect (rnd.nextInt ((int) W), rnd.nextInt ((int) H),
                        10 + rnd.nextInt (80), 2 + rnd.nextInt (8));
        }
    }
}

void CrunchBoxEditor::drawDrip (Graphics& g, const Drip& d)
{
    const float tail = jmax (0.0f, d.headY - d.maxLen);
    if (d.headY <= tail)
        return;

    Path p;
    p.startNewSubPath (d.x, tail);
    int i = 1;
    for (float y = tail + d.zig; y < d.headY; y += d.zig, ++i)
        p.lineTo (d.x + ((i % 2) ? d.amp : -d.amp), y);
    p.lineTo (d.x, d.headY);

    g.setColour (Colours::black);
    g.strokePath (p, PathStrokeType (d.width + 3.0f, PathStrokeType::mitered, PathStrokeType::butt));
    g.setColour (d.colour);
    g.strokePath (p, PathStrokeType (d.width, PathStrokeType::mitered, PathStrokeType::butt));

    // blood drop on the end
    g.setColour (Colours::black);
    g.fillEllipse (d.x - d.width - 1.5f, d.headY - d.width, d.width * 2.0f + 3.0f, d.width * 2.4f + 3.0f);
    g.setColour (d.colour);
    g.fillEllipse (d.x - d.width, d.headY - d.width + 1.0f, d.width * 2.0f, d.width * 2.4f);
}

void CrunchBoxEditor::drawOutlinedText (Graphics& g, const String& text, Rectangle<int> r,
                                        Colour fill, Colour outline)
{
    g.setColour (outline);
    for (int dx = -2; dx <= 2; dx += 2)
        for (int dy = -2; dy <= 2; dy += 2)
            if (dx != 0 || dy != 0)
                g.drawText (text, r.translated (dx, dy), Justification::centred, false);

    g.setColour (fill);
    g.drawText (text, r, Justification::centred, false);
}

void CrunchBoxEditor::paint (Graphics& g)
{
    const int w = getWidth();
    const int artH = frame.getHeight();

    renderScene();

    // draw the skull; with drive up, slice it into strips and tear them sideways
    g.setImageResamplingQuality (Graphics::lowResamplingQuality);
    g.setColour (Colours::black);
    g.fillRect (0, 0, w, artH);

    if (driveN > 0.04f)
    {
        Random r ((int64) glitchSeed);
        const int stripH = 6;
        for (int y = 0; y < artH; y += stripH)
        {
            float dx = 0.0f;
            if (r.nextFloat() < driveN * 0.5f)
                dx = (r.nextFloat() - 0.5f) * driveN * 70.0f;

            g.saveState();
            g.reduceClipRegion (0, y, w, stripH);
            g.drawImageAt (frame, roundToInt (dx), 0);
            g.restoreState();
        }
    }
    else
    {
        g.drawImageAt (frame, 0, 0);
    }

    // scanlines
    g.setColour (Colours::black.withAlpha (0.22f));
    for (int y = 0; y < artH; y += 3)
        g.fillRect (0, y, w, 1);

    // title
    g.setFont (Font (FontOptions ("Impact", 34.0f, Font::plain)));
    drawOutlinedText (g, "CRUNCHBOX", { 10, 6, 260, 40 }, Colour (0xffff2020), Colours::black);

    // control bar
    const int barY = artH;
    g.setColour (Colour (0xff101010));
    g.fillRect (0, barY, w, getHeight() - barY);
    g.setColour (Colour (0xffb00000));
    g.fillRect (0, barY, w, 4);
    g.setColour (Colours::white.withAlpha (0.4f));
    g.fillRect (0, barY + 4, w, 1);

    g.setFont (Font (FontOptions ("Courier New", 15.0f, Font::bold)));
    const char* names[] = { "DRIVE", "TONE", "MIX", "CRUSH" };
    for (int i = 0; i < 4; ++i)
    {
        const int cx = (int) (w * (i + 0.5f) / 4.0f);
        drawOutlinedText (g, names[i], { cx - 60, barY + 70, 120, 22 },
                          Colours::white, Colour (0xff900000));
    }

    // blood zigzags run over everything, top to bottom
    for (const auto& d : drips)
        drawDrip (g, d);
}
