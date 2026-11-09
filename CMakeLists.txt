cmake_minimum_required(VERSION 3.22)
project(MagDrum VERSION 0.1.0 LANGUAGES C CXX)
option(MAGDRUM_BUILD_MOBILE_PREVIEW "Build the iOS library UI as a desktop preview for shared-code validation" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)
FetchContent_Declare(sqlite URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30)
FetchContent_GetProperties(sqlite)
if(NOT sqlite_POPULATED)
 FetchContent_Populate(sqlite)
endif()
add_library(sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 juce_add_gui_app(MagDrum PRODUCT_NAME "MagDrum" COMPANY_NAME "Censor Design" BUNDLE_ID "com.censordesign.magdrum")
 juce_generate_juce_header(MagDrum)
 target_sources(MagDrum PRIVATE src/app/Main.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/PluginNoteScanner.cpp src/core/PluginScanProcess.cpp src/core/PluginPrograms.cpp src/core/PluginPresetFiles.cpp src/core/PatternAudioRenderer.cpp src/core/LoopingAudioSource.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp src/core/StructuredLogger.cpp src/core/MagDrumPaths.cpp)
 target_include_directories(MagDrum PRIVATE src)
 target_compile_definitions(MagDrum PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_APPLICATION_NAME_STRING="MagDrum" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
 target_link_libraries(MagDrum PRIVATE sqlite3 juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
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
juce_generate_juce_header(MagDrumPlugin)
target_sources(MagDrumPlugin PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp src/core/MagDrumPaths.cpp src/core/ScopedFileAccess.cpp)
target_include_directories(MagDrumPlugin PRIVATE src)
target_compile_definitions(MagDrumPlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MagDrumPlugin PRIVATE sqlite3 juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
if(BUILD_TESTING AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 add_executable(MagDrumTests tests/SmokeTests.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp src/core/PluginNoteScanner.cpp src/core/PluginScanProcess.cpp src/core/PluginPrograms.cpp src/core/PluginPresetFiles.cpp src/core/PatternAudioRenderer.cpp src/core/LoopingAudioSource.cpp src/core/PluginVelocityLayerAnalyzer.cpp src/core/PluginRoundRobinAnalyzer.cpp src/core/StructuredLogger.cpp)
 target_include_directories(MagDrumTests PRIVATE src)
 target_compile_definitions(MagDrumTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
 target_link_libraries(MagDrumTests PRIVATE sqlite3 juce::juce_audio_basics juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_core)
 add_test(NAME MagDrumTests COMMAND MagDrumTests)

 juce_add_console_app(MagDrumPluginTests PRODUCT_NAME "MagDrum Plugin Tests")
 juce_generate_juce_header(MagDrumPluginTests)
 target_sources(MagDrumPluginTests PRIVATE tests/PluginSmokeTests.cpp src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp src/core/MagDrumPaths.cpp src/core/ScopedFileAccess.cpp)
 target_include_directories(MagDrumPluginTests PRIVATE src)
 target_compile_definitions(MagDrumPluginTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_VST3_CAN_REPLACE_VST2=0)
 target_link_libraries(MagDrumPluginTests PRIVATE sqlite3 juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
 add_test(NAME MagDrumPluginTests COMMAND MagDrumPluginTests)
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
     src/core/SampleDatabase.cpp
     src/core/WavScanner.cpp
     src/core/AudioClassifier.cpp
     src/core/MidiMapping.cpp
     src/core/MidiPatternIO.cpp
     src/core/ScopedFileAccess.cpp
     src/core/MagDrumPaths.cpp)
 target_include_directories(${MAGDRUM_MOBILE_TARGET} PRIVATE src)
 target_compile_definitions(${MAGDRUM_MOBILE_TARGET} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
 if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_compile_definitions(${MAGDRUM_MOBILE_TARGET} PRIVATE MAGDRUM_APP_GROUP_ID=\"${MAGDRUM_APP_GROUP_ID}\")
 endif()
 target_link_libraries(${MAGDRUM_MOBILE_TARGET} PRIVATE
     sqlite3 juce::juce_audio_utils juce::juce_audio_formats
     juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography
     juce::juce_data_structures juce::juce_gui_extra
     juce::juce_recommended_config_flags
     juce::juce_recommended_lto_flags
     juce::juce_recommended_warning_flags)
endif()
