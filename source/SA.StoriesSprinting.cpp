#include "plugin.h"
#include "common.h"
#include "CPlayerPed.h"
#include "CWorld.h"
#include "CStats.h"
#include "CWeapon.h"
#include "CWeaponInfo.h"
#include "CAnimManager.h"
#include "CAnimBlock.h"
#include "CPools.h"
#include "CModelInfo.h"
#include "CPedModelInfo.h"
#include "eStats.h"
#include <windows.h>
#include <string>
#include <set>

using namespace plugin;

// mov [esi+0x4D4], eax
static const uintptr_t WRITE_ADDR = 0x609A4E;
static const int WALK_GROUP_OFFSET = 0x4D4;

// reblends the walk/run/sprint slots from the 0x4D4 group
static const uintptr_t RELOAD_MOVE_ANIMS = 0x609650;
typedef void(__thiscall* ReloadMoveAnims_t)(void*);

// walkstyle groups: 57 playerrocket, 60 player2armed, 63 playerBBBat (the jog),
// 66 playercsaw (fire-ext); each is followed by its fat and muscular variant
static const int ROCKET_BASE = 57;
static const int RIFLE_BASE = 60;
static const int JOG_BASE = 63;
static const int FIREEXT_BASE = 66;

// anim-pointer redirects (formerly eASIer's sprint.txt)
struct AnimPatch { uintptr_t dest; unsigned int value; };
static const AnimPatch g_animPatches[] = {
    { 0x8A8AF4, 0x85D24C }, { 0x8A8B0C, 0x85D24C }, { 0x8A8B24, 0x85D24C },
    { 0x8A8B3C, 0x85D3A8 }, { 0x8A8B54, 0x85D3A8 }, { 0x8A8B6C, 0x85D3A8 },
    { 0x8A8B84, 0x85D938 }, { 0x8A8B9C, 0x85D8F8 }, { 0x8A8BB4, 0x85D8B8 },
    { 0x8A8BCC, 0x85D3BC }, { 0x8A8BE4, 0x85D3BC }, { 0x8A8BFC, 0x85D3BC },
    { 0x8A8B80, 0x85D3CC }, { 0x8A8B98, 0x85D3CC }, { 0x8A8BB0, 0x85D3CC },
    { 0x8A8ACC, 0x85D920 }, { 0x8A8B14, 0x85D920 }, { 0x8A8B5C, 0x85D920 },
    { 0x8A8BA4, 0x85D920 }, { 0x8A8BEC, 0x85D920 }, { 0x8A8AE4, 0x85D920 },
    { 0x8A8B2C, 0x85D920 }, { 0x8A8B74, 0x85D920 }, { 0x8A8BBC, 0x85D920 },
    { 0x8A8C04, 0x85D920 },
};

class StoriesSprinting {
public:
    static std::set<int> jogWeapons;      // force-jog (priority ON), by weapon type or model id
    static std::set<int> noJogWeapons;    // force-no-jog (priority OFF), overrides everything
    static std::set<int> jogSlots;        // weapon.dat slots that jog by default (fallback)
    static std::set<int> fireExtWeapons;
    static std::set<int> aiRocketWeapons; // ped-only lists (AI walkstyles)
    static std::set<int> aiRifleWeapons;
    static std::set<int> aiBatWeapons;
    static std::set<int> aiHeavyWeapons;
    static std::set<int> aiRifleSlots;
    static bool aiWalkstyles;
    static bool aiCombo;
    static bool noFat;
    static bool noMuscle;
    static bool fireExtFix;
    static bool patched;
    static std::string iniPath;
    static int reloadTimer;

    static std::string AsiFolder() {
        char path[MAX_PATH] = { 0 };
        HMODULE hm = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&AsiFolder, &hm);
        GetModuleFileNameA(hm, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return (slash != std::string::npos) ? p.substr(0, slash + 1) : "";
    }

    static void ParseIds(const char* s, std::set<int>& out) {
        out.clear();
        for (const char* p = s; *p; ) {
            while (*p && (*p < '0' || *p > '9')) p++;
            if (!*p) break;
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            out.insert(v);
        }
    }

