#include "scanner.h"

#include "memory.h"

#include <cstring>
#include <sstream>

namespace {

bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

bool MatchesAt(const uint8_t* data, const std::vector<uint8_t>& bytes,
               const std::vector<bool>& mask) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (mask[i] && data[i] != bytes[i]) {
            return false;
        }
    }
    return true;
}

struct ScanContext {
    const std::vector<uint8_t>* bytes = nullptr;
    const std::vector<bool>* mask = nullptr;
    uintptr_t result = 0;
    bool found = false;
};

bool ScanRegion(uintptr_t regionStart, size_t regionSize, void* user) {
    auto* ctx = static_cast<ScanContext*>(user);
    const std::vector<uint8_t>& bytes = *ctx->bytes;
    const std::vector<bool>& mask = *ctx->mask;

    if (regionSize < bytes.size()) {
        return true; // region too small to hold the pattern
    }

    const auto* data = reinterpret_cast<const uint8_t*>(regionStart);
    const size_t last = regionSize - bytes.size();

    for (size_t i = 0; i <= last; ++i) {
        if (MatchesAt(data + i, bytes, mask)) {
            ctx->result = regionStart + i;
            ctx->found = true;
            return false; // stop the walk
        }
    }
    return true;
}

} // namespace

bool pattern::Parse(const std::string& text, std::vector<uint8_t>& bytes,
                    std::vector<bool>& mask) {
    bytes.clear();
    mask.clear();

    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        if (token == "?" || token == "??" || token == "*") {
            bytes.push_back(0);
            mask.push_back(false);
            continue;
        }
        if (token.size() != 2 || !IsHexDigit(token[0]) || !IsHexDigit(token[1])) {
            return false;
        }
        const auto value =
            static_cast<uint8_t>((HexValue(token[0]) << 4) | HexValue(token[1]));
        bytes.push_back(value);
        mask.push_back(true);
    }

    return !bytes.empty();
}

uintptr_t pattern::Find(uintptr_t start, size_t size, const std::vector<uint8_t>& bytes,
                        const std::vector<bool>& mask) {
    if (start == 0 || size == 0 || bytes.empty() || bytes.size() != mask.size()) {
        return 0;
    }

    ScanContext ctx;
    ctx.bytes = &bytes;
    ctx.mask = &mask;
    meml::ForEachReadableRegion(start, size, &ScanRegion, &ctx);
    return ctx.found ? ctx.result : 0;
}

uintptr_t pattern::FindInModule(const wchar_t* moduleName, const std::string& signature) {
    uintptr_t base = 0;
    size_t size = 0;
    if (!meml::GetModuleRange(moduleName, base, size)) {
        return 0;
    }

    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!Parse(signature, bytes, mask)) {
        return 0;
    }

    return Find(base, size, bytes, mask);
}

uintptr_t pattern::FindInModule(const char* moduleName, const std::string& signature) {
    uintptr_t base = 0;
    size_t size = 0;
    if (!meml::GetModuleRange(moduleName, base, size)) {
        return 0;
    }

    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!Parse(signature, bytes, mask)) {
        return 0;
    }

    return Find(base, size, bytes, mask);
}

namespace {

struct InsensitiveContext {
    std::vector<uint8_t> needle; // lowercased, including the terminator
    uintptr_t result = 0;
    bool found = false;
};

struct CandidateContext {
    std::vector<uint8_t> needle; // lowercased, including the terminator
    std::vector<uintptr_t> matches;
};

// Collects every match in a region instead of stopping at the first, so the
// caller can rank them and reject the ones nothing points at.
bool ScanRegionCandidates(uintptr_t regionStart, size_t regionSize, void* user) {
    auto* ctx = static_cast<CandidateContext*>(user);
    const size_t needleSize = ctx->needle.size();
    if (regionSize < needleSize) {
        return true;
    }

    const size_t chunkSize = 1u << 20;
    std::vector<uint8_t> buffer;

    for (size_t offset = 0; offset + needleSize <= regionSize; offset += chunkSize) {
        const size_t remaining = regionSize - offset;
        const size_t take =
            (chunkSize + needleSize - 1) < remaining ? (chunkSize + needleSize - 1) : remaining;

        buffer.resize(take);
        std::memcpy(buffer.data(), reinterpret_cast<const void*>(regionStart + offset), take);
        for (uint8_t& byte : buffer) {
            if (byte >= 'A' && byte <= 'Z') {
                byte = static_cast<uint8_t>(byte + ('a' - 'A'));
            }
        }

        const size_t last = take - needleSize;
        for (size_t i = 0; i <= last; ++i) {
            if (std::memcmp(buffer.data() + i, ctx->needle.data(), needleSize) == 0) {
                ctx->matches.push_back(regionStart + offset + i);
            }
        }
    }
    return true;
}

bool ScanRegionInsensitive(uintptr_t regionStart, size_t regionSize, void* user) {
    auto* ctx = static_cast<InsensitiveContext*>(user);
    const size_t needleSize = ctx->needle.size();
    if (regionSize < needleSize) {
        return true;
    }

    // Work in chunks so a huge region (the 70 MB .data section) never needs a
    // full-size copy. Chunks overlap by needleSize - 1 so a match straddling a
    // boundary is still seen.
    const size_t chunkSize = 1u << 20;
    std::vector<uint8_t> buffer;

    for (size_t offset = 0; offset + needleSize <= regionSize; offset += chunkSize) {
        const size_t remaining = regionSize - offset;
        const size_t take =
            (chunkSize + needleSize - 1) < remaining ? (chunkSize + needleSize - 1) : remaining;

        buffer.resize(take);
        std::memcpy(buffer.data(), reinterpret_cast<const void*>(regionStart + offset), take);
        for (uint8_t& byte : buffer) {
            if (byte >= 'A' && byte <= 'Z') {
                byte = static_cast<uint8_t>(byte + ('a' - 'A'));
            }
        }

        const size_t last = take - needleSize;
        for (size_t i = 0; i <= last; ++i) {
            if (std::memcmp(buffer.data() + i, ctx->needle.data(), needleSize) == 0) {
                ctx->result = regionStart + offset + i;
                ctx->found = true;
                return false;
            }
        }
    }
    return true;
}

} // namespace

