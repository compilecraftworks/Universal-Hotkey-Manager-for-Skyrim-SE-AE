cmake_minimum_required(VERSION 3.25)

project(UniversalHotkeyManager VERSION 1.0.6 LANGUAGES CXX)

find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(Zydis CONFIG REQUIRED)

option(UHI_BUILD_TESTS "Build UHI unit tests" ON)
option(UHI_BUILD_SKSE "Build the CommonLibSSE-NG SKSE plugin" OFF)
option(UHI_ENABLE_MENU_FRAMEWORK "Enable the SKSE Menu Framework adapter" OFF)
set(UHI_MENU_FRAMEWORK_ROOT "" CACHE PATH "Deprecated; retained for existing build presets")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# UHM's UI, persisted reports and ImGui boundary are UTF-8 regardless of the
# active Windows ANSI code page or the optional system-wide UTF-8 locale flag.
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(uhi_core STATIC
    src/BindingSerializer.cpp
    src/ConfigBindingParser.cpp
    src/TextDecoder.cpp
    src/InputCodeFormatter.cpp
    src/OpeningHotkey.cpp
    src/HotkeyRecord.cpp
    src/ActivationContextInference.cpp
    src/HotkeyViewModel.cpp
    src/HotkeyViewRenderer.cpp
    src/HotkeyCategory.cpp
    src/LastScanStore.cpp
    src/SexLabDependency.cpp
    src/StorageUtilOwnerResolver.cpp
    src/GameFilePolicy.cpp
    src/ScanCache.cpp
    src/ScanPipeline.cpp
    src/JsonReporter.cpp
    src/Registry.cpp
    src/scanners/GenericConfigScanner.cpp
    src/scanners/BsaScanner.cpp
    src/scanners/PexScanner.cpp
    src/scanners/PexBytecodeScanner.cpp
    src/scanners/UserDataScanner.cpp
    src/scanners/DllScanner.cpp
    src/scanners/PeInputAnalyzer.cpp
    src/scanners/ControlMapScanner.cpp
    src/writers/ControlMapWriter.cpp
    src/writers/ConfigFileWriter.cpp
    src/scanners/ReShadeScanner.cpp
    src/scanners/CommunityShadersScanner.cpp
    src/scanners/EnbScanner.cpp
)
target_include_directories(uhi_core PUBLIC include)
target_link_libraries(uhi_core PRIVATE ZLIB::ZLIB lz4::lz4)
target_link_libraries(uhi_core PRIVATE Zydis::Zydis)
if(WIN32)
    target_link_libraries(uhi_core PRIVATE Shell32 Ole32)
