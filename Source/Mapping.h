#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Which kind of MIDI message carries a value. Raw values are 0-127 for
// a plain CC, and 0-16383 for 14-bit CC (MSB on `number`, LSB on
// `number` + 32) and pitch bend.
enum class MidiMessageType { controlChange, controlChange14Bit, pitchBend };

// A MIDI value source or destination: which message, on which channel.
struct MidiSpec
{
    MidiMessageType type = MidiMessageType::controlChange;
    int channel = 1;   // 1-16
    int number = 7;    // controller number (0-127, or 0-31 for 14-bit CC); unused for pitch bend

    static float maxValueFor (MidiMessageType t) { return t == MidiMessageType::controlChange ? 127.0f : 16383.0f; }
    float maxValue() const { return maxValueFor (type); }

    juce::String describe() const
    {
        switch (type)
        {
            case MidiMessageType::controlChange:      return "CC" + juce::String (number) + " ch" + juce::String (channel);
            case MidiMessageType::controlChange14Bit: return "CC" + juce::String (number) + "/" + juce::String (number + 32)
                                                               + " ch" + juce::String (channel);
            case MidiMessageType::pitchBend:          return "Pitch Bend ch" + juce::String (channel);
        }
        return {};
    }

    bool operator== (const MidiSpec& other) const
    {
        return type == other.type && channel == other.channel
                 && (type == MidiMessageType::pitchBend || number == other.number);
    }

    static juce::String typeToString (MidiMessageType t)
    {
        switch (t)
        {
            case MidiMessageType::controlChange14Bit: return "cc14";
            case MidiMessageType::pitchBend:          return "pitchBend";
            case MidiMessageType::controlChange:      break;
        }
        return "cc";
    }

    static MidiMessageType typeFromString (const juce::String& s)
    {
        if (s == "cc14")      return MidiMessageType::controlChange14Bit;
        if (s == "pitchBend") return MidiMessageType::pitchBend;
        return MidiMessageType::controlChange;
    }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("type", typeToString (type));
        obj->setProperty ("channel", channel);
        obj->setProperty ("number", number);
        return juce::var (obj);
    }

    static MidiSpec fromVar (const juce::var& v)
    {
        MidiSpec s;
        s.type    = typeFromString (v.getProperty ("type", "cc").toString());
        s.channel = juce::jlimit (1, 16, (int) v.getProperty ("channel", 1));
        s.number  = juce::jlimit (0, 127, (int) v.getProperty ("number", 7));
        return s;
    }
};

enum class TargetKind { osc, midi };

// A named destination that outputs send to: either an OSC host:port, or
// a MIDI output port (matched by device name, so projects survive the
// device being unplugged and replugged, or moved to another machine).
struct SendTarget
{
    juce::String id { juce::Uuid().toString() };
    juce::String name { "Desk" };
    TargetKind kind = TargetKind::osc;
    juce::String host { "127.0.0.1" };
    int port = 9001;
    juce::String midiDevice;

    // True if both would open the same connection (name changes don't need a reconnect).
    bool sameConnectionAs (const SendTarget& other) const
    {
        return id == other.id && kind == other.kind && host == other.host
                 && port == other.port && midiDevice == other.midiDevice;
    }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", id);
        obj->setProperty ("name", name);
        obj->setProperty ("kind", kind == TargetKind::midi ? "midi" : "osc");
        obj->setProperty ("host", host);
        obj->setProperty ("port", port);
        obj->setProperty ("midiDevice", midiDevice);
        return juce::var (obj);
    }

    static SendTarget fromVar (const juce::var& v)
    {
        SendTarget t;
        t.id         = v.getProperty ("id", juce::Uuid().toString()).toString();
        t.name       = v.getProperty ("name", "Desk").toString();
        t.kind       = v.getProperty ("kind", "osc").toString() == "midi" ? TargetKind::midi : TargetKind::osc;
        t.host       = v.getProperty ("host", "127.0.0.1").toString();
        t.port       = (int) v.getProperty ("port", 9001);
        t.midiDevice = v.getProperty ("midiDevice", juce::String()).toString();
        return t;
    }
};

