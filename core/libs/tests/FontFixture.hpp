#ifndef CORE_LIBS_TESTS_FONTFIXTURE_HPP
#define CORE_LIBS_TESTS_FONTFIXTURE_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace FontFixture {

inline void Put16(std::vector<unsigned char>& out, int value) {
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(value & 0xFF));
}

inline std::vector<unsigned char> Words(std::initializer_list<int> values) {
    std::vector<unsigned char> out;
    for (const int value : values) Put16(out, value);
    return out;
}

inline std::vector<unsigned char> BitmapFont() {
    const std::string_view font =
        "STARTFONT 2.1\n"
        "FONT -anyps5-fixture-medium-r-normal--1-10-75-75-c-10-iso10646-1\n"
        "SIZE 1 75 75\n"
        "FONTBOUNDINGBOX 1 1 0 0\n"
        "STARTPROPERTIES 4\n"
        "FONT_ASCENT 1\n"
        "FONT_DESCENT 0\n"
        "CHARSET_REGISTRY \"ISO10646\"\n"
        "CHARSET_ENCODING \"1\"\n"
        "ENDPROPERTIES\n"
        "CHARS 1\n"
        "STARTCHAR A\n"
        "ENCODING 65\n"
        "SWIDTH 1000 0\n"
        "DWIDTH 1 0\n"
        "BBX 1 1 0 0\n"
        "BITMAP\n"
        "80\n"
        "ENDCHAR\n"
        "ENDFONT\n";
    return {font.begin(), font.end()};
}

inline std::vector<unsigned char> BuildFont(int glyphCount, std::map<std::string, std::vector<unsigned char>> tables) {
    tables["glyf"] = Words({0, 0});
    tables["head"] = Words({1, 0, 1, 0, 0, 0, 0x5F0F, 0x3CF5, 0, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0});
    tables["hhea"] = Words({1, 0, 800, -200, 0, 500, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1});
    tables["hmtx"] = Words({500});
    tables["hmtx"].resize(2 + 2 * static_cast<std::size_t>(glyphCount));
    tables["loca"].assign(2 * (static_cast<std::size_t>(glyphCount) + 1), 0);
    tables["maxp"] = Words({1, 0, glyphCount, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
    const int tableCount = static_cast<int>(tables.size());
    int entrySelector = 0;
    while ((2 << entrySelector) <= tableCount) ++entrySelector;
    std::vector<unsigned char> font = Words({1, 0, tableCount, 16 << entrySelector, entrySelector, 16 * tableCount - (16 << entrySelector)});
    int offset = 12 + 16 * tableCount;
    for (const auto& table : tables) {
        font.insert(font.end(), table.first.begin(), table.first.end());
        for (const int value : {0, 0, offset >> 16, offset & 0xFFFF, 0, static_cast<int>(table.second.size())}) Put16(font, value);
        offset += static_cast<int>((table.second.size() + 3) & ~std::size_t{3});
    }
    for (const auto& table : tables) {
        font.insert(font.end(), table.second.begin(), table.second.end());
        font.resize((font.size() + 3) & ~std::size_t{3});
    }
    return font;
}

inline void Put32(std::vector<unsigned char>& out, std::uint32_t value) {
    Put16(out, value >> 16);
    Put16(out, value & 0xFFFFu);
}

inline void PutAll16(std::vector<unsigned char>& out, std::initializer_list<int> values) {
    for (const int value : values) Put16(out, static_cast<std::uint32_t>(static_cast<std::uint16_t>(value)));
}

inline std::vector<unsigned char> SquareGlyphFont(std::uint32_t codepoint, int width, std::uint32_t alternate = 0, bool empty = false) {
    std::vector<unsigned char> head;
    Put32(head, 0x00010000u);
    Put32(head, 0x00010000u);
    Put32(head, 0);
    Put32(head, 0x5F0F3CF5u);
    PutAll16(head, {0x000B, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, width, 800, 0, 8, 2, 0, 0});
    std::vector<unsigned char> hhea;
    Put32(hhea, 0x00010000u);
    PutAll16(hhea, {800, -200, 0, width, 0, 0, width, 1, 0, 0, 0, 0, 0, 0, 0, 2});
    std::vector<unsigned char> maxp;
    Put32(maxp, 0x00010000u);
    PutAll16(maxp, {2, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
    std::vector<unsigned char> hmtx;
    PutAll16(hmtx, {width, 0, width, 100});
    std::vector<unsigned char> glyf;
    PutAll16(glyf, {empty ? 0 : 1, 100, 0, width - 100, 700, 3, 0});
    glyf.insert(glyf.end(), {1, 1, 1, 1});
    PutAll16(glyf, {100, 0, width - 200, 0, 0, 700, 0, -700});
    std::vector<unsigned char> loca;
    PutAll16(loca, {0, 0, static_cast<int>(glyf.size() / 2)});
    std::vector<unsigned char> cmap;
    PutAll16(cmap, {0, 1, 3, 1});
    Put32(cmap, 12);
    PutAll16(cmap, {4, 32, 0, 4, 4, 1, 0, static_cast<int>(codepoint), 0xFFFF, 0, static_cast<int>(codepoint), 0xFFFF, 1 - static_cast<int>(codepoint), 1, 0, 0});
    if (alternate) {
        const std::vector<unsigned char> original(cmap.begin() + 12, cmap.end());
        cmap.clear();
        PutAll16(cmap, {0, 2, 3, 1}); Put32(cmap, 20);
        PutAll16(cmap, {0, 3}); Put32(cmap, 52);
        cmap.insert(cmap.end(), original.begin(), original.end());
        PutAll16(cmap, {4, 32, 0, 4, 4, 1, 0, static_cast<int>(alternate), 0xFFFF, 0, static_cast<int>(alternate), 0xFFFF, 1 - static_cast<int>(alternate), 1, 0, 0});
    }
    const std::vector<std::pair<const char*, const std::vector<unsigned char>*>> tables = {
        {"cmap", &cmap}, {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
    std::vector<unsigned char> font;
    Put32(font, 0x00010000u);
    PutAll16(font, {static_cast<int>(tables.size()), 64, 2, static_cast<int>(tables.size()) * 16 - 64});
    std::uint32_t offset = 12 + static_cast<std::uint32_t>(tables.size()) * 16;
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), tag, tag + 4);
        Put32(font, 0);
        Put32(font, offset);
        Put32(font, static_cast<std::uint32_t>(data->size()));
        offset += (static_cast<std::uint32_t>(data->size()) + 3u) & ~3u;
    }
    for (const auto& [tag, data] : tables) {
        font.insert(font.end(), data->begin(), data->end());
        font.resize((font.size() + 3u) & ~std::size_t{3});
    }
    return font;
}

}

#endif
