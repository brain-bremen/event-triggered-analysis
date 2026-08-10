#pragma once
#include <JuceHeader.h>
#include <cstdint>
namespace TriggeredAverage
{
enum class TriggerType : std::int_fast8_t
{
    TTL_TRIGGER = 1,
    MSG_TRIGGER = 2,
    TTL_AND_MSG_TRIGGER = 3
};

constexpr auto TriggerTypeToString (TriggerType type)
{
    switch (type)
    {
        case TriggerType::TTL_TRIGGER:
            return "TTL Trigger";
        case TriggerType::MSG_TRIGGER:
            return "Message Trigger";
        case TriggerType::TTL_AND_MSG_TRIGGER:
            return "TTL and Message Trigger";
        default:
            return "Unknown Trigger Type";
    }
}
class TriggeredAvgNode;
class TriggerSource
{
public:
    TriggerSource (const juce::String& name_,
                   int line_,
                   TriggerType type_);

    static juce::Colour getColourForLine (int line);

    juce::String name;
    int line;
    TriggerType type;
    bool canTrigger;
    juce::Colour colour;

    // Message patterns (contains-match, case-insensitive, empty = disabled)
    juce::String armPattern;      // arm the trigger (TTL_AND_MSG) or fire directly (MSG_TRIGGER)
    juce::String cancelPattern;   // discard pending capture + disarm (TTL_AND_MSG)
    juce::String commitPattern;   // commit pending capture to average

    // How long to keep an uncommitted pending capture before auto-discarding (ms, 0 = disabled)
    int pendingTimeoutMs = 2000;
};

// Container class for managing multiple TriggerSource objects
class TriggerSources
{
public:
    TriggerSources() = delete;
    TriggerSources (const TriggerSources&) = delete;
    TriggerSources (TriggerSources&& other) noexcept
    {
        m_parentProcessor = std::move (other.m_parentProcessor);
        m_triggerSources = std::move (other.m_triggerSources);
        m_nextConditionIndex = other.m_nextConditionIndex;
        m_currentTriggerSource = other.m_currentTriggerSource;
    }
    TriggerSources& operator= (const TriggerSources&) = delete;
    TriggerSources& operator= (TriggerSources&& other) noexcept
    {
        if (this != &other)
        {
            m_parentProcessor = std::move (other.m_parentProcessor);
            m_triggerSources = std::move (other.m_triggerSources);
            m_nextConditionIndex = other.m_nextConditionIndex;
            m_currentTriggerSource = other.m_currentTriggerSource;
        }
        return *this;
    }
    TriggerSources (TriggeredAvgNode* parentProcessor) : m_parentProcessor (parentProcessor) {}

    juce::Array<TriggerSource*> getAll();
    TriggerSource* getByIndex (int index) const;
    int getIndexOf (const TriggerSource* source) const { return m_triggerSources.indexOf (source); }
    TriggerSource* addTriggerSource (int line, TriggerType type, int index = -1);
    TriggerSource* getLastAddedTriggerSource() const { return m_currentTriggerSource; }
    void removeTriggerSources (Array<TriggerSource*> sources);
    void removeTriggerSource (int indexToRemove);

    void setTriggerSourceName (TriggerSource* source, String name, bool updateEditor = true);
    void setTriggerSourceLine (TriggerSource* source, int line, bool updateEditor = true);
    void setTriggerSourceColour (TriggerSource* source, Colour colour, bool updateEditor = true);
    void setTriggerSourceTriggerType (TriggerSource* source,
                                      TriggerType type,
                                      bool updateEditor = true);
    void setTriggerSourceArmPattern (TriggerSource* source, const String& pattern);
    void setTriggerSourceCancelPattern (TriggerSource* source, const String& pattern);
    void setTriggerSourceCommitPattern (TriggerSource* source, const String& pattern);
    String ensureUniqueTriggerSourceName (String name);
    int getNextConditionIndex() const { return m_nextConditionIndex; }
    void clear() { m_triggerSources.clear(); }
    size_t size() const { return m_triggerSources.size(); }

private:
    // dependencies
    TriggeredAvgNode* m_parentProcessor = nullptr;

    // data
    OwnedArray<TriggerSource> m_triggerSources;
    int m_nextConditionIndex = 1;
    TriggerSource* m_currentTriggerSource = nullptr;
};

} // namespace TriggeredAverage
