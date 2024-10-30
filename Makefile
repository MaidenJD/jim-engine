all: engine.exe base.spv color.spv grid.vert.spv grid.frag.spv

engine.exe: main.c
	cl -Zi -nologo -ISDL3-3.1.3/include -Feengine.exe main.c SDL3-3.1.3/lib/x64/SDL3.lib

base.spv: base.vert
	glslang base.vert -o base.spv -V -g

color.spv: color.frag
	glslang color.frag -o color.spv -V -g

grid.vert.spv: grid.vert
	glslang grid.vert -o grid.vert.spv -V -g

grid.frag.spv: grid.frag
	glslang grid.frag -o grid.frag.spv -V -g
