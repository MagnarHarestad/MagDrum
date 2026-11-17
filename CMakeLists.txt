cmake_minimum_required(VERSION 3.22)
set(MAGDRUM_BUILD_REVISION "0" CACHE STRING "Numeric test-release revision appended to MagDrum's product version")
if(NOT MAGDRUM_BUILD_REVISION MATCHES "^[0-9]+$")
 message(FATAL_ERROR "MAGDRUM_BUILD_REVISION must be a non-negative integer")
endif()
project(MagDrum VERSION "0.1.2.${MAGDRUM_BUILD_REVISION}" LANGUAGES C CXX)
if(POLICY CMP0135)
 cmake_policy(SET CMP0135 NEW)
endif()
option(MAGDRUM_BUILD_MOBILE_PREVIEW "Build the iOS library and AUv3 editor UI as desktop previews for shared-code validation" OFF)
option(MAGDRUM_BUILD_CLASSIFIER_EVALUATION "Build the local labeled-folder classifier evaluation tool" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
include(CTest)
include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
FetchContent_Declare(sqlite URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30)
FetchContent_MakeAvailable(sqlite)
juce_add_binary_data(MagDrumUiAssets NAMESPACE MagDrumUiAssets SOURCES assets/ui/dark-charcoal-plaster.png)
add_library(sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_ENABLE_FTS5=1)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 juce_add_gui_app(MagDrum PRODUCT_NAME "MagDrum" COMPANY_NAME "Censor Design" BUNDLE_ID "com.censordesign.magdrum")
 juce_generate_juce_header(MagDrum)
 target_sources(MagDrum PRIVATE src/app/Main.cpp src/ui/MagDrumLocalization.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/DefaultLibraryDiscovery.cpp src/core/PluginNoteScanner.cpp src/core/DeviceRealtimeRenderer.cpp src/core/PluginScanProcess.cpp src/core/PluginPrograms.cpp src/core/PluginPresetFiles.cpp src/core/PatternAudioRenderer.cpp src/core/LoopingAudioSource.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp src/core/StructuredLogger.cpp src/core/MagDrumPaths.cpp)
 target_include_directories(MagDrum PRIVATE src)
 target_compile_definitions(MagDrum PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_APPLICATION_NAME_STRING="MagDrum" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
 target_link_libraries(MagDrum PRIVATE MagDrumUiAssets sqlite3 juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

set(MAGDRUM_APP_GROUP_ID "group.com.censordesign.magdrum" CACHE STRING "Apple app-group identifier shared by the iOS app and AUv3 extension")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
 set(MAGDRUM_PLUGIN_FORMATS AUv3 Standalone)
 set(MAGDRUM_APP_GROUP_ARGS APP_GROUPS_ENABLED TRUE APP_GROUP_IDS "${MAGDRUM_APP_GROUP_ID}")
elseif(APPLE)
 set(MAGDRUM_PLUGIN_FORMATS VST3 AU)
else()
 set(MAGDRUM_PLUGIN_FORMATS VST3)
endif()
juce_add_plugin(MagDrumPlugin
    COMPANY_NAME "Censor Design"
    BUNDLE_ID "com.censordesign.magdrum.plugin"
    PLUGIN_MANUFACTURER_CODE CnDe
    PLUGIN_CODE MgDr
    FORMATS ${MAGDRUM_PLUGIN_FORMATS}
    ${MAGDRUM_APP_GROUP_ARGS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    PRODUCT_NAME "MagDrum")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND TARGET MagDrumPlugin_VST3)
 # MIDI arrives on the VST3 event bus; do not expose JUCE's 2,080 legacy CC emulation parameters to the DAW.
 target_compile_definitions(MagDrumPlugin PRIVATE JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
 target_compile_definitions(MagDrumPlugin_VST3 PRIVATE JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
endif()
juce_generate_juce_header(MagDrumPlugin)
target_sources(MagDrumPlugin PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/ui/MagDrumLocalization.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/DefaultLibraryDiscovery.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MagDrumPaths.cpp src/core/ScopedFileAccess.cpp src/core/PluginScanProcess.cpp src/core/PluginNoteScanner.cpp src/core/DeviceRealtimeRenderer.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp)
target_include_directories(MagDrumPlugin PRIVATE src)
target_compile_definitions(MagDrumPlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR MAGDRUM_BUILD_MOBILE_PREVIEW)
 target_compile_definitions(MagDrumPlugin PRIVATE MAGDRUM_MOBILE_PLUGIN_UI=1)
endif()
target_link_libraries(MagDrumPlugin PRIVATE MagDrumUiAssets sqlite3 juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags ${CMAKE_DL_LIBS})
if(BUILD_TESTING AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 add_executable(MagDrumTests tests/SmokeTests.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/DefaultLibraryDiscovery.cpp src/core/PluginNoteScanner.cpp src/core/DeviceRealtimeRenderer.cpp src/core/PluginScanProcess.cpp src/core/PluginPrograms.cpp src/core/PluginPresetFiles.cpp src/core/PatternAudioRenderer.cpp src/core/LoopingAudioSource.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp src/core/StructuredLogger.cpp)
 target_include_directories(MagDrumTests PRIVATE src)
 target_compile_definitions(MagDrumTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1 MAGDRUM_TESTING=1)
 target_link_libraries(MagDrumTests PRIVATE sqlite3 juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_core)

 juce_add_console_app(MagDrumPluginTests PRODUCT_NAME "MagDrum Plugin Tests")
 juce_generate_juce_header(MagDrumPluginTests)
 target_sources(MagDrumPluginTests PRIVATE tests/PluginSmokeTests.cpp src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/ui/MagDrumLocalization.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/DefaultLibraryDiscovery.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MagDrumPaths.cpp src/core/ScopedFileAccess.cpp src/core/PluginScanProcess.cpp src/core/PluginNoteScanner.cpp src/core/DeviceRealtimeRenderer.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp)
 target_include_directories(MagDrumPluginTests PRIVATE src)
 target_compile_definitions(MagDrumPluginTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1)
 if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR MAGDRUM_BUILD_MOBILE_PREVIEW)
  target_compile_definitions(MagDrumPluginTests PRIVATE MAGDRUM_MOBILE_PLUGIN_UI=1)
 endif()
 target_link_libraries(MagDrumPluginTests PRIVATE MagDrumUiAssets sqlite3 juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags juce::juce_recommended_warning_flags ${CMAKE_DL_LIBS})
 add_test(NAME MagDrumPluginTests COMMAND MagDrumPluginTests)

 add_dependencies(MagDrumTests MagDrum MagDrumPlugin_VST3 MagDrumPluginTests)
 add_dependencies(MagDrumPluginTests MagDrumPlugin_VST3)
 get_target_property(MAGDRUM_VST3_TEST_BUNDLE MagDrumPlugin_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
 add_test(NAME MagDrumTests COMMAND MagDrumTests "${MAGDRUM_VST3_TEST_BUNDLE}" "$<TARGET_FILE:MagDrumPluginTests>" "$<TARGET_FILE:MagDrum>")
endif()

if(MAGDRUM_BUILD_CLASSIFIER_EVALUATION AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 juce_add_console_app(MagDrumClassifierEvaluation PRODUCT_NAME "MagDrum Classifier Evaluation")
 juce_generate_juce_header(MagDrumClassifierEvaluation)
 target_sources(MagDrumClassifierEvaluation PRIVATE tools/ClassifierEvaluation.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp)
 target_include_directories(MagDrumClassifierEvaluation PRIVATE src)
 target_compile_definitions(MagDrumClassifierEvaluation PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
 target_link_libraries(MagDrumClassifierEvaluation PRIVATE sqlite3 juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR MAGDRUM_BUILD_MOBILE_PREVIEW)
 if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(MAGDRUM_MOBILE_TARGET MagDrumMobile)
  juce_add_gui_app(${MAGDRUM_MOBILE_TARGET}
     PRODUCT_NAME "MagDrum"
     COMPANY_NAME "Censor Design"
     BUNDLE_ID "com.censordesign.magdrum.mobile"
     APP_GROUPS_ENABLED TRUE
     APP_GROUP_IDS "${MAGDRUM_APP_GROUP_ID}"
     FILE_SHARING_ENABLED TRUE
     DOCUMENT_BROWSER_ENABLED TRUE
     TARGETED_DEVICE_FAMILY "1,2"
     IPHONE_SCREEN_ORIENTATIONS portrait
     IPAD_SCREEN_ORIENTATIONS portrait landscape)
  target_compile_definitions(MagDrumPlugin PRIVATE MAGDRUM_APP_GROUP_ID=\"${MAGDRUM_APP_GROUP_ID}\")
 else()
  set(MAGDRUM_MOBILE_TARGET MagDrumMobilePreview)
  juce_add_gui_app(${MAGDRUM_MOBILE_TARGET}
      PRODUCT_NAME "MagDrum Mobile Preview"
      COMPANY_NAME "Censor Design"
      BUNDLE_ID "com.censordesign.magdrum.mobile.preview")
 endif()
 juce_generate_juce_header(${MAGDRUM_MOBILE_TARGET})
 target_sources(${MAGDRUM_MOBILE_TARGET} PRIVATE
     src/mobile/MobileMain.cpp
     src/ui/MagDrumLocalization.cpp
     src/core/SampleDatabase.cpp
     src/core/WavScanner.cpp
     src/core/AudioClassifier.cpp
     src/core/MidiMapping.cpp
     src/core/MidiPatternIO.cpp
     src/core/DefaultLibraryDiscovery.cpp
     src/core/ScopedFileAccess.cpp
     src/core/MagDrumPaths.cpp)
 target_include_directories(${MAGDRUM_MOBILE_TARGET} PRIVATE src)
 target_compile_definitions(${MAGDRUM_MOBILE_TARGET} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="MagDrum" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
 if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_compile_definitions(${MAGDRUM_MOBILE_TARGET} PRIVATE MAGDRUM_APP_GROUP_ID=\"${MAGDRUM_APP_GROUP_ID}\")
 endif()
 target_link_libraries(${MAGDRUM_MOBILE_TARGET} PRIVATE
     MagDrumUiAssets sqlite3 juce::juce_audio_utils juce::juce_audio_formats
     juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography
     juce::juce_data_structures juce::juce_gui_extra
     juce::juce_recommended_config_flags
     juce::juce_recommended_lto_flags
     juce::juce_recommended_warning_flags)
endif()
