# Strict warnings for our own (non-JUCE) code.
# GCC/Clang get -Wconversion so that implicit double->float / int narrowing is
# caught on Linux/macOS too (MSVC reports those as C4244/C4267 under /W4).
function(ab_set_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
            -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion)
    endif()
endfunction()
