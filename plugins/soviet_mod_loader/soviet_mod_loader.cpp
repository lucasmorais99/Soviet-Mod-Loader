#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tesmio_api.h"
#include "soviet_mod_loader_api.h"

namespace fs = std::filesystem;

static const TsmHost* H = nullptr;
static fs::path g_base;
static fs::path g_vfsRoot;
static fs::path g_stateDir;
static unsigned long long g_generation = 0;
static bool g_loadHooks = true;
static bool g_copyAssets = true;
static bool g_verbose = false;
static bool g_disableExternal = true;
static bool g_enableEmbedded = true;
static bool g_externalAlreadyLoaded = false;
static std::string g_confirmationMode = "changes";

extern "C" unsigned SmlBuildingsApiVersion(void);
extern "C" int SmlBuildingsInit(const TsmHost*, TsmPluginInfo*);
extern "C" unsigned SmlResourcesApiVersion(void);
extern "C" int SmlResourcesInit(const TsmHost*, TsmPluginInfo*);
extern "C" unsigned SmlDepositsApiVersion(void);
extern "C" int SmlDepositsInit(const TsmHost*, TsmPluginInfo*);
extern "C" unsigned SmlNeedsApiVersion(void);
extern "C" int SmlNeedsInit(const TsmHost*, TsmPluginInfo*);
extern "C" int SmlNeedsStart(void);
extern "C" int SmlResourcesDeclaredCount(void);
extern "C" const char* SmlResourcesDeclaredName(int);
extern "C" int SmlResourcesHookReady(void);
extern "C" int SmlDepositsDeclaredCount(void);
extern "C" int SmlDepositsPatchReady(void);
extern "C" int SmlDepositsMapsReady(void);
extern "C" int SmlNeedsDeclaredCount(void);
extern "C" int SmlNeedsHooksReady(void);
extern "C" int SmlBuildingsEnabledCount(void);
extern "C" int SmlBuildingsCompleteCount(void);
extern "C" int SmlBuildingsIncompleteCount(void);

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static bool IsTrue(const std::string& s, bool fallback = false) {
    if (s.empty()) return fallback;
    std::string v = Lower(Trim(s));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
static std::string PathUtf8(const fs::path& p) { return p.u8string(); }

// TesmioLoader resolves a source checkout as <tesmioloader>\build for its DLL
// base, while the checked-in VFS is <tesmioloader>\vfs. Mirror that layout and
// retain the distributed-layout fallback where vfs sits beside the DLL.
static fs::path ResolveVfsRoot(const fs::path& base) {
    fs::path sibling = base.parent_path() / "vfs";
    if (Lower(PathUtf8(base.filename())) == "build") return sibling;
    std::error_code ec;
    if (fs::is_directory(sibling, ec)) return sibling;
    return base / "vfs";
}

struct Entry { std::string key, value; int line = 0; };
struct Section { std::string name; std::vector<Entry> entries; };
struct Ini {
    std::vector<Section> sections;
    Section* find(const std::string& name) {
        std::string n = Lower(name);
        for (auto& s : sections) if (Lower(s.name) == n) return &s;
        return nullptr;
    }
    const Section* find(const std::string& name) const {
        return const_cast<Ini*>(this)->find(name);
    }
    std::string get(const std::string& sec, const std::string& key,
                    const std::string& fallback = "") const {
        const Section* s = find(sec);
        if (!s) return fallback;
        std::string k = Lower(key);
        for (auto it = s->entries.rbegin(); it != s->entries.rend(); ++it)
            if (Lower(it->key) == k) return it->value;
        return fallback;
    }
    std::vector<std::string> getAll(const std::string& sec, const std::string& key) const {
        std::vector<std::string> out;
        const Section* s = find(sec);
        if (!s) return out;
        std::string k = Lower(key);
        for (const auto& e : s->entries) if (Lower(e.key) == k) out.push_back(e.value);
        return out;
    }
};

static bool ReadText(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3);
    return true;
}

static bool ParseIniText(const std::string& text, Ini& ini, std::string& error) {
    Section* current = nullptr;
    std::istringstream in(text); std::string line; int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[') {
            size_t close = t.find(']');
            if (close == std::string::npos || Trim(t.substr(1, close - 1)).empty()) {
                error = "invalid section at line " + std::to_string(lineNo); return false;
            }
            ini.sections.push_back({Trim(t.substr(1, close - 1)), {}});
            current = &ini.sections.back(); continue;
        }
        size_t eq = t.find('=');
        if (!current || eq == std::string::npos || Trim(t.substr(0, eq)).empty()) {
            error = "invalid key at line " + std::to_string(lineNo); return false;
        }
        current->entries.push_back({Trim(t.substr(0, eq)), Trim(t.substr(eq + 1)), lineNo});
    }
    return true;
}

static bool LoadIni(const fs::path& path, Ini& ini, std::string& error) {
    std::string text;
    if (!ReadText(path, text)) { error = "cannot read " + PathUtf8(path); return false; }
    return ParseIniText(text, ini, error);
}

static std::string SerializeIni(const Ini& ini) {
    std::ostringstream out;
    out << "; generated by Soviet Mod Loader - edit state/base or the source mod, not this file\r\n";
    for (const auto& s : ini.sections) {
        out << "\r\n[" << s.name << "]\r\n";
        for (const auto& e : s.entries) out << e.key << " = " << e.value << "\r\n";
    }
    return out.str();
}

static bool WriteIfChanged(const fs::path& path, const std::string& data) {
    std::string old;
    if (ReadText(path, old) && old == data) return true;
    std::error_code ec; fs::create_directories(path.parent_path(), ec);
    fs::path temp = path; temp += L".sml.tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static uint64_t FnvBytes(uint64_t h, const void* ptr, size_t n) {
    const unsigned char* p = (const unsigned char*)ptr;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
static uint64_t HashFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary); if (!f) return 0;
    uint64_t h = 1469598103934665603ull; char buf[65536];
    while (f) { f.read(buf, sizeof(buf)); std::streamsize n = f.gcount(); if (n) h = FnvBytes(h, buf, (size_t)n); }
    return h;
}
static uint64_t FingerprintTree(const fs::path& root) {
    uint64_t h = 1469598103934665603ull; std::error_code ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) if (!ec && it->is_regular_file(ec)) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        std::string rel = Lower(PathUtf8(fs::relative(p, root, ec)));
        uint64_t size = fs::file_size(p, ec), mt = (uint64_t)fs::last_write_time(p, ec).time_since_epoch().count();
        h = FnvBytes(h, rel.data(), rel.size()); h = FnvBytes(h, &size, sizeof(size)); h = FnvBytes(h, &mt, sizeof(mt));
    }
    return h;
}

static uint64_t CreatedUtc(const fs::path& path) {
    HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    FILETIME c{}, a{}, w{}; GetFileTime(f, &c, &a, &w); CloseHandle(f);
    ULARGE_INTEGER u{}; u.LowPart = c.dwLowDateTime; u.HighPart = c.dwHighDateTime;
    return u.QuadPart > 116444736000000000ull ? (u.QuadPart - 116444736000000000ull) / 10000000ull : 0;
}

struct Dependency { std::string id, constraint; };
struct Mod {
    std::string id, name, version, detail, pathText;
    fs::path root;
    Ini manifest;
    uint64_t added = 0, fingerprint = 0;
    int priority = 0;
    SmlModState state = SML_MOD_ACTIVE;
    std::vector<Dependency> dependencies;
    std::vector<fs::path> hooks;
    bool usable() const { return state == SML_MOD_ACTIVE || state == SML_MOD_ADDED || state == SML_MOD_CONFLICT; }
};
static std::vector<Mod> g_mods;
static Section* EnsureSection(Ini& ini, const std::string& name);

struct InternalCatalog {
    Ini data;
    bool dirty = false;
};
static InternalCatalog g_catalog;

