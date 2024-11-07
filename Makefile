all: base.spv color.spv grid.vert.spv grid.frag.spv engine.exe

engine.exe: .\objzero\objzero.c main.c SDL3.dll .\dear_bindings\lib\dcimgui.lib
	cl -Zi -nologo -ISDL/include -IHandmadeMath -Iobjzero -Iimgui -Idear_bindings/generated -Feengine.exe main.c objzero\objzero.c .\SDL\VisualC\SDL\x64\Release\SDL3.lib .\dear_bindings\lib\dcimgui.lib

SDL3.dll: $(shell where /R .\SDL\src *.*)
	msbuild .\SDL\VisualC\SDL\SDL.vcxproj -p:Configuration=Release -p:Platform=x64
	copy .\SDL\VisualC\SDL\x64\Release\SDL3.dll .\SDL3.dll

.\dear_bindings\lib\dcimgui.lib: $(shell where /R .\dear_bindings\generated *.*) $(shell where /R .\imgui *.*) .\compile_imgui_bindings.bat
	.\compile_imgui_bindings.bat

base.spv: base.vert
	glslang base.vert -o base.spv -V -g

color.spv: color.frag
	glslang color.frag -o color.spv -V -g

grid.vert.spv: grid.vert
	glslang grid.vert -o grid.vert.spv -V -g

grid.frag.spv: grid.frag
	glslang grid.frag -o grid.frag.spv -V -g
