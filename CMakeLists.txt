cmake_minimum_required(VERSION 3.24)
project(SentinelCore VERSION 0.6.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Sentinel Core currently requires Windows x64 and MSVC.")
endif()
# Detect /showIncludes as UTF-8, matching the compiler's piped output. CMake's
# console-codepage detection can corrupt the localized prefix and lose all
# header dependencies. VSLANG also falls back when English resources are absent.
file(WRITE "${CMAKE_BINARY_DIR}/sentinel-dependency-probe.h" "\n")
file(WRITE "${CMAKE_BINARY_DIR}/sentinel-dependency-probe.c" "#include \"sentinel-dependency-probe.h\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env VSLANG=1033 -- "${CMAKE_C_COMPILER}"
  /nologo /showIncludes /Zs "${CMAKE_BINARY_DIR}/sentinel-dependency-probe.c"
  OUTPUT_VARIABLE sc_dep_output ERROR_VARIABLE sc_dep_error RESULT_VARIABLE sc_dep_result ENCODING UTF-8)
string(REGEX MATCH "([^\r\n]*: )[^\r\n]*sentinel-dependency-probe[.]h" sc_dep_match "${sc_dep_output}\n${sc_dep_error}")
if(NOT sc_dep_result EQUAL 0 OR NOT sc_dep_match)
  message(FATAL_ERROR "Cannot determine the MSVC header dependency prefix.")
endif()
set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_1}")
foreach(language C CXX)
  set(CMAKE_${language}_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env VSLANG=1033 --
    ${CMAKE_${language}_COMPILER_LAUNCHER})
endforeach()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_compile_options(/W4 /WX /permissive-)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0602)

# Local Windows candidate only: PE forwarders bind to this host's real System32.
# No renamed system DLL, guessed private signatures or shared trampoline slots.
file(TO_CMAKE_PATH "$ENV{SystemRoot}/System32/msimg32" SC_SYSTEM_FORWARDER)
if(SC_SYSTEM_FORWARDER MATCHES "[ .]")
  message(FATAL_ERROR "This local PE-forwarder candidate needs a SystemRoot without spaces or dots.")
endif()
if(NOT EXISTS "${SC_SYSTEM_FORWARDER}.dll")
  message(FATAL_ERROR "System32 msimg32.dll is required for this candidate.")
endif()
configure_file(bootstrap/msimg32.def.in msimg32.def @ONLY)
string(REPLACE "/" "\\\\" SC_SYSTEM_FORWARDER_C "${SC_SYSTEM_FORWARDER}")
configure_file(bootstrap/forwarders.cpp.in forwarders.cpp @ONLY)
set(identity_inputs CMakeLists.txt include/sentinel_core.h include/sentinel_bootstrap.h
  include/sentinel_engine.h src/engine_observer.h src/engine_observer.cpp
  include/sentinel_context.h src/context_observer.h src/context_observer.cpp
  include/sentinel_native.h src/native_model.h src/native_model.cpp
  src/native_target.h src/native_target.cpp src/native_runtime.h src/native_runtime.cpp
  include/sentinel_save.h src/save_observer.h src/save_observer.cpp
  src/save_storage.h src/save_storage.cpp src/prelaunch.h src/prelaunch.cpp src/startup_log.h src/startup_log.cpp tools/save_probe.h tools/save_probe.cpp
  src/save_b_trace.h src/save_b_io_trace.h src/save_session.h src/save_session.cpp src/save_collector.h src/save_collector.cpp
  src/save_delete.h src/save_delete.cpp src/save_write.h src/save_write.cpp
  src/save_sdk_write.h src/save_sdk_write.cpp
  include/sentinel_save_request.h src/save_readback.h src/save_readback.cpp
  src/save_backup.h src/save_backup.cpp src/save_submission.h src/save_submission.cpp
  tools/backup_probe.cpp tools/installation_probe.h tools/installation_probe.cpp
  include/sentinel_save_installation.h src/save_installation.h src/save_installation.cpp
  tools/retest_milestone_a.py tools/Retest-MilestoneA.ps1
  tools/prepare_vanilla_backup.py tools/compare_vanilla_campaign.py
  src/save_profile.h src/save_profile.cpp
  src/save_campaign.h src/save_campaign.cpp src/save_campaign_native.h src/save_campaign_native.cpp
  src/save_provider.h src/save_provider.cpp
  src/save_catalog.h src/save_catalog.cpp
  src/save_profile_prerequisite.h src/save_profile_prerequisite.cpp
  src/save_native_hooks.h src/save_native_hooks.cpp src/save_startup_gate.h
  third_party/minhook/include/MinHook.h third_party/minhook/src/hook.c
  third_party/minhook/src/buffer.c third_party/minhook/src/buffer.h
  third_party/minhook/src/trampoline.c third_party/minhook/src/trampoline.h
  third_party/minhook/src/hde/hde64.c third_party/minhook/src/hde/hde64.h
  third_party/minhook/src/hde/table64.h third_party/minhook/src/hde/pstdint.h
  third_party/minhook/LICENSE.txt third_party/minhook/NOTICE
  include/sentinel_inspection.h src/core.cpp src/inspection_server.h src/inspection_server.cpp
  src/inspection_client.cpp src/pipe_io.h src/protocol.h src/protocol.cpp tools/probe.cpp
  bootstrap/proxy.cpp bootstrap/msimg32.def.in bootstrap/forwarders.cpp.in)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${identity_inputs})