static std::string CatalogKey(const Mod& mod, const std::string& name) {
    std::string key = Lower(mod.id + "::" + name);
    for (char& c : key) if (c == '=' || c == '\r' || c == '\n' || c == '[' || c == ']') c = '_';
    return key;
}

static void LoadCatalog() {
    std::string error; fs::path path = g_stateDir / "catalog.ini";
    if (fs::exists(path) && !LoadIni(path, g_catalog.data, error))
        H->log("sml       catalog ignored: %s", error.c_str());
}

static uint64_t CatalogNumber(const std::string& group, const std::string& key,
                              uint64_t minValue, uint64_t maxValue, uint64_t preferred = UINT64_MAX,
                              bool allowPreferredBelowMin = false) {
    Section* section = EnsureSection(g_catalog.data, group);
    for (const auto& e : section->entries) if (Lower(e.key) == Lower(key))
        return _strtoui64(e.value.c_str(), nullptr, 10);
    std::set<uint64_t> used;
    for (const auto& e : section->entries) used.insert(_strtoui64(e.value.c_str(), nullptr, 10));
    uint64_t value = preferred;
    if ((!allowPreferredBelowMin && value < minValue) || value > maxValue || used.count(value)) {
        uint64_t span = maxValue - minValue + 1;
        uint64_t hash = FnvBytes(1469598103934665603ull, key.data(), key.size());
        value = minValue + hash % span;
        for (uint64_t i = 0; i < span && used.count(value); ++i)
            value = minValue + ((value - minValue + 1) % span);
        if (used.count(value)) return UINT64_MAX;
    }
    section->entries.push_back({key, std::to_string(value), 0}); g_catalog.dirty = true; return value;
}

static uint64_t CatalogOrdinal(const std::string& group, const std::string& key) {
    Section* section = EnsureSection(g_catalog.data, group);
    for (const auto& e : section->entries) if (Lower(e.key) == Lower(key)) return _strtoui64(e.value.c_str(), nullptr, 10);
    uint64_t next = 0; for (const auto& e : section->entries) next = (std::max)(next, _strtoui64(e.value.c_str(), nullptr, 10) + 1);
    section->entries.push_back({Lower(key), std::to_string(next), 0}); g_catalog.dirty = true; return next;
}

static bool SaveCatalog() {
    if (g_catalog.dirty && !WriteIfChanged(g_stateDir / "catalog.ini", SerializeIni(g_catalog.data))) {
        H->log("sml       failed to persist internal catalog"); return false;
    }
    return true;
}

static uint64_t RemoveNumericEntry(Section& section, const char* name) {
    uint64_t preferred = UINT64_MAX; std::string key = Lower(name);
    for (const auto& e : section.entries) if (Lower(e.key) == key) preferred = _strtoui64(e.value.c_str(), nullptr, 0);
    section.entries.erase(std::remove_if(section.entries.begin(), section.entries.end(), [&](const Entry& e){ return Lower(e.key) == key; }), section.entries.end());
    return preferred;
}

static void SetEntry(Section& section, const std::string& key, const std::string& value) {
    section.entries.erase(std::remove_if(section.entries.begin(), section.entries.end(), [&](const Entry& e){ return Lower(e.key) == Lower(key); }), section.entries.end());
    section.entries.insert(section.entries.begin(), {key, value, 0});
}

static void CatalogizeSection(const std::string& domain, Mod& mod, Section& section) {
    std::string key = CatalogKey(mod, section.name);
    if (domain == "buildings") {
        uint64_t preferred = RemoveNumericEntry(section, "id");
        uint64_t id = CatalogNumber("building_ids", key, 9100000000ull, 9199999999ull, preferred);
        if (id == UINT64_MAX) { mod.state = SML_MOD_ERROR; mod.detail = "building id catalog exhausted"; return; }
        SetEntry(section, "id", std::to_string(id));
    } else if (domain == "deposits") {
        uint64_t preferredType = RemoveNumericEntry(section, "type");
        uint64_t type = CatalogNumber("deposit_types", key, 10, 127, preferredType);
        if (type == UINT64_MAX) { mod.state = SML_MOD_ERROR; mod.detail = "deposit type catalog exhausted"; return; }
        SetEntry(section, "type", std::to_string(type));
        section.entries.erase(std::remove_if(section.entries.begin(), section.entries.end(), [](const Entry& e){
            std::string key = Lower(e.key); return key == "map" || key == "component";
        }), section.entries.end());
        SetEntry(section, "map", "auto");
    }
}

static void StabilizeListOrder(Ini& ini, const std::string& sectionName, const std::string& group) {
    Section* section = ini.find(sectionName); if (!section) return;
    std::map<std::string, uint64_t> order;
    for (const auto& e : section->entries) order[Lower(e.key)] = CatalogOrdinal(group, Lower(e.key));
    std::stable_sort(section->entries.begin(), section->entries.end(), [&](const Entry& a, const Entry& b) {
        return order[Lower(a.key)] < order[Lower(b.key)];
    });
}

static std::vector<int> VersionParts(const std::string& s) {
    std::vector<int> out; int n = 0; bool have = false;
    for (char c : s) { if (std::isdigit((unsigned char)c)) { n = n * 10 + c - '0'; have = true; }
        else if (c == '.') { out.push_back(have ? n : 0); n = 0; have = false; } else break; }
    if (have || out.empty()) out.push_back(n); while (out.size() < 3) out.push_back(0); return out;
}
static int CompareVersion(const std::string& a, const std::string& b) {
    auto x = VersionParts(a), y = VersionParts(b); size_t n = (std::max)(x.size(), y.size());
    x.resize(n); y.resize(n); for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1; return 0;
}
static bool Satisfies(const std::string& version, std::string c) {
    c = Trim(c); if (c.empty() || c == "*" || c == "any") return true;
    std::string op = "=";
    if (c.rfind(">=", 0) == 0 || c.rfind("<=", 0) == 0) { op = c.substr(0, 2); c.erase(0, 2); }
    else if (c[0] == '>' || c[0] == '<' || c[0] == '=') { op = c.substr(0, 1); c.erase(0, 1); }
    int cmp = CompareVersion(version, Trim(c));
    return op == "=" ? cmp == 0 : op == ">=" ? cmp >= 0 : op == "<=" ? cmp <= 0 : op == ">" ? cmp > 0 : cmp < 0;
}

static std::vector<fs::path> SteamLibraries() {
    std::vector<fs::path> out; char steam[MAX_PATH]{}; DWORD n = sizeof(steam);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, steam, &n) == ERROR_SUCCESS)
        out.push_back(fs::u8path(steam));
    if (out.empty()) return out;
    std::string vdf; if (!ReadText(out[0] / "steamapps" / "libraryfolders.vdf", vdf)) return out;
    size_t pos = 0;
    while ((pos = vdf.find("\"path\"", pos)) != std::string::npos) {
        size_t q1 = vdf.find('"', pos + 6), q2 = q1 == std::string::npos ? q1 : vdf.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) break;
        std::string p = vdf.substr(q1 + 1, q2 - q1 - 1); std::string unescaped;
        for (size_t i = 0; i < p.size(); ++i) { if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i; unescaped += p[i]; }
        fs::path lib = fs::u8path(unescaped); if (std::find(out.begin(), out.end(), lib) == out.end()) out.push_back(lib); pos = q2 + 1;
    }
    return out;
}

static fs::path FindWorkshopRoot(const std::string& configured, int appId) {
    if (!configured.empty() && Lower(configured) != "auto") return fs::u8path(configured);
    for (const auto& lib : SteamLibraries()) {
        fs::path p = lib / "steamapps" / "workshop" / "content" / std::to_string(appId);
        std::error_code ec; if (fs::is_directory(p, ec)) return p;
    }
    return {};
}

static std::map<std::string, uint64_t> LoadOldIndex() {
    std::map<std::string, uint64_t> out; std::ifstream f(g_stateDir / "mods.index", std::ios::binary); std::string line;
    while (std::getline(f, line)) { size_t bar = line.find('|'); if (bar != std::string::npos) out[line.substr(0, bar)] = _strtoui64(line.c_str() + bar + 1, nullptr, 16); }
    return out;
}

