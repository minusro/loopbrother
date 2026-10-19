// Renders the editor to a PNG without opening a window or an audio device:
// the processor runs a few blocks under a scripted transport so the pad shows
// live playback state.
//   cmake -B build -DJM_BUILD_SNAPSHOT=ON && cmake --build build --target jm_snapshot
//   build/jm_snapshot_artefacts/Release/jm_snapshot out.png [x y [size]]
// JM_STOPPED=1 renders with the transport stopped (nothing playing);
// JM_CIRCLE=<size> sets the Circle Size first.
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

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: jm_snapshot out.png [x y [size]]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui;

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

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (argc >= 5) ed->setSize(std::atoi(argv[4]), jmui::JungleEditor::heightFor(std::atoi(argv[4])));

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
        ask(980, 1013, 980);            // corner out, x ahead
        ask(980, 1018, 980);            // the mouse moved down only
        ask(990, 1018, 990);            // across only
        ask(990, 1070, W(1070));        // down only, and y overtakes
        ask(1000, 1065, W(1065));       // corner back in, y ahead
        ask(990, 1065, W(1065));        // across only: y still the further side
        ask(990, 1050, W(1050));
        newDrag();
        ask(1040, H(1000), 1040);       // a new drag: an edge again
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
        pad->mouseDrag(ev(448, 240, Mods::altModifier));
        report("Option, 160 px right: a sixteenth as far", near(value("x"), 0.6f + 10.0f / w));
        pad->mouseDrag(ev(448, 160, Mods::altModifier));
        report("Option, 80 px up: a sixteenth as far", near(value("y"), 0.5f + 5.0f / w));
        const float x0 = value("x"), y0 = value("y");
        // The pivot moved with the circle: from the press, 48 + 10 px right
        // and 5 px up.
        const juce::Point<float> pivot { 240.0f + 58.0f, 240.0f - 5.0f };
        const float d0 = juce::Point<float> { 448, 160 }.getDistanceFrom(pivot);
        pad->mouseDrag(ev(448, 130, Mods::shiftModifier));
        const float d1 = juce::Point<float> { 448, 130 }.getDistanceFrom(pivot);
        const float s1 = 1.0f + (d1 - d0) / (float) (jm::kPuckRadius * w);
        report("Shift, away from the pivot: grows, circle stays", near(value("size"), s1, 2e-3f) && value("size") > 1.0f
               && near(value("x"), x0) && near(value("y"), y0));
        pad->mouseDrag(ev(448, 160, Mods::shiftModifier));
        report("Shift, back towards it: shrinks back", near(value("size"), 1.0f, 2e-3f));
        pad->mouseDrag(ev(448, 160 + 400, Mods::shiftModifier | Mods::altModifier));
        const float d2 = juce::Point<float> { 448, 560 }.getDistanceFrom(pivot);
        report("Shift + Option, far away: a sixteenth as much",
               near(value("size"), 1.0f + (d2 - d0) / 16.0f / (float) (jm::kPuckRadius * w), 2e-3f));
        pad->mouseDrag(ev(448, 560 + 5000, Mods::shiftModifier));
        report("Shift, very far: held at the largest circle (3x)", near(value("size"), 3.0f, 1e-3f));
        proc.setPadHeld(false);
        proc.setFineDrag(false);
        pad->mouseDown(ev(10, 10, Mods::altModifier));
        report("Option press: the circle stays where it was", near(value("x"), x0) && near(value("y"), y0));
        proc.setPadHeld(false);
        proc.setFineDrag(false);

        // Command over the plugin (the editor's part, faked here): silent
        // from the next 16th (under 9 blocks away at 162 BPM), playing again
        // from the next 16th once let go. With the clock running, and with it
        // stopped during an audition (the pad held).
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
            loudest(10);
            const float muted = loudest(20);
            proc.setMuteHeld(false);
            loudest(10);
            const float back = loudest(20);
            std::printf("  %s: playing peak %.3f, Command held %.5f, let go %.3f\n",
                        running ? "clock running" : "clock stopped, pad held", before, muted, back);
            ok &= before > 0.05f && muted < 1e-6f && back > 0.05f;
        }
        proc.setPadHeld(false);
        std::printf(ok ? "pad check passed\n" : "pad check FAILED\n");
        if (! ok) return 1;
    }
    for (auto* c : ed->getChildren()) if (auto* pad = dynamic_cast<jmui::XYPad*>(c)) pad->refresh();
    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