uintptr_t pattern::FindInsensitive(uintptr_t start, size_t size, const std::string& name) {
    if (start == 0 || size == 0 || name.empty()) {
        return 0;
    }

    InsensitiveContext ctx;
    ctx.needle.reserve(name.size() + 1);
    for (const char character : name) {
        const auto byte = static_cast<uint8_t>(character);
        ctx.needle.push_back(byte >= 'A' && byte <= 'Z' ? static_cast<uint8_t>(byte + 32) : byte);
    }
    ctx.needle.push_back(0); // require the terminator, so cg_fov cannot match cg_fovScale

    meml::ForEachReadableRegion(start, size, &ScanRegionInsensitive, &ctx);
    return ctx.found ? ctx.result : 0;
}

void pattern::FindCandidates(uintptr_t start, size_t size, const std::string& name,
                             std::vector<uintptr_t>& out) {
    out.clear();
    if (start == 0 || size == 0 || name.empty()) {
        return;
    }

    CandidateContext ctx;
    ctx.needle.reserve(name.size() + 1);
    for (const char character : name) {
        const auto byte = static_cast<uint8_t>(character);
        ctx.needle.push_back(byte >= 'A' && byte <= 'Z' ? static_cast<uint8_t>(byte + 32) : byte);
    }
    ctx.needle.push_back(0);

    meml::ForEachReadableRegion(start, size, &ScanRegionCandidates, &ctx);

    // Rank the matches into four buckets and concatenate them in order, rather
    // than sorting: within a bucket the address order is the useful one, and it
    // keeps the caller's log readable.
    std::vector<uintptr_t> ideal;      // exact case, starts a string
    std::vector<uintptr_t> exactCase;  // exact case, inside a longer string
    std::vector<uintptr_t> startsText; // different case, starts a string
    std::vector<uintptr_t> rest;

    for (const uintptr_t match : ctx.matches) {
        char probe[64] = {};
        const size_t take = name.size() < sizeof(probe) ? name.size() : sizeof(probe) - 1;
        const bool readable = meml::Read(match, probe, take);
        const bool sameCase = readable && std::memcmp(probe, name.data(), take) == 0;

        uint8_t previous = 0;
        const bool beginsString =
            match == start || (meml::Read(match - 1, &previous, 1) && !(previous >= 32 && previous < 127));

        if (sameCase && beginsString) {
            ideal.push_back(match);
        } else if (sameCase) {
            exactCase.push_back(match);
        } else if (beginsString) {
            startsText.push_back(match);
        } else {
            rest.push_back(match);
        }
    }

    out.reserve(ctx.matches.size());
    for (const std::vector<uintptr_t>* bucket : {&ideal, &exactCase, &startsText, &rest}) {
        out.insert(out.end(), bucket->begin(), bucket->end());
    }
}

uintptr_t pattern::ResolveRipRelative(uintptr_t instructionAddress, size_t displacementOffset,
                                      size_t instructionLength) {
    int32_t displacement = 0;
    if (!meml::Read(instructionAddress + displacementOffset, &displacement,
                    sizeof(displacement))) {
        return 0;
    }

    return instructionAddress + instructionLength +
           static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
}