static void DiscoverMods(const fs::path& root) {
    auto old = LoadOldIndex(); std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) { H->log("sml       Steam Workshop root not found"); return; }
    for (const auto& d : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (!d.is_directory(ec)) continue;
        fs::path manifestPath = d.path() / "soviet.mod.ini";
        if (!fs::is_regular_file(manifestPath, ec)) continue;
        Mod m; m.root = d.path(); m.pathText = PathUtf8(m.root); std::string error;
        if (!LoadIni(manifestPath, m.manifest, error)) { m.id = PathUtf8(d.path().filename()); m.name = m.id; m.state = SML_MOD_ERROR; m.detail = error; g_mods.push_back(std::move(m)); continue; }
        m.id = Trim(m.manifest.get("mod", "id", PathUtf8(d.path().filename())));
        m.name = Trim(m.manifest.get("mod", "name", m.id)); m.version = Trim(m.manifest.get("mod", "version", "0.0.0"));
        m.priority = std::atoi(m.manifest.get("mod", "priority", "0").c_str());
        m.added = _strtoui64(m.manifest.get("mod", "added_utc", "0").c_str(), nullptr, 10); if (!m.added) m.added = CreatedUtc(d.path());
        m.fingerprint = FingerprintTree(d.path());
        if (!IsTrue(m.manifest.get("mod", "enabled", "1"), true)) { m.state = SML_MOD_DISABLED; m.detail = "disabled by manifest"; }
        unsigned apiMin = (unsigned)std::strtoul(m.manifest.get("mod", "tesmio_api_min", "3").c_str(), nullptr, 0);
        unsigned apiMax = (unsigned)std::strtoul(m.manifest.get("mod", "tesmio_api_max", "3").c_str(), nullptr, 0);
        if (m.usable() && (H->apiVersion < apiMin || H->apiVersion > apiMax)) { m.state = SML_MOD_INCOMPATIBLE; m.detail = "Tesmio API outside " + std::to_string(apiMin) + ".." + std::to_string(apiMax); }
        if (m.usable()) m.state = old.find(m.id) == old.end() ? SML_MOD_ADDED : SML_MOD_ACTIVE;
        if (const Section* deps = m.manifest.find("dependencies")) for (const auto& e : deps->entries) m.dependencies.push_back({Trim(e.key), Trim(e.value)});
        for (const auto& value : m.manifest.getAll("hooks", "dll")) m.hooks.push_back(m.root / fs::u8path(value));
        g_mods.push_back(std::move(m));
    }
}

static void ResolveDependenciesAndOrder() {
    std::stable_sort(g_mods.begin(), g_mods.end(), [](const Mod& a, const Mod& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.added != b.added) return a.added < b.added;
        return Lower(a.id) < Lower(b.id);
    });
    std::map<std::string, size_t> byId;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        std::string id = Lower(g_mods[i].id); auto old = byId.find(id);
        if (old != byId.end()) { g_mods[old->second].state = SML_MOD_ERROR; g_mods[old->second].detail = "duplicate mod id replaced by later item"; }
        byId[id] = i;
    }
    for (auto& m : g_mods) if (m.usable()) for (const auto& d : m.dependencies) {
        auto found = byId.find(Lower(d.id)); Mod* dep = found == byId.end() ? nullptr : &g_mods[found->second];
        if (!dep || !dep->usable() || !Satisfies(dep->version, d.constraint)) {
            m.state = SML_MOD_MISSING_DEPENDENCY; m.detail = "missing/incompatible dependency " + d.id + " " + d.constraint; break;
        }
    }
    std::vector<int> visit(g_mods.size(), 0); std::vector<size_t> order;
    std::function<void(size_t)> dfs = [&](size_t i) {
        if (visit[i] == 2) return;
        if (visit[i] == 1) { g_mods[i].state = SML_MOD_ERROR; g_mods[i].detail = "dependency cycle"; return; }
        visit[i] = 1;
        if (g_mods[i].usable()) for (const auto& d : g_mods[i].dependencies) {
            auto found = byId.find(Lower(d.id)); if (found != byId.end()) dfs(found->second);
            if (found != byId.end() && g_mods[found->second].state == SML_MOD_ERROR && g_mods[found->second].detail == "dependency cycle") {
                g_mods[i].state = SML_MOD_ERROR; g_mods[i].detail = "dependency cycle";
            }
        }
        visit[i] = 2; order.push_back(i);
    };
    for (size_t i = 0; i < g_mods.size(); ++i) dfs(i);
    std::vector<Mod> sorted; sorted.reserve(g_mods.size()); for (size_t i : order) sorted.push_back(std::move(g_mods[i])); g_mods.swap(sorted);
}

static Section* EnsureSection(Ini& ini, const std::string& name) {
    if (Section* s = ini.find(name)) return s; ini.sections.push_back({name, {}}); return &ini.sections.back();
}
static void MarkConflict(Mod& loser, const std::string& detail) {
    if (loser.usable()) { loser.state = SML_MOD_CONFLICT; if (!loser.detail.empty()) loser.detail += "; "; loser.detail += detail; }
}
static bool SafeContentSection(const std::string& domain, const std::string& section, bool allowSettings) {
    std::string s = Lower(section);
    if (domain == "resources") return s == "list" || s == "base_price" || s == "price" || allowSettings;
    if (domain == "needs") return s == "list" || allowSettings;
    if (domain == "deposits") return s != "deposits" || allowSettings;
    if (domain == "buildings") return s != "buildings" || allowSettings;
    return false;
}

struct DomainPlan {
    std::string domain;
    fs::path target;
    fs::path baseline;
    std::string baselineText;
    std::string mergedText;
    bool writeBaseline = false;
};
static std::vector<DomainPlan> g_domainPlans;
static std::vector<std::string> g_validationErrors;
static std::string g_pendingConfirmationIndex;
static bool g_applicationReady;

static size_t ContentCount(const Ini& ini, const std::string& settings, bool listDomain) {
    if (listDomain) {
        const Section* list = ini.find("list");
        return list ? list->entries.size() : 0;
    }
    size_t count = 0;
    for (const auto& section : ini.sections)
        if (Lower(section.name) != Lower(settings) &&
            IsTrue(ini.get(section.name, "enabled", "1"), true)) count++;
    return count;
}

static void ApplyEmbeddedInvariants(const std::string& domain, Ini& ini) {
    if (domain == "resources" && ContentCount(ini, "resources", true)) {
        SetEntry(*EnsureSection(ini, "resources"), "hook", "2");
        H->log("sml       invariant resources.hook=2 (%d planned resource(s))",
               (int)ContentCount(ini, "resources", true));
    } else if (domain == "deposits" && ContentCount(ini, "deposits", false)) {
        SetEntry(*EnsureSection(ini, "deposits"), "code_patch", "1");
        H->log("sml       invariant deposits.code_patch=1 (%d planned deposit(s))",
               (int)ContentCount(ini, "deposits", false));
    } else if (domain == "needs" && ContentCount(ini, "needs", true)) {
        Section* settings = EnsureSection(ini, "needs");
        SetEntry(*settings, "enabled", "1");
        SetEntry(*settings, "demand", "1");
        SetEntry(*settings, "storage", "1");
        H->log("sml       invariants needs.enabled/demand/storage=1 (%d planned need(s))",
               (int)ContentCount(ini, "needs", true));
    } else if (domain == "buildings" && ContentCount(ini, "buildings", false)) {
        Section* settings = EnsureSection(ini, "buildings");
        SetEntry(*settings, "enabled", "1");
        SetEntry(*settings, "out", "media_soviet\\workshop_wip");
        H->log("sml       invariants buildings.enabled=1, out=media_soviet\\workshop_wip (%d planned building(s))",
               (int)ContentCount(ini, "buildings", false));
    }
}

