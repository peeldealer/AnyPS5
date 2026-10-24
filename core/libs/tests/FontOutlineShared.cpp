#include "prx/libSceFont/include/FontDriver.hpp"
#include "tests/FontFixture.hpp"
#ifdef ANYPS5_FONT_LOCK_OBSERVER
#include "tests/FontLockObserver.hpp"
#endif
#include <array>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI sceFontMemoryInit(FontMemory*, void*, std::uint32_t, const FontMemoryInterface*, void*, FontMemoryDestroyFunction, void*);
int APS5_VABI sceFontCreateLibrary(const FontMemory*, const void*, FontLibrary*);
int APS5_VABI sceFontDestroyLibrary(FontLibrary*);
int APS5_VABI sceFontSupportSystemFonts(FontLibrary);
int APS5_VABI sceFontOpenFontSet(FontLibrary, std::uint32_t, std::uint32_t, const FontOpenDetail*, FontHandle*);
int APS5_VABI sceFontOpenFontInstance(FontHandle, FontHandle, FontHandle*);
int APS5_VABI sceFontCloseFont(FontHandle);
int APS5_VABI sceFontSetScalePixel(FontHandle, float, float);
int APS5_VABI sceFontGetCharGlyphMetrics(FontHandle, std::uint32_t, FontGlyphMetrics*);
int APS5_VABI sceFontGenerateCharGlyph(FontHandle, std::uint32_t, const FontGenerateGlyphDetail*, FontGlyph*);
int APS5_VABI sceFontDeleteGlyph(const FontMemory*, FontGlyph*);
FontGlyphOutline* APS5_VABI sceFontGlyphRefersOutline(FontGlyph);
const Font::SysDriver* APS5_VABI sceFontSelectLibraryFt(int);
}

static void Require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "Shared font outline: %s\n", message);
        std::abort();
    }
}

static void* APS5_VABI Allocate(void*, std::uint32_t size) { return std::malloc(size); }
static void APS5_VABI Release(void*, void* pointer) { std::free(pointer); }

static FontGlyph Generate(FontHandle font, std::uint32_t code) {
    FontGenerateGlyphDetail detail{};
    detail.id = 0x0FD3;
    detail.glyph_form = 1;
    detail.metrics_form = 1;
    FontGlyph glyph = nullptr;
    Require(sceFontGenerateCharGlyph(font, code, &detail, &glyph) == SCE_FONT_OK && glyph, "generate glyph");
    return glyph;
}

static bool Matches(const FontGlyphOutline* outline, int width) {
    if (!outline || outline->points_cnt != 4 || outline->contours_cnt != 1 || !outline->points_ptr
        || !outline->tags_ptr || !outline->contour_end_idx || outline->contour_end_idx[0] != 3) return false;
    const auto position = [](int units) { return static_cast<float>((units * 2048 + 500) / 1000) / 64.0f; };
    const std::array<FontGlyphOutlinePoint, 4> expected = {{{position(100), 0}, {position(100), position(700)},
        {position(width - 100), position(700)}, {position(width - 100), 0}}};
    for (unsigned i = 0; i < expected.size(); ++i) {
        if (outline->points_ptr[i].x != expected[i].x || outline->points_ptr[i].y != expected[i].y || outline->tags_ptr[i] != 1) return false;
    }
    return true;
}

#ifdef ANYPS5_FONT_LOCK_OBSERVER
static void CheckBlocking(FontHandle font, std::uint32_t code, int width) {
    Require(sceFontSetScalePixel(font, 32, 32) == SCE_FONT_OK, "blocking scale");
    FontGlyph glyph = Generate(font, code);
    auto* native = reinterpret_cast<Font::FontHandleNative*>(font);
    const std::uint32_t previous = std::atomic_ref(native->lock_word).fetch_or(Font::LOCK_BIT, std::memory_order_acq_rel);
    Require((previous & Font::LOCK_BIT) == 0, "owner initially unlocked");
    FontTestResetLockObservation_nid_no_patch();
    FontGlyphOutline* outline = nullptr;
    std::thread worker([&] {
        FontTestWatchOwnerLock_nid_no_patch(&native->lock_word);
        outline = sceFontGlyphRefersOutline(glyph);
        FontTestCompleteLockObservation_nid_no_patch();
    });
    const unsigned observed = FontTestWaitLockObservation_nid_no_patch();
    std::fprintf(stderr, "shared code=0x%X owner_lock_observation=%u\n", code, observed);
    Require(observed == FONT_TEST_CONTENDED, "shared outline completed without observing real owner-lock contention");
    Require((std::atomic_ref(native->lock_word).load(std::memory_order_acquire) & Font::LOCK_BIT) != 0, "shared owner still held at observation");
    std::atomic_ref(native->lock_word).store(previous, std::memory_order_release);
    worker.join();
    std::printf("shared code=0x%X owner_contention_observed=%u\n", code, observed);
    Require(Matches(outline, width), "shared outline exact");
    Require(sceFontGlyphRefersOutline(glyph) == outline, "cached outline");
    Require(sceFontDeleteGlyph(nullptr, &glyph) == SCE_FONT_OK && !glyph, "delete blocking glyph");
}

