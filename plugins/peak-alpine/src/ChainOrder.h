#pragma once

#include <array>

namespace ee::alpine::chainOrder
{

/**
 * The four modules Peak Alpine chains, and the Lehmer-code (factorial number
 * system) that packs one ordering of them into a single 0-23 `chain.order`
 * parameter - see Params.h and PluginProcessor.cpp's createParameterLayout.
 *
 * A single choice parameter rather than one per slot: the UI always hands
 * over a whole new order in one gesture (a drag-drop), so one atomic write
 * matches that, and there is no way for it to land on an invalid ordering -
 * every one of the 24 indices decodes to a genuine permutation of the four
 * modules, so nothing has to validate that four separate slot parameters
 * still agree with each other.
 *
 * `permutationForIndex`/`indexForPermutation` are exact inverses, and index 0
 * decodes to `{ moduleArtifact, moduleModulation, moduleDelay, moduleReverb }`
 * by construction (see the digit-by-digit trace in either function) - today's
 * fixed chain order, so a session or preset that predates this parameter
 * keeps playing back exactly as it always has.
 */
enum ModuleId : int
{
    moduleArtifact = 0,
    moduleModulation = 1,
    moduleDelay = 2,
    moduleReverb = 3
};

inline constexpr int kNumModules = 4;
inline constexpr int kNumPermutations = 24; // 4!

// (kNumModules - 1)! down to 0!, the place values of a Lehmer code for N = 4.
inline constexpr int kPlaceValue[kNumModules] = { 6, 2, 1, 1 };

/** Lehmer decode: index (0-23, clamped) -> an ordering of the four modules. */
inline std::array<int, kNumModules> permutationForIndex (int index) noexcept
{
    if (index < 0)
        index = 0;
    if (index > kNumPermutations - 1)
        index = kNumPermutations - 1;

    std::array<int, kNumModules> pool { 0, 1, 2, 3 };
    std::array<int, kNumModules> order {};
    int poolSize = kNumModules;

    for (int slot = 0; slot < kNumModules; ++slot)
    {
        const int digit = index / kPlaceValue[slot];
        index -= digit * kPlaceValue[slot];

        order[static_cast<size_t> (slot)] = pool[static_cast<size_t> (digit)];

        for (int i = digit; i < poolSize - 1; ++i)
            pool[static_cast<size_t> (i)] = pool[static_cast<size_t> (i + 1)];
        --poolSize;
    }

    return order;
}

/** Lehmer encode: an ordering of the four modules -> its index (0-23). Inverse of permutationForIndex. */
inline int indexForPermutation (const std::array<int, kNumModules>& order) noexcept
{
    std::array<int, kNumModules> pool { 0, 1, 2, 3 };
    int poolSize = kNumModules;
    int index = 0;

    for (int slot = 0; slot < kNumModules; ++slot)
    {
        int digit = 0;
        while (digit < poolSize && pool[static_cast<size_t> (digit)] != order[static_cast<size_t> (slot)])
            ++digit;

        index += digit * kPlaceValue[slot];

        for (int i = digit; i < poolSize - 1; ++i)
            pool[static_cast<size_t> (i)] = pool[static_cast<size_t> (i + 1)];
        --poolSize;
    }

    return index;
}

} // namespace ee::alpine::chainOrder
