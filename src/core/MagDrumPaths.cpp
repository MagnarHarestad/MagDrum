#include "MagDrumPaths.h"

juce::File getMagDrumDataDirectory()
{
#if JUCE_IOS
   #ifndef MAGDRUM_APP_GROUP_ID
    #define MAGDRUM_APP_GROUP_ID "group.com.censordesign.magdrum"
   #endif
    const auto shared = juce::File::getContainerForSecurityApplicationGroupIdentifier(MAGDRUM_APP_GROUP_ID);
    if (! shared.isDirectory())
        return {};
    return shared.getChildFile("Library")
                 .getChildFile("Application Support")
                 .getChildFile("MagDrum");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MagDrum");
#endif
}