    static void LoadConfig() {
        const char* f = iniPath.c_str();
        noFat = GetPrivateProfileIntA("SA.StoriesSprinting", "NoFat1Armed", 0, f) != 0;
        noMuscle = GetPrivateProfileIntA("SA.StoriesSprinting", "NoMuscle1Armed", 0, f) != 0;
        fireExtFix = GetPrivateProfileIntA("SA.StoriesSprinting", "FireExtinguisherWalkstyleFix", 1, f) != 0;

        char buf[2048];
        GetPrivateProfileStringA("JogWeapons", "Weapons",
            "2,8,10,12,14,15,22,23,24,26,28,29,32,43", buf, sizeof(buf), f);
        ParseIds(buf, jogWeapons);

        // Fallback: any weapon whose weapon.dat slot is listed here jogs unless overridden above.
        // Slots: 0 unarmed, 1 melee, 2 handguns, 3 shotguns, 4 SMGs, 5 assault, 6 rifles, 7 heavy,
        // 8 thrown, 9 special, 10 gifts.
        GetPrivateProfileStringA("JogSlots", "Slots", "2,4", buf, sizeof(buf), f);
        ParseIds(buf, jogSlots);

        GetPrivateProfileStringA("NoJogWeapons", "Weapons", "", buf, sizeof(buf), f);
        ParseIds(buf, noJogWeapons);

        GetPrivateProfileStringA("FireExtWeapons", "Weapons", "42", buf, sizeof(buf), f);
        ParseIds(buf, fireExtWeapons);

        aiWalkstyles = GetPrivateProfileIntA("SA.StoriesSprinting", "AIWeaponWalkstyles", 0, f) != 0;
        aiCombo = GetPrivateProfileIntA("SA.StoriesSprinting", "AIStoriesSprintingCombo", 0, f) != 0;

        GetPrivateProfileStringA("AIWalkstyles", "RocketWeapons", "35,36", buf, sizeof(buf), f);
        ParseIds(buf, aiRocketWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "RifleWeapons", "25,27,30,31,33,34", buf, sizeof(buf), f);
        ParseIds(buf, aiRifleWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "BatWeapons", "5,6,7", buf, sizeof(buf), f);
        ParseIds(buf, aiBatWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "HeavyWeapons", "9,37,38", buf, sizeof(buf), f);
        ParseIds(buf, aiHeavyWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "RifleSlots", "3,5,6", buf, sizeof(buf), f);
        ParseIds(buf, aiRifleSlots);
    }

    static void ApplyAnimPatches() {
        for (const AnimPatch& p : g_animPatches)
            patch::SetUInt(p.dest, p.value);
    }

    static bool BlockLoaded(const char* name) {
        CAnimBlock* b = CAnimManager::GetAnimationBlock(name);
        return b && b->bLoaded;
    }

    // weapon type (vanilla) or model id (addon)
    static bool InList(const std::set<int>& s, int type, int model) {
        return s.count(type) || (model > 0 && s.count(model));
    }

    static void SetPatched(bool on) {
        if (on == patched) return;
        if (on) patch::Nop(WRITE_ADDR, 6);
        else patch::NopRestore(WRITE_ADDR);
        patched = on;
    }

    static void Process() {
        if (--reloadTimer <= 0) { LoadConfig(); reloadTimer = 100; }
        ProcessPlayer();
        if (aiWalkstyles) ProcessPeds();
    }

