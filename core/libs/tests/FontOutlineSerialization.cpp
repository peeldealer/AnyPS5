#include "tests/FontFixture.hpp"
#include "tests/FontLockObserver.hpp"
#include "prx/libSceFont/include/FontInternal.hpp"
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static void Require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "Font outline: %s\n", message);
        std::abort();
    }
}

using FontFixture::SquareGlyphFont;

static bool CheckSerialization(std::uint32_t code) {
    Font::FontHandleNative native{};
    native.magic = Font::HANDLE_MAGIC;
    const auto handle = reinterpret_cast<FontHandle>(&native);
    auto& state = Font::ResetState(handle);
    const auto bytes = std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('A', 600));
    Font::LoadStateFace(state, bytes, 0);
    Require(state.face != nullptr, "load fixture");
    Font::AddStateFallbackFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont(0x4E00, 900)), 0);
    Font::GeneratedGlyph glyph{};
    glyph.owner = handle;
    glyph.codepoint = code;
    glyph.glyph.scale_x = 32;
    glyph.glyph.base_scale = 32;
    std::uint32_t previous = 0;
    Require(Font::AcquireFontLock(&native, previous), "acquire owner lock");
    FontTestResetLockObservation_nid_no_patch();
    bool built = false;
    std::thread worker([&] {
        FontTestWatchOwnerLock_nid_no_patch(&native.lock_word);
        built = Font::BuildTrueOutline(glyph);
        FontTestCompleteLockObservation_nid_no_patch();
    });
    const unsigned observed = FontTestWaitLockObservation_nid_no_patch();
    std::fprintf(stderr, "direct code=0x%X owner_lock_observation=%u\n", code, observed);
    Require(observed == FONT_TEST_CONTENDED, "outline completed without observing real owner-lock contention");
    Require((std::atomic_ref(native.lock_word).load(std::memory_order_acquire) & Font::LOCK_BIT) != 0, "owner still held at observation");
    Font::ReleaseFontLock(&native, previous);
    worker.join();
    std::printf("code=0x%X owner_contention_observed=%u outline_built=%d points=%zu\n", code, observed, built, glyph.outlinePoints.size());
    Font::RemoveState(handle);
    return built && glyph.outlinePoints.size() == 4;
}

static bool CheckScaleIsolation(std::uint32_t code, FontHandle existing = nullptr) {
    Font::FontHandleNative native{};
    native.magic = Font::HANDLE_MAGIC;
    native.style.scale_w = 128;
    native.style.scale_h = 128;
    const auto handle = existing ? existing : reinterpret_cast<FontHandle>(&native);
    if (!existing) {
        auto& state = Font::ResetState(handle);
        Font::LoadStateFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('A', 600)), 0);
        Require(state.face != nullptr, "load fixture");
        Font::AddStateFallbackFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont(0x4E00, 900)), 0);
    }
    const auto initialize = [&](Font::GeneratedGlyph& glyph) {
        glyph.owner = handle;
        glyph.codepoint = code;
        glyph.glyph.scale_x = 32;
        glyph.glyph.base_scale = 32;
    };
    Font::GeneratedGlyph expected{};
    initialize(expected);
    Require(Font::BuildTrueOutline(expected), "reference outline");
    FontGlyphMetrics expectedMetrics{};
    Require(Font::GetCharGlyphMetrics(handle, code, &expectedMetrics, false) == SCE_FONT_OK, "reference metrics");
    std::barrier start(5);
    std::atomic<unsigned> outlineFailures = 0;
    std::atomic<unsigned> metricFailures = 0;
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker] {
            start.arrive_and_wait();
            for (unsigned i = 0; i < 10000; ++i) {
                if (worker < 2) {
                    Font::GeneratedGlyph glyph{};
                    initialize(glyph);
                    bool ok = Font::BuildTrueOutline(glyph) && glyph.outlinePoints.size() == expected.outlinePoints.size()
                        && glyph.outlineTags == expected.outlineTags && glyph.outlineContours == expected.outlineContours
                        && glyph.outlineInitialized && glyph.outline.points_ptr == glyph.outlinePoints.data()
                        && glyph.outline.tags_ptr == glyph.outlineTags.data() && glyph.outline.contour_end_idx == glyph.outlineContours.data()
                        && glyph.outline.points_cnt == expected.outline.points_cnt && glyph.outline.contours_cnt == expected.outline.contours_cnt;
                    if (ok) for (std::size_t p = 0; p < glyph.outlinePoints.size(); ++p) {
                        if (glyph.outlinePoints[p].x != expected.outlinePoints[p].x || glyph.outlinePoints[p].y != expected.outlinePoints[p].y) ok = false;
                    }
                    if (!ok) ++outlineFailures;
                } else {
                    FontGlyphMetrics metrics{};
                    if (Font::GetCharGlyphMetrics(handle, code, &metrics, false) != SCE_FONT_OK
                        || std::memcmp(&metrics, &expectedMetrics, sizeof(metrics)) != 0) ++metricFailures;
                }
            }
        });
    }
    start.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    std::printf("code=0x%X real_handle=%d outline_failures=%u metric_failures=%u threads=4 iterations_per_thread=10000\n",
        code, existing != nullptr, outlineFailures.load(), metricFailures.load());
    if (!existing) Font::RemoveState(handle);
    return outlineFailures == 0 && metricFailures == 0;
}

