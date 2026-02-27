#pragma once

#include <JuceHeader.h>
#include <set>

class MainComponent : public juce::AudioAppComponent,
    private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    std::set<int> activeNotes; // 현재 눌려 있는 건반 번호들을 저장
    juce::String analyzeChord(const std::set<int>& notes); // 코드 판별 함수
    // -------------------

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};