static void PlanDomain(const std::string& domain) {
    fs::path target = g_base / "plugins" / fs::u8path(domain + ".ini");
    fs::path baseline = g_stateDir / "base" / fs::u8path(domain + ".ini"); std::error_code ec;
    std::string baselineText;
    bool writeBaseline = !fs::exists(baseline, ec);
    if (!fs::exists(baseline, ec)) {
        std::string current;
        if (ReadText(target, current) && current.find("generated by Soviet Mod Loader") == std::string::npos) baselineText = current;
    } else {
        ReadText(baseline, baselineText);
    }
    Ini merged; std::string error;
    if (!baselineText.empty() && !ParseIniText(baselineText, merged, error)) H->log("sml       invalid baseline %s: %s", domain.c_str(), error.c_str());
    if (domain == "deposits" || domain == "buildings") {
        Mod baseOwner; baseOwner.id = "__base__"; baseOwner.state = SML_MOD_ACTIVE;
        for (auto& section : merged.sections)
            if (SafeContentSection(domain, section.name, false)) CatalogizeSection(domain, baseOwner, section);
    }
    std::map<std::string, Mod*> owners;
    for (auto& m : g_mods) {
        if (!m.usable()) continue;
        std::string rel = m.manifest.get("content", domain, "tesmio\\" + domain + ".ini");
        fs::path fragment = m.root / fs::u8path(rel); if (!fs::is_regular_file(fragment, ec)) continue;
        Ini add; error.clear(); if (!LoadIni(fragment, add, error)) { m.state = SML_MOD_ERROR; m.detail = error; continue; }
        bool allowSettings = IsTrue(m.manifest.get("content", "allow_settings", "0"));
        for (const auto& srcSec : add.sections) {
            if (!SafeContentSection(domain, srcSec.name, allowSettings)) { MarkConflict(m, "ignored protected [" + srcSec.name + "] in " + domain); continue; }
            std::string secKey = Lower(srcSec.name);
            bool atomic = domain == "deposits" || domain == "buildings";
            if (atomic) {
                std::string ownerKey = domain + "/" + secKey;
                auto own = owners.find(ownerKey); if (own != owners.end()) MarkConflict(*own->second, ownerKey + " replaced by " + m.id);
                Section catalogized = srcSec; CatalogizeSection(domain, m, catalogized);
                if (!m.usable()) continue;
                Section* dst = EnsureSection(merged, srcSec.name); *dst = std::move(catalogized); owners[ownerKey] = &m;
            } else {
                Section* dst = EnsureSection(merged, srcSec.name);
                for (const auto& e : srcSec.entries) {
                    std::string ownerKey = domain + "/" + secKey + "/" + Lower(e.key);
                    auto own = owners.find(ownerKey); if (own != owners.end()) MarkConflict(*own->second, ownerKey + " replaced by " + m.id);
                    dst->entries.erase(std::remove_if(dst->entries.begin(), dst->entries.end(), [&](const Entry& x){ return Lower(x.key) == Lower(e.key); }), dst->entries.end());
                    Entry normalized = e;
                    if (domain == "resources" && secKey == "list") {
                        size_t comma = normalized.value.find(',');
                        if (comma != std::string::npos) {
                            std::string first = Trim(normalized.value.substr(0, comma));
                            bool numeric = !first.empty() && std::all_of(first.begin(), first.end(), [](char c){ return std::isdigit((unsigned char)c); });
                            if (numeric) normalized.value = Trim(normalized.value.substr(comma + 1));
                        }
                    }
                    dst->entries.push_back(std::move(normalized)); owners[ownerKey] = &m;
                }
            }
        }
    }
    if (domain == "resources") StabilizeListOrder(merged, "list", "resource_order");
    if (domain == "needs") StabilizeListOrder(merged, "list", "need_order");
    ApplyEmbeddedInvariants(domain, merged);
    g_domainPlans.push_back({domain, target, baseline, baselineText, SerializeIni(merged), writeBaseline});
}

static const DomainPlan* PlannedDomain(const char* domain) {
    for (const auto& plan : g_domainPlans) if (plan.domain == domain) return &plan;
    return nullptr;
}

static bool LoadPlannedDomain(const char* domain, Ini& out) {
    const DomainPlan* plan = PlannedDomain(domain); std::string error;
    return plan && ParseIniText(plan->mergedText, out, error);
}

static std::set<std::string> KnownResources(const Ini& resources) {
    static const char* vanilla[] = {
        "workers","eletric","vehicles","trains","heat","gravel","rawgravel","plants","steel",
        "aluminium","prefabpanels","bricks","wood","oil","chemicals","coal","rawcoal","iron",
        "rawiron","bauxite","rawbauxite","bitumen","boards","uranium","yellowcake","uf6",
        "nuclearfuel","nuclearfuelburned","fuel","fabric","alcohol","cement","alumina","food",
        "clothes","meat","livestock","asphalt","concrete","ecomponents","mcomponents","plastics",
        "eletronics","explosives","water","usagewater","fertiliser_liquid","waste_gravel",
        "waste_steel","waste_aluminium","waste_plastic","waste_bio","fertiliser","waste_burnable",
        "waste_toxic","waste_other","waste_ash","waste_mixed","service_material"
    };
    std::set<std::string> known;
    for (const char* name : vanilla) known.insert(name);
    if (const Section* list = resources.find("list"))
        for (const auto& entry : list->entries) known.insert(Lower(entry.key));
    return known;
}

static std::vector<std::string> Words(const std::string& value) {
    std::istringstream in(value); std::vector<std::string> words; std::string word;
    while (in >> word) words.push_back(word);
    return words;
}

static bool LooksNumeric(const std::string& value) {
    if (value.empty()) return false; char* end = nullptr;
    std::strtod(value.c_str(), &end); return end && *end == 0;
}

static bool ParseRichnessOffset(const std::string& value, double* result = nullptr) {
    std::string text = Trim(value);
    if (text.empty()) return false;
    char* end = nullptr;
    double parsed = std::strtod(text.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(parsed) || parsed < -0.25 || parsed > 0.25)
        return false;
    if (result) *result = parsed;
    return true;
}

static void RequireResource(const std::set<std::string>& known, const std::string& name,
                            const std::string& owner) {
    std::string key = Lower(Trim(name));
    if (!key.empty() && !known.count(key))
        g_validationErrors.push_back(owner + " references a missing resource: " + name);
}

static fs::path GameRoot() {
    wchar_t exe[MAX_PATH] = {};
    if (H && H->exeModule && GetModuleFileNameW((HMODULE)H->exeModule, exe, MAX_PATH))
        return fs::path(exe).parent_path();
    return g_base.parent_path();
}

