#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <xmmintrin.h>
    #define AB_DENORMALS_X86 1
#elif (defined(__aarch64__) || defined(__arm64__)) && (defined(__GNUC__) || defined(__clang__))
    #define AB_DENORMALS_ARM64 1
#endif

namespace ab::dsp
{

/** Enables flush-to-zero / denormals-are-zero for the current scope and restores the previous
    state afterwards. The engine uses it itself so decaying tails never fall into the (very
    slow) denormal range, whatever the host does. */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if AB_DENORMALS_X86
        previous = _mm_getcsr();
        _mm_setcsr (previous | 0x8040u); // FTZ | DAZ
#elif AB_DENORMALS_ARM64
        std::uint64_t fpcr = 0;
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(fpcr));
        previous = fpcr;
        fpcr |= (1ull << 24); // FZ
        __asm__ __volatile__ ("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedFlushDenormals() noexcept
    {
#if AB_DENORMALS_X86
        _mm_setcsr (previous);
#elif AB_DENORMALS_ARM64
        __asm__ __volatile__ ("msr fpcr, %0" : : "r"(previous));
#endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
#if AB_DENORMALS_X86
    unsigned int previous = 0;
#elif AB_DENORMALS_ARM64
    std::uint64_t previous = 0;
#endif
};

} // namespace ab::dsp