    static void ProcessPlayer() {
        CPlayerPed* ped = FindPlayerPed();
        if (!ped) return;
        // bInVehicle, not m_pVehicle (that lingers after you exit a car)
        if (ped->bInVehicle) { SetPatched(false); return; }

        int type = (int)ped->GetWeapon()->m_eWeaponType;
        int model = -1, slot = -1;
        if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, 0)) {
            model = wi->m_nModelId;
            slot = (int)wi->m_nSlot;
        }

        // INI wins, weapon.dat slot is the fallback:
        //   NoJogWeapons (off) > JogWeapons (on) > FireExtWeapons > JogSlots fallback > nothing.
        int base = -1;
        if (InList(noJogWeapons, type, model)) base = -1;
        else if (InList(jogWeapons, type, model)) base = JOG_BASE;
        else if (fireExtFix && InList(fireExtWeapons, type, model)) base = FIREEXT_BASE;
        else if (slot >= 0 && jogSlots.count(slot)) base = JOG_BASE;

        if (base < 0) { SetPatched(false); return; }

        float fat = CStats::GetStatValue(STAT_FAT);
        float musc = CStats::GetStatValue(STAT_MUSCLE);
        int group = base;
        if (fat > 500.0f && fat >= musc) {
            if (!noFat && BlockLoaded("FAT")) group = base + 1;
        } else if (musc > 500.0f && musc >= fat) {
            if (!noMuscle && BlockLoaded("MUSCULAR")) group = base + 2;
        }

        SetPatched(true);
        *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET) = group;
        ((ReloadMoveAnims_t)RELOAD_MOVE_ANIMS)(ped);
    }

    // the group a ped falls back to when it holds nothing we care about
    static int ModelAnimGroup(CPed* ped) {
        int id = (int)(unsigned short)ped->m_nModelIndex;
        CBaseModelInfo* mi = CModelInfo::GetModelInfo(id);
        // m_nAnimType is only a CPedModelInfo field, so don't trust any other type
        if (!mi || mi->GetModelType() != MODEL_INFO_PED) return -1;
        int group = ((CPedModelInfo*)mi)->m_nAnimType;
        return (group >= 0 && group < CAnimManager::ms_numAnimAssocDefinitions) ? group : -1;
    }

    static bool IsAIGroup(int group) {
        return group == ROCKET_BASE || group == RIFLE_BASE
            || group == JOG_BASE || group == FIREEXT_BASE;
    }

    // AI Weapon Walkstyles: give peds the same weapon walkstyle the player gets,
    // so they carry rifles/rockets two-handed instead of the silly one-hand hold.
    // Unlike the player there is no fat/muscular variant - peds are their own model.
    static void ProcessPeds() {
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (!pool) return;

        for (int i = 0; i < pool->m_nSize; i++) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped->m_pPlayerData) continue;

            int type = (int)ped->GetWeapon()->m_eWeaponType;
            int model = -1, slot = -1;
            if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, 0)) {
                model = wi->m_nModelId;
                slot = (int)wi->m_nSlot;
            }

            //   RocketWeapons > RifleWeapons > HeavyWeapons > [combo] BatWeapons +
            //   JogWeapons > [combo] FireExtWeapons > RifleSlots fallback >
            //   [combo] JogSlots fallback > the ped model's own walkstyle.
            // Every JOG_BASE path is gated on the combo: group 63's run slot is the
            // jog, so handing a ped that group is the combo whether it came from
            // BatWeapons or JogWeapons.
            int group = -1;
            if (InList(aiRocketWeapons, type, model)) group = ROCKET_BASE;
            else if (InList(aiRifleWeapons, type, model)) group = RIFLE_BASE;
            else if (InList(aiHeavyWeapons, type, model)) group = FIREEXT_BASE;
            else if (aiCombo && !InList(noJogWeapons, type, model)
                && (InList(aiBatWeapons, type, model) || InList(jogWeapons, type, model)))
                group = JOG_BASE;
            else if (aiCombo && fireExtFix && InList(fireExtWeapons, type, model)) group = FIREEXT_BASE;
            else if (slot >= 0 && aiRifleSlots.count(slot)) group = RIFLE_BASE;
            else if (aiCombo && slot >= 0 && jogSlots.count(slot)
                && !InList(noJogWeapons, type, model)) group = JOG_BASE;

            int* cur = (int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
            if (group < 0) {
                // only undo our own groups, so scripted walkstyles survive
                if (!IsAIGroup(*cur)) continue;
                group = ModelAnimGroup(ped);
                if (group < 0) continue;
            }
            if (*cur != group) *cur = group;
        }
    }
};

std::set<int> StoriesSprinting::jogWeapons;
std::set<int> StoriesSprinting::noJogWeapons;
std::set<int> StoriesSprinting::jogSlots;
std::set<int> StoriesSprinting::fireExtWeapons;
std::set<int> StoriesSprinting::aiRocketWeapons;
std::set<int> StoriesSprinting::aiRifleWeapons;
std::set<int> StoriesSprinting::aiBatWeapons;
std::set<int> StoriesSprinting::aiHeavyWeapons;
std::set<int> StoriesSprinting::aiRifleSlots;
bool StoriesSprinting::aiWalkstyles = false;
bool StoriesSprinting::aiCombo = false;
bool StoriesSprinting::noFat = false;
bool StoriesSprinting::noMuscle = false;
bool StoriesSprinting::fireExtFix = true;
bool StoriesSprinting::patched = false;
std::string StoriesSprinting::iniPath;
int StoriesSprinting::reloadTimer = 0;

class StoriesSprintingPlugin {
public:
    StoriesSprintingPlugin() {
        StoriesSprinting::iniPath = StoriesSprinting::AsiFolder() + "SA.StoriesSprinting.ini";
        StoriesSprinting::LoadConfig();
        StoriesSprinting::ApplyAnimPatches();
        Events::gameProcessEvent += [] { StoriesSprinting::Process(); };
    }
} storiesSprintingPlugin;
