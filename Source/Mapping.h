#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// One output that an incoming fade is forwarded to, with its own scale range.
struct OutputTarget
{
    juce::String address { "/output/1" };
    float outMin = 0.0f;
    float outMax = 1.0f;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("address", address);
        obj->setProperty ("outMin", (double) outMin);
        obj->setProperty ("outMax", (double) outMax);
        return juce::var (obj);
    }

    static OutputTarget fromVar (const juce::var& v)
    {
        OutputTarget t;
        t.address = v.getProperty ("address", "/output/1").toString();
        t.outMin  = (float) (double) v.getProperty ("outMin", 0.0);
        t.outMax  = (float) (double) v.getProperty ("outMax", 1.0);
        return t;
    }
};

// One input address that, on receiving [x, y], fades from the last
// received x to the new x over y seconds, and forwards the ramped,
// scaled value to every output it is mapped to. The input address
// itself serves as the mapping's display name - there's no separate
// name field to keep in sync.
struct Mapping
{
    juce::String id { juce::Uuid().toString() };
    juce::String inputAddress { "/bridge/fader1" };
    float inMin = 0.0f;
    float inMax = 1.0f;
    std::vector<OutputTarget> outputs;

    // Runtime-only state, not persisted.
    bool hasLastValue = false;   // last x received - the anchor a new fade starts from
    float lastValue = 0.0f;
    bool hasLiveValue = false;   // the value currently being sent (moves during a fade)
    float liveValue = 0.0f;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", id);
        obj->setProperty ("inputAddress", inputAddress);
        obj->setProperty ("inMin", (double) inMin);
        obj->setProperty ("inMax", (double) inMax);

        juce::Array<juce::var> outs;
        for (auto& o : outputs)
            outs.add (o.toVar());
        obj->setProperty ("outputs", outs);

        return juce::var (obj);
    }

    static Mapping fromVar (const juce::var& v)
    {
        Mapping m;
        m.id           = v.getProperty ("id", juce::Uuid().toString()).toString();
        m.inputAddress = v.getProperty ("inputAddress", "/bridge/fader1").toString();
        m.inMin        = (float) (double) v.getProperty ("inMin", 0.0);
        m.inMax        = (float) (double) v.getProperty ("inMax", 1.0);
        m.outputs.clear();

        if (auto* arr = v.getProperty ("outputs", juce::var()).getArray())
            for (auto& o : *arr)
                m.outputs.push_back (OutputTarget::fromVar (o));

        return m;
    }
};

struct GlobalSettings
{
    int receivePort = 9000;
    juce::String sendHost { "127.0.0.1" };
    int sendPort = 9001;
    double updateFrequencyHz = 30.0;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("receivePort", receivePort);
        obj->setProperty ("sendHost", sendHost);
        obj->setProperty ("sendPort", sendPort);
        obj->setProperty ("updateFrequencyHz", updateFrequencyHz);
        return juce::var (obj);
    }

    static GlobalSettings fromVar (const juce::var& v)
    {
        GlobalSettings s;
        s.receivePort       = (int) v.getProperty ("receivePort", 9000);
        s.sendHost           = v.getProperty ("sendHost", "127.0.0.1").toString();
        s.sendPort           = (int) v.getProperty ("sendPort", 9001);
        s.updateFrequencyHz  = (double) v.getProperty ("updateFrequencyHz", 30.0);
        return s;
    }
};
