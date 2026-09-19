cmake_minimum_required(VERSION 3.25)

project(UniversalHotkeyManager VERSION 1.1.1 LANGUAGES CXX)

find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(Zydis CONFIG REQUIRED)

option(UHI_BUILD_TESTS "Build UHI unit tests" ON)
option(UHI_BUILD_SKSE "Build the CommonLibSSE-NG SKSE plugin" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# UHM's UI, persisted reports and ImGui boundary are UTF-8 regardless of the
# active Windows ANSI code page or the optional system-wide UTF-8 locale flag.
if(MSVC)
    add_compile_options(/utf-8)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        # Capture the actual /showIncludes prefix without code-page conversion.
        # VSLANG can fall back to Korean when the English language pack is not
        # installed, so an English prefix must never be assumed. Ninja consumes
        # the same raw bytes as this probe, including in direct incremental builds.
        set(uhi_probe "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/uhi_header_probe")
        file(WRITE "${uhi_probe}.h" "// Header dependency probe\n")
        file(WRITE "${uhi_probe}.cpp" "#include \"uhi_header_probe.h\"\n")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VSLANG=1033"
            "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c "${uhi_probe}.cpp" "/Fo${uhi_probe}.obj"
            RESULT_VARIABLE uhi_probe_result OUTPUT_VARIABLE uhi_probe_output
            ERROR_VARIABLE uhi_probe_error ENCODING NONE)
        string(REGEX MATCH "([^\r\n]*: +)[^\r\n]*uhi_header_probe\\.h" uhi_probe_match "${uhi_probe_output}")
        if(NOT uhi_probe_result EQUAL 0 OR NOT uhi_probe_match)
            message(FATAL_ERROR "Cannot verify MSVC header dependency prefix: ${uhi_probe_output}${uhi_probe_error}")
        endif()
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_1}")
        list(PREPEND CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env "VSLANG=1033")
    endif()
endif()

