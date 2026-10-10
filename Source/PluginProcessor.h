#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Pitch-synchroner Overlap-Add Pitchshifter (PSOLA-artig), Stereo im Gleichschritt
class PsolaShifter
{
public:
    static constexpr int kSize = 16384;
    static constexpr int kMask = kSize - 1;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        minPeriod = sr / 900.0;
        maxPeriod = sr / 75.0;
        latency = (int) std::ceil (3.0 * maxPeriod);
        reset();
    }

    void reset()
    {
        for (auto& b : inBuf)  b.assign ((size_t) kSize, 0.0f);
        for (auto& b : outBuf) b.assign ((size_t) kSize, 0.0f);
        n = 0;
        nextCenter = 0.0;
        analysisCenter = -(double) latency;
        period = juce::jlimit (minPeriod, maxPeriod, sr / 150.0);
    }

    int getLatency() const { return latency; }
    void setPeriod (double p) { period = juce::jlimit (minPeriod, maxPeriod, p); }

    // in/out/dry: je 2 Kanaele
    void process (const float* in, double ratio, float* out, float* dryDelayed)
    {
        for (int c = 0; c < 2; ++c)
            inBuf[c][(size_t) (n & kMask)] = in[c];

        ratio = juce::jlimit (0.25, 4.0, ratio);

        while (nextCenter - period <= (double) n)
            emitGrain (ratio);

        const auto idx = (size_t) (n & kMask);
        const auto dIdx = (size_t) ((n - latency) & kMask);
        for (int c = 0; c < 2; ++c)
        {
            out[c] = outBuf[c][idx];
            outBuf[c][idx] = 0.0f;
            dryDelayed[c] = inBuf[c][dIdx];
        }
        ++n;
    }

private:
    void emitGrain (double ratio)
    {
        const double target = nextCenter - (double) latency;
        while (target - analysisCenter > 0.5 * period) analysisCenter += period;
        while (analysisCenter - target > 1.5 * period) analysisCenter -= period;

        const std::int64_t oc = (std::int64_t) std::llround (nextCenter);
        const double d = (double) oc - nextCenter;
        const int half = (int) std::ceil (period);
        const float gain = (float) (1.0 / ratio);
        const double invP = juce::MathConstants<double>::pi / period;

        for (int k = -half; k <= half; ++k)
        {
            const std::int64_t outIdx = oc + k;
            if (outIdx < n) continue;

            const double rel = (double) k + d;
            if (std::abs (rel) >= period) continue;

            const float w = (float) (0.5 + 0.5 * std::cos (invP * rel)) * gain;
            const double rp = analysisCenter + rel;
            const std::int64_t i0 = (std::int64_t) std::floor (rp);
            const float f = (float) (rp - (double) i0);

            for (int c = 0; c < 2; ++c)
            {
                const float a = inBuf[c][(size_t) (i0 & kMask)];
                const float b = inBuf[c][(size_t) ((i0 + 1) & kMask)];
                outBuf[c][(size_t) (outIdx & kMask)] += w * (a + f * (b - a));
            }
        }
        nextCenter += period / ratio;
    }

    std::vector<float> inBuf[2], outBuf[2];
    double sr = 44100.0, minPeriod = 50.0, maxPeriod = 600.0, period = 300.0;
    double nextCenter = 0.0, analysisCenter = 0.0;
    int latency = 1800;
    std::int64_t n = 0;
};

class RedTuneAudioProcessor : public juce::AudioProcessor
{
public:
    RedTuneAudioProcessor();
    ~RedTuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RedTune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::StringArray getKeyNames();
    static juce::StringArray getScaleNames();

    juce::AudioProcessorValueTreeState apvts;

    // fuer die Oberflaeche
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    float detectPitch (std::int64_t segStart);
    double quantise (double midi) const;
    void runDetection();

    static constexpr int kRingSize = 32768;
    static constexpr int kRingMask = kRingSize - 1;

    PsolaShifter shifter;
    std::vector<float> detRing, detLin, yin;
    std::int64_t sampleCounter = 0;
    int hopCountdown = 0, detW = 1024, detTauMin = 50, detTauMax = 640, detHop = 512;
    double sr = 44100.0;

    // aktuelle Parameter (pro Block gesetzt)
    int curKey = 0, curScale = 0;
    double curAmount = 1.0, curPitchOff = 0.0;

    double shiftTarget = 0.0, shiftSmoothed = 0.0, corr = 0.0;
    int unvoicedFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedTuneAudioProcessor)
};
