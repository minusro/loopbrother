// Renders the editor to a PNG without opening a window or an audio device:
// the processor runs a few blocks under a scripted transport so the pad shows
// live playback state.
//   cmake -B build -DJM_BUILD_SNAPSHOT=ON && cmake --build build --target jm_snapshot
//   build/jm_snapshot_artefacts/Release/jm_snapshot out.png [x y [size]]
// JM_STOPPED=1 renders with the transport stopped (nothing playing);
// JM_CIRCLE=<size> sets the Circle Size first; JM_RING_STYLE=<n> draws the
// marks in kRingStyles[n]; JM_MUTED=1 holds the mute.
// JM_STYLE_SHEET=<out.png> draws every ring style in every colour instead.
// JM_LOGO_SHEET=<out.png> JM_LOGO_FONTS=<dir> draws the logo in the current
// font and in every .ttf/.otf in dir, each in a mock of the status bar.
// JM_LOGO_FONT=<font file> [JM_LOGO_KERN=<factor>] draws the logo in that font.
// JM_RIPPLE=1 drags the pad (synthetic events) just before the render, so
// it shows ripples; JM_TIME_PAINT=1 times the pad's painting and
// JM_TIME_AUDIO=1 the processor's blocks.
// JM_PAD_CHECK=1 drives the pad with synthetic mouse events (plain, Option
// and Shift drags) and checks the parameters they set, and the mute.
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

struct ScriptedPlayHead : juce::AudioPlayHead {
    double ppq = 0.0;
    bool playing = std::getenv("JM_STOPPED") == nullptr;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(162.0);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        return p;
    }
};

// Every ring style in every colour, large, with the plain O (an unselected
// cell's mark) below each, and the marks at their real size on the right:
// at rest, at a loop's peak, and plain Os.
static int styleSheet(const char* path)
{
    using namespace jmui;
    constexpr int labelW = 190, colW = 118, rowH = 168, realW = 200, k2 = 2;
    const int w = labelW + kFrootLoops * colW + realW, h = kNumRingStyles * rowH + 40;
    juce::Image img(juce::Image::ARGB, w * k2, h * k2, true);
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale((float) k2));
    g.fillAll(white);
    const Grains grains;
    g.setColour(jmui::brown);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("actual size", labelW + kFrootLoops * colW, 8, realW, 24, juce::Justification::centred);
    for (int s = 0; s < kNumRingStyles; ++s)
    {
        const auto& st = kRingStyles[s];
        const float y = 40.0f + (float) s * rowH;
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawHorizontalLine((int) y, 12.0f, (float) w - 12.0f);
        g.setColour(jmui::brown);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        g.drawText(juce::String(s) + "  " + st.name, 16, (int) y, labelW - 20, rowH, juce::Justification::centredLeft);
        for (int c = 0; c < kFrootLoops; ++c)
        {
            const juce::Point<float> at { labelW + (c + 0.5f) * colW, y + 0.52f * rowH };
            drawOShadow(g, at, 36.0f, st, c * 7 + s);
            drawO(g, at, 36.0f, c, grains, st, c * 7 + s);
            drawOShadow(g, at.translated(0.0f, 58.0f), 0.5f * 36.0f * 0.5f, st, c);
            drawO(g, at.translated(0.0f, 58.0f), 0.5f * 36.0f * 0.5f, c, grains, st, c);
        }
        const float x0 = (float) (labelW + kFrootLoops * colW);
        const float r0 = XYPad::kRingRadius;
        for (int c = 0; c < 2; ++c)
        {
            const juce::Point<float> rest { x0 + 40.0f + c * 30.0f, y + 0.45f * rowH };
            drawOShadow(g, rest, r0, st, c);
            drawO(g, rest, r0, c * 3 + 1, grains, st, c);
        }
        const juce::Point<float> peak { x0 + 140.0f, y + 0.45f * rowH };
        drawOShadow(g, peak, 2 * r0, st, 9);
        drawO(g, peak, 2 * r0, 4, grains, st, 9);
        for (int c = 0; c < kFrootLoops; ++c)
        {
            const juce::Point<float> at { x0 + 30.0f + c * 22.0f, y + 0.82f * rowH };
            drawOShadow(g, at, XYPad::kSmallO, st, c);
            drawO(g, at, XYPad::kSmallO, c, grains, st, c);
        }
    }
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(path)));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

