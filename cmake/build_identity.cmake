# Always check Git on an incremental build, not just at CMake configuration.
# The generator changes the header only when provenance changes.
add_custom_target(mt_build_identity
    COMMAND python3 -B ${CMAKE_SOURCE_DIR}/tools/generate_build_identity.py
        --source ${CMAKE_SOURCE_DIR}
        --output ${CMAKE_BINARY_DIR}/generated/git_identity.h
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/git_identity.h
    VERBATIM)
# Inject metadata without making portable application headers depend on a
# generated file or a board-specific include path. Standalone logic compiles
# remain valid and explicitly identify provenance as unknown.
add_compile_options("$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_BINARY_DIR}/generated/git_identity.h>")
