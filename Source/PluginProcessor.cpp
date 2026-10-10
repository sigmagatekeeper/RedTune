#include "PluginProcessor.h"
#include "PluginEditor.h"

using Range = juce::NormalisableRange<float>;

static const int kScaleMasks[7] = { 0xFFF, 2741, 1453, 1193, 661, 2477, 1451 };

juce::StringArray RedTuneAudioProcessor::getKeyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray RedTuneAudioProcessor::getScaleNames()
{
    return { "Chromatic", "Major", "Minor", "Pentatonic Minor",
             "Pentatonic Major", "Harmonic Minor", "Phrygian" };
}

static std::unique_ptr<juce::AudioParameterFloat>
mk (const char* id, const char* name, Range r, float def, const char* suffix)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

juce::AudioProcessorValueTreeState::ParameterLayout RedTuneAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "key", 1 }, "Key", getKeyNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "scale", 1 }, "Scale", getScaleNames(), 0));
    l.add (mk ("speed",   "Retune Speed", Range (0.f, 400.f, 0.1f, 0.4f), 0.f,   "ms"));
    l.add (mk ("amount",  "Amount",       Range (0.f, 100.f, 0.1f),       100.f, "%"));
    l.add (mk ("pitch",   "Pitch",        Range (-12.f, 12.f, 0.01f),     0.f,   "st"));
    l.add (mk ("mix",     "Dry/Wet",      Range (0.f, 100.f, 0.1f),       100.f, "%"));
    l.add (mk ("inGain",  "Input",        Range (-24.f, 24.f, 0.1f),      0.f,   "dB"));
    l.add (mk ("outGain", "Output",       Range (-24.f, 12.f, 0.1f),      0.f,   "dB"));
    return l;
}

RedTuneAudioProcessor::RedTuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool RedTuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

juce::AudioProcessorEditor* RedTuneAudioProcessor::createEditor()
{
    return new RedTuneAudioProcessorEditor (*this);
}

void RedTuneAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    shifter.prepare (sr);
    setLatencySamples (shifter.getLatency());

    detW      = juce::jmin (2048, (int) std::round (sr * 0.0213));
    detTauMin = (int) std::floor (sr / 900.0);
    detTauMax = (int) std::ceil (sr / 75.0);
    detHop    = (int) std::round (sr * 0.0107);

    detRing.assign ((size_t) kRingSize, 0.0f);
    detLin.assign ((size_t) (detW + detTauMax + 4), 0.0f);
    yin.assign ((size_t) (detTauMax + 4), 1.0f);

    sampleCounter = 0;
    hopCountdown = 0;
    shiftTarget = shiftSmoothed = corr = 0.0;
    unvoicedFrames = 0;
}

