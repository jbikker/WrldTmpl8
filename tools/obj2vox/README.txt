This tool converts an obj file to a .vox file, ready to be used in 
MagicaVoxel. The obj file may have textures, and a palette will be
automatically generated.

To use the tool:

By default, the tool loads data/lego/legocar.obj and converts it to
a 127x127x127 (max) voxel object.

To use a different obj file:

Run obj2vox.exe in a folder that contains an obj file, and drag the
obj file onto the executable.

To use a different voxel object size:

Run obj2vox.exe from the commandline; the first argument is the obj
file, the second the voxel oject size (1..255).

Note that the tool does not do a lot of error checking. It 'works
on my machine'. :)

Source code is public domain. Original can be found at
https://github.com/jbikker/WrldTmpl8
where it is distributed with the Voxel World Template project.

questions / remarks: bikker.j@gmail.com.
