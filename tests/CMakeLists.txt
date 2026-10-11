# Unit tests (QtTest) for the logic that needs no hardware. Each test runs in its own temporary
# HOME and working directory (TestHome.hpp), the shell is not started.
find_package(Qt6 REQUIRED COMPONENTS Test)

function(openhu_test name)
    add_executable(${name} ${name}.cpp TestHome.hpp)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE openhu_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endfunction()

openhu_test(test_eventbus)
openhu_test(test_configuration)
openhu_test(test_actions)
openhu_test(test_api)
openhu_test(test_pluginstore)
openhu_test(test_video)