static bool CheckEarlyRelease() {
    const auto code = 'A';
    Font::FontHandleNative native{};
    native.magic = Font::HANDLE_MAGIC;
    const auto handle = reinterpret_cast<FontHandle>(&native);
    native.lock_word = 0x31;
    auto& state = Font::ResetState(handle);
    Font::GeneratedGlyph glyph{};
    glyph.owner = handle;
    glyph.codepoint = 'B';
    glyph.glyph.scale_x = 32;
    glyph.glyph.base_scale = 32;
    const bool missingFace = !Font::BuildTrueOutline(glyph) && native.lock_word == 0x31;
    Font::LoadStateFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('A', 600)), 0);
    Require(state.face != nullptr, "load early-release fixture");
    const bool missingCode = !Font::BuildTrueOutline(glyph) && native.lock_word == 0x31;
    glyph.codepoint = code;
    const bool valid = Font::BuildTrueOutline(glyph) && native.lock_word == 0x31;
    Font::LoadStateFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('A', 600, 0, true)), 0);
    glyph.outlineInitialized = false;
    const bool emptyOutline = !Font::BuildTrueOutline(glyph) && !glyph.outlineInitialized && native.lock_word == 0x31;
    native.magic = 0;
    const bool invalidMagic = !Font::BuildTrueOutline(glyph) && native.lock_word == 0x31;
    Font::RemoveState(handle);
    native.magic = Font::HANDLE_MAGIC;
    const bool missingState = !Font::BuildTrueOutline(glyph);
    glyph.owner = nullptr;
    const bool nullOwner = !Font::BuildTrueOutline(glyph);
    return missingFace && missingCode && valid && emptyOutline && invalidMagic && missingState && nullOwner;
}

extern "C" {
int APS5_VABI sceFontMemoryInit(FontMemory*, void*, std::uint32_t, const FontMemoryInterface*, void*, FontMemoryDestroyFunction, void*);
int APS5_VABI sceFontCreateLibrary(const FontMemory*, const void*, FontLibrary*);
int APS5_VABI sceFontDestroyLibrary(FontLibrary*);
int APS5_VABI sceFontSupportExternalFonts(FontLibrary, std::uint32_t, std::uint32_t);
int APS5_VABI sceFontOpenFontMemory(FontLibrary, const void*, std::uint32_t, const FontOpenDetail*, FontHandle*);
int APS5_VABI sceFontOpenFontInstance(FontHandle, FontHandle, FontHandle*);
int APS5_VABI sceFontCloseFont(FontHandle);
const Font::SysDriver* APS5_VABI sceFontSelectLibraryFt(int);
}
static void* APS5_VABI Allocate(void*, std::uint32_t size) { return std::malloc(size); }
static void APS5_VABI Release(void*, void* pointer) { std::free(pointer); }

