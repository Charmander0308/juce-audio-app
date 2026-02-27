#include "MainComponent.h"

// ==============================================================================
// 1. 소리를 발생시키는 기초 신디사이저(사인파) 클래스 정의
struct SineWaveSound : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct SineWaveVoice : public juce::SynthesiserVoice
{
    bool canPlaySound(juce::SynthesiserSound* sound) override { return dynamic_cast<SineWaveSound*>(sound) != nullptr; }

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        currentAngle = 0.0;
        level = velocity * 0.15; // 너무 시끄럽지 않게 볼륨 조절
        tailOff = 0.0;

        auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        auto cyclesPerSample = cyclesPerSecond / getSampleRate();
        angleDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
    }

    void stopNote(float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff) {
            if (tailOff == 0.0) tailOff = 1.0;
        }
        else {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (angleDelta != 0.0)
        {
            if (tailOff > 0.0)
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float)(std::sin(currentAngle) * level * tailOff);
                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample(i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                    tailOff *= 0.99; // 건반을 뗐을 때 소리가 자연스럽게 페이드아웃 됨 (Release)

                    if (tailOff <= 0.005)
                    {
                        clearCurrentNote();
                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float)(std::sin(currentAngle) * level);
                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample(i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
        }
    }

private:
    double currentAngle = 0.0, angleDelta = 0.0, level = 0.0, tailOff = 0.0;
};
// ==============================================================================

MainComponent::MainComponent()
{
    setSize(800, 600);

    // 1. JUCE의 기본 방식으로 오디오 채널을 먼저 정상 초기화합니다.
    if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio)
        && !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio,
            [&](bool granted) { setAudioChannels(0, 2); });
    }
    else
    {
        setAudioChannels(0, 2);
    }

    // 2. 초기화가 완료된 오디오 장치의 설정을 불러와서 버퍼 사이즈만 256으로 변경합니다.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    setup.bufferSize = 256;
    deviceManager.setAudioDeviceSetup(setup, true); // 변경된 설정 적용

    // 2. 신디사이저 초기화 (8동시 발음 설정)
    synth.clearVoices();
    for (int i = 0; i < 8; ++i)
        synth.addVoice(new SineWaveVoice());

    synth.clearSounds();
    synth.addSound(new SineWaveSound());

    // --- MIDI 장치 활성화 ---
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto input : midiInputs)
    {
        DBG("Found MIDI Device: " << input.name);
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }
}

MainComponent::~MainComponent()
{
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto input : midiInputs)
    {
        deviceManager.removeMidiInputDeviceCallback(input.identifier, this);
    }
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // 3. 샘플레이트 동기화
    synth.setCurrentPlaybackSampleRate(sampleRate);
    midiCollector.reset(sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // 4. 오디오 버퍼를 비운 뒤, 콜렉터에 쌓인 MIDI 신호를 신디사이저로 렌더링
    bufferToFill.clearActiveBufferRegion();

    juce::MidiBuffer incomingMidi;
    midiCollector.removeNextBlockOfMessages(incomingMidi, bufferToFill.numSamples);

    synth.renderNextBlock(*bufferToFill.buffer, incomingMidi,
        bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    // 5. 건반 입력 신호를 오디오 스레드(신디사이저)로 전달
    midiCollector.addMessageToQueue(message);

    // --- 기존 화음 판별 로직 ---
    bool stateChanged = false;

    if (message.isNoteOn())
    {
        activeNotes.insert(message.getNoteNumber());
        stateChanged = true;
    }
    else if (message.isNoteOff())
    {
        activeNotes.erase(message.getNoteNumber());
        stateChanged = true;
    }

    if (stateChanged)
    {
        std::set<int> currentNotes = activeNotes;
        juce::MessageManager::callAsync([this, currentNotes]()
            {
                juce::String chordName = analyzeChord(currentNotes);
                if (chordName.isNotEmpty())
                {
                    DBG("Current Voicing: " << chordName);
                }
            });
    }
}

juce::String MainComponent::analyzeChord(const std::set<int>& notes)
{
    if (notes.size() < 3) return "";

    auto it = notes.begin();
    int lowestNote = *it;

    int rootPitchClass = lowestNote % 12;
    juce::StringArray noteNames = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    juce::String rootName = noteNames[rootPitchClass];

    std::set<int> intervals;
    for (int note : notes)
    {
        intervals.insert((note - lowestNote) % 12);
    }

    if (intervals.count(4) && intervals.count(7))
    {
        if (intervals.count(11)) return rootName + "M7";
        if (intervals.count(10)) return rootName + "7";
        return rootName + " Major";
    }
    else if (intervals.count(3) && intervals.count(7))
    {
        if (intervals.count(10)) return rootName + "m7";
        return rootName + " Minor";
    }
    else if (intervals.count(3) && intervals.count(6))
    {
        if (intervals.count(9)) return rootName + "dim7";
        if (intervals.count(10)) return rootName + "m7(b5)";
    }

    return rootName + " (Unknown)";
}