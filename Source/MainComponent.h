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

    std::set<int> activeNotes;
    juce::String analyzeChord(const std::set<int>& notes);

    // --- 소리를 내기 위해 추가된 객체들 ---
    juce::Synthesiser synth;                 // 신디사이저 본체
    juce::MidiMessageCollector midiCollector;// MIDI 신호를 모아서 오디오 스레드로 안전하게 전달
    // --------------------------------------

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};