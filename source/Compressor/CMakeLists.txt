set(COMPRESSOR_CXX_SOURCES
    AritCode.cpp
    CompressionState.cpp
    CompressionStream.cpp
    Compressor.cpp
    CounterState.cpp
    ModelList.cpp
    Model.cpp
    CompressionStateEvaluator.cpp
)

set_source_files_properties(log_table.obj PROPERTIES EXTERNAL_OBJECT true GENERATED true)

add_library(Compressor ${COMPRESSOR_CXX_SOURCES} log_table.obj)

add_custom_command(TARGET Compressor PRE_BUILD BYPRODUCTS log_table.obj 
    COMMAND ${NASM} ${CMAKE_CURRENT_SOURCE_DIR}/log_table.asm  ${NASM_OPTIONS} -o log_table.obj
    COMMENT "Assembling log_table.asm..."
)