float RedTuneAudioProcessor::detectPitch (std::int64_t segStart)
{
    const int S = detW + detTauMax;
    for (int i = 0; i < S; ++i)
        detLin[(size_t) i] = detRing[(size_t) ((segStart + i) & kRingMask)];

    // Lautstaerke-Gate
    double e = 0.0;
    for (int i = 0; i < detW; ++i) e += (double) detLin[(size_t) i] * detLin[(size_t) i];
    if (std::sqrt (e / detW) < 0.004) return 0.0f;

    // YIN
    const float* x = detLin.data();
    yin[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= detTauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < detW; ++j)
        {
            const float d = x[j] - x[j + tau];
            sum += d * d;
        }
        running += sum;
        yin[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
    }

    int tauEst = -1;
    for (int tau = detTauMin; tau < detTauMax; ++tau)
    {
        if (yin[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < detTauMax && yin[(size_t) tau + 1] < yin[(size_t) tau]) ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0)
    {
        float best = 1e9f;
        for (int tau = detTauMin; tau < detTauMax; ++tau)
            if (yin[(size_t) tau] < best) { best = yin[(size_t) tau]; tauEst = tau; }
        if (best > 0.4f || tauEst < 0) return 0.0f;
    }

    float better = (float) tauEst;
    if (tauEst > 1 && tauEst < detTauMax - 1)
    {
        const float s0 = yin[(size_t) tauEst - 1], s1 = yin[(size_t) tauEst], s2 = yin[(size_t) tauEst + 1];
        const float denom = s0 + s2 - 2.0f * s1;
        if (std::abs (denom) > 1e-9f) better = (float) tauEst + 0.5f * (s0 - s2) / denom;
    }
    return (float) (sr / better);
}

double RedTuneAudioProcessor::quantise (double midi) const
{
    const int mask = kScaleMasks[juce::jlimit (0, 6, curScale)];
    const int base = (int) std::floor (midi + 0.5);
    double best = midi, bestDist = 1e9;
    for (int note = base - 7; note <= base + 7; ++note)
    {
        const int pc = (((note - curKey) % 12) + 12) % 12;
        if (mask & (1 << pc))
        {
            const double dist = std::abs ((double) note - midi);
            if (dist < bestDist) { bestDist = dist; best = (double) note; }
        }
    }
    return best;
}

void RedTuneAudioProcessor::runDetection()
{
    const int S = detW + detTauMax;
    const std::int64_t segEnd = sampleCounter - shifter.getLatency() + S / 2;
    const float f0 = detectPitch (segEnd - S);

    if (f0 > 0.0f)
    {
        const double midi = 69.0 + 12.0 * std::log2 ((double) f0 / 440.0);
        const double target = quantise (midi);
        corr = (target - midi) * curAmount;
        unvoicedFrames = 0;
        shifter.setPeriod (sr / (double) f0);
        detectedMidi.store ((float) midi);
        targetMidi.store ((float) target);
    }
    else
    {
        if (++unvoicedFrames > 4) corr = 0.0;
        detectedMidi.store (-1.0f);
    }
    shiftTarget = corr + curPitchOff;
}

void RedTuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh < 1) return;

    curKey      = (int) get ("key");
    curScale    = (int) get ("scale");
    curAmount   = (double) get ("amount") / 100.0;
    curPitchOff = (double) get ("pitch");

    const double speedMs = (double) get ("speed");
    const double coef = speedMs <= 0.05 ? 1.0 : 1.0 - std::exp (-1.0 / (speedMs * 0.001 * sr));
    const float inG  = juce::Decibels::decibelsToGain (get ("inGain"));
    const float outG = juce::Decibels::decibelsToGain (get ("outGain"));
    const float mix  = get ("mix") / 100.0f;

    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float inPk = 0.0f, outPk = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float in[2], wet[2], dry[2];
        in[0] = ch0[i] * inG;
        in[1] = ch1 ? ch1[i] * inG : in[0];
        inPk = juce::jmax (inPk, std::abs (in[0]), std::abs (in[1]));

        detRing[(size_t) (sampleCounter & kRingMask)] = 0.5f * (in[0] + in[1]);
        ++sampleCounter;

        if (--hopCountdown <= 0)
        {
            hopCountdown = detHop;
            runDetection();
        }

        shiftSmoothed += coef * (shiftTarget - shiftSmoothed);
        const double ratio = std::pow (2.0, shiftSmoothed / 12.0);

        shifter.process (in, ratio, wet, dry);

        const float o0 = (dry[0] * (1.0f - mix) + wet[0] * mix) * outG;
        const float o1 = (dry[1] * (1.0f - mix) + wet[1] * mix) * outG;
        ch0[i] = o0;
        if (ch1) ch1[i] = o1;
        outPk = juce::jmax (outPk, std::abs (o0), std::abs (o1));
    }

    inPeak.store (juce::jmax (inPeak.load(), inPk));
    outPeak.store (juce::jmax (outPeak.load(), outPk));
}

void RedTuneAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void RedTuneAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RedTuneAudioProcessor();
}
