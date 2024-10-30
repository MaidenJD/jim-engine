all: engine.exe base.spv color.spv grid.vert.spv grid.frag.spv

engine.exe: main.c ./SDL/VisualC/x64/Debug/SDL3.dll ./SDL3.dll ./SDL/VisualC/x64/Debug/SDL3.lib
	copy .\SDL\VisualC\x64\Debug\SDL3.dll .\SDL3.dll
	cl -Zi -nologo -ISDL/include -IHandmadeMath -Feengine.exe main.c SDL/VisualC/x64/Debug/SDL3.lib

base.spv: base.vert
	glslang base.vert -o base.spv -V -g

color.spv: color.frag
	glslang color.frag -o color.spv -V -g

grid.vert.spv: grid.vert
	glslang grid.vert -o grid.vert.spv -V -g

grid.frag.spv: grid.frag
	glslang grid.frag -o grid.frag.spv -V -g
