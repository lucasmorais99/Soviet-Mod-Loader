// TesmioLoader plugin ABI v3, mirrored from MaxLegend/TesmioLoader.
// Keep this POD-only contract byte-for-byte compatible with upstream.
#ifndef TESMIO_API_H
#define TESMIO_API_H

#include <stddef.h>

#define TSM_API_VERSION 3u
#define TSM_API_VERSION_MIN 3u

#ifdef __cplusplus
extern "C" {
#endif

#define TSM_MAP_RESOURCEMAP 0
#define TSM_MAP_RESOURCEMAP2 1
#define TSM_MAP_EXTRA_FIRST 2
#define TSM_MAP_TERRAIN 64

typedef struct TsmDeposit {
    const char* name;
    const char* token;
    int type;
    int buildingType;
    int map;
    int component;
    float radius;
    const char* icon;
} TsmDeposit;

typedef struct TsmHost {
    unsigned apiVersion;
    unsigned structSize;
    void* exeModule;
    unsigned char* exeBase;
    size_t exeSize;
    void* engineModule;
    const char* baseDir;
    const char* pluginDir;
    void (*log)(const char* fmt, ...);
    void** (*findIatSlot)(void* module, const char* dll, const char* fn);
    int (*patchIat)(void* module, const char* dll, const char* fn,
                    void* detour, void** original, const char* label);
    int (*installInlineHook)(void* target, void* detour, void** trampoline,
                             const unsigned char* expect, size_t stolen,
                             const char* label);
    unsigned char* (*allocNear)(unsigned char* anchor, size_t size);
    int (*readablePtr)(const void* p, size_t n);
    long (*faultFilter)(const char* what, void* exceptionPointers);
    int (*configInt)(const char* iniName, const char* section,
                     const char* key, int fallback);
    int (*configString)(const char* iniName, const char* section,
                        const char* key, char* out, int outSize,
                        const char* fallback);
    int (*provide)(const char* service, unsigned version, const void* iface);
    const void* (*consume)(const char* service, unsigned version);
} TsmHost;

#define TSM_SERVICE_DEPOSITS "deposits"
#define TSM_DEPOSITS_VERSION 2u
typedef struct TsmDepositApi {
    int (*count)(void);
    int (*get)(int index, TsmDeposit* out);
    const char* (*setting)(int index, const char* key);
    void* (*texture)(int index);
} TsmDepositApi;

#define TSM_SERVICE_RESOURCES "resources"
#define TSM_RESOURCES_VERSION 1u
typedef struct TsmResourceApi {
    int (*count)(void);
    const char* (*name)(int i);
    int (*index)(int i);
    int (*indexOf)(const char* name);
} TsmResourceApi;

typedef struct TsmPluginInfo {
    const char* name;
    const char* version;
} TsmPluginInfo;

typedef unsigned (*TsmPluginApiVersionFn)(void);
typedef int (*TsmPluginInitFn)(const TsmHost* host, TsmPluginInfo* info);
typedef int (*TsmPluginStartFn)(void);

#define TSM_EXPORT_APIVERSION "TsmPluginApiVersion"
#define TSM_EXPORT_INIT "TsmPluginInit"
#define TSM_EXPORT_START "TsmPluginStart"

#ifdef __cplusplus
}
#endif
#endif