set(identity "${PROJECT_VERSION}|${SC_SYSTEM_FORWARDER}")
foreach(input IN LISTS identity_inputs)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${input}" digest)
  string(APPEND identity "|${input}:${digest}")
endforeach()
string(SHA256 SC_BUILD_ID "${identity}")

add_library(sentinel_protocol STATIC src/protocol.cpp)
target_include_directories(sentinel_protocol PUBLIC include src)
add_library(sentinel_inspection_client STATIC src/inspection_client.cpp)
target_link_libraries(sentinel_inspection_client PUBLIC sentinel_protocol)
add_library(sentinel_save_storage STATIC src/save_storage.cpp src/prelaunch.cpp)
target_include_directories(sentinel_save_storage PUBLIC src)
target_link_libraries(sentinel_save_storage PUBLIC bcrypt)
add_executable(sentinel_probe tools/probe.cpp tools/save_probe.cpp tools/backup_probe.cpp tools/installation_probe.cpp)
target_link_libraries(sentinel_probe PRIVATE sentinel_inspection_client sentinel_save_storage bcrypt)
add_library(sentinel_observer STATIC src/engine_observer.cpp src/context_observer.cpp src/save_observer.cpp)
target_include_directories(sentinel_observer PUBLIC include src)
target_link_libraries(sentinel_observer PUBLIC bcrypt)
add_library(sentinel_minhook STATIC third_party/minhook/src/hook.c
  third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(sentinel_minhook PUBLIC third_party/minhook/include third_party/minhook/src)
# Pinned upstream uses intentional byte casts/anonymous unions; HDE initializes
# its prefix byte in the loop. Keep upstream unchanged and Core warnings strict.
target_compile_options(sentinel_minhook PRIVATE /wd4201 /wd4244 /wd4310 /wd4701)
add_library(sentinel_native_model STATIC src/native_model.cpp)
target_include_directories(sentinel_native_model PUBLIC include src)
add_library(sentinel_save_session STATIC src/save_session.cpp src/save_installation.cpp src/save_collector.cpp src/save_delete.cpp src/save_write.cpp src/save_sdk_write.cpp src/save_profile.cpp src/save_provider.cpp src/save_catalog.cpp src/save_profile_prerequisite.cpp src/save_readback.cpp src/save_backup.cpp src/save_submission.cpp src/save_campaign.cpp)
target_link_libraries(sentinel_save_session PUBLIC sentinel_save_storage sentinel_observer)
add_executable(sentinel_campaign_tests tests/save_campaign.cpp tests/save_profile.cpp src/native_runtime.cpp src/native_target.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp)
target_include_directories(sentinel_campaign_tests PRIVATE tests)
target_compile_definitions(sentinel_campaign_tests PRIVATE SC_NATIVE_TESTING)
target_link_libraries(sentinel_campaign_tests PRIVATE sentinel_native_model sentinel_minhook sentinel_save_session)
target_link_libraries(sentinel_native_model PUBLIC sentinel_save_session)
add_library(sentinel_native STATIC src/native_target.cpp src/native_runtime.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp)
target_link_libraries(sentinel_native PUBLIC sentinel_native_model sentinel_observer sentinel_minhook sentinel_save_session)
add_library(sentinel_core SHARED src/core.cpp src/inspection_server.cpp src/startup_log.cpp)
target_link_libraries(sentinel_core PRIVATE sentinel_protocol sentinel_native advapi32 bcrypt)
target_include_directories(sentinel_core PUBLIC include)
add_executable(sentinel_workflow_tests tests/workflow.cpp src/startup_log.cpp)
target_link_libraries(sentinel_workflow_tests PRIVATE sentinel_save_session sentinel_protocol)
add_test(NAME workflow_handoff_logging_contract COMMAND sentinel_workflow_tests)
set_tests_properties(workflow_handoff_logging_contract PROPERTIES TIMEOUT 15)
target_compile_definitions(sentinel_core PRIVATE SC_BUILDING_CORE
  SC_VERSION="${PROJECT_VERSION}" SC_BUILD_ID="${SC_BUILD_ID}")
add_library(sentinel_bootstrap SHARED bootstrap/proxy.cpp "${CMAKE_BINARY_DIR}/forwarders.cpp" "${CMAKE_BINARY_DIR}/msimg32.def")
target_link_options(sentinel_core PRIVATE /WX)
target_link_options(sentinel_bootstrap PRIVATE /WX)
set_target_properties(sentinel_bootstrap PROPERTIES OUTPUT_NAME msimg32)
target_link_libraries(sentinel_bootstrap PRIVATE sentinel_core)
add_executable(sentinel_harness tests/harness.cpp tests/header_c.c)
target_include_directories(sentinel_harness PRIVATE include)
add_dependencies(sentinel_harness sentinel_bootstrap)
add_executable(sentinel_import_host tests/import_host.cpp)
target_include_directories(sentinel_import_host PRIVATE include)
target_link_libraries(sentinel_import_host PRIVATE sentinel_bootstrap gdi32)
enable_testing()
add_executable(sentinel_installation_tests tests/installation.cpp)
target_link_libraries(sentinel_installation_tests PRIVATE sentinel_protocol sentinel_native)
add_test(NAME retained_installation_contract COMMAND sentinel_installation_tests)
add_executable(sentinel_save_session_tests tests/save_session.cpp tests/save_profile.cpp tests/save_provider.cpp tests/save_sdk_write.cpp tests/save_readback.cpp tests/save_submission.cpp)
target_link_libraries(sentinel_save_session_tests PRIVATE sentinel_save_session sentinel_native_model sentinel_protocol)
add_test(NAME native_save_session_collector_contract COMMAND sentinel_save_session_tests)
set_tests_properties(native_save_session_collector_contract PROPERTIES TIMEOUT 15)
add_test(NAME native_profile_selection_contract COMMAND sentinel_save_session_tests --profile)
add_test(NAME native_sdk_write_ownership_contract COMMAND sentinel_save_session_tests --sdk-write)
add_test(NAME native_write_readback_contract COMMAND sentinel_save_session_tests --readback)
set_tests_properties(native_write_readback_contract PROPERTIES TIMEOUT 15)
set_tests_properties(native_sdk_write_ownership_contract PROPERTIES TIMEOUT 15)
set_tests_properties(native_profile_selection_contract PROPERTIES TIMEOUT 15)
  add_test(NAME native_provider_identity_contract COMMAND sentinel_save_session_tests --provider)
  add_test(NAME native_startup_routing_contract COMMAND sentinel_save_session_tests --startup-route)
  set_tests_properties(native_startup_routing_contract PROPERTIES TIMEOUT 15)
  set_tests_properties(native_provider_identity_contract PROPERTIES TIMEOUT 15)
  add_test(NAME native_catalog_selection_contract COMMAND sentinel_save_session_tests --catalog)
  set_tests_properties(native_catalog_selection_contract PROPERTIES TIMEOUT 15)
  add_test(NAME native_profile_prerequisite_contract COMMAND sentinel_save_session_tests --prerequisite)
  # Includes two real delays beyond the removed ten-second Core deadline.
  set_tests_properties(native_profile_prerequisite_contract PROPERTIES TIMEOUT 45)
add_executable(sentinel_save_observer_tests tests/save_observer.cpp)
target_link_libraries(sentinel_save_observer_tests PRIVATE sentinel_observer sentinel_protocol sentinel_core)
add_test(NAME native_save_observation_contract COMMAND sentinel_save_observer_tests)
set_tests_properties(native_save_observation_contract PROPERTIES TIMEOUT 10)
add_executable(sentinel_save_storage_tests tests/save_storage.cpp)
target_link_libraries(sentinel_save_storage_tests PRIVATE sentinel_save_storage)
add_test(NAME isolated_save_storage_contract COMMAND sentinel_save_storage_tests)
set_tests_properties(isolated_save_storage_contract PROPERTIES TIMEOUT 30)
add_executable(sentinel_native_model_tests tests/native_model.cpp tests/backup_request.cpp)
target_link_libraries(sentinel_native_model_tests PRIVATE sentinel_native_model sentinel_protocol)
add_test(NAME native_lifecycle_dispatch_model COMMAND sentinel_native_model_tests)
set_tests_properties(native_lifecycle_dispatch_model PROPERTIES TIMEOUT 10)
# Separate test executable with explicit fixture adapters. The production Core
# never compiles SC_NATIVE_TESTING or accepts an address/callback through IPC.
add_executable(sentinel_native_host tests/native_host.cpp src/core.cpp src/inspection_server.cpp src/startup_log.cpp
  src/native_runtime.cpp src/native_target.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp)
target_include_directories(sentinel_native_host PRIVATE tests)
target_link_libraries(sentinel_native_host PRIVATE sentinel_native_model sentinel_observer sentinel_minhook
  sentinel_inspection_client sentinel_save_session advapi32 bcrypt)
target_compile_definitions(sentinel_native_host PRIVATE SC_BUILDING_CORE SC_NATIVE_TESTING
  SC_VERSION="${PROJECT_VERSION}" SC_BUILD_ID="${SC_BUILD_ID}")
add_dependencies(sentinel_native_host sentinel_probe)
add_test(NAME native_hook_dispatch_host COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" basic)
add_test(NAME native_wrong_thread_host COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" wrong-thread)
add_test(NAME native_unobserved_map_host COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" unobserved-map)
add_test(NAME native_unobserved_loading_host COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" unobserved-loading)
set_tests_properties(native_hook_dispatch_host native_wrong_thread_host native_unobserved_map_host native_unobserved_loading_host PROPERTIES TIMEOUT 60)
add_executable(sentinel_context_tests tests/context.cpp)
target_link_libraries(sentinel_context_tests PRIVATE sentinel_observer sentinel_protocol)
add_test(NAME runtime_context_contract COMMAND sentinel_context_tests)
set_tests_properties(runtime_context_contract PROPERTIES TIMEOUT 10)
add_executable(sentinel_engine_tests tests/engine.cpp)
target_link_libraries(sentinel_engine_tests PRIVATE sentinel_observer sentinel_protocol)
add_test(NAME engine_observation_contract COMMAND sentinel_engine_tests)
set_tests_properties(engine_observation_contract PROPERTIES TIMEOUT 10)
add_executable(sentinel_inspection_tests tests/inspection.cpp)
target_link_libraries(sentinel_inspection_tests PRIVATE sentinel_inspection_client sentinel_save_storage advapi32)
add_dependencies(sentinel_inspection_tests sentinel_core sentinel_probe)
add_test(NAME live_inspection_contract COMMAND sentinel_inspection_tests "$<TARGET_FILE:sentinel_core>" "$<TARGET_FILE:sentinel_probe>")
set_tests_properties(live_inspection_contract PROPERTIES TIMEOUT 40)
add_test(NAME save_prelaunch_owner_contract COMMAND sentinel_inspection_tests "$<TARGET_FILE:sentinel_core>" "$<TARGET_FILE:sentinel_probe>" --prelaunch)
set_tests_properties(save_prelaunch_owner_contract PROPERTIES TIMEOUT 15)
add_test(NAME core_lifecycle COMMAND sentinel_harness --core "$<TARGET_FILE:sentinel_core>")
add_test(NAME proxy_forwarding_lifecycle COMMAND sentinel_harness --proxy "$<TARGET_FILE:sentinel_bootstrap>")
add_test(NAME proxy_normal_exit COMMAND sentinel_harness --exit "$<TARGET_FILE:sentinel_bootstrap>")
add_test(NAME proxy_second_process COMMAND sentinel_harness --proxy "$<TARGET_FILE:sentinel_bootstrap>")
add_test(NAME static_import_drawing COMMAND sentinel_import_host)
add_test(NAME missing_dll COMMAND sentinel_harness --missing "${CMAKE_BINARY_DIR}/absent.dll")
add_test(NAME missing_core_dependency COMMAND "${CMAKE_COMMAND}"
  "-DPROXY=$<TARGET_FILE:sentinel_bootstrap>" "-DHARNESS=$<TARGET_FILE:sentinel_harness>"
  "-DSTAGE=${CMAKE_BINARY_DIR}/missing-core" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/missing_dependency.cmake")
set_tests_properties(core_lifecycle proxy_forwarding_lifecycle proxy_normal_exit proxy_second_process static_import_drawing missing_dll missing_core_dependency
  PROPERTIES TIMEOUT 15)
