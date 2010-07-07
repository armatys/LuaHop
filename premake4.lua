solution "LuaHop"
    configurations { "release" }
    flags { "ExtraWarnings", "NoFramePointer", "OptimizeSpeed" }
    buildoptions { "-pedantic" }
    platforms { "native", "x32", "x64" }

project "LuaHop"
    kind "SharedLib"
    language "c"
    location "build"
    files { "src/*.c" }
    excludes { "src/*_hop.c" }
    targetprefix ""
    targetname "luahop"
    
    
    configuration { "macosx" }
        targetdir "build/macosx"
        targetextension ".so"
        linkoptions { "-single_module", "-undefined dynamic_lookup" }
    
    configuration { "linux" }
        includedirs { "/usr/include/lua5.1" }
        targetdir "build/linux"
