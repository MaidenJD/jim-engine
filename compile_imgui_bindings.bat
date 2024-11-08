@echo off
pushd dear_bindings
if not exist lib (
    mkdir lib
)
call BuildAllBindings.bat
cl -Zi -c -Fo"./lib/" -I"../imgui" -I"../imgui/backends" -I"../SDL/include" -I"./generated" ./generated/*.cpp ./generated/backends/dcimgui_impl_sdl3.cpp ../imgui/*.cpp ../imgui/backends/imgui_impl_sdl3.cpp
lib /OUT:".\lib\dcimgui.lib" ./lib/*.obj
popd