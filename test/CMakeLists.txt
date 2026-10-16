find_package(GTest CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
include(GoogleTest)

add_executable(NavalCommand.Symbology.Nato.ImGui.Test
    NavalCommand/Symbology/Nato/ImGui/RendererTest.cpp)
target_link_libraries(NavalCommand.Symbology.Nato.ImGui.Test PRIVATE
    NavalCommand.Symbology.Nato.ImGui
    imgui::imgui
    GTest::gtest_main)
NavalCommandConfigureTarget(NavalCommand.Symbology.Nato.ImGui.Test)
gtest_discover_tests(NavalCommand.Symbology.Nato.ImGui.Test DISCOVERY_MODE PRE_TEST)
