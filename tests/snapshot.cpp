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
    for (auto* c : ed->getChildren()) if (auto* pad = dynamic_cast<jmui::XYPad*>(c)) pad->refresh();
    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