#endif

static void CheckContention(FontHandle font) {
    constexpr unsigned iterations = 1000;
    Require(sceFontSetScalePixel(font, 32, 32) == SCE_FONT_OK, "outline scale");
    std::array<std::vector<FontGlyph>, 2> glyphs;
    for (unsigned face = 0; face < glyphs.size(); ++face) {
        for (unsigned i = 0; i < iterations; ++i) glyphs[face].push_back(Generate(font, face == 0 ? 'A' : 0x4E00));
    }
    Require(sceFontSetScalePixel(font, 128, 128) == SCE_FONT_OK, "metric scale");
    std::barrier start(5);
    std::atomic<unsigned> failures = 0;
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker] {
            start.arrive_and_wait();
            for (unsigned i = 0; i < iterations; ++i) {
                if (worker < 2) {
                    const FontGlyph glyph = glyphs[worker][i];
                    const auto* outline = sceFontGlyphRefersOutline(glyph);
                    if (!Matches(outline, worker == 0 ? 600 : 900) || sceFontGlyphRefersOutline(glyph) != outline) ++failures;
                } else {
                    FontGlyphMetrics metrics{};
                    const bool primary = worker == 2;
                    if (sceFontGetCharGlyphMetrics(font, primary ? 'A' : 0x4E00, &metrics) != SCE_FONT_OK
                        || metrics.Horizontal.advance != static_cast<float>(primary ? 4915 : 7373) / 64.0f
                        || metrics.height != 5734.0f / 64.0f) ++failures;
                }
            }
        });
    }
    start.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    for (auto& face : glyphs) for (auto& glyph : face) Require(sceFontDeleteGlyph(nullptr, &glyph) == SCE_FONT_OK, "delete stressed glyph");
    std::printf("shared outline_and_metric_failures=%u threads=4 iterations_per_thread=%u\n", failures.load(), iterations);
    Require(failures == 0, "shared primary and fallback contention");
}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "all";
#ifdef ANYPS5_FONT_LOCK_OBSERVER
    Require(mode == "all" || mode == "primary-blocking" || mode == "fallback-blocking", "test mode");
#else
    Require(mode == "all", "test mode");
#endif
    std::filesystem::path directory;
    for (unsigned attempt = 0; attempt < 10 && directory.empty(); ++attempt) {
        const auto candidate = std::filesystem::temp_directory_path() / ("anyps5-outline-" + std::to_string(std::random_device{}()));
        if (std::filesystem::create_directory(candidate)) directory = candidate;
    }
    Require(!directory.empty(), "exclusive fixture directory");
    const auto write = [&](const char* name, std::uint32_t code, int width) {
        const auto bytes = FontFixture::SquareGlyphFont(code, width);
        std::ofstream file(directory / name, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(file.good(), "fixture write");
    };
    write("SST-Roman.otf", 'A', 600);
    write("SSTJpPro-Regular.otf", 0x4E00, 900);
#ifdef _WIN32
    Require(_putenv_s("ANYPS5_SYSTEM_FONTS", directory.string().c_str()) == 0, "font directory");
#else
    Require(setenv("ANYPS5_SYSTEM_FONTS", directory.string().c_str(), 1) == 0, "font directory");
#endif
    const FontMemoryInterface iface{Allocate, Release, nullptr, nullptr, nullptr, nullptr};
    FontMemory memory{};
    Require(sceFontMemoryInit(&memory, nullptr, 0, &iface, nullptr, nullptr, nullptr) == SCE_FONT_OK, "memory init");
    FontLibrary library = nullptr;
    Require(sceFontCreateLibrary(&memory, sceFontSelectLibraryFt(0), &library) == SCE_FONT_OK, "library init");
    Require(sceFontSupportSystemFonts(library) == SCE_FONT_OK, "system fonts");
    FontHandle source = nullptr;
    Require(sceFontOpenFontSet(library, 0x18070444u, 1, nullptr, &source) == SCE_FONT_OK, "open primary and fallback set");
    FontHandle instance = nullptr;
    Require(sceFontOpenFontInstance(source, nullptr, &instance) == SCE_FONT_OK, "open instance");
#ifdef ANYPS5_FONT_LOCK_OBSERVER
    if (mode != "fallback-blocking") CheckBlocking(source, 'A', 600);
    if (mode != "primary-blocking") CheckBlocking(instance, 0x4E00, 900);
#endif
    if (mode == "all") {
        CheckContention(source);
        CheckContention(instance);
    }
    Require(sceFontCloseFont(source) == SCE_FONT_OK, "source close");
    if (mode == "all") CheckContention(instance);
    Require(sceFontCloseFont(instance) == SCE_FONT_OK, "instance close");
    Require(sceFontDestroyLibrary(&library) == SCE_FONT_OK, "library close");
    std::filesystem::remove_all(directory);
}
