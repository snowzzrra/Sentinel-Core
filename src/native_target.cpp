#include "native_target.h"
#include "hde/hde64.h"
#include <algorithm>
#include <cstring>

namespace sentinel::native {
Target profile_target(uintptr_t base, unsigned index) {
    constexpr uint32_t rvas[] = {0x43d1f0, 0x666aa0, 0x668400};
    constexpr const char* bytes[] = {
        "405556415441554156488dac24c0c2ffffb8403e0000e895eb4202482be0488b",
        "48895c24205556574154415541564157488dac24a058fbffb860a80400e8de52",
        "48895c2408574883ec20488bf9488bda488b0a4885c9747f488b01ff5058f640"};
    Target out{}; out.address = base + rvas[index];
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    // Exact 32-byte prefixes retained from the supported Ghidra image.
    for (size_t n = 0; n < std::strlen(bytes[index]) / 2; ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(bytes[index][n * 2]) * 16 + digit(bytes[index][n * 2 + 1]));
    return out;
}
Target save_target(uintptr_t base, unsigned index) {
    constexpr uint32_t rvas[] = {0x66b320, 0x1bd6290, 0x3faff0, 0x1aa2e50, 0x1be33b0, 0x367770,
        0x1be0260, 0x1496d00, 0x1494ae0, 0x17e6c40, 0x35b350, 0x35a130, 0x1ddab90, 0x1bdfbf0,
        0x1bd74f0, 0x1bde680, 0x1bdeea0, 0x14911a0, 0x1bdf290, 0x148dd10,
        0x26ec090, 0x1a4fe20, 0x14902f0, 0x1bd6870, 0x14978e0, 0x1a50a80, 0x1a50080, 0x1bd7a60,
        0x1bd5b10, 0x11c4f30, 0x1ac8d10, 0x6765c0, 0x1bdd0a0, 0x1bde610, 0x1bd8350, 0x1bd6e80,
        0x37d7a0, 0x1bd4b40, 0x1bdc300, 0x6744e0, 0x1495a80};
    constexpr const char* bytes[] = {
        "48895c241048896c2418488974242057415641574881ece0000000488b059636",
        "40555356488dac2400fcffff4881ec00050000488b052e875d024833c4488985",
        "48895c2410488974241848897c242041564883ec304c8bf2488bd94885d20f85",
        "40574883ec2048896c2438488bf933ed48396908764848895c24304889742440",
        "4053415441564883ec40410fb640094d8be0488bda4c8bf184c0741448c70201",
        "48895c2410564883ec20488b19488bf14885db744548897c2430bfffffffff8b",
        "48895c240855565741564157488d6c24b04881ec500100004d8b1133ff4889bd",
        "48895c24184889742420574881eca0000000488b05bf7cd1024833c448898424",
        "48895c24205557415441564157488dac24b0beffffb850420000e8a1723d0148",
        "48895c240848896c2410488974241848897c24204154415641574883ec404c8b",
        "40534883ec308b4108488bda85c0741f83f80774274c8d05141c6f02488d155d",
        "405356574883ec208b4108488bf983f804747b83f8050f8e8900000083f8077e",
        "4883ec384c8bca4c8bd14883fa18772a4533c04883fa0c77184883fa04770948",
        "48895c240855565741564157488d6c24c94881eca0000000498b0133ff48897d",
        "4055535657415441564157488dac2420feffff4881ece0020000488b05c7745d",
        "48895c240855565741564157488d6c24c94881eca0000000498b0133ff48897d",
        "48895c240855565741564157488d6c24c94881eca0000000498b0133ff48897d",
        "40574883ec60488b01488bf94885c0740b48837808000f85b0010000488b0dd5",
        "48895c240855565741564157488dac2450ffffff4881ecb0010000488b0526f7",
        "40555741554156488dac2478e6ffffb8881a0000e877e03d01482be0488b05a5",
        "48895c2408574883ec20488bf9eb1c488b05b212ac014885c0741d488bcfff15",
        "48895c241048896c2418488974242057415641574883ec40488b0599eb750248",
        "4885c9741f534883ec20488bd9e87efd5b00ba80020000488bcbe8f5bd250148",
        "405553574155488d6c24c14881eca8000000488b054f815d024833c448894517",
        "48895c24185556574154415541564157488d6c24f04881ec10010000488b05d5",
        "48895c2410488974241855574156488d6c24f04881ec10010000488b0537df75",
        "48895c240848896c24104889742418574883ec2033ed488bd98bfd39a9c80100",
        "4c8944241848895424105553565741544156488bec4883ec784c8bf1498bd848",
        "4c8bdc55564154415541564157488d6c24984881ec68010000488b05a88e5d02",
        "48895c2418574883ec50488b5940488bf94885db0f84100100008b430485c00f",
        "40534883ec2048897c2438488bd933ff48397908762c488974243033f60f1f00",
        "40534883ec20488bd9488d15a043430233c9ff1580663a024889034883c4205b",
        "48895c241048896c241848897424205741544155415641574883ec304d8b0833",
        "4885c97469534883ec20488b4118488bd94883f8ff742b48c74118ffffffff48",
        "48895c24205556574154415541564157488dac2490feffff4881ec7002000048",
        "4885c9746a534883ec20488b4160488bd94883f8ff742b48c74160ffffffff48",
        "48895c2408574883ec20418bd8488bf9e87bfcffff488d05ec9d6d02899f8001",
        "405341574881ecb8000000488b05869e5d024833c44889842488000000488339",
        "405557415541564157488dac24a0fdffff4881ec60030000488b05b9265d0248",
        "4055535657415441564157488d6c24e14881ec00010000488b05daa4b3034833",
        "48895c241856574156b890380000e80d633d01482be0488b053b8fd1024833c4"};
    Target out{}; out.address = base + rvas[index];
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t n = 0; n < out.bytes.size(); ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(bytes[index][n * 2]) * 16 + digit(bytes[index][n * 2 + 1]));
    if (index == 13 || index == 15 || index == 16) {
        const char* unique = index == 13 ? "8bdf48895d6f488d557f498bc8e83e3e9ffe48897dcf48c7c6ffffffff488975" :
            index == 15 ? "8bdf48895d6f488d557f498bc8e8ae539ffe48897dcf48c7c6ffffffff488975" :
            "8bdf48895d6f488d557f498bc8e88e4b9ffe48897dcf48c7c6ffffffff488975";
        out.signature_offset = 112;
        for (size_t n = 0; n < out.signature.size(); ++n)
            out.signature[n] = static_cast<uint8_t>(digit(unique[n * 2]) * 16 + digit(unique[n * 2 + 1]));
    }
    if (index == 30 || index == 34) {
        const char* unique = index == 30 ? "488b0b33d24803ce488b01ff1048ffc74881c680010000483b7b0872e3488b74" :
            "8b0562665d024833c4488985680100004c8bbdd00100004c8be1488d0df777da";
        out.signature_offset = 32;
        for (size_t n = 0; n < out.signature.size(); ++n)
            out.signature[n] = static_cast<uint8_t>(digit(unique[n * 2]) * 16 + digit(unique[n * 2 + 1]));
    }
    return out;
}
uint32_t validate_target(engine::Memory& memory, const engine::Image& image,
                         const Target& target, HANDLE stop, uint64_t deadline) {
    constexpr size_t signature_size = 32;
    if (target.address < image.base || target.address - image.base > UINT32_MAX ||
        !image.contains(static_cast<uint32_t>(target.address - image.base), 32,
                        IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
        return SC_NATIVE_TARGET_BOUNDARY;
    std::array<uint8_t, 32> actual{};
    if (memory.copy(target.address, actual.data(), actual.size()).reason != SC_REASON_NONE ||
        std::memcmp(actual.data(), target.bytes.data(), signature_size)) return SC_NATIVE_TARGET_BYTES;
    DWORD64 base = 0;
    const auto unwind = RtlLookupFunctionEntry(target.address, &base, nullptr);
    if (!unwind || base != image.base || base + unwind->BeginAddress != target.address)
        return SC_NATIVE_TARGET_BOUNDARY;
    const auto& signature = target.signature_offset ? target.signature : target.bytes;
    if (target.signature_offset) {
        if (static_cast<uint64_t>(target.signature_offset) + signature_size >
            static_cast<uint64_t>(unwind->EndAddress) - unwind->BeginAddress)
            return SC_NATIVE_TARGET_BOUNDARY;
        std::array<uint8_t, 32> actual_signature{};
        if (memory.copy(target.address + target.signature_offset, actual_signature.data(), actual_signature.size()).reason ||
            actual_signature != signature) return SC_NATIVE_TARGET_BYTES;
    }
    size_t length = 0;
    while (length < 5) {
        hde64s instruction{};
        const auto size = hde64_disasm(actual.data() + length, &instruction);
        if (!size || instruction.flags & F_ERROR) return SC_NATIVE_TARGET_BOUNDARY;
        length += size;
    }
    std::array<uint8_t, 65536> chunk{};
    unsigned matches = 0;
    for (const auto& section : image.sections) {
        if (!(section.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
        for (size_t offset = 0; offset + signature_size <= section.size;) {
            if (GetTickCount64() >= deadline || (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT))
                return SC_NATIVE_BUDGET;
            const auto count = std::min(chunk.size(), static_cast<size_t>(section.size) - offset);
            if (memory.copy(image.base + section.rva + offset, chunk.data(), count).reason != SC_REASON_NONE)
                return SC_NATIVE_READ_FAILED;
            auto cursor = chunk.begin(); const auto end = chunk.begin() + count;
            while ((cursor = std::search(cursor, end, signature.begin(), signature.end())) != end) {
                if (++matches > 1) return SC_NATIVE_TARGET_NOT_UNIQUE;
                ++cursor;
            }
            if (count <= signature_size) break;
            offset += count - signature_size + 1;
        }
    }
    return matches == 1 ? SC_NATIVE_NONE : SC_NATIVE_TARGET_NOT_UNIQUE;
}
}