static bool ValidatePlannedContent() {
    g_validationErrors.clear(); Ini resources, deposits, needs, buildings;
    if (!LoadPlannedDomain("resources", resources) || !LoadPlannedDomain("deposits", deposits) ||
        !LoadPlannedDomain("needs", needs) || !LoadPlannedDomain("buildings", buildings)) {
        g_validationErrors.push_back("unable to parse the consolidated INI files"); return false;
    }
    std::set<std::string> known = KnownResources(resources);

    if (const Section* list = resources.find("list")) for (const auto& entry : list->entries) {
        std::string value = entry.value; size_t comma = value.find(',');
        std::string donor = Trim(value.substr(0, comma));
        if (!donor.empty() && Lower(donor) != "custom" && !LooksNumeric(donor))
            RequireResource(known, donor, "resource [" + entry.key + "]");
    }
    for (const auto& section : deposits.sections) if (Lower(section.name) != "deposits") {
        std::string icon = deposits.get(section.name, "icon");
        if (!icon.empty()) RequireResource(known, icon, "deposit [" + section.name + "]");
        std::string offset = deposits.get(section.name, "richness_offset");
        if (!offset.empty() && !ParseRichnessOffset(offset))
            g_validationErrors.push_back("deposit [" + section.name +
                "] richness_offset must be a finite number from -0.25 to +0.25: " + offset);
    }
    if (const Section* list = needs.find("list")) for (const auto& entry : list->entries) {
        size_t comma = entry.value.find(',');
        RequireResource(known, Trim(entry.value.substr(0, comma)), "need [" + entry.key + "]");
        RequireResource(known, entry.key, "need [" + entry.key + "]");
    }

    fs::path media = GameRoot() / "media_soviet";
    for (const auto& section : buildings.sections) if (Lower(section.name) != "buildings" &&
        IsTrue(buildings.get(section.name, "enabled", "1"), true)) {
        for (const auto& entry : section.entries) if (Lower(entry.key) == "line") {
            auto words = Words(entry.value); if (words.size() < 2) continue;
            std::string command = Lower(words[0]);
            if (command == "$production" || command == "$consumption" || command == "$consumption_per_second")
                RequireResource(known, words[1], "building [" + section.name + "]");
            else if (command.rfind("$storage", 0) == 0 && words.size() >= 3 && !LooksNumeric(words.back()) &&
                     Lower(words.back()).rfind("resource_transport_", 0) != 0)
                RequireResource(known, words.back(), "building [" + section.name + "]");
        }
        std::string donor = buildings.get(section.name, "donor");
        if (donor.empty()) g_validationErrors.push_back("building [" + section.name + "] has no donor");
        else {
            if (!fs::is_regular_file(media / "buildings_types" / fs::u8path(donor + ".ini")))
                g_validationErrors.push_back("building [" + section.name + "] donor is missing buildings_types/" + donor + ".ini");
            if (!fs::is_regular_file(media / "buildings" / fs::u8path(donor + ".nmf")))
                g_validationErrors.push_back("building [" + section.name + "] donor is missing buildings/" + donor + ".nmf");
            if (!fs::is_regular_file(media / "buildings" / fs::u8path(donor + ".mtl")))
                g_validationErrors.push_back("building [" + section.name + "] donor is missing buildings/" + donor + ".mtl");
        }
    }
    return g_validationErrors.empty();
}

static bool ApplyDomainPlans() {
    bool ok = true; std::error_code ec;
    for (const auto& plan : g_domainPlans) {
        if (plan.writeBaseline) {
            fs::create_directories(plan.baseline.parent_path(), ec);
            if (ec || !WriteIfChanged(plan.baseline, plan.baselineText)) {
                H->log("sml       failed to preserve baseline for %s", plan.domain.c_str()); ok = false; continue;
            }
        }
        if (!WriteIfChanged(plan.target, plan.mergedText)) {
            H->log("sml       failed to write plugins\\%s.ini (%lu)", plan.domain.c_str(), GetLastError()); ok = false;
        } else if (g_verbose) H->log("sml       merged plugins\\%s.ini", plan.domain.c_str());
    }
    return ok;
}

struct WipMismatch {
    fs::path folder;
    std::string reason;
};

static bool IsSmlBuildingFolder(const std::string& name) {
    if (name.size() != 10 || name.rfind("91", 0) != 0 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return false;
    uint64_t id = _strtoui64(name.c_str(), nullptr, 10);
    return id >= 9100000000ull && id <= 9199999999ull;
}

static std::map<std::string, std::string> ExpectedWipBuildings() {
    std::map<std::string, std::string> expected;
    for (const auto& plan : g_domainPlans) {
        if (plan.domain != "buildings") continue;
        Ini ini; std::string error;
        if (!ParseIniText(plan.mergedText, ini, error)) break;
        if (!IsTrue(ini.get("buildings", "enabled", "1"), true)) break;
        for (const auto& section : ini.sections) {
            if (Lower(section.name) == "buildings") continue;
            if (!IsTrue(ini.get(section.name, "enabled", "1"), true) ||
                Trim(ini.get(section.name, "donor", "")).empty()) continue;
            for (const auto& entry : section.entries) if (Lower(entry.key) == "id") {
                std::string id = Trim(entry.value);
                if (IsSmlBuildingFolder(id)) expected[Lower(id)] = section.name;
                break;
            }
        }
        break;
    }
    return expected;
}

static std::string StampSection(const std::string& stamp) {
    size_t begin = stamp.find("section=");
    if (begin == std::string::npos) return {};
    begin += 8;
    size_t end = stamp.find(" donor=", begin);
    if (end == std::string::npos) end = stamp.find_first_of("\r\n", begin);
    return Trim(stamp.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
}

static std::vector<WipMismatch> FindWipMismatches(
    const fs::path& root, const std::map<std::string, std::string>& expected) {
    std::vector<WipMismatch> mismatches; std::error_code ec;
    if (!fs::is_directory(root, ec)) return mismatches;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_directory(ec)) continue;
        std::string id = PathUtf8(it->path().filename());
        if (!IsSmlBuildingFolder(id)) continue;
        auto wanted = expected.find(Lower(id));
        if (wanted == expected.end()) {
            mismatches.push_back({it->path(), "folder ID is not present in the current SML building catalog"});
            continue;
        }
        std::string stamp;
        if (!ReadText(it->path() / "tesmioloader.stamp", stamp)) {
            mismatches.push_back({it->path(), "missing tesmioloader.stamp"});
            continue;
        }
        std::string section = StampSection(stamp);
        if (section.empty() || Lower(section) != Lower(wanted->second))
            mismatches.push_back({it->path(), "stamp belongs to a different building section"});
    }
    return mismatches;
}

static fs::path WorkshopWipRoot() {
    wchar_t exe[32768]{};
    DWORD count = GetModuleFileNameW((HMODULE)H->exeModule, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    if (!H->exeModule) return g_base.parent_path() / "media_soviet" / "workshop_wip";
    if (!count || count >= sizeof(exe) / sizeof(exe[0])) return {};
    return fs::path(exe).parent_path() / "media_soviet" / "workshop_wip";
}

struct Asset { fs::path source; Mod* owner = nullptr; uint64_t hash = 0; };
static std::map<std::string, uint64_t> LoadAssetIndex() {
    std::map<std::string, uint64_t> out; std::ifstream f(g_stateDir / "assets.index", std::ios::binary); std::string line;
    while (std::getline(f, line)) { size_t bar = line.rfind('|'); if (bar != std::string::npos) out[line.substr(0, bar)] = _strtoui64(line.c_str() + bar + 1, nullptr, 16); } return out;
}
static std::map<std::string, Asset> g_assetPlan;
static std::map<std::string, uint64_t> g_oldAssets;

static void PlanAssets() {
    g_assetPlan.clear(); g_oldAssets.clear();
    if (!g_copyAssets) return;
    std::error_code ec;
    for (auto& m : g_mods) if (m.usable()) {
        fs::path assets = m.root / fs::u8path(m.manifest.get("content", "assets", "assets")); if (!fs::is_directory(assets, ec)) continue;
        for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) if (!ec && it->is_regular_file(ec)) {
            std::string rel = PathUtf8(fs::relative(it->path(), assets, ec)); std::string key = Lower(rel);
            auto found = g_assetPlan.find(key); if (found != g_assetPlan.end()) MarkConflict(*found->second.owner, "asset " + rel + " replaced by " + m.id);
            g_assetPlan[key] = {it->path(), &m, HashFile(it->path())};
        }
    }
    g_oldAssets = LoadAssetIndex();
}

static bool ApplyAssetPlan() {
    if (!g_copyAssets) return true;
    bool ok = true; std::error_code ec; const fs::path& vfs = g_vfsRoot;
    for (const auto& kv : g_oldAssets) if (g_assetPlan.find(kv.first) == g_assetPlan.end()) {
        fs::path target = vfs / fs::u8path(kv.first); if (fs::is_regular_file(target, ec) && HashFile(target) == kv.second) fs::remove(target, ec);
        if (ec) { H->log("sml       failed to remove stale asset %s", kv.first.c_str()); ok = false; ec.clear(); }
    }
    std::ostringstream index;
    for (const auto& kv : g_assetPlan) {
        fs::path target = vfs / fs::u8path(kv.first); bool copy = !fs::is_regular_file(target, ec) || HashFile(target) != kv.second.hash;
        if (copy) { fs::create_directories(target.parent_path(), ec); fs::copy_file(kv.second.source, target, fs::copy_options::overwrite_existing, ec); if (ec) { kv.second.owner->state = SML_MOD_ERROR; kv.second.owner->detail = "asset copy failed: " + kv.first; ok = false; ec.clear(); continue; } }
        index << kv.first << '|' << std::hex << kv.second.hash << std::dec << "\n";
    }
    if (!WriteIfChanged(g_stateDir / "assets.index", index.str())) ok = false;
    return ok;
}

