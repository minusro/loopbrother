// Renders the editor to a PNG without opening a window or an audio device:
// the processor runs a few blocks under a scripted transport so the pad shows
// live playback state.
//   cmake -B build -DJM_BUILD_SNAPSHOT=ON && cmake --build build --target jm_snapshot
//   build/jm_snapshot_artefacts/Release/jm_snapshot out.png [x y [size]]
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

struct ScriptedPlayHead : juce::AudioPlayHead {
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(162.0);
        p.setPpqPosition(ppq);
        p.setIsPlaying(true);
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

    // Play about a bar and a half so the playhead rings are partway round.
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 140; ++b)
    {
        proc.processBlock(buf, midi);
        head.ppq += 512 * 162.0 / 60.0 / 48000.0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (argc >= 5) ed->setSize(std::atoi(argv[4]), std::atoi(argv[4]));

    // Window resizing, by every route a new size takes.
    if (std::getenv("JM_RESIZE_CHECK"))
    {
        bool ok = true;
        auto newDrag = [] { juce::Thread::sleep((int) jmui::SquareConstrainer::kDragGapMs + 50); };

        // A host that ignores the constraint: the editor squares itself
        // afterwards along the side that moved.
        auto drag = [&](int w, int h, int want)
        {
            ed->setSize(w, h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            std::printf("  host %dx%d -> editor %dx%d (want %d)\n", w, h, ed->getWidth(), ed->getHeight(), want);
            ok &= ed->getWidth() == want && ed->getHeight() == want;
        };
        const int s0 = ed->getWidth();
        drag(s0 + 200, s0, s0 + 200);           // right edge out: grows
        drag(s0 + 200, s0 + 440, s0 + 440);     // bottom edge out: grows
        drag(s0 + 240, s0 + 440, s0 + 240);     // right edge in: shrinks
        drag(1500, s0 + 240, 1440);             // past the maximum: held at it
        newDrag();
        ed->setSize(s0, s0);
        newDrag();
        drag(s0 + 20, s0 + 5, s0 + 20);         // corner, off the diagonal: the further side
        drag(s0 + 20, s0 + 10, s0 + 20);        // only the nearer side moved: still the corner
        drag(s0 + 30, s0 + 10, s0 + 30);
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
        auto frame = [&](juce::Rectangle<int> proposed, bool top, bool left, bool bottom, bool right, juce::Rectangle<int> want)
        {
            newDrag();
            const auto old = window.border.addedTo(juce::Rectangle<int> { 100, 100, 960, 960 });
            auto r = window.border.addedTo(proposed);
            window.checkBounds(r, old, {}, top, left, bottom, right);
            r = window.border.subtractedFrom(r);
            std::printf("  frame %s -> %s (want %s)\n", proposed.toString().toRawUTF8(), r.toString().toRawUTF8(), want.toString().toRawUTF8());
            ok &= r == want;
        };
        frame({ 100, 100, 1060, 960 }, false, false, false, true,  { 100,  50, 1060, 1060 }); // right edge out
        frame({  40, 100, 1020, 960 }, false, true,  false, false, {  40,  70, 1020, 1020 }); // left edge out
        frame({ 100, 100,  960, 860 }, false, false, true,  false, { 150, 100,  860,  860 }); // bottom edge in
        frame({   0,  60, 1060, 1000 }, true, true,  false, false, {   0,   0, 1060, 1060 }); // top-left corner out
        frame({ 100, 100,  900,  920 }, false, false, true,  true, { 100, 100,  920,  920 }); // bottom-right corner in

        // Plugin hosts: the window is the host's; it asks for the size the
        // mouse points at and says which sides changed (JUCE's VST3 wrapper,
        // patched). A corner dragged off the diagonal often moves along one
        // axis only: that must not read as an edge drag of the other side.
        newDrag();
        int side = 960;
        auto ask = [&](int w, int h, int want)
        {
            juce::Rectangle<int> r { w, h };
            const juce::Rectangle<int> current { side, side };
            ed->getConstrainer()->checkBounds(r, current, {}, false, false, h != side, w != side);
            std::printf("  ask %dx%d at %d -> %dx%d (want %d)\n", w, h, side, r.getWidth(), r.getHeight(), want);
            ok &= r.getWidth() == want && r.getHeight() == want;
            side = r.getWidth();
        };
        ask(980, 965, 980);     // corner out, x ahead
        ask(980, 970, 980);     // the mouse moved down only
        ask(990, 970, 990);     // across only
        ask(990, 975, 990);
        ask(1000, 1010, 1010);  // y overtakes
        ask(1000, 1005, 1005);  // corner back in
        ask(990, 1005, 1005);   // across only: y still the further side
        ask(990, 995, 995);
        newDrag();
        ask(1040, 995, 1040);   // a new drag: an edge again
        ask(1040, 900, 900);
        std::printf(ok ? "resize check passed\n" : "resize check FAILED\n");
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