static bool CheckInstances() {
    const FontMemoryInterface iface{Allocate, Release, nullptr, nullptr, nullptr, nullptr};
    FontMemory memory{};
    Require(sceFontMemoryInit(&memory, nullptr, 0, &iface, nullptr, nullptr, nullptr) == SCE_FONT_OK, "memory init");
    FontLibrary library = nullptr;
    Require(sceFontCreateLibrary(&memory, sceFontSelectLibraryFt(0), &library) == SCE_FONT_OK, "library init");
    Require(sceFontSupportExternalFonts(library, 2, 0x52) == SCE_FONT_OK, "external fonts");
    const auto bytes = SquareGlyphFont('A', 600);
    FontHandle source = nullptr;
    Require(sceFontOpenFontMemory(library, bytes.data(), bytes.size(), nullptr, &source) == SCE_FONT_OK, "open memory");
    auto* state = Font::TryGetState(source);
    Require(state != nullptr, "source state");
    Font::AddStateFallbackFace(*state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont(0x4E00, 900)), 0);
    FontHandle instance = nullptr;
    Require(sceFontOpenFontInstance(source, nullptr, &instance) == SCE_FONT_OK, "open instance");
    const auto* cloned = Font::TryGetState(instance);
    Require(cloned && cloned->fallbacks.size() == 1, "instance fallback");
    Require(state->face != cloned->face && state->faceData == cloned->faceData, "independent primary shared bytes");
    Require(state->fallbacks[0].face != cloned->fallbacks[0].face && state->fallbacks[0].data == cloned->fallbacks[0].data, "independent fallback shared bytes");
    Font::GeneratedGlyph primary{}, fallback{};
    primary.owner = source; primary.codepoint = 'A'; primary.glyph.scale_x = 32; primary.glyph.base_scale = 32;
    fallback.owner = instance; fallback.codepoint = 0x4E00; fallback.glyph.scale_x = 32; fallback.glyph.base_scale = 32;
    Require(Font::BuildTrueOutline(primary) && Font::BuildTrueOutline(fallback), "primary and fallback outlines");
    Require(primary.outlinePoints.size() == 4 && fallback.outlinePoints.size() == 4, "four outline points");
    Require(primary.outlinePoints[2].x != fallback.outlinePoints[2].x, "distinguishable fallback outline");
    Require(CheckScaleIsolation('A', source) && CheckScaleIsolation(0x4E00, instance), "live instance stress");
    Require(sceFontCloseFont(source) == SCE_FONT_OK, "sequential source close");
    Font::GeneratedGlyph after{};
    after.owner = instance; after.codepoint = 0x4E00; after.glyph.scale_x = 32; after.glyph.base_scale = 32;
    Require(Font::BuildTrueOutline(after) && after.outlinePoints.size() == fallback.outlinePoints.size(), "instance after source close");
    for (std::size_t i=0; i<after.outlinePoints.size(); ++i) Require(std::memcmp(&after.outlinePoints[i], &fallback.outlinePoints[i], sizeof(FontGlyphOutlinePoint)) == 0, "outline retained after close");
    Require(CheckScaleIsolation('A', instance) && CheckScaleIsolation(0x4E00, instance), "surviving instance stress");
    Require(sceFontCloseFont(instance) == SCE_FONT_OK, "instance close");
    Require(sceFontDestroyLibrary(&library) == SCE_FONT_OK, "library close");
    return true;
}
static bool CheckCharmaps(bool fallback) {
    Font::FontHandleNative native{}; native.magic = Font::HANDLE_MAGIC;
    const auto handle = reinterpret_cast<FontHandle>(&native);
    auto& state = Font::ResetState(handle);
    Font::LoadStateFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('A', 600, fallback ? 0 : 0x4E00)), 0);
    if (fallback) Font::AddStateFallbackFace(state, std::make_shared<const std::vector<unsigned char>>(SquareGlyphFont('B', 900, 0x4E00)), 0);
    const FT_Face face = fallback ? state.fallbacks[0].face : state.face;
    Require(face->num_charmaps == 2, "two charmaps");
    Require(FT_Set_Charmap(face, face->charmaps[0]) == 0, "select original charmap");
    const FT_CharMap original = face->charmap;
    Require(FT_Get_Char_Index(face, 0x4E00) == 0, "alternate-only codepoint");
    Font::GeneratedGlyph glyph{}; glyph.owner = handle; glyph.codepoint = 0x4E00; glyph.glyph.scale_x = 32; glyph.glyph.base_scale = 32;
    const bool built = Font::BuildTrueOutline(glyph);
    Require(built && glyph.outlinePoints.size() == 4, "alternate charmap outline");
    Require(face->charmap == original, "charmap restored");
    Require(CheckScaleIsolation(0x4E00, handle), "alternate charmap stress");
    Require(face->charmap == original, "charmap restored after contention");
    Font::RemoveState(handle);
    return true;
}
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "sequential") return CheckEarlyRelease() && CheckInstances() && CheckCharmaps(false) && CheckCharmaps(true) ? 0 : 1;
    if (mode == "primary-blocking") return CheckSerialization('A') ? 0 : 1;
    if (mode == "fallback-blocking") return CheckSerialization(0x4E00) ? 0 : 1;
    if (mode == "primary-stress") return CheckScaleIsolation('A') ? 0 : 1;
    if (mode == "fallback-stress") return CheckScaleIsolation(0x4E00) ? 0 : 1;
    if (mode == "stress") return CheckScaleIsolation('A') && CheckScaleIsolation(0x4E00) ? 0 : 1;
    const bool primary = CheckSerialization('A');
    const bool fallback = CheckSerialization(0x4E00);
    if (mode == "blocking") return primary && fallback ? 0 : 1;
    return primary && fallback && CheckScaleIsolation('A') && CheckScaleIsolation(0x4E00) && CheckEarlyRelease() && CheckInstances() && CheckCharmaps(false) && CheckCharmaps(true) ? 0 : 1;
}
