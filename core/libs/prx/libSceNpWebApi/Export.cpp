#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

static constexpr int SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);
static constexpr int SCE_NP_WEBAPI_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80552902);
static constexpr int SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND = static_cast<int>(0x80552904);
static constexpr int SCE_NP_WEBAPI_ERROR_PUSH_EVENT_FILTER_NOT_FOUND = static_cast<int>(0x8055290B);
static constexpr int SCE_NP_WEBAPI_ERROR_HANDLE_NOT_FOUND = static_cast<int>(0x8055290D);
static constexpr int SCE_NP_WEBAPI_ERROR_SERVICE_PUSH_EVENT_FILTER_NOT_FOUND = static_cast<int>(0x8055290E);
static constexpr int SCE_NP_WEBAPI_ERROR_SIGNED_IN_USER_NOT_FOUND = static_cast<int>(0x80552910);
static constexpr int SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_MAX = static_cast<int>(0x8055291A);

namespace {
struct WebApiNpId {
    NpOnlineId handle;
    std::uint8_t opt[8];
    std::uint8_t reserved[8];
};
static_assert(sizeof(WebApiNpId) == 36);

std::string DecodeBase64(const char* text) {
    std::string out;
    int buffer = 0;
    int bits = 0;
    for (; *text != '\0' && *text != '='; ++text) {
        const char character = *text;
        int value = -1;
        if (character >= 'A' && character <= 'Z') value = character - 'A';
        else if (character >= 'a' && character <= 'z') value = character - 'a' + 26;
        else if (character >= '0' && character <= '9') value = character - '0' + 52;
        else if (character == '+') value = 62;
        else if (character == '/') value = 63;
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

struct LibraryContext {
    int libHttpCtxId;
    std::size_t poolSize;
    std::unordered_set<int> handles;
    std::unordered_set<int> pushEventFilters;
};

std::mutex contextsMutex;
std::unordered_map<int, LibraryContext> contexts;
int lastContext = 0;
int lastObject = 0;

int nextObject() {
    lastObject = lastObject % 0xefffffff + 1;
    return lastObject;
}

}

extern "C" {

int APS5_VABI sceNpWebApiAbortRequest() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiCreateContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiCreateHandle(int libCtxId) {
    std::lock_guard lock(contextsMutex);
    const auto context = contexts.find(libCtxId);
    if (context == contexts.end()) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    const auto handle = nextObject();
    context->second.handles.insert(handle);
    return handle;
}

int APS5_VABI sceNpWebApiCreatePushEventFilter(int libCtxId, const void* filterParam, std::size_t filterParamNum) {
    if (!filterParam || filterParamNum == 0) return SCE_NP_WEBAPI_ERROR_INVALID_ARGUMENT;
    std::lock_guard lock(contextsMutex);
    const auto context = contexts.find(libCtxId);
    if (context == contexts.end()) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    const auto filter = nextObject();
    context->second.pushEventFilters.insert(filter);
    return filter;
}

int APS5_VABI sceNpWebApiCreateRequest() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiCreateServicePushEventFilter(int libCtxId, int handleId, const char* npServiceName, std::uint32_t npServiceLabel, const void* filterParam, std::size_t filterParamNum) {
    (void)npServiceLabel;
    if (!npServiceName || !filterParam || filterParamNum == 0) return SCE_NP_WEBAPI_ERROR_INVALID_ARGUMENT;
    std::lock_guard lock(contextsMutex);
    const auto context = contexts.find(libCtxId);
    if (context == contexts.end()) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    if (!context->second.handles.contains(handleId)) return SCE_NP_WEBAPI_ERROR_HANDLE_NOT_FOUND;
    return SCE_NP_WEBAPI_ERROR_SIGNED_IN_USER_NOT_FOUND;
}

int APS5_VABI sceNpWebApiDeleteContext() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiDeleteHandle(int libCtxId, int handleId) {
    std::lock_guard lock(contextsMutex);
    const auto context = contexts.find(libCtxId);
    if (context == contexts.end()) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    return context->second.handles.erase(handleId) != 0 ? 0 : SCE_NP_WEBAPI_ERROR_HANDLE_NOT_FOUND;
}

int APS5_VABI sceNpWebApiDeletePushEventFilter(int libCtxId, int filterId) {
    std::lock_guard lock(contextsMutex);
    const auto context = contexts.find(libCtxId);
    if (context == contexts.end()) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    return context->second.pushEventFilters.erase(filterId) != 0 ? 0 : SCE_NP_WEBAPI_ERROR_PUSH_EVENT_FILTER_NOT_FOUND;
}

int APS5_VABI sceNpWebApiDeleteRequest() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiDeleteServicePushEventFilter(int libCtxId, int filterId) {
    (void)filterId;
    std::lock_guard lock(contextsMutex);
    if (!contexts.contains(libCtxId)) return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    return SCE_NP_WEBAPI_ERROR_SERVICE_PUSH_EVENT_FILTER_NOT_FOUND;
}

int APS5_VABI sceNpWebApiGetHttpResponseHeaderValue() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiGetHttpResponseHeaderValueLength() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiGetHttpStatusCode() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiInitialize(int libHttpCtxId, std::size_t poolSize) {
    std::lock_guard lock(contextsMutex);
    for (int attempt = 0; attempt < 0x7fff; ++attempt) {
        lastContext = lastContext % 0x7fff + 1;
        if (contexts.contains(lastContext)) continue;
        contexts[lastContext] = {libHttpCtxId, poolSize, {}, {}};
        return lastContext;
    }
    return SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_MAX;
}

int APS5_VABI sceNpWebApiReadData() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiRegisterPushEventCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiRegisterServicePushEventCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiSendRequest() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiTerminate(int libCtxId) {
    std::lock_guard lock(contextsMutex);
    return contexts.erase(libCtxId) != 0 ? 0 : SCE_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
}

int APS5_VABI sceNpWebApiUnregisterPushEventCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiUnregisterServicePushEventCallback() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNpWebApiUtilityParseNpId(const char* jsonNpId, WebApiNpId* npId) {
    if (jsonNpId == nullptr) return SCE_NP_ERROR_INVALID_ARGUMENT;
    const std::string decoded = DecodeBase64(jsonNpId);
    const std::size_t at = decoded.find('@');
    const std::string handle = decoded.substr(0, at);
    std::string opt;
    if (at != std::string::npos) {
        for (std::size_t index = at + 1; index < decoded.size(); ++index)
            if (decoded[index] != '/' && decoded[index] != '.') opt.push_back(decoded[index]);
    }
    if (npId != nullptr) {
        std::memset(npId, 0, sizeof(WebApiNpId));
        std::memcpy(npId->handle.data, handle.data(), std::min<std::size_t>(handle.size(), 16));
        std::memcpy(npId->opt, opt.data(), std::min(opt.size(), sizeof(npId->opt)));
        npId->reserved[0] = 1;
    }
    return 0;
}

}
