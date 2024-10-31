all: engine.exe base.spv color.spv grid.vert.spv grid.frag.spv

engine.exe: objzero/objzero.c main.c SDL3.dll SDL/VisualC/x64/Debug/SDL3.lib
	cl -Zi -nologo -ISDL/include -IHandmadeMath -Iobjzero -Feengine.exe main.c objzero/objzero.c SDL/VisualC/x64/Debug/SDL3.lib

SDL3.dll: SDL/VisualC/SDL/x64/Release/SDL3.lib
	copy .\SDL\VisualC\SDL\x64\Release\SDL3.dll .\SDL3.dll

SDL/VisualC/SDL/x64/Release/SDL3.lib: $(shell where /R SDL\src *.*)
	msbuild .\SDL\VisualC\SDL\SDL.vcxproj -p:Configuration=Release -p:Platform=x64

base.spv: base.vert
	glslang base.vert -o base.spv -V -g

color.spv: color.frag
	glslang color.frag -o color.spv -V -g

grid.vert.spv: grid.vert
	glslang grid.vert -o grid.vert.spv -V -g

grid.frag.spv: grid.frag
	glslang grid.frag -o grid.frag.spv -V -g
