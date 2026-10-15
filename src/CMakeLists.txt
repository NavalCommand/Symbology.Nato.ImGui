find_package(imgui CONFIG REQUIRED)

add_library(NavalCommand.Symbology.Nato.ImGui STATIC
    NavalCommand/Symbology/Nato/ImGui/Renderer.cpp)
target_include_directories(NavalCommand.Symbology.Nato.ImGui PUBLIC "${PROJECT_SOURCE_DIR}/public")
target_link_libraries(NavalCommand.Symbology.Nato.ImGui PUBLIC NavalCommand.Symbology.Nato.Api PRIVATE imgui::imgui)
NavalCommandConfigureTarget(NavalCommand.Symbology.Nato.ImGui)
