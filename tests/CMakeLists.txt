add_executable(vulkan_probe vulkan_probe.cpp)
target_link_libraries(vulkan_probe PRIVATE Vulkan::Vulkan)
add_test(NAME vulkan_probe COMMAND vulkan_probe)

add_executable(wayland_smoke wayland_smoke.cpp)
target_link_libraries(wayland_smoke PRIVATE PkgConfig::WAYLAND_SERVER)
add_test(NAME wayland_smoke COMMAND wayland_smoke)

add_executable(ev_smoke ev_smoke.cpp)
target_link_libraries(ev_smoke PRIVATE ev::ev)
add_test(NAME ev_smoke COMMAND ev_smoke)

add_executable(client_shm client_shm.c)
target_link_libraries(client_shm PRIVATE protocols PkgConfig::WAYLAND_CLIENT)

add_test(NAME headless_shm
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_shm.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_shm>)
set_tests_properties(headless_shm PROPERTIES TIMEOUT 60)

add_executable(client_subsurface client_subsurface.c)
target_link_libraries(client_subsurface PRIVATE protocols PkgConfig::WAYLAND_CLIENT)

add_test(NAME headless_subsurface
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_subsurface.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_subsurface>)
set_tests_properties(headless_subsurface PROPERTIES TIMEOUT 60)

add_executable(client_viewport client_viewport.c)
target_link_libraries(client_viewport PRIVATE protocols PkgConfig::WAYLAND_CLIENT)

add_test(NAME headless_viewport
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_viewport.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_viewport>)
set_tests_properties(headless_viewport PROPERTIES TIMEOUT 60)

add_executable(client_popup client_popup.c)
target_link_libraries(client_popup PRIVATE protocols PkgConfig::WAYLAND_CLIENT)

add_test(NAME headless_popup
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_popup.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_popup>)
set_tests_properties(headless_popup PROPERTIES TIMEOUT 60)

add_executable(client_dmabuf client_dmabuf.c)
target_link_libraries(client_dmabuf PRIVATE protocols PkgConfig::WAYLAND_CLIENT)

add_test(NAME headless_dmabuf
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_dmabuf.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_dmabuf>)
set_tests_properties(headless_dmabuf PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 127)

add_test(NAME headless_input
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_input.sh $<TARGET_FILE:imway>)
set_tests_properties(headless_input PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 127)

add_test(NAME headless_clipboard
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_clipboard.sh $<TARGET_FILE:imway>)
set_tests_properties(headless_clipboard PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 127)

add_executable(client_sigbus client_sigbus.c)
target_link_libraries(client_sigbus PRIVATE protocols PkgConfig::WAYLAND_CLIENT)
add_test(NAME headless_sigbus
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/headless_sigbus.sh
            $<TARGET_FILE:imway> $<TARGET_FILE:client_sigbus>)
set_tests_properties(headless_sigbus PROPERTIES TIMEOUT 60)
