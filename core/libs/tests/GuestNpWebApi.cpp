#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int APS5_VABI sceNpWebApiInitialize(int libHttpCtxId, std::size_t poolSize);
int APS5_VABI sceNpWebApiTerminate(int libCtxId);
int APS5_VABI sceNpWebApiCreateHandle(int libCtxId);
int APS5_VABI sceNpWebApiDeleteHandle(int libCtxId, int handleId);
int APS5_VABI sceNpWebApiCreatePushEventFilter(int libCtxId, const void* filterParam, std::size_t filterParamNum);
int APS5_VABI sceNpWebApiDeletePushEventFilter(int libCtxId, int filterId);
int APS5_VABI sceNpWebApiCreateServicePushEventFilter(int libCtxId, int handleId, const char* npServiceName, unsigned npServiceLabel, const void* filterParam, std::size_t filterParamNum);
int APS5_VABI sceNpWebApiDeleteServicePushEventFilter(int libCtxId, int filterId);
int APS5_VABI sceNpWebApiUtilityParseNpId(const char* jsonNpId, void* npId);
}

namespace {

constexpr int InvalidArgument = static_cast<int>(0x80552902u);
constexpr int LibContextNotFound = static_cast<int>(0x80552904u);
constexpr int PushEventFilterNotFound = static_cast<int>(0x8055290Bu);
constexpr int HandleNotFound = static_cast<int>(0x8055290Du);
constexpr int ServicePushEventFilterNotFound = static_cast<int>(0x8055290Eu);
constexpr int SignedInUserNotFound = static_cast<int>(0x80552910u);

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "NpWebApi: %s\n", message);
        std::abort();
    }
}

}

struct ParseCase {
    const char* encoded;
    const char* handle;
    const char* opt;
};

void CheckParseNpId() {
    constexpr int NpInvalidArgument = static_cast<int>(0x80550003u);
    const ParseCase cases[] = {
    {"cGxheWVyX29uZUBiNy9rMi5lNQ==", "player_one", "b7k2e5"},
    {"bm9fc2VwYXJhdG9y", "no_separator", ""},
    {"YV9oYW5kbGVfbG9uZ2VyX3RoYW5fMTZAYWJjZGVmZ2hpag==", "a_handle_longer_", "abcdefgh"},
        {"cGxh \neWVyX29uZUBiNy9rMi5lNQ==", "player_one", "b7k2e5"},
    };
    for (const ParseCase& item : cases) {
        unsigned char npId[36];
        std::memset(npId, 0xAA, sizeof(npId));
        Require(sceNpWebApiUtilityParseNpId(item.encoded, npId) == 0, "ParseNpId accepts encoded input");
        unsigned char expected[36] = {};
        std::memcpy(expected, item.handle, std::strlen(item.handle));
        std::memcpy(expected + 20, item.opt, std::strlen(item.opt));
        expected[28] = 1;
        Require(std::memcmp(npId, expected, sizeof(npId)) == 0, "ParseNpId fills handle, opt and reserved flag");
    }
    unsigned char untouched[36];
    std::memset(untouched, 0xAA, sizeof(untouched));
    Require(sceNpWebApiUtilityParseNpId(nullptr, untouched) == NpInvalidArgument && untouched[0] == 0xAA, "ParseNpId rejects null input");
    Require(sceNpWebApiUtilityParseNpId("eEB5", nullptr) == 0, "ParseNpId accepts a null output");
}

int main() {
    CheckParseNpId();
    const int context = sceNpWebApiInitialize(1, 0x10000);
    Require(context > 0 && context <= 0x7fff, "library context id out of range");
    const int other = sceNpWebApiInitialize(1, 0x10000);
    Require(other > 0 && other != context, "a second library context must get its own id");
    Require(sceNpWebApiCreateHandle(0x7fff + 1) == LibContextNotFound, "handle of an unknown context");

    const int handle = sceNpWebApiCreateHandle(context);
    Require(handle > 0 && static_cast<unsigned>(handle) < 0xf0000000u, "handle id out of range");
    const char filterParam[16] = {};
    Require(sceNpWebApiCreatePushEventFilter(context, nullptr, 1) == InvalidArgument, "push event filter without its parameter");
    Require(sceNpWebApiCreatePushEventFilter(context, filterParam, 0) == InvalidArgument, "push event filter with no parameters");
    const int filter = sceNpWebApiCreatePushEventFilter(context, filterParam, 1);
    Require(filter > 0 && filter != handle, "push event filter id");

    Require(sceNpWebApiCreateServicePushEventFilter(context, handle, nullptr, 0, filterParam, 1) == InvalidArgument, "service filter without a service name");
    Require(sceNpWebApiCreateServicePushEventFilter(0x7fff + 1, handle, "service", 0, filterParam, 1) == LibContextNotFound, "service filter of an unknown context");
    Require(sceNpWebApiCreateServicePushEventFilter(other, handle, "service", 0, filterParam, 1) == HandleNotFound, "service filter with another context's handle");
    Require(sceNpWebApiCreateServicePushEventFilter(context, handle, "service", 0, filterParam, 1) == SignedInUserNotFound, "service filter without a signed-in user");
    Require(sceNpWebApiDeleteServicePushEventFilter(context, 1) == ServicePushEventFilterNotFound, "no service filter exists");

    Require(sceNpWebApiDeletePushEventFilter(other, filter) == PushEventFilterNotFound, "filter deleted through another context");
    Require(sceNpWebApiDeletePushEventFilter(context, filter) == 0, "delete the push event filter");
    Require(sceNpWebApiDeletePushEventFilter(context, filter) == PushEventFilterNotFound, "push event filter deleted twice");
    Require(sceNpWebApiDeleteHandle(context, handle) == 0, "delete the handle");
    Require(sceNpWebApiDeleteHandle(context, handle) == HandleNotFound, "handle deleted twice");

    Require(sceNpWebApiTerminate(context) == 0, "terminate the library context");
    Require(sceNpWebApiTerminate(context) == LibContextNotFound, "library context terminated twice");
    Require(sceNpWebApiCreateHandle(context) == LibContextNotFound, "handle of a terminated context");
    Require(sceNpWebApiTerminate(other) == 0, "terminate the second library context");
    return 0;
}