add_library(uhi_core STATIC
    src/BindingSerializer.cpp
    src/BindingHistory.cpp
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
    find_package(imgui CONFIG REQUIRED)
    if(UHI_BUILD_TESTS)
        add_executable(uhi_native_font_tests tests/NativeFontAtlasTests.cpp)
        target_include_directories(uhi_native_font_tests PRIVATE include)
        target_link_libraries(uhi_native_font_tests PRIVATE imgui::imgui User32 Gdi32)
        target_compile_definitions(uhi_native_font_tests PRIVATE UHI_UI_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/MenuFrameworkAdapter.cpp")
        add_executable(uhi_native_menu_popup_tests tests/NativeMenuPopupTests.cpp)
        target_include_directories(uhi_native_menu_popup_tests PRIVATE include)
        target_link_libraries(uhi_native_menu_popup_tests PRIVATE imgui::imgui)
    endif()
    add_commonlibsse_plugin(UniversalHotkeyManager
        NAME "Universal Hotkey Manager for Skyrim SE-AE"
        AUTHOR "UHI contributors"
        VERSION ${PROJECT_VERSION}
        MINIMUM_SKSE_VERSION 0.0.0.0
        SOURCES
            src/plugin.cpp
            src/MenuFrameworkAdapter.cpp
            src/NativeImGuiHost.cpp
            src/RuntimeAPI.cpp
            resources/UniversalHotkeyManager.rc
    )
    target_link_libraries(UniversalHotkeyManager PRIVATE uhi_core imgui::imgui)
    if(WIN32)
        target_link_libraries(UniversalHotkeyManager PRIVATE d3d11 dxgi windowscodecs Ole32 User32 Gdi32)
    endif()
    target_include_directories(UniversalHotkeyManager PRIVATE include)
    target_compile_definitions(UniversalHotkeyManager PRIVATE UHI_VERSION="${PROJECT_VERSION}")
    target_precompile_headers(UniversalHotkeyManager PRIVATE <UHI/PCH.h>)

endif()

if(UHI_BUILD_TESTS)
    enable_testing()
    add_executable(uhi_conflict_resource_tests tests/ConflictResourceTests.cpp)
    target_link_libraries(uhi_conflict_resource_tests PRIVATE uhi_core)
    add_test(NAME uhi_conflict_resource_tests COMMAND uhi_conflict_resource_tests)
    if(WIN32)
        add_executable(uhi_runtime_api_queue_tests tests/RuntimeApiQueueTests.cpp src/RuntimeAPI.cpp)
        target_include_directories(uhi_runtime_api_queue_tests PRIVATE tests/support/runtime include)
        add_test(NAME uhi_runtime_api_queue_tests COMMAND uhi_runtime_api_queue_tests)
        # Exercise the actual target-selection implementation with engine-only
        # stand-ins. Regenerate when plugin.cpp changes; no copied algorithm.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/plugin.cpp)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp" mcm_source)
        set(mcm_impl "")
        foreach(pair IN ITEMS "CanonicalMcmIdentifier|AppendRuntimeMcmValue" "PexScriptName|// Reads only already-registered SkyUI MCM objects.")
            string(REPLACE "|" ";" markers "${pair}")
            list(GET markers 0 first)
            list(GET markers 1 last)
            string(FIND "${mcm_source}" "    std::string ${first}(" begin)
            if(last STREQUAL "AppendRuntimeMcmValue")
                set(last "void ${last}(")
            endif()
            string(FIND "${mcm_source}" "    ${last}" end)
            if(begin LESS 0 OR end LESS begin)
                message(FATAL_ERROR "MCM regression extraction markers changed")
            endif()
            math(EXPR length "${end} - ${begin}")
            string(SUBSTRING "${mcm_source}" ${begin} ${length} fragment)
            string(APPEND mcm_impl "${fragment}")
        endforeach()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/McmImplementation.inc" "${mcm_impl}")
        add_executable(uhi_mcm_target_tests tests/McmTargetTests.cpp)
        target_include_directories(uhi_mcm_target_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME uhi_mcm_target_tests COMMAND uhi_mcm_target_tests)
    endif()
    add_executable(uhi_precision_regression_tests tests/PrecisionRegressionTests.cpp)
    target_link_libraries(uhi_precision_regression_tests PRIVATE uhi_core)
    add_test(NAME uhi_precision_regression_tests COMMAND uhi_precision_regression_tests)
    add_executable(uhi_functional_regression_tests tests/FunctionalRegressionTests.cpp)
    target_link_libraries(uhi_functional_regression_tests PRIVATE uhi_core)
    add_test(NAME uhi_functional_regression_tests COMMAND uhi_functional_regression_tests)
    add_executable(uhi_native_menu_lifecycle_tests tests/NativeMenuLifecycleTests.cpp)
    target_include_directories(uhi_native_menu_lifecycle_tests PRIVATE include)
    add_test(NAME uhi_native_menu_lifecycle_tests COMMAND uhi_native_menu_lifecycle_tests)
    if(TARGET uhi_native_font_tests)
        add_test(NAME uhi_native_font_tests COMMAND uhi_native_font_tests)
        add_test(NAME uhi_native_menu_popup_tests COMMAND uhi_native_menu_popup_tests)
    endif()
    add_executable(uhi_forum_regression_tests tests/ForumRegressionTests.cpp)
    target_link_libraries(uhi_forum_regression_tests PRIVATE uhi_core)
    add_test(NAME uhi_forum_regression_tests COMMAND uhi_forum_regression_tests)
    if(WIN32)
        # Compile the public GetProcAddress consumer example on every test
        # build so the shipped declarations cannot silently drift.
        add_library(uhi_menu_api_consumer_compile OBJECT tests/MenuApiConsumer.cpp)
    endif()
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
    add_executable(uhi_escape_close_guard_tests tests/EscapeCloseGuardTests.cpp)
    target_include_directories(uhi_escape_close_guard_tests PRIVATE include)
    add_test(NAME uhi_escape_close_guard_tests COMMAND uhi_escape_close_guard_tests)
endif()