endif()
target_compile_features(uhi_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(uhi_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(uhi_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UHI_BUILD_SKSE)
    if(WIN32)
        enable_language(RC)
    endif()
    find_package(CommonLibSSE CONFIG REQUIRED)
    add_commonlibsse_plugin(UniversalHotkeyManager
        NAME "Universal Hotkey Manager for Skyrim SE-AE"
        AUTHOR "UHI contributors"
        VERSION ${PROJECT_VERSION}
        MINIMUM_SKSE_VERSION 0.0.0.0
        SOURCES src/plugin.cpp src/MenuFrameworkAdapter.cpp resources/UniversalHotkeyManager.rc
    )
    target_link_libraries(UniversalHotkeyManager PRIVATE uhi_core)
    if(WIN32)
        target_link_libraries(UniversalHotkeyManager PRIVATE d3d11 windowscodecs Ole32)
    endif()
    target_include_directories(UniversalHotkeyManager PRIVATE include)
    target_compile_definitions(UniversalHotkeyManager PRIVATE UHI_VERSION="${PROJECT_VERSION}")
    target_precompile_headers(UniversalHotkeyManager PRIVATE <UHI/PCH.h>)

    if(UHI_ENABLE_MENU_FRAMEWORK)
        target_compile_definitions(UniversalHotkeyManager PRIVATE UHI_ENABLE_MENU_FRAMEWORK=1)
        # Menu Framework 2.x and 3.x embed Dear ImGui and export its C API.
        # The vendored official single-header client resolves those exports at
        # runtime, so UHI must not link the legacy 1.x imgui.dll SDK.
    endif()
endif()

if(UHI_BUILD_TESTS)
    enable_testing()
    add_executable(uhi_tests tests/ReShadeScannerTests.cpp)
    target_link_libraries(uhi_tests PRIVATE uhi_core)
    add_test(NAME uhi_tests COMMAND uhi_tests)
    add_executable(uhi_generic_tests tests/GenericConfigScannerTests.cpp)
    target_link_libraries(uhi_generic_tests PRIVATE uhi_core)
    add_test(NAME uhi_generic_tests COMMAND uhi_generic_tests)
    add_executable(uhi_pipeline_tests tests/ScanPipelineTests.cpp)
    target_link_libraries(uhi_pipeline_tests PRIVATE uhi_core)
    add_test(NAME uhi_pipeline_tests COMMAND uhi_pipeline_tests)
    add_executable(uhi_static_tests tests/StaticScannerTests.cpp)
    target_link_libraries(uhi_static_tests PRIVATE uhi_core)
    add_test(NAME uhi_static_tests COMMAND uhi_static_tests)
    add_executable(uhi_bsa_tests tests/BsaScannerTests.cpp)
    target_link_libraries(uhi_bsa_tests PRIVATE uhi_core ZLIB::ZLIB lz4::lz4)
    add_test(NAME uhi_bsa_tests COMMAND uhi_bsa_tests)
    add_executable(uhi_pex_bytecode_tests tests/PexBytecodeScannerTests.cpp)
    target_link_libraries(uhi_pex_bytecode_tests PRIVATE uhi_core)
    add_test(NAME uhi_pex_bytecode_tests COMMAND uhi_pex_bytecode_tests)
    add_executable(uhi_user_data_tests tests/UserDataScannerTests.cpp)
    target_link_libraries(uhi_user_data_tests PRIVATE uhi_core)
    add_test(NAME uhi_user_data_tests COMMAND uhi_user_data_tests)
    add_executable(uhi_pe_input_tests tests/PeInputAnalyzerTests.cpp)
    target_link_libraries(uhi_pe_input_tests PRIVATE uhi_core)
    add_test(NAME uhi_pe_input_tests COMMAND uhi_pe_input_tests)
    add_executable(uhi_view_tests tests/HotkeyViewModelTests.cpp)
    target_link_libraries(uhi_view_tests PRIVATE uhi_core)
    add_test(NAME uhi_view_tests COMMAND uhi_view_tests)
    add_executable(uhi_renderer_tests tests/HotkeyViewRendererTests.cpp)
    target_link_libraries(uhi_renderer_tests PRIVATE uhi_core)
    add_test(NAME uhi_renderer_tests COMMAND uhi_renderer_tests)
    add_executable(uhi_controlmap_tests tests/ControlMapScannerTests.cpp)
    target_link_libraries(uhi_controlmap_tests PRIVATE uhi_core)
    add_test(NAME uhi_controlmap_tests COMMAND uhi_controlmap_tests)
    add_executable(uhi_config_binding_tests tests/ConfigBindingParserTests.cpp)
    target_link_libraries(uhi_config_binding_tests PRIVATE uhi_core)
    add_test(NAME uhi_config_binding_tests COMMAND uhi_config_binding_tests)
    add_executable(uhi_binding_serializer_tests tests/BindingSerializerTests.cpp)
    target_link_libraries(uhi_binding_serializer_tests PRIVATE uhi_core)
    add_test(NAME uhi_binding_serializer_tests COMMAND uhi_binding_serializer_tests)
    add_executable(uhi_controlmap_writer_tests tests/ControlMapWriterTests.cpp)
    target_link_libraries(uhi_controlmap_writer_tests PRIVATE uhi_core)
    add_test(NAME uhi_controlmap_writer_tests COMMAND uhi_controlmap_writer_tests)
    add_executable(uhi_config_writer_tests tests/ConfigFileWriterTests.cpp)
    target_link_libraries(uhi_config_writer_tests PRIVATE uhi_core)
    add_test(NAME uhi_config_writer_tests COMMAND uhi_config_writer_tests)
    add_executable(uhi_registry_tests tests/RegistryTests.cpp)
    target_link_libraries(uhi_registry_tests PRIVATE uhi_core)
    add_test(NAME uhi_registry_tests COMMAND uhi_registry_tests)
    add_executable(uhi_community_shaders_tests tests/CommunityShadersScannerTests.cpp)
    target_link_libraries(uhi_community_shaders_tests PRIVATE uhi_core)
    add_test(NAME uhi_community_shaders_tests COMMAND uhi_community_shaders_tests)
    add_executable(uhi_enb_tests tests/EnbScannerTests.cpp)
    target_link_libraries(uhi_enb_tests PRIVATE uhi_core)
    add_test(NAME uhi_enb_tests COMMAND uhi_enb_tests)
    add_executable(uhi_cache_tests tests/ScanCacheTests.cpp)
    target_link_libraries(uhi_cache_tests PRIVATE uhi_core)
    add_test(NAME uhi_cache_tests COMMAND uhi_cache_tests)
    add_executable(uhi_opening_hotkey_tests tests/OpeningHotkeyTests.cpp)
    target_link_libraries(uhi_opening_hotkey_tests PRIVATE uhi_core)
    add_test(NAME uhi_opening_hotkey_tests COMMAND uhi_opening_hotkey_tests)
    add_executable(uhi_state_dependency_tests tests/StateAndDependencyTests.cpp)
    target_link_libraries(uhi_state_dependency_tests PRIVATE uhi_core)
    add_test(NAME uhi_state_dependency_tests COMMAND uhi_state_dependency_tests)
endif()