struct ChildPlugin { HMODULE module = nullptr; TsmPluginStartFn start = nullptr; std::string modId, name; };
static std::vector<ChildPlugin> g_children;
static int CallChildInit(TsmPluginInitFn fn, TsmPluginInfo* info) {
    __try { return fn(H, info); } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int CallChildStart(TsmPluginStartFn fn) {
    __try { return fn(); } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

struct EmbeddedPlugin {
    const char* key;
    const char* dll;
    TsmPluginApiVersionFn api;
    TsmPluginInitFn init;
    TsmPluginStartFn start;
    bool initialized;
};
static EmbeddedPlugin g_embedded[] = {
    {"resources", "resources.dll", SmlResourcesApiVersion, SmlResourcesInit, nullptr, false},
    {"deposits",  "deposits.dll",  SmlDepositsApiVersion,  SmlDepositsInit,  nullptr, false},
    {"needs",     "needs.dll",     SmlNeedsApiVersion,     SmlNeedsInit,     SmlNeedsStart, false},
    {"buildings", "buildings.dll", SmlBuildingsApiVersion, SmlBuildingsInit, nullptr, false}
};

static bool DisableExternalPlugins() {
    if (!g_disableExternal) return true;
    bool ok = true;
    fs::path ini = g_base / "tesmioloader.ini";
    for (auto& component : g_embedded) {
        if (GetModuleHandleA(component.dll)) {
            g_externalAlreadyLoaded = true;
            if (strcmp(component.key, "buildings") == 0)
                H->log("sml       external buildings.dll already loaded; merged buildings will be refreshed safely");
            else
                H->log("sml       external %s already loaded; duplicate-sensitive embedded copy skipped", component.dll);
        }
        if (!WritePrivateProfileStringA("plugins", component.key, "0", ini.u8string().c_str())) {
            H->log("sml       could not disable external %s (%lu)", component.dll, GetLastError()); ok = false;
        }
    }
    return ok;
}

static void InitEmbeddedPlugins() {
    if (!g_enableEmbedded) return;
    for (auto& component : g_embedded) {
        // buildings installs no hooks and only writes generated Workshop files,
        // so running it again after the merge is safe and fixes the migration
        // launch itself. The other three mutate process state and must never be
        // initialized twice in one process.
        if (GetModuleHandleA(component.dll) && strcmp(component.key, "buildings") != 0) continue;
        unsigned api = component.api();
        if (api < TSM_API_VERSION_MIN || api > H->apiVersion) {
            H->log("sml       embedded %s API %u refused", component.key, api); continue;
        }
        TsmPluginInfo info{}; int rc = CallChildInit(component.init, &info);
        if (rc != 0) { H->log("sml       embedded %s inactive (Init=%d)", component.key, rc); continue; }
        component.initialized = true;
        H->log("sml       embedded %-12s %s initialized", component.key, info.version ? info.version : "?");
    }
}

static int PlannedCount(const char* domain, const char* settings, bool listDomain) {
    Ini ini; if (!LoadPlannedDomain(domain, ini)) return -1;
    return (int)ContentCount(ini, settings, listDomain);
}

static bool EmbeddedInitialized(const char* key) {
    for (const auto& component : g_embedded)
        if (strcmp(component.key, key) == 0) return component.initialized;
    return false;
}

static bool ValidateEmbeddedRuntime() {
    std::vector<std::string> errors;
    int expectedResources = PlannedCount("resources", "resources", true);
    int actualResources = SmlResourcesDeclaredCount();
    if (expectedResources != actualResources)
        errors.push_back("resources planned=" + std::to_string(expectedResources) +
                         ", registered=" + std::to_string(actualResources));
    if (expectedResources > 0 && !SmlResourcesHookReady())
        errors.push_back("resources registration hook was not installed in mode 2");
    if (expectedResources > 0 && !EmbeddedInitialized("resources"))
        errors.push_back("resources component declined initialization");
    Ini resources;
    if (LoadPlannedDomain("resources", resources)) if (const Section* list = resources.find("list")) {
        std::set<std::string> actual;
        for (int i = 0; i < actualResources; ++i) {
            const char* name = SmlResourcesDeclaredName(i); if (name) actual.insert(Lower(name));
        }
        for (const auto& entry : list->entries) if (!actual.count(Lower(entry.key)))
            errors.push_back("resource was not registered: " + entry.key);
    }

    int expectedDeposits = PlannedCount("deposits", "deposits", false);
    int actualDeposits = SmlDepositsDeclaredCount();
    if (expectedDeposits != actualDeposits)
        errors.push_back("deposits planned=" + std::to_string(expectedDeposits) +
                         ", accepted=" + std::to_string(actualDeposits));
    if (expectedDeposits > 0 && !SmlDepositsPatchReady())
        errors.push_back("deposit type patch was not installed");
    if (expectedDeposits > 0 && !SmlDepositsMapsReady())
        errors.push_back("additional deposit map hooks were not installed");
    if (expectedDeposits > 0 && !EmbeddedInitialized("deposits"))
        errors.push_back("deposits component declined initialization");

    int expectedNeeds = PlannedCount("needs", "needs", true);
    int actualNeeds = SmlNeedsDeclaredCount();
    if (expectedNeeds != actualNeeds)
        errors.push_back("needs planned=" + std::to_string(expectedNeeds) +
                         ", accepted=" + std::to_string(actualNeeds));
    if (expectedNeeds > 0 && !EmbeddedInitialized("needs"))
        errors.push_back("needs component declined initialization");

    int expectedBuildings = PlannedCount("buildings", "buildings", false);
    if (expectedBuildings != SmlBuildingsEnabledCount())
        errors.push_back("buildings planned=" + std::to_string(expectedBuildings) +
                         ", enabled=" + std::to_string(SmlBuildingsEnabledCount()));
    if (SmlBuildingsIncompleteCount() > 0 || SmlBuildingsCompleteCount() != expectedBuildings)
        errors.push_back("buildings complete=" + std::to_string(SmlBuildingsCompleteCount()) +
                         ", incomplete=" + std::to_string(SmlBuildingsIncompleteCount()));
    if (expectedBuildings > 0 && !EmbeddedInitialized("buildings"))
        errors.push_back("buildings component declined initialization");

    if (errors.empty()) {
        H->log("sml       validation OK: resources=%d deposits=%d needs=%d buildings=%d",
               actualResources, actualDeposits, actualNeeds, SmlBuildingsCompleteCount());
        return true;
    }
    g_validationErrors = std::move(errors);
    for (const auto& error : g_validationErrors) H->log("sml       FATAL %s", error.c_str());
    return false;
}
static void LoadHooks() {
    if (!g_loadHooks) return; std::error_code ec;
    for (auto& m : g_mods) if (m.usable()) for (const auto& path : m.hooks) {
        if (!fs::is_regular_file(path, ec) || Lower(path.extension().u8string()) != ".dll") { m.state = SML_MOD_ERROR; m.detail = "hook missing/not DLL: " + PathUtf8(path); continue; }
        HMODULE dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll) { m.state = SML_MOD_ERROR; m.detail = "hook load failed: " + std::to_string(GetLastError()); continue; }
        auto api = (TsmPluginApiVersionFn)GetProcAddress(dll, TSM_EXPORT_APIVERSION); auto init = (TsmPluginInitFn)GetProcAddress(dll, TSM_EXPORT_INIT);
        if (!api || !init) { FreeLibrary(dll); m.state = SML_MOD_ERROR; m.detail = "hook is not a Tesmio plugin"; continue; }
        unsigned version = api(); if (version < TSM_API_VERSION_MIN || version > H->apiVersion) { FreeLibrary(dll); m.state = SML_MOD_INCOMPATIBLE; m.detail = "hook API " + std::to_string(version) + " refused"; continue; }
        TsmPluginInfo info{}; int rc = CallChildInit(init, &info); if (rc != 0) { FreeLibrary(dll); m.state = SML_MOD_ERROR; m.detail = "hook Init declined/faulted"; continue; }
        ChildPlugin child; child.module = dll; child.start = (TsmPluginStartFn)GetProcAddress(dll, TSM_EXPORT_START); child.modId = m.id; child.name = info.name ? info.name : path.filename().u8string(); g_children.push_back(std::move(child));
        H->log("sml       hook %-20s from %s", g_children.back().name.c_str(), m.id.c_str());
    }
}

static const char* ModStateText(SmlModState state) {
    switch (state) {
        case SML_MOD_ACTIVE: return "active";
        case SML_MOD_ADDED: return "added";
        case SML_MOD_CONFLICT: return "conflict";
        case SML_MOD_DISABLED: return "disabled";
        case SML_MOD_INCOMPATIBLE: return "incompatible";
        case SML_MOD_MISSING_DEPENDENCY: return "missing dependency";
        case SML_MOD_ERROR: return "error";
        default: return "unknown";
    }
}

static std::string IndexEscape(const std::string& value) {
    std::string out; out.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '|' || c == '\n' || c == '\r') {
            out += '\\'; out += c == '\n' ? 'n' : c == '\r' ? 'r' : c;
        } else out += c;
    }
    return out;
}

