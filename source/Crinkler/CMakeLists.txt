set(CRINKLER_CXX_SOURCES
    ExplicitHunkSorter.cpp
    Export.cpp
    Fix.cpp
    HtmlReport.cpp
    Hunk.cpp
    HunkList.cpp
    ImportHandler.cpp
    LTCGLoader.cpp
    main.cpp
    Reuse.cpp
    Symbol.cpp
    Log.cpp
    MemoryFile.cpp
    MiniDump.cpp
    Misc.cpp
    NameMangling.cpp
    StringMisc.cpp
    CmdLineInterface/CmdLineInterface.cpp
    CmdLineInterface/CmdParam.cpp
    CmdLineInterface/CmdParamFlags.cpp
    CmdLineInterface/CmdParamInt.cpp
    CmdLineInterface/CmdParamMultiAssign.cpp
    CmdLineInterface/CmdParamString.cpp
    CmdLineInterface/CmdParamSwitch.cpp
    CoffLibraryLoader.cpp
    CoffObjectLoader.cpp
    HunkLoader.cpp
    MultiLoader.cpp
    EmpiricalHunkSorter.cpp
    HeuristicHunkSorter.cpp
    CallTransform.cpp
    Transform.cpp
    CompositeProgressBar.cpp
    ConsoleProgressBar.cpp
    WindowProgressBar.cpp
    Crinkler.cpp

    Crinkler.rc
)

set_source_files_properties(Crinkler.rc LANGUAGE RC)

set(CRINKLER_ASM_SOURCES
    modules/calltrans.asm
    modules/header20.asm
    modules/header20_compatibility.asm
    modules/header22_1k.asm
    modules/import20.asm
    modules/import20_1k.asm
)

set(DATA_OBJECT_FILES
    calltrans.obj
    header20.obj
    header20_compatibility.obj
    header22_1k.obj
    import20.obj
    import20-range.obj
    import20-safe.obj
    import20-safe-range.obj
    import20-safe-fallback.obj
    import20-safe-fallback-range.obj
    import20_1k.obj
    runtime.obj
)
set_source_files_properties(${DATA_OBJECT_FILES} PROPERTIES EXTERNAL_OBJECT true GENERATED true)
set_source_files_properties(data.obj PROPERTIES EXTERNAL_OBJECT true GENERATED true)

add_executable(Crinkler ${CRINKLER_CXX_SOURCES} data.obj)
target_link_libraries(Crinkler Compressor distorm Dbghelp comctl32)

add_custom_command(TARGET Crinkler PRE_BUILD BYPRODUCTS ${DATA_OBJECT_FILES}
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/calltrans.asm" ${NASM_OPTIONS} -o calltrans.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/header20.asm" ${NASM_OPTIONS} -o header20.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/header20.asm" ${NASM_OPTIONS} -o header20.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/header20_compatibility.asm" ${NASM_OPTIONS} -o header20_compatibility.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/header22_1k.asm" ${NASM_OPTIONS} -o header22_1k.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" ${NASM_OPTIONS} -o import20.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" -dIMPORT_RANGE ${NASM_OPTIONS} -o import20-range.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" -dIMPORT_SAFE ${NASM_OPTIONS} -o import20-safe.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" -dIMPORT_SAFE -dIMPORT_RANGE ${NASM_OPTIONS} -o import20-safe-range.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" -dIMPORT_SAFE -dIMPORT_FALLBACK ${NASM_OPTIONS} -o import20-safe-fallback.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20.asm" -dIMPORT_SAFE -dIMPORT_FALLBACK -dIMPORT_RANGE ${NASM_OPTIONS} -o import20-safe-fallback-range.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/import20_1k.asm" ${NASM_OPTIONS} -o import20_1k.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/modules/runtime.asm" ${NASM_OPTIONS} -o runtime.obj
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/known_dll_exports.dat" "${CMAKE_CURRENT_BINARY_DIR}/known_dll_exports.dat"
    COMMENT "Assembling ${CRINKLER_ASM_SOURCES}..."
)

add_custom_command(TARGET Crinkler PRE_BUILD BYPRODUCTS data.obj
    COMMAND ${NASM} "${CMAKE_CURRENT_SOURCE_DIR}/data.asm" -dPATH="${CMAKE_CURRENT_BINARY_DIR}/" ${NASM_OPTIONS} -o data.obj
)
