#pragma once
#include "PluginProcessor.h"

namespace jmui {

// The house palette (shared with canon / dirty630).
const juce::Colour bg     { 0xff111111 };
const juce::Colour panel  { 0xff161616 };
const juce::Colour line   { 0xff222222 };
const juce::Colour dark   { 0xff2c2c2c };
const juce::Colour dim    { 0xff707070 };
const juce::Colour light  { 0xffd0d0d0 };
const juce::Colour ink    { 0xffffffff };
const juce::Colour accent { 0xff30e6aa };

// The XY pad: every break sits at its similarity-layout position and owns the
// patch of pad nearest to it. Drag the puck to choose which break plays.
class XYPad : public juce::Component {
public:
    explicit XYPad(JungleProcessor& p);
    void refresh();                         // poll params + playback state (30 Hz)
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Point<float> toScreen(juce::Point<float> u) const;
    juce::Point<float> toUnit(juce::Point<float> s) const;
    void setFromMouse(juce::Point<float> s);
    void buildCells();
    void buildHighlight();

    JungleProcessor&    proc;
    const LoopLibrary&  lib;
    juce::RangedAudioParameter* px = nullptr;
    juce::RangedAudioParameter* py = nullptr;

    juce::Point<float> puck { 0.5f, 0.5f };
    int    selected = -1, sounding = -1;
    double phase = 0.0;

    std::vector<int> cellOf;                // nearest loop per map pixel
    int   mapSize = 0;
    juce::Image cells, highlight;           // cell borders; the selected cell
};

class JungleEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit JungleEditor(JungleProcessor&);
    ~JungleEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { pad.refresh(); }

    XYPad pad;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JungleEditor)
};

} // namespace jmui
