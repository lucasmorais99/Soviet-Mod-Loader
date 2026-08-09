#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <string>

#include "../plugins/000_soviet_mod_loader/000_soviet_mod_loader.cpp"

extern "C" unsigned SmlDepositsNoiseChecksumForTest(unsigned __int64 seed);
extern "C" int SmlDepositsShouldGenerateForTest(int manifestValid, int ddsExists,
                                                  int initialized);
extern "C" int SmlDepositsChannelIsolationForTest(unsigned __int64 seed, int component);

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static std::string fakeWorkshop;
static void FakeLog(const char*, ...) {}
static int FakeInt(const char*, const char*, const char* key, int fallback) {
    if (std::string(key) == "log_verbose") return 0;
    if (std::string(key) == "embedded_plugins") return 0;
    return fallback;
}
static int FakeString(const char*, const char*, const char* key, char* out,
                      int outSize, const char* fallback) {
    std::string value = std::string(key) == "workshop_root" ? fakeWorkshop :
                        std::string(key) == "confirmation_mode" ? "never" : fallback;
    strncpy_s(out, (size_t)outSize, value.c_str(), _TRUNCATE);
    return 1;
}
static int FakeProvide(const char*, unsigned, const void*) { return 1; }
static const void* FakeConsume(const char*, unsigned) { return nullptr; }
static void Put(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary); f << text;
}

