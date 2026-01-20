#include <juce_core/juce_core.h>
#include "../Source/SamplerEngine.h"

//==============================================================================
// Note Name Parsing Tests
//==============================================================================
class NoteNameParsingTests : public juce::UnitTest
{
public:
    NoteNameParsingTests() : juce::UnitTest("Note Name Parsing") {}

    void runTest() override
    {
        beginTest("Basic note names");
        {
            // C4 = MIDI 60 (middle C)
            expect(SamplerEngine::parseNoteName("C4") == 60);
            expect(SamplerEngine::parseNoteName("D4") == 62);
            expect(SamplerEngine::parseNoteName("E4") == 64);
            expect(SamplerEngine::parseNoteName("F4") == 65);
            expect(SamplerEngine::parseNoteName("G4") == 67);
            expect(SamplerEngine::parseNoteName("A4") == 69);
            expect(SamplerEngine::parseNoteName("B4") == 71);
        }

        beginTest("Sharps");
        {
            expect(SamplerEngine::parseNoteName("C#4") == 61);
            expect(SamplerEngine::parseNoteName("D#4") == 63);
            expect(SamplerEngine::parseNoteName("F#4") == 66);
            expect(SamplerEngine::parseNoteName("G#4") == 68);
            expect(SamplerEngine::parseNoteName("A#4") == 70);
        }

        beginTest("Flats");
        {
            expect(SamplerEngine::parseNoteName("Db4") == 61);
            expect(SamplerEngine::parseNoteName("Eb4") == 63);
            expect(SamplerEngine::parseNoteName("Gb4") == 66);
            expect(SamplerEngine::parseNoteName("Ab4") == 68);
            expect(SamplerEngine::parseNoteName("Bb4") == 70);
        }

        beginTest("Different octaves");
        {
            expect(SamplerEngine::parseNoteName("C0") == 12);
            expect(SamplerEngine::parseNoteName("C1") == 24);
            expect(SamplerEngine::parseNoteName("C2") == 36);
            expect(SamplerEngine::parseNoteName("C3") == 48);
            expect(SamplerEngine::parseNoteName("C5") == 72);
            expect(SamplerEngine::parseNoteName("C6") == 84);
            expect(SamplerEngine::parseNoteName("C7") == 96);
            expect(SamplerEngine::parseNoteName("C8") == 108);
        }

        beginTest("Boundary notes");
        {
            // A0 = MIDI 21 (lowest piano key)
            expect(SamplerEngine::parseNoteName("A0") == 21);
            // C8 = MIDI 108 (highest piano key)
            expect(SamplerEngine::parseNoteName("C8") == 108);
            // C-1 = MIDI 0 (lowest MIDI note)
            expect(SamplerEngine::parseNoteName("C-1") == 0);
            // G9 = MIDI 127 (highest MIDI note)
            expect(SamplerEngine::parseNoteName("G9") == 127);
        }

        beginTest("Case insensitivity");
        {
            expect(SamplerEngine::parseNoteName("c4") == 60);
            expect(SamplerEngine::parseNoteName("C4") == 60);
            expect(SamplerEngine::parseNoteName("c#4") == 61);
            expect(SamplerEngine::parseNoteName("db4") == 61);
        }

        beginTest("Invalid inputs");
        {
            expect(SamplerEngine::parseNoteName("") == -1);
            expect(SamplerEngine::parseNoteName("X4") == -1);
            expect(SamplerEngine::parseNoteName("C") == -1);
            expect(SamplerEngine::parseNoteName("4") == -1);
            expect(SamplerEngine::parseNoteName("CC4") == -1);
        }

        beginTest("Out of MIDI range");
        {
            // Too high
            expect(SamplerEngine::parseNoteName("G#9") == -1);  // Would be 128
            expect(SamplerEngine::parseNoteName("A9") == -1);   // Would be 129
            // C-2 would be negative
            expect(SamplerEngine::parseNoteName("C-2") == -1);
        }
    }
};

//==============================================================================
// File Name Parsing Tests
//==============================================================================
class FileNameParsingTests : public juce::UnitTest
{
public:
    FileNameParsingTests() : juce::UnitTest("File Name Parsing") {}