// The logo in each candidate font, on the status bar at 480 px (drawn 2x).
static int logoSheet(const char* path, const char* fontDir)
{
    using namespace jmui;
    using Ed = JungleEditor;
    struct Candidate { juce::String name; juce::Typeface::Ptr face; };
    std::vector<Candidate> fonts { { "current (system sans, bold)", nullptr } };
    if (fontDir != nullptr)
    {
        auto files = juce::File(juce::String::fromUTF8(fontDir)).findChildFiles(juce::File::findFiles, false, "*.ttf;*.otf");
        files.sort();
        for (const auto& f : files)
        {
            juce::MemoryBlock data;
            f.loadFileAsData(data);
            if (auto face = juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize()))
                fonts.push_back({ f.getFileNameWithoutExtension(), face });
        }
    }
    constexpr int cols = 2, cellW = Ed::kDesignSize, labelH = 22, barH = Ed::kTitleHeight, gap = 18, k2 = 2;
    const int rows = ((int) fonts.size() + cols - 1) / cols, cellH = labelH + barH + gap;
    const int w = cols * cellW + (cols + 1) * gap, h = rows * cellH + gap;
    juce::Image img(juce::Image::ARGB, w * k2, h * k2, true);
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale((float) k2));
    g.fillAll(juce::Colour(0xfff2f2f2));
    for (size_t i = 0; i < fonts.size(); ++i)
    {
        const int x = gap + (int) (i % cols) * (cellW + gap), y = gap + (int) (i / cols) * cellH;
        g.setColour(juce::Colour(0xff808080));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(juce::String((int) i) + "  " + fonts[i].name, x, y, cellW, labelH - 4, juce::Justification::bottomLeft);
        const juce::Rectangle<float> bar((float) x, (float) (y + labelH), (float) cellW, (float) barH);
        g.setColour(white);
        g.fillRect(bar);
        g.setColour(cream);
        g.fillRect(bar.withHeight(1.0f));
        g.setColour(brown);
        g.setFont(fonts[i].face == nullptr
                      ? juce::Font(juce::FontOptions(Ed::kLogoHeight, juce::Font::bold).withKerningFactor(0.14f))
                      : juce::Font(juce::FontOptions(fonts[i].face).withHeight(Ed::kLogoHeight).withKerningFactor(0.05f)));
        g.drawText(Ed::logo(), bar, juce::Justification::centred, false);
    }
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(path)));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s (%zu fonts)\n", out.getFullPathName().toRawUTF8(), fonts.size());
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (const char* sheet = std::getenv("JM_STYLE_SHEET")) return styleSheet(sheet);
    if (const char* sheet = std::getenv("JM_LOGO_SHEET")) return logoSheet(sheet, std::getenv("JM_LOGO_FONTS"));
    if (argc < 2) { std::printf("usage: jm_snapshot out.png [x y [size]]\n"); return 2; }

    JungleProcessor proc;
    ScriptedPlayHead head;
    proc.setPlayHead(&head);
    proc.setPlayConfigDetails(0, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);
    if (argc >= 4)
    {
        proc.apvts.getParameter("x")->setValueNotifyingHost((float) std::atof(argv[2]));
        proc.apvts.getParameter("y")->setValueNotifyingHost((float) std::atof(argv[3]));
    }
    if (std::getenv("JM_MUTED")) proc.setMuteHeld(true);
    if (const char* c = std::getenv("JM_CIRCLE"))
    {
        auto* size = proc.apvts.getParameter("size");
        size->setValueNotifyingHost(size->convertTo0to1((float) std::atof(c)));
    }

    // Play about a bar and a half so the playhead rings are partway round.
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 140; ++b)
    {
        proc.processBlock(buf, midi);
        head.ppq += 512 * 162.0 / 60.0 / 48000.0;
    }

    if (std::getenv("JM_TIME_AUDIO"))
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < 1000; ++b)
        {
            proc.processBlock(buf, midi);
            head.ppq += 512 * 162.0 / 60.0 / 48000.0;
        }
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        std::printf("audio: %.3f ms a 512-sample block (%.1f%% of real time at 48 kHz)\n", ms, 100.0 * ms / (512.0 / 48.0));
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (argc >= 5) ed->setSize(std::atoi(argv[4]), jmui::JungleEditor::heightFor(std::atoi(argv[4])));
    if (const char* path = std::getenv("JM_LOGO_FONT"))
    {
        juce::MemoryBlock data;
        juce::File(juce::String::fromUTF8(path)).loadFileAsData(data);
        if (auto face = juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize()))
        {
            const float kern = std::getenv("JM_LOGO_KERN") ? (float) std::atof(std::getenv("JM_LOGO_KERN")) : 0.0f;
            if (auto* jed = dynamic_cast<jmui::JungleEditor*>(ed.get()))
                jed->setLogoFont(juce::Font(juce::FontOptions(face).withHeight(30.0f).withKerningFactor(kern)));
        }
    }

    // Window resizing, by every route a new size takes. The window is the
    // pad plus a title bar, at a fixed ratio: H(w) is the height for a width,
    // W(h) the width for a height.
    if (std::getenv("JM_RESIZE_CHECK"))
    {
        using Ed = jmui::JungleEditor;
        auto H = [](int w) { return Ed::heightFor(w); };
        auto W = [](int h) { return juce::roundToInt(h * Ed::kRatio); };
        bool ok = true;
        auto newDrag = [] { juce::Thread::sleep((int) jmui::WindowConstrainer::kDragGapMs + 50); };

        // A host that ignores the constraint: the editor fixes the ratio
        // afterwards along the side that moved.
        auto drag = [&](int w, int h, int want)
        {
            ed->setSize(w, h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            std::printf("  host %dx%d -> editor %dx%d (want %dx%d)\n", w, h, ed->getWidth(), ed->getHeight(), want, H(want));
            ok &= ed->getWidth() == want && std::abs(ed->getHeight() - H(want)) <= 1; // a height-driven size keeps its height
        };
        const int s0 = ed->getWidth();
        ok &= s0 == Ed::kDefaultSize && ed->getHeight() == H(s0);
        drag(s0 + 200, H(s0), s0 + 200);                    // right edge out: grows
        drag(s0 + 200, H(s0 + 200) + 240, W(H(s0 + 200) + 240)); // bottom edge out: grows
        const int s1 = ed->getWidth();
        drag(s1 - 200, H(s1), s1 - 200);                    // right edge in: shrinks
        drag(1500, H(s1 - 200), Ed::kMaxSize);              // past the maximum: held at it
        newDrag();
        ed->setSize(s0, H(s0));
        newDrag();
        drag(s0 + 20, H(s0) + 5, s0 + 20);                  // corner, off the diagonal: the further side
        drag(s0 + 20, H(s0) + 10, s0 + 20);                 // only the nearer side moved: still the corner
        drag(s0 + 30, H(s0) + 10, s0 + 30);
        newDrag();

        // The standalone: JUCE's window frame asks through a bordered
        // constrainer (the title bar) and places the window where it says.
        // A corner keeps the opposite corner, an edge keeps the opposite edge
        // and grows the two others half each way.
        struct Frame : juce::BorderedComponentBoundsConstrainer {
            juce::ComponentBoundsConstrainer* editor = nullptr;
            juce::BorderSize<int> border { 28, 1, 1, 1 };
            juce::ComponentBoundsConstrainer* getWrappedConstrainer() const override { return editor; }
            juce::BorderSize<int> getAdditionalBorder() const override { return border; }
        } window;
        window.editor = ed->getConstrainer();
        const juce::Rectangle<int> old { 100, 100, 960, H(960) };
        auto frame = [&](const char* what, juce::Rectangle<int> proposed, bool top, bool left, bool bottom, bool right,
                         std::function<bool(juce::Rectangle<int>)> want)
        {
            newDrag();
            auto r = window.border.addedTo(proposed);
            window.checkBounds(r, window.border.addedTo(old), {}, top, left, bottom, right);
            r = window.border.subtractedFrom(r);
            const bool good = want(r) && std::abs(r.getHeight() - H(r.getWidth())) <= 1;
            std::printf("  %s: %s -> %s%s\n", what, proposed.toString().toRawUTF8(), r.toString().toRawUTF8(), good ? "" : "  WRONG");
            ok &= good;
        };
        auto near = [](int a, int b) { return std::abs(a - b) <= 1; };
        frame("right edge out", { 100, 100, 1060, old.getHeight() }, false, false, false, true, [&](auto r)
              { return r.getX() == old.getX() && r.getWidth() == 1060 && near(r.getCentreY(), old.getCentreY()); });
        frame("left edge out", { 40, 100, 1020, old.getHeight() }, false, true, false, false, [&](auto r)
              { return r.getRight() == old.getRight() && r.getWidth() == 1020 && near(r.getCentreY(), old.getCentreY()); });
        frame("bottom edge in", { 100, 100, 960, 908 }, false, false, true, false, [&](auto r)
              { return r.getY() == old.getY() && r.getHeight() == 908 && near(r.getCentreX(), old.getCentreX()); });
        frame("top-left corner out", { 0, 60, 1060, 1048 }, true, true, false, false, [&](auto r)
              { return r.getRight() == old.getRight() && r.getBottom() == old.getBottom() && r.getWidth() == 1060; });
        frame("bottom-right corner in", { 100, 100, 900, 920 }, false, false, true, true, [&](auto r)
              { return r.getPosition() == old.getPosition() && r.getWidth() == 900; });

        // Plugin hosts: the window is the host's; it asks for the size the
        // mouse points at and says which sides changed (JUCE's VST3 wrapper,
        // patched). A corner dragged off the diagonal often moves along one
        // axis only: that must not read as an edge drag of the other side.
        newDrag();
        int side = 960;
        auto ask = [&](int w, int h, int want)
        {
            juce::Rectangle<int> r { w, h };
            const juce::Rectangle<int> current { side, H(side) };
            ed->getConstrainer()->checkBounds(r, current, {}, false, false, h != current.getHeight(), w != side);
            std::printf("  ask %dx%d at %dx%d -> %dx%d (want %dx%d)\n", w, h, side, H(side), r.getWidth(), r.getHeight(), want, H(want));
            ok &= r.getWidth() == want && std::abs(r.getHeight() - H(want)) <= 1;
            side = r.getWidth();
        };
        // (Sizes relative to the window's shape: y ahead means a height
        // past H of the width.)
        const int h0 = H(960);
        ask(980, h0 + 5, 980);                  // corner out, x ahead
        ask(980, h0 + 10, 980);                 // the mouse moved down only
        ask(990, h0 + 10, 990);                 // across only
        ask(990, H(990) + 20, W(H(990) + 20));  // down only, and y overtakes
        const int y1 = H(side) - 5;
        ask(side - 8, y1, W(y1));               // corner back in, y ahead
        ask(side - 13, H(side), side);          // across only: y still the further side
        const int y2 = H(side) - 15;
        ask(side - 14, y2, W(y2));
        newDrag();
        ask(1040, H(side), 1040);               // a new drag: an edge again
        ask(1040, 900, W(900));
        std::printf(ok ? "resize check passed\n" : "resize check FAILED\n");
        if (! ok) return 1;
    }
    if (std::getenv("JM_PAD_CHECK"))
    {
        jmui::XYPad* pad = nullptr;
        for (auto* c : ed->getChildren()) if (auto* p = dynamic_cast<jmui::XYPad*>(c)) pad = p;
        auto value = [&](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
        using Mods = juce::ModifierKeys;
        auto ev = [&](float x, float y, int mods)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, y },
                                    Mods(Mods::leftButtonModifier | mods), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    pad, pad, now, { x, y }, now, 1, false);
        };
        const float w = (float) pad->getWidth();
        bool ok = pad != nullptr && std::abs(w - 480.0f) < 0.5f;
        auto near = [](float a, float b, float tol = 2e-4f) { return std::abs(a - b) < tol; };
        auto report = [&](const char* what, bool good)
        {
            std::printf("  %-52s x %.4f y %.4f size %.3f%s\n", what, value("x"), value("y"), value("size"), good ? "" : "  WRONG");
            ok &= good;
        };
        // (No mouseUp: it would move the real pointer onto the circle.)
        pad->mouseDown(ev(240, 240, 0));
        report("press in the middle: the circle goes there", near(value("x"), 0.5f) && near(value("y"), 0.5f));
        pad->mouseDrag(ev(288, 240, 0));
        report("drag 48 px right: follows", near(value("x"), 0.6f));
        const float fine = jmui::XYPad::kFineSpeed;
        pad->mouseDrag(ev(448, 240, Mods::altModifier));
        report("Option, 160 px right: an eighth as far", near(value("x"), 0.6f + 160.0f * fine / w));
        pad->mouseDrag(ev(448, 160, Mods::altModifier));
        report("Option, 80 px up: an eighth as far", near(value("y"), 0.5f + 80.0f * fine / w));
        const float x0 = value("x"), y0 = value("y");
        // Shift is a knob: right or up grows the radius a pixel per pixel,
        // left or down shrinks it, and the circle stays put.
        const float px = 1.0f / (float) (jm::kPuckRadius * w); // size per pixel of radius
        pad->mouseDrag(ev(478, 160, Mods::shiftModifier));
        report("Shift, 30 px right: radius 30 px bigger, circle stays",
               near(value("size"), 1.0f + 30.0f * px, 2e-3f) && near(value("x"), x0) && near(value("y"), y0));
        pad->mouseDrag(ev(478, 140, Mods::shiftModifier));
        report("Shift, 20 px up: 20 px bigger again", near(value("size"), 1.0f + 50.0f * px, 2e-3f));
        pad->mouseDrag(ev(448, 140, Mods::shiftModifier));
        pad->mouseDrag(ev(448, 160, Mods::shiftModifier));
        report("Shift, back left and down: back to where it was", near(value("size"), 1.0f, 2e-3f));
        pad->mouseDrag(ev(448 + 160, 160, Mods::shiftModifier | Mods::altModifier));
        report("Shift + Option, 160 px right: an eighth as much", near(value("size"), 1.0f + 160.0f * fine * px, 2e-3f));
        pad->mouseDrag(ev(608 - 600, 160, Mods::shiftModifier));
        report("Shift, far left: held at the smallest circle", near(value("size"), jm::kCircleSizeMin, 2e-3f));
        pad->mouseDrag(ev(8, 160 - 5000, Mods::shiftModifier));
        report("Shift, very far: held at the largest circle", near(value("size"), jm::kCircleSizeMax, 2e-3f));
        proc.setPadHeld(false);
        proc.setFineDrag(false);
        pad->mouseDown(ev(10, 10, Mods::altModifier));
        report("Option press: the circle stays where it was", near(value("x"), x0) && near(value("y"), y0));
        proc.setPadHeld(false);
        proc.setFineDrag(false);

        // Ripples: dragging right pushes the cells the circle's centre
        // crossed right, once, and they come back to rest: never pushed the
        // other way (no ping-pong), still at the end. The nearest cell beside
        // the path is pushed straight outwards and back; a cell well away
        // from it never moves. A crossed cell's O bobs on after it's back:
        // squashed along the drag, then across it, round by the end.
        {
            juce::Thread::sleep(1000); // the earlier drags' pushes settle first
            pad->mouseDown(ev(100, 300, 0));
            const double start = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            for (int i = 1; i <= 5; ++i)
            {
                juce::Thread::sleep(70);
                pad->mouseDrag(ev(100.0f + 12.0f * i, 300, 0));
            }
            proc.setPadHeld(false);
            const auto& lib = proc.library();
            const float y = 1.0f - 300.0f / w;
            std::vector<int> crossed;
            for (float x = 100.0f; x <= 160.0f; x += 0.5f)
            {
                const int c = pad->cellAt({ x / w, y });
                if (std::find(crossed.begin(), crossed.end(), c) == crossed.end()) crossed.push_back(c);
            }
            int beside = -1;
            float besideDist = 1e9f;
            for (int i = 0; i < lib.size(); ++i)
            {
                if (std::find(crossed.begin(), crossed.end(), i) != crossed.end()) continue;
                const auto p = lib.entry(i).pos;
                const float px = juce::jlimit(100.0f / w, 160.0f / w, p.x);
                const float d = juce::Point<float> { px, y }.getDistanceFrom(p);
                if (d < besideDist) { besideDist = d; beside = i; }
            }
            int far = -1; // a break well away from the path
            for (int i = 0; i < lib.size() && far < 0; ++i)
            {
                const auto p = lib.entry(i).pos;
                const float px = juce::jlimit(100.0f / w, 160.0f / w, p.x);
                if (juce::Point<float> { px, y }.getDistanceFrom(p) > 0.25f) far = i;
            }
            const float outwards = lib.entry(beside).pos.y >= y ? 1.0f : -1.0f; // its side of the path (pad y is up)
            float most = 0.0f, against = 0.0f, across = 0.0f, last = 0.0f;
            float out = 0.0f, inwards = 0.0f, sideways = 0.0f, still = 0.0f;
            float bobAlong = 0.0f, bobAcross = 0.0f, bobAfter = 0.0f, bobLast = 0.0f;
            for (double t = 0.0; t < 2.0; t += 0.005)
            {
                bool moving = false;
                for (const int c : crossed) moving |= pad->cellShift(c, start + t).getDistanceFromOrigin() > 1e-6f;
                for (const int c : crossed)
                {
                    float axis = 0.0f;
                    const float squash = 1.0f - pad->cellBob(c, start + t, axis);
                    const bool along = std::abs(std::cos(axis)) > std::abs(std::sin(axis)); // the drag went right
                    (along ? bobAlong : bobAcross) = std::max(along ? bobAlong : bobAcross, squash);
                    if (! moving && t > 0.1) bobAfter = std::max(bobAfter, squash);
                    if (t > 1.9) bobLast = std::max(bobLast, squash);
                }
                for (const int c : crossed)
                {
                    const auto d = pad->cellShift(c, start + t);
                    most = std::max(most, d.x);
                    against = std::min(against, d.x);
                    across = std::max(across, std::abs(d.y)); // beside the path's other steps, they're pushed out too
                    if (t > 1.9) last = std::max(last, d.getDistanceFromOrigin());
                }
                const auto b = pad->cellShift(beside, start + t);
                out = std::max(out, b.y * outwards);
                inwards = std::min(inwards, b.y * outwards);
                sideways = std::max(sideways, std::abs(b.x));
                if (t > 1.9) last = std::max(last, b.getDistanceFromOrigin());
                still = std::max(still, pad->cellShift(far, start + t).getDistanceFromOrigin());
            }
            std::printf("  ripple: %zu crossed cells pushed up to %.2f px right, %.4f px back, %.4f px across;"
                        " the nearest cell beside (%.0f px off) %.2f px outwards, %.4f px in, %.4f px along;"
                        " %.4f px at the end; a far cell %.4f px\n",
                        crossed.size(), most * w, -against * w, across * w, besideDist * w, out * w, -inwards * w,
                        sideways * w, last * w, still * w);
            std::printf("  bob: squashed up to %.2f along the drag, %.2f across it, %.2f once the cells are back,"
                        " %.4f at the end\n", bobAlong, bobAcross, bobAfter, bobLast);
            ok &= most * w > 1.0f && against > -1e-6f && out * w > 0.8f && inwards > -1e-6f
                  && sideways < 1e-4f && last < 1e-6f && still < 1e-6f
                  && bobAlong > 0.1f && bobAlong <= jmui::XYPad::kBob + 1e-4f && bobAcross > 0.02f && bobAfter > 0.02f
                  && bobLast < 1e-6f;

            proc.setPadHeld(false);
        }

        // The cat: up over the logo once the circle is at its largest, back
        // down below the window's edge when it's small again (easing
        // between). While up it sways both ways on the beat while the loops
        // play, upright once they've stopped. The sway's shape: at one side
        // on each beat, then the other, moving most of the time and never
        // faster than an eased swing allows.
        if (auto* jed = dynamic_cast<jmui::JungleEditor*>(ed.get()))
        {
            auto* sizeParam = proc.apvts.getParameter("size");
            sizeParam->setValueNotifyingHost(1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
            const float up = jed->catRise();
            bool sides = true;
            float steepest = 0.0f, moving = 0.0f, jump = 0.0f;
            constexpr double db = 0.001;
            for (int k = 0; k <= 8; ++k)
                sides &= std::abs(jmui::JungleEditor::catSwayAt(k) - (k % 2 == 0 ? 1.0f : -1.0f)) < 1e-5f;
            for (double b = 0.0; b < 8.0; b += db)
            {
                const float d = jmui::JungleEditor::catSwayAt(b + db) - jmui::JungleEditor::catSwayAt(b);
                steepest = std::max(steepest, std::abs(d) / (float) db);
                jump = std::max(jump, std::abs(d));
                if (std::abs(d) > 1e-7f) moving += (float) (db / 8.0);
            }
            std::printf("  cat sway: a side on each beat %s, moving %.0f%% of the time, fastest %.2f a beat (a plain"
                        " pendulum: 3.14), %.4f a step\n", sides ? "yes" : "NO", 100.0f * moving, steepest, jump);
            ok &= sides && moving > 0.8f && moving < 0.9f && steepest < 3.8f && jump < 0.005f;

            float left = 0.0f, right = 0.0f, still = 0.0f;
            auto sway = [&](int frames) // about 21 ms of audio a frame
            {
                left = right = still = 0.0f;
                for (int f = 0; f < frames; ++f)
                {
                    for (int b = 0; b < 2; ++b)
                    {
                        proc.processBlock(buf, midi);
                        if (head.playing) head.ppq += 512 * 162.0 / 60.0 / 48000.0;
                    }
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                    left = std::min(left, jed->catTurn());
                    right = std::max(right, jed->catTurn());
                    still = std::max(still, std::abs(jed->catTurn()));
                }
            };
            const bool wasPlaying = head.playing;
            head.playing = true;
            sway(10);
            sway(50);
            const float swayL = left, swayR = right;
            head.playing = false;
            sway(40); // back upright
            sway(10);
            head.playing = wasPlaying;
            std::printf("  cat: swaying %.1f to %.1f degrees while playing, %.2f stopped\n", juce::radiansToDegrees(swayL),
                        juce::radiansToDegrees(swayR), juce::radiansToDegrees(still));
            ok &= swayL < -0.8f * jmui::JungleEditor::kCatSway && swayR > 0.8f * jmui::JungleEditor::kCatSway
                  && still < 1e-6f;
            sizeParam->setValueNotifyingHost(sizeParam->convertTo0to1(1.0f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
            const float down = jed->catRise();
            std::printf("  cat: at the largest circle %.2f up, at size 1 %.2f\n", up, down);
            ok &= up > 0.99f && down < 0.01f;
        }

        // The bowl's lip covers the pad's lower corners: a press there is on
        // the bowl, not the pad; just above the lip's lowest point it's the pad.
        {
            const int h = pad->getHeight(), wi = pad->getWidth();
            const bool corner = pad->hitTest(3, h - 3), middle = pad->hitTest(wi / 2, h - 3);
            std::printf("  bowl lip: a press in the lower corner reaches the pad: %s; at the middle: %s\n",
                        corner ? "yes" : "no", middle ? "yes" : "no");
            ok &= ! corner && middle;
        }

        // The logo's colour fade keeps its saturation: halfway from pastel
        // red to pastel blue round the hue wheel, where an RGB mix greys.
        {
            float h, s1, s2, smid, srgb, v;
            jmui::frootLoop(0).getHSB(h, s1, v);
            jmui::frootLoop(4).getHSB(h, s2, v);
            jmui::JungleEditor::blendHues(jmui::frootLoop(0), jmui::frootLoop(4), 0.5f).getHSB(h, smid, v);
            jmui::frootLoop(0).interpolatedWith(jmui::frootLoop(4), 0.5f).getHSB(h, srgb, v);
            std::printf("  logo fade: saturation %.2f and %.2f at the ends, %.2f halfway (an RGB mix: %.2f)\n", s1, s2, smid, srgb);
            ok &= std::abs(smid - 0.5f * (s1 + s2)) < 0.01f && srgb < smid - 0.1f;
        }

        // Command over the plugin (the editor's part, faked here): silent
        // from the next block, playing again from the next block once let go.
        // With the clock running, and with it stopped during an audition (the
        // pad held).
        auto loudest = [&](int blocks)
        {
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                proc.processBlock(buf, midi);
                peak = std::max(peak, buf.getMagnitude(0, 0, buf.getNumSamples()));
                if (head.playing) head.ppq += 512 * 162.0 / 60.0 / 48000.0;
            }
            return peak;
        };
        for (const bool running : { true, false })
        {
            head.playing = running;
            proc.setPadHeld(! running);
            loudest(10);
            const float before = loudest(10);
            proc.setMuteHeld(true);
            loudest(1);                     // the 5 ms fade
            const float muted = loudest(20);
            proc.setMuteHeld(false);
            loudest(1);
            const float back = loudest(20);
            std::printf("  %s: playing peak %.3f, Command held %.5f, let go %.3f\n",
                        running ? "clock running" : "clock stopped, pad held", before, muted, back);
            ok &= before > 0.05f && muted < 1e-6f && back > 0.05f;
        }
        proc.setPadHeld(false);
        std::printf(ok ? "pad check passed\n" : "pad check FAILED\n");
        if (! ok) return 1;
    }
    for (auto* c : ed->getChildren())
        if (auto* pad = dynamic_cast<jmui::XYPad*>(c))
        {
            if (const char* st = std::getenv("JM_RING_STYLE"))
                pad->setRingStyle(jmui::kRingStyles[juce::jlimit(0, jmui::kNumRingStyles - 1, std::atoi(st))]);
            pad->refresh();
            if (std::getenv("JM_RIPPLE"))
            {
                const auto now = juce::Time::getCurrentTime();
                auto ev = [&](float x, float y)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, y },
                                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, pad, pad, now, { x, y }, now, 1, true);
                };
                const float w = (float) pad->getWidth();
                pad->mouseDown(ev(0.30f * w, 0.55f * w));
                for (int i = 1; i <= 8; ++i)
                {
                    juce::Thread::sleep(70);
                    pad->mouseDrag(ev((0.30f + 0.03f * i) * w, (0.55f - 0.012f * i) * w));
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil(std::getenv("JM_RIPPLE_WAIT") ? std::atoi(std::getenv("JM_RIPPLE_WAIT")) : 250); // the ripples move on
                proc.setPadHeld(false);
            }
            if (std::getenv("JM_TIME_PAINT"))
            {
                pad->createComponentSnapshot(pad->getLocalBounds(), true, 1.0f); // builds the sprites: not timed
                // JM_TIME_FRAMES=<n> times fewer frames, e.g. to stay inside a
                // ripple, which settles within about half a second.
                const int frames = std::getenv("JM_TIME_FRAMES") ? std::atoi(std::getenv("JM_TIME_FRAMES")) : 100;
                const int rounds = std::getenv("JM_TIME_FRAMES") ? 1 : 3;
                double best = 1e9;
                for (int round = 0; round < rounds; ++round)
                {
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int i = 0; i < frames; ++i) pad->createComponentSnapshot(pad->getLocalBounds(), true, 1.0f);
                    best = std::min(best, (juce::Time::getMillisecondCounterHiRes() - t0) / frames);
                }
                std::printf("pad paint: %.2f ms a frame at %d px (best of %d x %d)\n", best, pad->getWidth(), rounds, frames);
            }
        }
    // A few frames of the editor's timer first, so a cat that's up shows the
    // output's level and sways to the beat as it would playing.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(std::getenv("JM_CAT_WAIT") ? std::atoi(std::getenv("JM_CAT_WAIT")) : 40);
    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
