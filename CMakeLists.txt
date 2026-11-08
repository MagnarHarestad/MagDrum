cmake_minimum_required(VERSION 3.22)
project(MagDrum VERSION 0.1.0 LANGUAGES C CXX)
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
target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
juce_add_gui_app(MagDrum PRODUCT_NAME "MagDrum" COMPANY_NAME "Censor Design" BUNDLE_ID "com.censordesign.magdrum")
juce_generate_juce_header(MagDrum)
target_sources(MagDrum PRIVATE src/app/Main.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp)
target_include_directories(MagDrum PRIVATE src)
target_compile_definitions(MagDrum PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="MagDrum" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(MagDrum PRIVATE sqlite3 juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

juce_add_plugin(MagDrumPlugin
    COMPANY_NAME "Censor Design"
    BUNDLE_ID "com.censordesign.magdrum.plugin"
    PLUGIN_MANUFACTURER_CODE CnDe
    PLUGIN_CODE MgDr
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    PRODUCT_NAME "MagDrum")
juce_generate_juce_header(MagDrumPlugin)
target_sources(MagDrumPlugin PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/core/SampleDatabase.cpp src/core/MidiMapping.cpp)
target_include_directories(MagDrumPlugin PRIVATE src)
target_compile_definitions(MagDrumPlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MagDrumPlugin PRIVATE sqlite3 juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
if(BUILD_TESTING)
 add_executable(MagDrumTests tests/SmokeTests.cpp src/core/SampleDatabase.cpp src/core/WavScanner.cpp src/core/AudioClassifier.cpp src/core/MidiMapping.cpp src/core/MidiPatternIO.cpp)
 target_include_directories(MagDrumTests PRIVATE src)
 target_compile_definitions(MagDrumTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
 target_link_libraries(MagDrumTests PRIVATE sqlite3 juce::juce_audio_basics juce::juce_audio_formats juce::juce_dsp juce::juce_cryptography juce::juce_core)
 add_test(NAME MagDrumTests COMMAND MagDrumTests)
endif()
