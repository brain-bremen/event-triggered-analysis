// Minimal stub for juce::Colour constructor
// The Colour class is declared in juce_graphics headers but we're not compiling that module
// We just need this one constructor to satisfy the static Colours:: initializers

#include <JuceHeader.h>

namespace juce
{
    // Provide the minimal Colour constructor implementation
    // The Colour class layout: first member is a PixelARGB which contains a uint32
    Colour::Colour(uint32 argbValue) noexcept
    {
        // Use placement new to initialize the PixelARGB member with the argb value
        // This is a simple POD type so we can just memcpy
        std::memcpy(this, &argbValue, sizeof(uint32));
    }
}