// One output that an incoming fade is forwarded to, with its own scale range.
// Which of `address` / `midi` is used depends on the kind of its target.
struct OutputTarget
{
    juce::String targetId;                  // SendTarget::id - empty means "not sent anywhere"
    juce::String address { "/output/1" };   // for OSC targets
    MidiSpec midi;                          // for MIDI targets
    float outMin = 0.0f;
    float outMax = 1.0f;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("targetId", targetId);
        obj->setProperty ("address", address);
        obj->setProperty ("midi", midi.toVar());
        obj->setProperty ("outMin", (double) outMin);
        obj->setProperty ("outMax", (double) outMax);
        return juce::var (obj);
    }

    static OutputTarget fromVar (const juce::var& v)
    {
        OutputTarget t;
        t.targetId = v.getProperty ("targetId", juce::String()).toString();
        t.address  = v.getProperty ("address", "/output/1").toString();
        t.midi     = MidiSpec::fromVar (v.getProperty ("midi", juce::var()));
        t.outMin   = (float) (double) v.getProperty ("outMin", 0.0);
        t.outMax   = (float) (double) v.getProperty ("outMax", 1.0);
        return t;
    }
};

enum class InputSource { osc, midi };

// Something that receives values and fades between them: the input half
// shared by Mapping and Scaler.
//
// OSC inputs receive [x, y] (value, fade seconds) on inputAddress. MIDI
// inputs receive a raw value from midiIn and fade over midiFadeSeconds
// (0 = follow instantly, e.g. a hardware fader).
struct InputChannel
{
    juce::String id { juce::Uuid().toString() };
    InputSource source = InputSource::osc;
    juce::String inputAddress { "/bridge/fader1" };
    juce::String midiDevice;   // MIDI input device name; empty = any device
    MidiSpec midiIn;
    double midiFadeSeconds = 0.0;
    float inMin = 0.0f;
    float inMax = 1.0f;

    // Runtime-only state, not persisted.
    bool hasLiveValue = false;   // the current value (moves during a fade)
    float liveValue = 0.0f;
    int midiMsb = 0;             // last 14-bit CC MSB received
    double midiMsbPendingSince = -1.0;   // when an MSB arrived that's still waiting for its LSB (-1 = none)

    void copyRuntimeStateFrom (const InputChannel& other)
    {
        hasLiveValue = other.hasLiveValue;
        liveValue = other.liveValue;
        midiMsb = other.midiMsb;
        midiMsbPendingSince = other.midiMsbPendingSince;
    }

    // The live value as 0-1 within the input range (0 if nothing received yet).
    float getNormalisedValue() const
    {
        if (! hasLiveValue || juce::approximatelyEqual (inMin, inMax))
            return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (liveValue - inMin) / (inMax - inMin));
    }

    InputChannel() = default;
    InputChannel (const InputChannel&) = default;
    InputChannel (InputChannel&&) = default;
    InputChannel& operator= (const InputChannel&) = default;
    InputChannel& operator= (InputChannel&&) = default;
    virtual ~InputChannel() = default;

    // The input address or MIDI message - doubles as a mapping's name, so
    // there's no separate name field to keep in sync.
    juce::String getInputDescription() const
    {
        if (source == InputSource::osc)
            return inputAddress;
        return "MIDI " + midiIn.describe() + (midiDevice.isNotEmpty() ? " (" + midiDevice + ")" : juce::String());
    }

    virtual juce::String getDisplayName() const { return getInputDescription(); }

protected:
    juce::DynamicObject* inputFieldsToObject() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", id);
        obj->setProperty ("source", source == InputSource::midi ? "midi" : "osc");
        obj->setProperty ("inputAddress", inputAddress);
        obj->setProperty ("midiDevice", midiDevice);
        obj->setProperty ("midiIn", midiIn.toVar());
        obj->setProperty ("midiFadeSeconds", midiFadeSeconds);
        obj->setProperty ("inMin", (double) inMin);
        obj->setProperty ("inMax", (double) inMax);
        return obj;
    }

    void readInputFields (const juce::var& v)
    {
        id              = v.getProperty ("id", juce::Uuid().toString()).toString();
        source          = v.getProperty ("source", "osc").toString() == "midi" ? InputSource::midi : InputSource::osc;
        inputAddress    = v.getProperty ("inputAddress", "/bridge/fader1").toString();
        midiDevice      = v.getProperty ("midiDevice", juce::String()).toString();
        midiIn          = MidiSpec::fromVar (v.getProperty ("midiIn", juce::var()));
        midiFadeSeconds = juce::jmax (0.0, (double) v.getProperty ("midiFadeSeconds", 0.0));
        inMin           = (float) (double) v.getProperty ("inMin", 0.0);
        inMax           = (float) (double) v.getProperty ("inMax", 1.0);
    }
};

