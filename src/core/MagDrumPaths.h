#pragma once

#include <juce_core/juce_core.h>

// Returns the per-user data folder on desktop, or the shared app-group folder
// used by the iOS app and AUv3 extension. It may be empty if the iOS entitlement
// is not present in the signed build.
juce::File getMagDrumDataDirectory();
