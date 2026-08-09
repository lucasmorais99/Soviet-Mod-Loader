#ifndef SOVIET_MOD_LOADER_API_H
#define SOVIET_MOD_LOADER_API_H

#define SML_SERVICE "soviet.mod.loader"
#define SML_SERVICE_VERSION 1u

typedef enum SmlModState {
    SML_MOD_ACTIVE = 0,
    SML_MOD_ADDED = 1,
    SML_MOD_CONFLICT = 2,
    SML_MOD_DISABLED = 3,
    SML_MOD_INCOMPATIBLE = 4,
    SML_MOD_MISSING_DEPENDENCY = 5,
    SML_MOD_ERROR = 6
} SmlModState;

typedef struct SmlModInfo {
    const char* id;
    const char* name;
    const char* version;
    const char* workshopPath;
    unsigned long long addedUtc;
    unsigned long long fingerprint;
    int priority;
    SmlModState state;
    const char* detail;
} SmlModInfo;

typedef struct SmlApi {
    int (*count)(void);
    int (*get)(int index, SmlModInfo* out);
    unsigned long long (*generation)(void);
} SmlApi;

#endif
