#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    setSize(800, 600);

    if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio)
        && !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio,
            //[&](bool granted) { setAudioChannels(granted ? 2 : 0, 2); });
            [&](bool granted) { setAudioChannels(0, 2); });
    }
    else
    {
        //setAudioChannels(2, 2);
        setAudioChannels(0, 2);
    }

    // --- MIDI 입력 장치 활성화 및 콜백 등록 ---
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto input : midiInputs)
    {
        // 앱 실행 시 인식된 장치 이름을 출력
        DBG("Found MIDI Device: " << input.name);

        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback(input.identifier, this);
    }
}

MainComponent::~MainComponent()
{
    // --- 등록된 MIDI 콜백 해제 ---
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (auto input : midiInputs)
    {
        deviceManager.removeMidiInputDeviceCallback(input.identifier, this);
    }

    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto level = bufferToFill.buffer->getRMSLevel(0, bufferToFill.startSample, bufferToFill.numSamples);

    if (level > 0.01f)
        DBG("Audio Input Detected! Level: " << level);

    bufferToFill.clearActiveBufferRegion();
}

void MainComponent::releaseResources()
{
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
}

// --- MIDI 수신 함수 구현부 ---
void MainComponent::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    bool stateChanged = false;

    // 1. 건반이 눌렸을 때 배열에 추가
    if (message.isNoteOn())
    {
        activeNotes.insert(message.getNoteNumber());
        stateChanged = true;
    }
    // 2. 건반이 떨어졌을 때 배열에서 제거
    else if (message.isNoteOff())
    {
        activeNotes.erase(message.getNoteNumber());
        stateChanged = true;
    }

    // 3. 상태가 변했다면 코드를 판별하여 출력
    if (stateChanged)
    {
        // 비동기 스레드에서 안전하게 사용하기 위해 현재 눌린 건반 목록을 복사
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

// --- 코드 판별 로직 구현부 ---
juce::String MainComponent::analyzeChord(const std::set<int>& notes)
{
    // 음이 3개 미만이면 코드로 판별하지 않음
    if (notes.size() < 3) return "";

    // 가장 낮은 음을 베이스로 간주
    auto it = notes.begin();
    int lowestNote = *it;

    // 12음계 기준 루트 노트 문자열 매핑 (C = 0)
    int rootPitchClass = lowestNote % 12;
    juce::StringArray noteNames = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    juce::String rootName = noteNames[rootPitchClass];

    // 베이스 음과의 간격(반음 개수)을 모두 계산하여 저장
    std::set<int> intervals;
    for (int note : notes)
    {
        intervals.insert((note - lowestNote) % 12);
    }

    // C++ 호환성을 위해 contains 대신 count 사용
    // 간격을 바탕으로 기본 코드 쉐입 매칭
    if (intervals.count(4) && intervals.count(7)) // 장3도(4), 완전5도(7) 포함
    {
        if (intervals.count(11)) return rootName + "M7"; // 장7도(11)
        if (intervals.count(10)) return rootName + "7";  // 단7도(10)
        return rootName + " Major";
    }
    else if (intervals.count(3) && intervals.count(7)) // 단3도(3), 완전5도(7) 포함
    {
        if (intervals.count(10)) return rootName + "m7";
        return rootName + " Minor";
    }
    else if (intervals.count(3) && intervals.count(6)) // 감3화음 계열
    {
        if (intervals.count(9)) return rootName + "dim7";
        if (intervals.count(10)) return rootName + "m7(b5)";
    }

    // 판별할 수 없는 구성음일 경우 루트음만 표기
    return rootName + " (Unknown)";
}