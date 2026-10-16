# Unit tests (QtTest) for the logic that needs no hardware. Each test runs in its own temporary
# HOME and working directory (TestHome.hpp), the shell is not started.
find_package(Qt6 REQUIRED COMPONENTS Test)

function(openhu_test name)
    add_executable(${name} ${name}.cpp TestHome.hpp English.hpp)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # the qm files of the shell (top level CMakeLists.txt): English.hpp
    add_dependencies(${name} openhu_qm)
    target_compile_definitions(${name} PRIVATE OPENHU_QM_DIR="${CMAKE_BINARY_DIR}/i18n")
    target_link_libraries(${name} PRIVATE openhu_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endfunction()

openhu_test(test_eventbus)
openhu_test(test_instancelock)
openhu_test(test_childprocesses)
openhu_test(test_configuration)
openhu_test(test_actions)
openhu_test(test_audiofocus)
openhu_test(test_videofocus)
openhu_test(test_api)
openhu_test(test_pluginstore)
openhu_test(test_video)
openhu_test(test_inputdevice)
openhu_test(test_volume)
openhu_test(test_shellcontroller)
openhu_test(test_systemcontroller)
openhu_test(test_video_decoder)
openhu_test(test_translations)
openhu_test(test_wificlient)
openhu_test(test_radio)
openhu_test(test_usbwait)
openhu_test(test_ducking)
# a plugin translation
set(test_plugin_qm ${CMAKE_CURRENT_BINARY_DIR}/plugin_de.qm)
add_custom_command(OUTPUT ${test_plugin_qm}
                   COMMAND ${OPENHU_LRELEASE} -silent -idbased ${CMAKE_CURRENT_SOURCE_DIR}/data/plugin_de.ts -qm ${test_plugin_qm}
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/plugin_de.ts VERBATIM)
add_custom_target(test_translations_qm DEPENDS ${test_plugin_qm})
add_dependencies(test_translations test_translations_qm)
target_compile_definitions(test_translations PRIVATE OPENHU_TEST_QM="${test_plugin_qm}")
target_link_libraries(test_video_decoder PRIVATE ${CMAKE_DL_LIBS})