static int ConfirmationSignatureState(SmlModState state) {
    return state == SML_MOD_ADDED ? SML_MOD_ACTIVE : (int)state;
}

static std::string BuildConfirmationIndex() {
    std::ostringstream out;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        int signatureState = ConfirmationSignatureState(m.state);
        out << i << '|' << IndexEscape(m.id) << '|' << IndexEscape(m.version) << '|'
            << std::hex << m.fingerprint << std::dec << '|' << signatureState << '|'
            << IndexEscape(m.detail) << "\n";
    }
    return out.str();
}

static bool ShouldRequestConfirmation(const std::string& mode, const std::string& current,
                                      const std::string& previous, bool haveMods) {
    if (!haveMods || mode == "never") return false;
    if (mode == "always") return true;
    return current != previous;
}

static std::wstring Utf8Wide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    if (!count) count = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
    if (!count) return L"?";
    std::wstring out((size_t)count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), out.data(), count))
        MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), out.data(), count);
    return out;
}

static void ShowWipMismatchWarning(const std::vector<WipMismatch>& mismatches) {
    std::ostringstream message;
    message << "SML detected stale or conflicting generated building folders in workshop_wip.\r\n\r\n"
            << "Delete every folder listed below before launching the game. Leaving these folders in place can make the game crash.\r\n\r\n";
    for (const auto& mismatch : mismatches)
        message << "• " << PathUtf8(mismatch.folder) << "\r\n  " << mismatch.reason << "\r\n";
    message << "\r\nThe game will now close. No SML configuration was applied.";
    std::wstring wide = Utf8Wide(message.str());
    MessageBoxW(nullptr, wide.c_str(), L"Soviet Mod Loader — unsafe workshop_wip",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

static void ShowValidationFailure(const std::vector<std::string>& errors, bool applied) {
    std::ostringstream message;
    message << "Soviet Mod Loader detected unsafe content and blocked game startup.\r\n\r\n";
    for (const auto& error : errors) message << "- " << error << "\r\n";
    message << "\r\nSee tesmioloader.log and fix the mod or installation before trying again.";
    if (!applied) message << " No SML configuration was applied.";
    std::wstring wide = Utf8Wide(message.str());
    MessageBoxW(nullptr, wide.c_str(), L"Soviet Mod Loader - startup blocked",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

static bool ConfirmModsWithUser() {
    int usable = 0, warnings = 0;
    std::ostringstream details;
    details << "MODS TO BE LOADED\r\n";
    for (const auto& m : g_mods) if (m.usable()) {
        usable++;
        details << "• " << m.name << " " << m.version << "  [" << m.id << "]";
        if (m.state == SML_MOD_CONFLICT) details << " - conflict";
        details << "\r\n";
    }
    for (const auto& m : g_mods) if (!m.usable() || m.state == SML_MOD_CONFLICT) warnings++;
    if (warnings) {
        details << "\r\nWARNINGS\r\n";
        for (const auto& m : g_mods) if (!m.usable() || m.state == SML_MOD_CONFLICT) {
            details << "• " << m.name << " [" << ModStateText(m.state) << "]";
            if (!m.detail.empty()) details << ": " << m.detail;
            details << "\r\n";
        }
    }

    std::wstring title = L"Soviet Mod Loader";
    std::wstring instruction = L"The mod configuration has changed";
    std::wstring content = L"SML will load " + std::to_wstring(usable) +
                           (usable == 1 ? L" mod. Start the game?" : L" mods. Start the game?");
    std::wstring expanded = Utf8Wide(details.str());
    std::wstring acceptText = L"Load mods and start\nApply the displayed configuration and continue.";
    std::wstring declineText = L"Decline and exit\nDo not apply changes; close the game.";
    TASKDIALOG_BUTTON buttons[] = {{1001, acceptText.c_str()}, {1002, declineText.c_str()}};

    HMODULE common = GetModuleHandleW(L"comctl32.dll"); bool owned = false;
    if (!common) { common = LoadLibraryW(L"comctl32.dll"); owned = common != nullptr; }
    typedef HRESULT (WINAPI* t_TaskDialogIndirect)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    t_TaskDialogIndirect taskDialog = common ? (t_TaskDialogIndirect)GetProcAddress(common, "TaskDialogIndirect") : nullptr;
    if (taskDialog) {
        TASKDIALOGCONFIG cfg{}; cfg.cbSize = sizeof(cfg); cfg.hInstance = GetModuleHandleW(nullptr);
        cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_SIZE_TO_CONTENT;
        cfg.pszWindowTitle = title.c_str(); cfg.pszMainIcon = warnings ? TD_WARNING_ICON : TD_INFORMATION_ICON;
        cfg.pszMainInstruction = instruction.c_str(); cfg.pszContent = content.c_str();
        cfg.cButtons = (UINT)(sizeof(buttons) / sizeof(buttons[0])); cfg.pButtons = buttons; cfg.nDefaultButton = 1001;
        cfg.pszExpandedInformation = expanded.c_str(); cfg.pszExpandedControlText = L"Show details";
        cfg.pszCollapsedControlText = L"Hide details";
        int pressed = IDCANCEL; HRESULT hr = taskDialog(&cfg, &pressed, nullptr, nullptr);
        if (owned) FreeLibrary(common);
        if (SUCCEEDED(hr)) return pressed == 1001;
    } else if (owned) FreeLibrary(common);

    std::wstring fallback = instruction + L"\r\n\r\n" + content + L"\r\n\r\n" + expanded +
                            L"\r\nSelect Yes to load mods or No to exit.";
    int answer = MessageBoxW(nullptr, fallback.c_str(), title.c_str(),
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1 | MB_SETFOREGROUND | MB_TOPMOST);
    return answer == IDYES;
}

static bool SaveConfirmationIndex(const std::string& index) {
    return WriteIfChanged(g_stateDir / "confirmation.index", index);
}

static bool SaveModIndex() {
    std::ostringstream out; for (const auto& m : g_mods) out << m.id << '|' << std::hex << m.fingerprint << std::dec << "\n";
    bool ok = WriteIfChanged(g_stateDir / "mods.index", out.str());
    auto json = [](const std::string& s) { std::string out; for (unsigned char c : s) { if (c == '\\' || c == '"') { out += '\\'; out += (char)c; } else if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r"; else if (c >= 32) out += (char)c; } return out; };
    std::ostringstream report; report << "{\n  \"generation\": " << g_generation << ",\n  \"mods\": [\n";
    for (size_t i = 0; i < g_mods.size(); ++i) { const auto& m = g_mods[i]; report << "    {\"id\": \"" << json(m.id) << "\", \"state\": " << (int)m.state << ", \"detail\": \"" << json(m.detail) << "\"}" << (i + 1 == g_mods.size() ? "" : ",") << "\n"; }
    report << "  ]\n}\n"; return WriteIfChanged(g_stateDir / "report.json", report.str()) && ok;
}

static int ApiCount() { return (int)g_mods.size(); }
static int ApiGet(int index, SmlModInfo* out) {
    if (!out || index < 0 || index >= (int)g_mods.size()) return 0; const Mod& m = g_mods[index];
    out->id = m.id.c_str(); out->name = m.name.c_str(); out->version = m.version.c_str(); out->workshopPath = m.pathText.c_str();
    out->addedUtc = m.added; out->fingerprint = m.fingerprint; out->priority = m.priority; out->state = m.state; out->detail = m.detail.c_str(); return 1;
}
static unsigned long long ApiGeneration() { return g_generation; }
static const SmlApi kApi = {ApiCount, ApiGet, ApiGeneration};

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void) { return TSM_API_VERSION; }

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info) {
    H = host; info->name = "Soviet Mod Loader"; info->version = "0.5.2"; g_base = fs::u8path(host->baseDir);
    g_applicationReady = false; g_pendingConfirmationIndex.clear();
    g_vfsRoot = host->structSize >= offsetof(TsmHost, vfsRoot) + sizeof(host->vfsRoot) && host->vfsRoot
              ? fs::u8path(host->vfsRoot) : ResolveVfsRoot(g_base);
    const char* ini = "plugins\\soviet_mod_loader.ini";
    std::error_code configEc;
    if (!fs::is_regular_file(g_base / "plugins" / "soviet_mod_loader.ini", configEc) &&
        fs::is_regular_file(g_base / "plugins" / "000_soviet_mod_loader.ini", configEc))
        ini = "plugins\\000_soviet_mod_loader.ini";
    char value[1024]{};
    if (!H->configInt(ini, "loader", "enabled", 1)) return 1;
    int appId = H->configInt(ini, "loader", "steam_app_id", 784150);
    H->configString(ini, "loader", "workshop_root", value, sizeof(value), "auto"); std::string rootSetting = value;
    H->configString(ini, "loader", "state_dir", value, sizeof(value), "soviet_mod_loader"); g_stateDir = g_base / fs::u8path(value);
    g_copyAssets = H->configInt(ini, "loader", "copy_assets", 1) != 0; g_loadHooks = H->configInt(ini, "loader", "load_hooks", 1) != 0; g_verbose = H->configInt(ini, "loader", "log_verbose", 0) != 0;
    g_disableExternal = H->configInt(ini, "loader", "disable_external_plugins", 1) != 0;
    g_enableEmbedded = H->configInt(ini, "loader", "embedded_plugins", 1) != 0;
    H->configString(ini, "loader", "confirmation_mode", value, sizeof(value), "changes");
    g_confirmationMode = Lower(Trim(value));
    if (g_confirmationMode != "always" && g_confirmationMode != "changes" && g_confirmationMode != "never") {
        H->log("sml       invalid confirmation_mode=%s; using changes", g_confirmationMode.c_str());
        g_confirmationMode = "changes";
    }
    std::error_code ec; g_generation = (unsigned long long)time(nullptr);
    fs::path workshop = FindWorkshopRoot(rootSetting, appId); H->log("sml       Workshop %s", workshop.empty() ? "not found" : PathUtf8(workshop).c_str());
    H->log("sml       VFS %s", PathUtf8(g_vfsRoot).c_str());
    try {
        DiscoverMods(workshop); ResolveDependenciesAndOrder(); LoadCatalog();
        g_domainPlans.clear();
        PlanDomain("resources"); PlanDomain("deposits"); PlanDomain("needs"); PlanDomain("buildings");
        PlanAssets();

        if (!ValidatePlannedContent()) {
            for (const auto& error : g_validationErrors) H->log("sml       FATAL %s", error.c_str());
            ShowValidationFailure(g_validationErrors, false);
            ExitProcess(ERROR_INVALID_DATA);
            return 1;
        }

        fs::path wipRoot = WorkshopWipRoot();
        std::vector<WipMismatch> wipMismatches = FindWipMismatches(wipRoot, ExpectedWipBuildings());
        if (!wipMismatches.empty()) {
            H->log("sml       FATAL workshop_wip mismatch: %d unsafe folder(s)", (int)wipMismatches.size());
            for (const auto& mismatch : wipMismatches)
                H->log("sml       delete %s (%s)", PathUtf8(mismatch.folder).c_str(), mismatch.reason.c_str());
            ShowWipMismatchWarning(wipMismatches);
            ExitProcess(ERROR_INVALID_DATA);
            return 1;
        }

        std::string confirmationIndex = BuildConfirmationIndex(), previousConfirmation;
        ReadText(g_stateDir / "confirmation.index", previousConfirmation);
        if (ShouldRequestConfirmation(g_confirmationMode, confirmationIndex,
                                      previousConfirmation, !g_mods.empty())) {
            if (!ConfirmModsWithUser()) {
                H->log("sml       mod configuration refused by user; terminating before application");
                ExitProcess(ERROR_CANCELLED);
                return 1;
            }
            H->log("sml       mod configuration accepted by user");
        }

        fs::create_directories(g_stateDir, ec);
        bool applied = !ec;
        applied = DisableExternalPlugins() && applied;
        applied = ApplyDomainPlans() && applied;
        applied = SaveCatalog() && applied;
        applied = ApplyAssetPlan() && applied;
        InitEmbeddedPlugins();
#ifdef SML_TESTING
        bool runtimeValid = true;
#else
        bool runtimeValid = ValidateEmbeddedRuntime();
#endif
        if (runtimeValid) LoadHooks();
        applied = runtimeValid && SaveModIndex() && applied;
        if (applied) {
            g_pendingConfirmationIndex = confirmationIndex;
            g_applicationReady = true;
        } else {
            H->log("sml       application incomplete; confirmation will be requested again");
        }
        if (!runtimeValid) {
            ShowValidationFailure(g_validationErrors, true);
            ExitProcess(ERROR_INVALID_DATA);
            return 1;
        }
        if (g_externalAlreadyLoaded) H->log("sml       restart required once; external plugin keys are now disabled");
    } catch (const std::exception& e) { H->log("sml       soft failure: %s", e.what()); return 1; }
    for (const auto& m : g_mods) H->log("sml       %-24s state=%d %s", m.id.c_str(), (int)m.state, m.detail.c_str());
    H->provide(SML_SERVICE, SML_SERVICE_VERSION, &kApi); return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void) {
    for (auto& component : g_embedded) if (component.initialized && component.start) {
        int rc = CallChildStart(component.start);
        if (rc) H->log("sml       embedded %s Start returned %d", component.key, rc);
    }
    if (PlannedCount("needs", "needs", true) > 0 && !SmlNeedsHooksReady()) {
        g_validationErrors = {"required needs hooks were not installed"};
        H->log("sml       FATAL %s", g_validationErrors[0].c_str());
        ShowValidationFailure(g_validationErrors, true);
        ExitProcess(ERROR_INVALID_DATA);
        return 1;
    }
    for (auto& child : g_children) if (child.start) { int rc = CallChildStart(child.start); if (rc) H->log("sml       hook %s Start returned %d", child.name.c_str(), rc); }
    if (g_applicationReady && !g_pendingConfirmationIndex.empty()) {
        if (!SaveConfirmationIndex(g_pendingConfirmationIndex))
            H->log("sml       failed to persist confirmation.index after runtime validation");
        else
            H->log("sml       confirmation.index persisted after all component validations");
        g_pendingConfirmationIndex.clear();
    }
    return 0;
}