    void runTest() override
    {
        beginTest("Valid file names");
        {
            int note, velocity, roundRobin;

            expect(SamplerEngine::parseFileName("C4_127_01.wav", note, velocity, roundRobin));
            expect(note == 60);
            expect(velocity == 127);
            expect(roundRobin == 1);

            expect(SamplerEngine::parseFileName("G#6_040_02.wav", note, velocity, roundRobin));
            expect(note == 92);  // G#6 = (6+1)*12 + 8 = 92
            expect(velocity == 40);
            expect(roundRobin == 2);

            expect(SamplerEngine::parseFileName("Db3_080_03.wav", note, velocity, roundRobin));
            expect(note == 49);
            expect(velocity == 80);
            expect(roundRobin == 3);
        }

        beginTest("File names with suffixes");
        {
            int note, velocity, roundRobin;

            expect(SamplerEngine::parseFileName("A0_040_01_piano.wav", note, velocity, roundRobin));
            expect(note == 21);
            expect(velocity == 40);
            expect(roundRobin == 1);

            expect(SamplerEngine::parseFileName("F#5_100_02_soft_v2.wav", note, velocity, roundRobin));
            expect(note == 78);
            expect(velocity == 100);
            expect(roundRobin == 2);
        }

        beginTest("Different audio formats");
        {
            int note, velocity, roundRobin;

            expect(SamplerEngine::parseFileName("C4_127_01.aif", note, velocity, roundRobin));
            expect(SamplerEngine::parseFileName("C4_127_01.aiff", note, velocity, roundRobin));
            expect(SamplerEngine::parseFileName("C4_127_01.flac", note, velocity, roundRobin));
            expect(SamplerEngine::parseFileName("C4_127_01.mp3", note, velocity, roundRobin));
        }

        beginTest("Velocity boundaries");
        {
            int note, velocity, roundRobin;

            // Valid velocities: 1-127
            expect(SamplerEngine::parseFileName("C4_001_01.wav", note, velocity, roundRobin));
            expect(velocity == 1);

            expect(SamplerEngine::parseFileName("C4_127_01.wav", note, velocity, roundRobin));
            expect(velocity == 127);

            // Invalid velocities
            expect(!SamplerEngine::parseFileName("C4_000_01.wav", note, velocity, roundRobin));  // 0 is invalid
            expect(!SamplerEngine::parseFileName("C4_128_01.wav", note, velocity, roundRobin));  // > 127
            expect(!SamplerEngine::parseFileName("C4_256_01.wav", note, velocity, roundRobin));  // Way too high
        }

        beginTest("Round robin boundaries");
        {
            int note, velocity, roundRobin;

            // Valid RR: 1+
            expect(SamplerEngine::parseFileName("C4_127_01.wav", note, velocity, roundRobin));
            expect(roundRobin == 1);

            expect(SamplerEngine::parseFileName("C4_127_99.wav", note, velocity, roundRobin));
            expect(roundRobin == 99);

            // Invalid RR: 0
            expect(!SamplerEngine::parseFileName("C4_127_00.wav", note, velocity, roundRobin));
        }

        beginTest("Invalid file names");
        {
            int note, velocity, roundRobin;

            // Too few parts
            expect(!SamplerEngine::parseFileName("C4_127.wav", note, velocity, roundRobin));
            expect(!SamplerEngine::parseFileName("C4.wav", note, velocity, roundRobin));
            expect(!SamplerEngine::parseFileName(".wav", note, velocity, roundRobin));

            // Invalid note
            expect(!SamplerEngine::parseFileName("X4_127_01.wav", note, velocity, roundRobin));
            expect(!SamplerEngine::parseFileName("_127_01.wav", note, velocity, roundRobin));

            // Non-numeric velocity/RR
            expect(!SamplerEngine::parseFileName("C4_abc_01.wav", note, velocity, roundRobin));
            expect(!SamplerEngine::parseFileName("C4_127_ab.wav", note, velocity, roundRobin));

            // Empty parts
            expect(!SamplerEngine::parseFileName("__01.wav", note, velocity, roundRobin));
        }
    }
};

//==============================================================================
// Static test instances (auto-registered with JUCE)
//==============================================================================
static NoteNameParsingTests noteNameParsingTests;
static FileNameParsingTests fileNameParsingTests;

//==============================================================================
// Main test runner
//==============================================================================
int main(int argc, char* argv[])
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int numFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* result = runner.getResult(i);
        if (result->failures > 0)
        {
            numFailures += result->failures;
            std::cout << "FAILED: " << result->unitTestName << " - " << result->subcategoryName << std::endl;
            for (auto& msg : result->messages)
                std::cout << "  " << msg << std::endl;
        }
    }

    if (numFailures == 0)
        std::cout << "All tests passed!" << std::endl;
    else
        std::cout << numFailures << " test(s) failed." << std::endl;

    return numFailures > 0 ? 1 : 0;
}
