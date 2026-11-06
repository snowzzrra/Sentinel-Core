cmake_minimum_required(VERSION 3.24)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" sc_version_json)
foreach(field major minor patch channel rc_number)
  string(JSON SC_${field} GET "${sc_version_json}" ${field})
endforeach()
set(SC_BASE_VERSION "${SC_major}.${SC_minor}.${SC_patch}")
set(SC_FULL_VERSION "${SC_BASE_VERSION}")
if(SC_channel STREQUAL "rc" AND SC_rc_number MATCHES "^[1-9][0-9]*$")
  string(APPEND SC_FULL_VERSION "-rc-${SC_rc_number}")
elseif(NOT SC_channel STREQUAL "stable" OR NOT SC_rc_number STREQUAL "0")
  message(FATAL_ERROR "Invalid release channel or RC number in version.json")
endif()
project(SentinelCore VERSION ${SC_BASE_VERSION} LANGUAGES C CXX ASM_MASM RC)
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
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/WX;/permissive-;/Z7>")
add_link_options(/DEBUG /OPT:REF /OPT:ICF)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0602)

option(SC_ENABLE_COMPLEXITY_GATE "Enable the development-only C++ complexity target" OFF)
if(SC_ENABLE_COMPLEXITY_GATE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_target(complexity_gate
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/complexity_gate.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL VERBATIM)
endif()

configure_file(bootstrap/msimg32.def.in msimg32.def COPYONLY)
configure_file(include/sentinel_version.h.in generated/sentinel_version.h @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
set(identity_inputs include/sentinel_evidence.h src/currencies.h src/currencies.cpp src/encounter_recovery.h src/encounter_recovery.cpp CMakeLists.txt version.json include/sentinel_version.h.in resources/version.rc.in include/sentinel_core.h include/sentinel_bootstrap.h
  src/engine/builds/build_descriptor.h src/engine/builds/build_descriptor.cpp src/engine/builds/steam_20260818.cpp
  src/engine/hooks/hook_set.h src/engine/hooks/hook_set.cpp
  include/sentinel_engine.h src/engine_observer.h src/engine_observer.cpp
  include/sentinel_context.h src/context_observer.h src/context_observer.cpp
  include/sentinel_native.h src/native_model.h src/native_model.cpp
  src/command_registry.h src/command_registry.cpp
  include/sentinel_combat.h src/combat.h src/combat.cpp src/combat_native.h src/combat_native.cpp src/combat_presentation.h src/combat_progress_hud.h
  src/expanded.h src/expanded.cpp src/expanded_native.h src/expanded_native.cpp
  src/features/expanded/expanded_internal.h
  src/features/expanded/movement_native.cpp
  src/features/expanded/recipes_native.cpp
  src/features/expanded/summons_native.cpp
  src/features/expanded/encounters_native.cpp
  src/features/expanded/spawn_native.cpp
  src/features/expanded/checkpoint_native.cpp
  src/features/expanded/recovery_native.cpp
  src/darklord_telemetry.h src/enemy_recipe_state.h src/enemy_recipe_state.cpp
  src/enemy_checkpoint_native.h src/enemy_checkpoint_native.cpp
  include/sentinel_weapon_points.h src/weapon_points.h src/weapon_points.cpp src/weapon_points_native.cpp
  tools/weapon_points_probe.cpp
  src/challenge_match.h src/challenge_seam_layout.h src/challenge_suppression.h src/challenge_suppression.cpp
  include/sentinel_campaign_menu.h src/campaign_menu.h src/campaign_menu.cpp src/campaign_stages_generated.h src/fast_travel.h tools/campaign_menu_probe.cpp
  src/campaign_menu_native.h src/campaign_menu_native.cpp src/physical_contact_observer.h
  include/sentinel_inventory.h src/inventory.h src/inventory.cpp src/inventory_native.cpp
  tools/inventory_probe.cpp
  include/sentinel_arsenal.h src/arsenal.h src/arsenal.cpp src/arsenal_native.cpp
  tools/arsenal_probe.cpp
  include/sentinel_runes.h src/runes.h src/runes.cpp src/runes_native.cpp
  tools/runes_probe.cpp
  include/sentinel_special.h src/special.h src/special.cpp src/special_native.cpp src/special_hud_native.h
  tools/special_probe.cpp src/local_controls.h tools/local_controls_probe.cpp
  src/ap_controls_native.h src/ap_controls_native.cpp src/player_strings_generated.h
  include/sentinel_deathlink.h src/deathlink.h src/deathlink.cpp src/deathlink_native.cpp
  include/sentinel_commands.h src/commands.h src/commands.cpp src/commands_native.h src/commands_native.cpp src/commands_local.cpp
  tools/deathlink_probe.cpp
  tools/combat_probe.cpp
  src/native_target.h src/native_target.cpp src/native_runtime.h src/native_runtime.cpp
  include/sentinel_automap.h src/automap_catalog.h
  include/sentinel_save.h src/save_observer.h src/save_observer.cpp
  src/save_storage.h src/save_storage.cpp src/prelaunch.h src/prelaunch.cpp src/startup_log.h src/startup_log.cpp tools/save_probe.h tools/save_probe.cpp
  src/save_b_trace.h src/save_b_io_trace.h src/save_session.h src/save_session.cpp src/save_collector.h src/save_collector.cpp
  src/save_delete.h src/save_delete.cpp src/save_write.h src/save_write.cpp
  src/save_sdk_write.h src/save_sdk_write.cpp
  include/sentinel_save_request.h src/save_readback.h src/save_readback.cpp
  src/save_backup.h src/save_backup.cpp src/save_submission.h src/save_submission.cpp
  src/save_recovery.h src/save_recovery.cpp
  tools/backup_probe.cpp tools/installation_probe.h tools/installation_probe.cpp
  include/sentinel_save_installation.h src/save_installation.h src/save_installation.cpp
  tools/prepare_vanilla_backup.py tools/compare_vanilla_campaign.py
  src/save_profile.h src/save_profile.cpp
  src/save_campaign.h src/save_campaign.cpp src/save_restart.cpp src/save_campaign_native.h src/save_campaign_native.cpp
  src/save_provider.h src/save_provider.cpp
  src/save_catalog.h src/save_catalog.cpp
  src/save_profile_prerequisite.h src/save_profile_prerequisite.cpp
  src/save_native_hooks.h src/save_native_hooks.cpp src/save_startup_gate.h src/save_hook_call.h
  third_party/minhook/include/MinHook.h third_party/minhook/src/hook.c
  third_party/minhook/src/buffer.c third_party/minhook/src/buffer.h
  third_party/minhook/src/trampoline.c third_party/minhook/src/trampoline.h
  third_party/minhook/src/hde/hde64.c third_party/minhook/src/hde/hde64.h
  third_party/minhook/src/hde/table64.h third_party/minhook/src/hde/pstdint.h
  third_party/minhook/LICENSE.txt third_party/minhook/NOTICE
  include/sentinel_inspection.h src/core.cpp src/inspection_server.h src/inspection_server.cpp
  src/inspection_client.cpp src/pipe_io.h src/client_payload.h src/client_payload.cpp src/inspection_names.cpp tools/probe_v2.h tools/probe_v2.cpp
  src/protocol_dispatch.cpp src/protocol_v2.h src/protocol_v2.cpp tools/probe.cpp
  protocol/schema.json tools/generate_protocol.py
  bootstrap/proxy.cpp bootstrap/msimg32.def.in bootstrap/forwarders.cpp bootstrap/forwarders.asm)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${identity_inputs})
set(identity "${SC_FULL_VERSION}")
foreach(input IN LISTS identity_inputs)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${input}" digest)
  string(APPEND identity "|${input}:${digest}")
endforeach()
string(SHA256 SC_BUILD_ID "${identity}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(sc_protocol_output "${CMAKE_CURRENT_BINARY_DIR}/protocol_generated")
set(sc_protocol_header "${sc_protocol_output}/include/sentinel_protocol_generated.h")
set(sc_protocol_source "${sc_protocol_output}/src/protocol_generated.cpp")
set(sc_protocol_python "${sc_protocol_output}/protocol/generated/sentinel_protocol.py")
add_custom_command(OUTPUT "${sc_protocol_header}" "${sc_protocol_source}" "${sc_protocol_python}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_protocol.py"
    --output-root "${sc_protocol_output}"
  DEPENDS protocol/schema.json tools/generate_protocol.py
  VERBATIM)
add_library(sentinel_protocol STATIC src/protocol_v2.cpp src/client_payload.cpp src/inspection_names.cpp src/commands.cpp "${sc_protocol_source}" "${sc_protocol_header}")
target_link_libraries(sentinel_protocol PUBLIC bcrypt)
target_include_directories(sentinel_protocol PUBLIC "${sc_protocol_output}/include" include src)
add_library(sentinel_inspection_client STATIC src/inspection_client.cpp)
target_link_libraries(sentinel_inspection_client PUBLIC sentinel_protocol)
add_library(sentinel_save_storage STATIC src/save_storage.cpp src/prelaunch.cpp)
target_include_directories(sentinel_save_storage PUBLIC src)
target_link_libraries(sentinel_save_storage PUBLIC bcrypt)
add_executable(sentinel_probe tools/probe_v2.cpp tools/probe.cpp tools/local_controls_probe.cpp tools/combat_probe.cpp tools/save_probe.cpp tools/backup_probe.cpp tools/installation_probe.cpp tools/weapon_points_probe.cpp tools/campaign_menu_probe.cpp tools/inventory_probe.cpp tools/arsenal_probe.cpp tools/runes_probe.cpp tools/special_probe.cpp tools/deathlink_probe.cpp)
target_link_libraries(sentinel_probe PRIVATE sentinel_inspection_client sentinel_save_storage bcrypt)
add_library(sentinel_observer STATIC src/engine/builds/build_descriptor.cpp src/engine/builds/steam_20260818.cpp src/engine_observer.cpp src/context_observer.cpp src/save_observer.cpp)
target_include_directories(sentinel_observer PUBLIC include src)
target_link_libraries(sentinel_observer PUBLIC bcrypt)
add_library(sentinel_minhook STATIC third_party/minhook/src/hook.c
  third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(sentinel_minhook PUBLIC third_party/minhook/include third_party/minhook/src)
# Pinned upstream uses intentional byte casts/anonymous unions; HDE initializes
# its prefix byte in the loop. Keep upstream unchanged and Core warnings strict.
target_compile_options(sentinel_minhook PRIVATE /wd4201 /wd4244 /wd4310 /wd4701)
add_library(sentinel_fault_minhook STATIC tests/fault_minhook.c)
target_compile_options(sentinel_fault_minhook PRIVATE /wd4201 /wd4244 /wd4310 /wd4701)
target_link_libraries(sentinel_fault_minhook PUBLIC sentinel_minhook)
add_library(sentinel_native_model STATIC src/encounter_recovery.cpp src/native_model.cpp src/combat.cpp src/expanded.cpp src/enemy_recipe_state.cpp src/weapon_points.cpp src/currencies.cpp src/campaign_menu.cpp src/inventory.cpp src/arsenal.cpp src/runes.cpp src/special.cpp src/deathlink.cpp)
target_link_libraries(sentinel_native_model PUBLIC sentinel_protocol)
target_include_directories(sentinel_native_model PUBLIC include src)
add_executable(sentinel_combat_tests tests/combat.cpp)
target_link_libraries(sentinel_combat_tests PRIVATE sentinel_native_model)
add_test(NAME native_combat_registration_delivery COMMAND sentinel_combat_tests)
set_tests_properties(native_combat_registration_delivery PROPERTIES TIMEOUT 10)
add_executable(sentinel_expanded_tests tests/expanded.cpp)
target_link_libraries(sentinel_expanded_tests PRIVATE sentinel_native_model)
add_test(NAME native_expanded_movement COMMAND sentinel_expanded_tests)
add_executable(sentinel_enemy_recipe_tests tests/enemy_recipe_state.cpp)
target_link_libraries(sentinel_enemy_recipe_tests PRIVATE sentinel_native_model)
add_test(NAME native_enemy_recipe_state COMMAND sentinel_enemy_recipe_tests)
set_tests_properties(native_expanded_movement PROPERTIES TIMEOUT 10)
add_library(sentinel_save_session STATIC src/save_session.cpp src/save_installation.cpp src/save_collector.cpp src/save_delete.cpp src/save_write.cpp src/save_sdk_write.cpp src/save_profile.cpp src/save_provider.cpp src/save_catalog.cpp src/save_profile_prerequisite.cpp src/save_readback.cpp src/save_backup.cpp src/save_submission.cpp src/save_campaign.cpp)
target_link_libraries(sentinel_save_session PUBLIC sentinel_save_storage sentinel_observer)
target_sources(sentinel_save_session PRIVATE src/save_restart.cpp)
target_sources(sentinel_save_session PRIVATE src/save_recovery.cpp)
# Fault controls and the storage copy belong only to synthetic test targets.
add_library(sentinel_fault_storage STATIC src/save_storage.cpp src/prelaunch.cpp)
target_include_directories(sentinel_fault_storage PRIVATE include tests third_party/minhook/include PUBLIC src)
target_compile_definitions(sentinel_fault_storage PRIVATE SC_FAULT_REDIRECT)
target_compile_options(sentinel_fault_storage PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/tests/fault_injection.h")
target_link_libraries(sentinel_fault_storage PUBLIC bcrypt)
add_executable(sentinel_campaign_tests tests/save_campaign.cpp tests/save_profile.cpp src/native_runtime.cpp src/combat_native.cpp src/ap_controls_native.cpp src/native_target.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp src/campaign_menu_native.cpp src/startup_log.cpp)
target_sources(sentinel_campaign_tests PRIVATE tests/hooks/cleanup.cpp)
target_include_directories(sentinel_campaign_tests PRIVATE tests)
target_compile_definitions(sentinel_campaign_tests PRIVATE SC_NATIVE_TESTING)
target_link_libraries(sentinel_campaign_tests PRIVATE sentinel_fault_storage sentinel_native_model sentinel_fault_minhook sentinel_save_session sentinel_protocol)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/campaign-fixtures")
foreach(case fresh_catalog foreign_catalog foreign_write foreign_erase wrong_slot_write limit_inputs quantity_inputs recipe_inputs recipe_checkpoint combat_contention expanded_stop hook_cleanup encounter_recovery hands_restore)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/campaign-fixtures/${case}")
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/campaign-fixtures/${case}" campaign_fixture)
  add_test(NAME native_campaign_${case}_contract COMMAND sentinel_campaign_tests create "${campaign_fixture}" 1 ${case})
  set_tests_properties(native_campaign_${case}_contract PROPERTIES TIMEOUT 15)
endforeach()
set_tests_properties(native_campaign_hook_cleanup_contract PROPERTIES TIMEOUT 60)
target_link_libraries(sentinel_native_model PUBLIC sentinel_save_session)
add_library(sentinel_native STATIC src/native_target.cpp src/native_runtime.cpp src/combat_native.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp src/campaign_menu_native.cpp)
target_link_libraries(sentinel_native PUBLIC sentinel_native_model sentinel_observer sentinel_minhook sentinel_save_session)
add_library(sentinel_core SHARED src/core.cpp src/inspection_server.cpp src/protocol_dispatch.cpp src/startup_log.cpp)
target_link_libraries(sentinel_core PRIVATE sentinel_protocol sentinel_native advapi32 bcrypt)
target_include_directories(sentinel_core PUBLIC include)
add_executable(sentinel_workflow_tests tests/workflow.cpp src/startup_log.cpp)
target_link_libraries(sentinel_workflow_tests PRIVATE sentinel_native sentinel_save_session sentinel_protocol)
add_test(NAME workflow_handoff_logging_contract COMMAND sentinel_workflow_tests)
set_tests_properties(workflow_handoff_logging_contract PROPERTIES TIMEOUT 15 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.tmp")
target_compile_definitions(sentinel_core PRIVATE SC_BUILDING_CORE
  SC_VERSION="${SC_FULL_VERSION}" SC_BUILD_ID="${SC_BUILD_ID}")
add_library(sentinel_bootstrap SHARED bootstrap/proxy.cpp bootstrap/forwarders.cpp bootstrap/forwarders.asm "${CMAKE_BINARY_DIR}/msimg32.def")
set_source_files_properties(bootstrap/forwarders.asm PROPERTIES COMPILE_OPTIONS "/W3")
target_link_options(sentinel_core PRIVATE /WX)
target_link_options(sentinel_bootstrap PRIVATE /WX)
set_target_properties(sentinel_bootstrap PROPERTIES OUTPUT_NAME msimg32)
target_link_libraries(sentinel_bootstrap PRIVATE sentinel_core)
target_compile_definitions(sentinel_bootstrap PRIVATE SC_BUILD_ID="${SC_BUILD_ID}")
foreach(target sentinel_core sentinel_bootstrap sentinel_probe)
  set(SC_RESOURCE_NAME "${target}")
  if(target STREQUAL "sentinel_bootstrap")
    set(SC_RESOURCE_NAME "msimg32")
  endif()
  configure_file(resources/version.rc.in "generated/${target}.rc" @ONLY)
  target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/${target}.rc")
endforeach()
add_executable(sentinel_harness tests/harness.cpp tests/header_c.c)
target_include_directories(sentinel_harness PRIVATE include)
add_dependencies(sentinel_harness sentinel_bootstrap)
add_executable(sentinel_import_host tests/import_host.cpp)
target_include_directories(sentinel_import_host PRIVATE include)
target_link_libraries(sentinel_import_host PRIVATE sentinel_bootstrap gdi32)
enable_testing()
add_executable(sentinel_command_tests tests/commands.cpp)
target_link_libraries(sentinel_command_tests PRIVATE sentinel_native_model)
add_test(NAME command_dispatch_contract COMMAND sentinel_command_tests)
add_executable(sentinel_installation_tests tests/installation.cpp src/startup_log.cpp)
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
  set_tests_properties(native_profile_prerequisite_contract PROPERTIES TIMEOUT 45)
# these fixtures share the synthetic Steam-root mutex
set_tests_properties(native_provider_identity_contract native_catalog_selection_contract
  native_profile_prerequisite_contract PROPERTIES RESOURCE_LOCK synthetic_native_provider_root)
add_executable(sentinel_save_observer_tests tests/save_observer.cpp)
target_link_libraries(sentinel_save_observer_tests PRIVATE sentinel_observer sentinel_protocol sentinel_core)
add_test(NAME native_save_observation_contract COMMAND sentinel_save_observer_tests)
set_tests_properties(native_save_observation_contract PROPERTIES TIMEOUT 10)
add_executable(sentinel_save_storage_tests tests/save_storage.cpp)
target_link_libraries(sentinel_save_storage_tests PRIVATE sentinel_fault_storage sentinel_save_session sentinel_minhook)
add_test(NAME isolated_save_storage_contract COMMAND sentinel_save_storage_tests)
set_tests_properties(isolated_save_storage_contract PROPERTIES TIMEOUT 30)
add_executable(sentinel_native_model_tests tests/native_model.cpp tests/backup_request.cpp tests/command_identity.cpp)
target_link_libraries(sentinel_native_model_tests PRIVATE sentinel_native_model sentinel_protocol)
add_test(NAME native_lifecycle_dispatch_model COMMAND sentinel_native_model_tests)

add_executable(sentinel_special_model_tests tests/special_model.cpp)
target_link_libraries(sentinel_special_model_tests PRIVATE sentinel_native_model)
add_test(NAME special_hammer_first_acquisition_model COMMAND sentinel_special_model_tests)
set_tests_properties(native_lifecycle_dispatch_model PROPERTIES TIMEOUT 10)
# Separate test executable with explicit fixture adapters. The production Core
# never compiles SC_NATIVE_TESTING or accepts an address/callback through IPC.
add_executable(sentinel_native_host tests/native_host.cpp tests/protocol_v2.cpp src/core.cpp src/inspection_server.cpp src/protocol_dispatch.cpp src/startup_log.cpp
  src/native_runtime.cpp src/combat_native.cpp src/ap_controls_native.cpp src/native_target.cpp src/save_native_hooks.cpp src/save_campaign_native.cpp src/campaign_menu_native.cpp)
target_include_directories(sentinel_native_host PRIVATE tests)
target_link_libraries(sentinel_native_host PRIVATE sentinel_native_model sentinel_observer sentinel_fault_minhook
  sentinel_inspection_client sentinel_save_session advapi32 bcrypt)
target_compile_definitions(sentinel_native_host PRIVATE SC_WIRE_V2_FIXTURE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire/v2.json" SC_BUILDING_CORE SC_NATIVE_TESTING
  SC_VERSION="${SC_FULL_VERSION}" SC_BUILD_ID="${SC_BUILD_ID}")
add_dependencies(sentinel_native_host sentinel_probe)
add_test(NAME protocol_v2_contract COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" v2)
set_tests_properties(protocol_v2_contract PROPERTIES TIMEOUT 60)
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
add_test(NAME static_import_drawing COMMAND sentinel_import_host)
add_test(NAME missing_dll COMMAND sentinel_harness --missing "${CMAKE_BINARY_DIR}/absent.dll")
add_test(NAME missing_core_dependency COMMAND "${CMAKE_COMMAND}"
  "-DPROXY=$<TARGET_FILE:sentinel_bootstrap>" "-DHARNESS=$<TARGET_FILE:sentinel_harness>"
  "-DSTAGE=${CMAKE_CURRENT_SOURCE_DIR}/.tmp/missing-core" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/missing_dependency.cmake")
foreach(target sentinel_native sentinel_campaign_tests sentinel_native_host)
  target_sources(${target} PRIVATE src/engine/hooks/hook_set.cpp)
  target_sources(${target} PRIVATE src/command_registry.cpp)
  target_sources(${target} PRIVATE src/weapon_points_native.cpp src/challenge_suppression.cpp src/inventory_native.cpp src/arsenal_native.cpp src/runes_native.cpp src/special_native.cpp src/ap_controls_native.cpp src/expanded_native.cpp
    src/features/expanded/movement_native.cpp
    src/features/expanded/recipes_native.cpp
    src/features/expanded/summons_native.cpp
    src/features/expanded/encounters_native.cpp
    src/features/expanded/spawn_native.cpp
    src/features/expanded/checkpoint_native.cpp
    src/features/expanded/recovery_native.cpp
    src/enemy_checkpoint_native.cpp src/deathlink_native.cpp src/commands_native.cpp src/commands_local.cpp)
endforeach()
add_executable(sentinel_hook_set_tests tests/hooks/hook_set.cpp src/engine/hooks/hook_set.cpp)
target_link_libraries(sentinel_hook_set_tests PRIVATE sentinel_save_session sentinel_minhook)
target_include_directories(sentinel_hook_set_tests PRIVATE src include)
add_test(NAME hook_set_transaction_contract COMMAND sentinel_hook_set_tests)
set_tests_properties(hook_set_transaction_contract PROPERTIES TIMEOUT 20)
add_executable(sentinel_challenge_seam_tests tests/hooks/challenge_seam.cpp)
target_include_directories(sentinel_challenge_seam_tests PRIVATE src)
add_test(NAME challenge_seam_forwarding_contract COMMAND sentinel_challenge_seam_tests)
set_tests_properties(challenge_seam_forwarding_contract PROPERTIES TIMEOUT 10)
add_executable(sentinel_weapon_points_tests tests/weapon_points.cpp)
target_link_libraries(sentinel_weapon_points_tests PRIVATE sentinel_native_model sentinel_protocol)
target_compile_options(sentinel_weapon_points_tests PRIVATE /UNDEBUG)
add_test(NAME weapon_points_contract COMMAND sentinel_weapon_points_tests)
set_tests_properties(core_lifecycle proxy_forwarding_lifecycle proxy_normal_exit static_import_drawing missing_dll missing_core_dependency
  PROPERTIES TIMEOUT 15)

add_executable(sentinel_encounter_recovery_tests tests/encounter_recovery.cpp)
target_link_libraries(sentinel_encounter_recovery_tests PRIVATE sentinel_native_model)
target_compile_options(sentinel_encounter_recovery_tests PRIVATE /UNDEBUG)
add_test(NAME encounter_recovery_contract COMMAND sentinel_encounter_recovery_tests)

foreach(target sentinel_campaign_tests sentinel_native_host sentinel_save_storage_tests)
  target_sources(${target} PRIVATE tests/fault_injection.cpp)
  target_include_directories(${target} PRIVATE tests third_party/minhook/include)
  target_compile_definitions(${target} PRIVATE SC_FAULT_REDIRECT)
  target_compile_options(${target} PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/tests/fault_injection.h")
endforeach()
target_compile_definitions(sentinel_campaign_tests PRIVATE SC_FAULT_HOOK_RUNNER)
foreach(installer native ap_controls arsenal campaign_menu challenge combat deathlink enemy_checkpoint expanded save save_campaign special weapon_points)
  add_test(NAME test_partial_hook_installation_releases_owned_hooks_${installer}
    COMMAND sentinel_campaign_tests create "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/campaign-fixtures/hook_cleanup" 1 "hook_cleanup-${installer}")
  set_tests_properties(test_partial_hook_installation_releases_owned_hooks_${installer} PROPERTIES TIMEOUT 180)
  set_tests_properties(test_partial_hook_installation_releases_owned_hooks_${installer} PROPERTIES LABELS "hook-cleanup")
endforeach()
foreach(pair "stop-queued;test_stop_cancels_queued_work" "stop-claimed;test_stop_blocks_claimed_work"
             "stop-executing;test_stop_during_execution_preserves_terminal_result"
             "wrong-command-thread;test_wrong_thread_cannot_execute" "partial-seh;test_partial_mutation_fault_does_not_report_success")
  list(GET pair 0 mode)
  list(GET pair 1 behavior)
  add_test(NAME ${behavior} COMMAND sentinel_native_host "$<TARGET_FILE:sentinel_probe>" ${mode})
  set_tests_properties(${behavior} PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME test_retained_hooks_only_forward_after_stop COMMAND sentinel_campaign_tests create "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/campaign-fixtures/expanded_stop" 1 expanded_stop)
add_test(NAME test_interrupted_metadata_blocks_unsafe_resume COMMAND sentinel_save_storage_tests --metadata)
add_test(NAME checkpoint_reader_plan_contract COMMAND sentinel_save_storage_tests --checkpoint-plan)
add_test(NAME checkpoint_probe_archive_contract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint_probe.py"
  "$<TARGET_FILE:sentinel_campaign_tests>" "$<TARGET_FILE:sentinel_probe>" "${CMAKE_CURRENT_SOURCE_DIR}/.tmp")
set_tests_properties(checkpoint_probe_archive_contract PROPERTIES TIMEOUT 180)
add_test(NAME checkpoint_archive_crash_contract COMMAND sentinel_save_storage_tests --archive-faults)
set_tests_properties(checkpoint_archive_crash_contract PROPERTIES TIMEOUT 120)
set_tests_properties(test_interrupted_metadata_blocks_unsafe_resume PROPERTIES TIMEOUT 90)
add_test(NAME previous_checkpoint_generation COMMAND sentinel_save_storage_tests --previous-checkpoint)
set_tests_properties(previous_checkpoint_generation PROPERTIES TIMEOUT 30 LABELS "two-generation-checkpoint")
add_test(NAME checkpoint_protocol_crash_contract COMMAND sentinel_save_storage_tests --protocol-faults)
set_tests_properties(checkpoint_protocol_crash_contract PROPERTIES TIMEOUT 180)
add_test(NAME checkpoint_recovery_resume_contract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint_resume.py"
  "$<TARGET_FILE:sentinel_campaign_tests>" "${CMAKE_CURRENT_SOURCE_DIR}/.tmp")
set_tests_properties(checkpoint_recovery_resume_contract PROPERTIES TIMEOUT 180)
add_test(NAME protocol_generator_contract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
  -s tests -p test_generate_protocol.py -q)
set_tests_properties(protocol_generator_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(sentinel_currency_tests tests/currencies.cpp)
target_link_libraries(sentinel_currency_tests PRIVATE sentinel_native_model)
target_compile_options(sentinel_currency_tests PRIVATE /UNDEBUG)
add_test(NAME currency_evidence_contract COMMAND sentinel_currency_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire/evidence.json")
add_test(NAME evidence_wire_python_contract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s tests -p test_evidence_wire.py -q)
set_tests_properties(evidence_wire_python_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(sentinel_progression_tests tests/progression.cpp)
target_link_libraries(sentinel_progression_tests PRIVATE sentinel_native_model)
target_compile_options(sentinel_progression_tests PRIVATE /UNDEBUG)
add_test(NAME progression_evidence_contract COMMAND sentinel_progression_tests)
add_executable(sentinel_mission_access_tests tests/mission_access.cpp)
target_link_libraries(sentinel_mission_access_tests PRIVATE sentinel_native_model)
target_compile_options(sentinel_mission_access_tests PRIVATE /UNDEBUG)
add_test(NAME mission_access_evidence_contract COMMAND sentinel_mission_access_tests)
add_executable(sentinel_evidence_wire_tests tests/evidence_wire.cpp)
target_link_libraries(sentinel_evidence_wire_tests PRIVATE sentinel_native_model)
target_compile_options(sentinel_evidence_wire_tests PRIVATE /UNDEBUG)
add_test(NAME evidence_wire_cpp_contract COMMAND sentinel_evidence_wire_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire/evidence_results.json")
get_property(sc_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${sc_tests} PROPERTIES ENVIRONMENT
  "TMP=${CMAKE_CURRENT_SOURCE_DIR}/.tmp;TEMP=${CMAKE_CURRENT_SOURCE_DIR}/.tmp;LOCALAPPDATA=${CMAKE_CURRENT_SOURCE_DIR}/.tmp")