int main() {
    CHECK(SmlBuildingsApiVersion() == TSM_API_VERSION);
    CHECK(SmlResourcesApiVersion() == TSM_API_VERSION);
    CHECK(SmlDepositsApiVersion() == TSM_API_VERSION);
    CHECK(SmlNeedsApiVersion() == TSM_API_VERSION);
    CHECK(ResolveVfsRoot(std::filesystem::path("C:/mods/tesmioloader/build")) ==
          std::filesystem::path("C:/mods/tesmioloader/vfs"));
    CHECK(SmlDepositsShouldGenerateForTest(0, 0, 0));
    CHECK(!SmlDepositsShouldGenerateForTest(0, 1, 0)); // legacy DDS is opaque
    CHECK(!SmlDepositsShouldGenerateForTest(1, 1, 1));
    CHECK(SmlDepositsShouldGenerateForTest(1, 1, 0));  // late-installed deposit
    CHECK(SmlDepositsShouldGenerateForTest(1, 0, 1));  // manifest cannot replace a DDS
    unsigned noiseA = SmlDepositsNoiseChecksumForTest(0x123456789abcdef0ull);
    CHECK(noiseA == SmlDepositsNoiseChecksumForTest(0x123456789abcdef0ull));
    CHECK(noiseA != SmlDepositsNoiseChecksumForTest(0x123456789abcdef1ull));
    CHECK(SmlDepositsChannelIsolationForTest(0x123456789abcdef0ull, 2));
    CHECK(ShouldRequestConfirmation("always", "same", "same", true));
    CHECK(!ShouldRequestConfirmation("changes", "same", "same", true));
    CHECK(ShouldRequestConfirmation("changes", "new", "old", true));
    CHECK(!ShouldRequestConfirmation("never", "new", "old", true));
    CHECK(!ShouldRequestConfirmation("always", "new", "old", false));
    CHECK(ConfirmationSignatureState(SML_MOD_ADDED) == SML_MOD_ACTIVE);
    CHECK(ConfirmationSignatureState(SML_MOD_CONFLICT) == SML_MOD_CONFLICT);

    Ini ini; std::string error;
    CHECK(ParseIniText("[building]\nline = one\nline = two\n", ini, error));
    CHECK(ini.sections.size() == 1);
    CHECK(ini.getAll("BUILDING", "line").size() == 2);
    CHECK(ini.get("building", "line") == "two");

    Ini bad; error.clear();
    CHECK(!ParseIniText("key=value\n", bad, error));
    CHECK(!error.empty());

    CHECK(Satisfies("1.4.2", ">=1.2.0"));
    CHECK(!Satisfies("1.1.9", ">=1.2.0"));
    CHECK(Satisfies("3.0", "=3.0.0"));
    CHECK(Satisfies("9.0", "*"));

    CHECK(SafeContentSection("resources", "list", false));
    CHECK(!SafeContentSection("resources", "resources", false));
    CHECK(!SafeContentSection("deposits", "deposits", false));
    CHECK(SafeContentSection("deposits", "copper", false));
    CHECK(SafeContentSection("deposits", "deposits", true));

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "sml-core-tests";
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    std::filesystem::path file = dir / "atomic.ini";
    CHECK(WriteIfChanged(file, "[x]\r\na = 1\r\n"));
    uint64_t firstHash = HashFile(file);
    CHECK(WriteIfChanged(file, "[x]\r\na = 1\r\n"));
    CHECK(HashFile(file) == firstHash);
    CHECK(WriteIfChanged(file, "[x]\r\na = 2\r\n"));
    CHECK(HashFile(file) != firstHash);

    std::filesystem::path wip = dir / "workshop_wip";
    std::map<std::string, std::string> expectedWip = {
        {"9100000001", "automatic_factory"}, {"9100000002", "missing_is_safe"}
    };
    Put(wip / "9100000001/tesmioloader.stamp",
        "tesmioloader generated\r\nsection=automatic_factory donor=fabric_factory object=Factory\r\n");
    std::filesystem::create_directories(wip / "9200000000", ec);
    CHECK(FindWipMismatches(wip, expectedWip).empty());
    std::filesystem::create_directories(wip / "9100000003", ec);
    CHECK(FindWipMismatches(wip, expectedWip).size() == 1);
    std::filesystem::remove_all(wip / "9100000003", ec);
    Put(wip / "9100000001/tesmioloader.stamp",
        "tesmioloader generated\r\nsection=wrong_factory donor=fabric_factory object=Factory\r\n");
    CHECK(FindWipMismatches(wip, expectedWip).size() == 1);
    std::filesystem::remove(wip / "9100000001/tesmioloader.stamp", ec);
    CHECK(FindWipMismatches(wip, expectedWip).size() == 1);
    std::filesystem::remove_all(dir, ec);

    // End-to-end scan/merge against a fake Workshop and fake Tesmio v3 host.
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("sml-integration-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::path base = root / "tesmioloader";
    std::filesystem::path workshop = root / "workshop";
    Put(base / "plugins/resources.ini", "[resources]\nhook=2\n[list]\nbase_only=rawiron\n");
    Put(workshop / "100/soviet.mod.ini",
        "[mod]\nid=org.test.first\nname=First\nversion=1.0.0\nadded_utc=100\n"
        "tesmio_api_min=3\ntesmio_api_max=3\n[content]\nresources=tesmio/resources.ini\n");
    Put(workshop / "100/tesmio/resources.ini", "[list]\nshared=rawiron, First\n");
    Put(workshop / "200/soviet.mod.ini",
        "[mod]\nid=org.test.second\nname=Second\nversion=1.0.0\nadded_utc=200\n"
        "tesmio_api_min=3\ntesmio_api_max=3\n[dependencies]\norg.test.first=>=1.0.0\n"
        "[content]\nresources=tesmio/resources.ini\nassets=assets\n");
    Put(workshop / "200/tesmio/resources.ini", "[list]\nshared=57, bauxite, Second\n");
    Put(workshop / "200/tesmio/buildings.ini",
        "[automatic_factory]\ndonor=fabric_factory\nobject=AutomaticFactory\nname=Automatic Factory\nline=$TYPE_FACTORY\n");
    Put(workshop / "200/tesmio/deposits.ini",
        "[automatic_ore]\ntoken=$TYPE_MINE_AUTOMATIC\nradius=ore\nmap=auto\n");
    Put(workshop / "200/assets/media_soviet/resources/shared.txt", "asset-v1");
    fakeWorkshop = workshop.u8string();
    std::string baseText = base.u8string();
    TsmHost host{}; host.apiVersion = 3; host.structSize = sizeof(host); host.baseDir = baseText.c_str();
    host.pluginDir = baseText.c_str(); host.log = FakeLog; host.configInt = FakeInt;
    host.configString = FakeString; host.provide = FakeProvide; host.consume = FakeConsume;
    TsmPluginInfo plugin{};
    CHECK(TsmPluginInit(&host, &plugin) == 0);
    std::string merged; CHECK(ReadText(base / "plugins/resources.ini", merged));
    CHECK(merged.find("shared = bauxite, Second") != std::string::npos);
    CHECK(merged.find("shared = 57") == std::string::npos);
    CHECK(merged.find("base_only = rawiron") != std::string::npos);
    std::string staged; CHECK(ReadText(base / "vfs/media_soviet/resources/shared.txt", staged));
    CHECK(staged == "asset-v1");
    CHECK(ApiCount() == 2);
    SmlModInfo first{}, second{}; CHECK(ApiGet(0, &first)); CHECK(ApiGet(1, &second));
    CHECK(std::string(first.id) == "org.test.first");
    CHECK(first.state == SML_MOD_CONFLICT);
    CHECK(std::string(second.id) == "org.test.second");
    Ini generatedBuildings, generatedDeposits; std::string generatedError;
    CHECK(LoadIni(base / "plugins/buildings.ini", generatedBuildings, generatedError));
    uint64_t buildingId = _strtoui64(generatedBuildings.get("automatic_factory", "id").c_str(), nullptr, 10);
    CHECK(buildingId >= 9100000000ull && buildingId <= 9199999999ull);
    auto expectedGeneratedWip = ExpectedWipBuildings();
    CHECK(expectedGeneratedWip[std::to_string(buildingId)] == "automatic_factory");
    generatedError.clear(); CHECK(LoadIni(base / "plugins/deposits.ini", generatedDeposits, generatedError));
    int depositType = std::atoi(generatedDeposits.get("automatic_ore", "type").c_str());
    CHECK(depositType >= 10 && depositType <= 127);
    CHECK(generatedDeposits.get("automatic_ore", "map") == "auto");
    CHECK(generatedDeposits.get("automatic_ore", "component").empty());
    CHECK(std::filesystem::is_regular_file(base / "soviet_mod_loader/catalog.ini"));
    std::filesystem::remove_all(root, ec);

    if (failures) return 1;
    std::puts("Soviet Mod Loader core tests: OK");
    return 0;
}