// An input with no outputs of its own, used to scale mappings - like a
// VCA. A mapping scaled by it has its normalised value (0-1 within its
// input range) multiplied by the scaler's before being mapped to each
// output range.
struct Scaler : InputChannel
{
    juce::String name { "Scaler" };   // shown in lists and the "Scaled By" dropdown

    juce::String getDisplayName() const override { return name.isNotEmpty() ? name : getInputDescription(); }

    juce::var toVar() const
    {
        auto* obj = inputFieldsToObject();
        obj->setProperty ("name", name);
        return juce::var (obj);
    }

    static Scaler fromVar (const juce::var& v)
    {
        Scaler s;
        s.readInputFields (v);
        s.name = v.getProperty ("name", juce::String()).toString();
        return s;
    }
};

// An input that forwards its ramped value, optionally multiplied by a
// Scaler, to every output it is mapped to. The scaler's normalised value
// is first mapped onto scaleMin-scaleMax, so e.g. 0.9-1.0 lets the scaler
// trim this mapping only slightly.
struct Mapping : InputChannel
{
    juce::String scaledById;   // Scaler::id, or empty
    float scaleMin = 0.0f;
    float scaleMax = 1.0f;
    std::vector<OutputTarget> outputs;

    juce::var toVar() const
    {
        auto* obj = inputFieldsToObject();
        obj->setProperty ("scaledById", scaledById);
        obj->setProperty ("scaleMin", (double) scaleMin);
        obj->setProperty ("scaleMax", (double) scaleMax);

        juce::Array<juce::var> outs;
        for (auto& o : outputs)
            outs.add (o.toVar());
        obj->setProperty ("outputs", outs);

        return juce::var (obj);
    }

    static Mapping fromVar (const juce::var& v)
    {
        Mapping m;
        m.readInputFields (v);
        m.scaledById = v.getProperty ("scaledById", juce::String()).toString();
        m.scaleMin   = (float) (double) v.getProperty ("scaleMin", 0.0);
        m.scaleMax   = (float) (double) v.getProperty ("scaleMax", 1.0);

        if (auto* arr = v.getProperty ("outputs", juce::var()).getArray())
            for (auto& o : *arr)
                m.outputs.push_back (OutputTarget::fromVar (o));

        return m;
    }
};

struct GlobalSettings
{
    int receivePort = 9000;
    double updateFrequencyHz = 30.0;
    std::vector<SendTarget> targets;

    static GlobalSettings makeDefault()
    {
        GlobalSettings s;
        s.targets.push_back (SendTarget {});
        return s;
    }

    const SendTarget* findTarget (const juce::String& targetId) const
    {
        for (auto& t : targets)
            if (t.id == targetId)
                return &t;
        return nullptr;
    }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("receivePort", receivePort);
        obj->setProperty ("updateFrequencyHz", updateFrequencyHz);

        juce::Array<juce::var> arr;
        for (auto& t : targets)
            arr.add (t.toVar());
        obj->setProperty ("targets", arr);

        return juce::var (obj);
    }

    // Projects saved before send targets existed have a single global
    // sendHost/sendPort instead - that becomes the one and only target.
    static GlobalSettings fromVar (const juce::var& v)
    {
        GlobalSettings s;
        s.receivePort       = (int) v.getProperty ("receivePort", 9000);
        s.updateFrequencyHz = (double) v.getProperty ("updateFrequencyHz", 30.0);

        if (auto* arr = v.getProperty ("targets", juce::var()).getArray())
        {
            for (auto& t : *arr)
                s.targets.push_back (SendTarget::fromVar (t));
        }
        else
        {
            SendTarget legacy;
            legacy.host = v.getProperty ("sendHost", "127.0.0.1").toString();
            legacy.port = (int) v.getProperty ("sendPort", 9001);
            s.targets.push_back (legacy);
        }

        return s;
    }
};
