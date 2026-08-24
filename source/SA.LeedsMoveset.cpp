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
#include "CWeaponModelInfo.h"
#include "CAnimBlendAssociation.h"
#include "CAnimBlendStaticAssociation.h"
#include "CPedIntelligence.h"
#include "CPad.h"
#include "CTimer.h"
#include "CProjectileInfo.h"
#include "eStats.h"
#include <windows.h>
#include <string>
#include <set>
#include <vector>
#include <cstdio>

using namespace plugin;

// mov [esi+0x4D4], eax
static const uintptr_t WRITE_ADDR = 0x609A4E;
static const int WALK_GROUP_OFFSET = 0x4D4;

// reblends the walk/run/sprint slots from the 0x4D4 group; safe on any ped, and peds
// need it because CPed::SetMoveAnim only reblends on a move state change
static const uintptr_t RELOAD_MOVE_ANIMS = 0x609650;
typedef void(__thiscall* ReloadMoveAnims_t)(void*);

// conversation gestures come from the gangs group; -8.0 is the blend delta the game
// itself uses to end one
static const float CHAT_BLEND_OUT = -8.0f;
// prtial_gngtlkA..H - the gesture gangs throw while talking and after a kill
static const int GANG_TALK_FIRST = 279;
static const int GANG_TALK_LAST = 286;
// a move anim group is six slots: 0 walk, 1 run, 2 sprint, 3 idle, 4 roadcross, 5 walkstart
static const int RUN_SLOT = 1;
// slot 4 of a move anim group - the arms-out wave peds also throw after a kill
static const int ROADCROSS_SLOT = 4;
// move anim groups: 54-71 are the player/weapon ones, 118+ come from animgrp.dat
static bool IsMoveAnimGroup(int g) { return (g >= 54 && g <= 71) || g >= 118; }
// idle_hbhb, the look-around fidget - it sits at two ids, which is why it reads as twice
static const int IDLE_HBHB_FIRST = 8;
static const int IDLE_HBHB_LAST = 9;

// surfinfo.dat marks some surfaces "can't sprint on" and SurfaceInfos_c::CantSprintOn
// (thiscall, ecx = g_surfaceInfos) just reports bit 28 of that surface's flags. Both of
// its callers use it to choose between the run and sprint anims - 0x688609 in the player
// on-foot task and 0x66D94B on the ped path - so making it return 0 allows sprinting on
// every surface. Doing it here rather than in surfinfo.dat leaves the data file alone,
// which matters because every other mod wants to edit it too.
static const uintptr_t CANT_SPRINT_ON = 0x55E870;
static const unsigned char CANT_SPRINT_ORIG[5] = { 0x8B, 0x44, 0x24, 0x04, 0x8D }; // mov eax,[esp+4] ...
static const unsigned char CANT_SPRINT_OFF[5] = { 0x31, 0xC0, 0xC2, 0x04, 0x00 };  // xor eax,eax ; ret 4

// "bomber" is group 0 anim 48 and it is still sitting in ped.ifp, but nothing plays it -
// there is not one BlendAnimation call for it anywhere in the exe, and the detonator's
// weapon.dat anim group is "null", so pressing the detonator animates nothing at all.
// Rockstar cut the code, not just the data, so restoring it means blending it ourselves.
static const int DETONATOR_TYPE = 40;
static const float BOMBER_BLEND = 4.0f;
// CPad::UpdatePads refreshes the pad; this is the call the game makes each frame, before
// anything reads the buttons. Redirecting it is how we get in front of the fire press.
// SkyUI and DebugMenu redirect the same call and this mod loads after both, so read what
// the call points at and chain onto it - jumping straight to CPad::UpdatePads would drop
// their hook and take their menus with it. The hook only goes in if the detonator is on.
static const uintptr_t UPDATE_PADS_CALL = 0x53BEE6;
static const uintptr_t UPDATE_PADS = 0x541DD0;
typedef void(__cdecl* UpdatePads_t)();
static UpdatePads_t nextUpdatePads = NULL;
static void HookUpdatePads();

// push 54 in CPed::SetMoveAnim's sprint case - hardcoded for peds in the player's group
static const uintptr_t GROUP_SPRINT_ADDR = 0x5E4BFF;
static const unsigned char GROUP_SPRINT_ORIG[2] = { 0x6A, 0x36 }; // push 54
static const unsigned char GROUP_SPRINT_OWN[2] = { 0x57, 0x90 };  // push edi ; nop

// diagnostics: the game reads the walkstyle's weapon type from the weapon MODEL INFO,
// modelinfo(m_pWeaponObject)->m_weaponInfo, and falls back to group 54 if that fails
static const uintptr_t MODELINFO_FROM_RWOBJECT = 0x732AC0;
static const uintptr_t BODY_BASE_GROUP = 0x5A81B0; // 54/55/56, itself falls back to 54
static const int WEAPON_OBJECT_OFFSET = 0x4F4;
static const int INTELLIGENCE_OFFSET = 0x47C;

// GetWeaponInfo indexes by skill: 0 is aWeaponInfo[type+25], 1 is aWeaponInfo[type],
// 2 is aWeaponInfo[type+36]. Only 1 lands on the weapon asked for.
static const unsigned char WEAPON_SKILL_STD = 1;
typedef void* (__cdecl* MiFromRwObject_t)(void*);
typedef int (__cdecl* BodyBaseGroup_t)();

// walkstyle groups: 57 playerrocket, 60 player2armed, 63 playerBBBat (the jog),
// 66 playercsaw (fire-ext); each is followed by its fat and muscular variant
static const int ROCKET_BASE = 57;
static const int RIFLE_BASE = 60;
static const int JOG_BASE = 63;
static const int FIREEXT_BASE = 66;
// the same six-slot layout, but slot 1 is not a run in either - keep the jog out of them
static const int JETPACK_GROUP = 70;
static const int SWIM_GROUP = 71;

// the assoc groups are allocated at load and only the BASE POINTER lives here
// (mov [0xB4EA34], edi @ 0x4D56BF; every game read derefs it, e.g. 0x4D3A64).
// plugin-sdk's CAnimManager::ms_aAnimAssocGroups points at the pointer itself, so it
// indexes 0xB4EA34 as if it were the array - do not use it.
static const uintptr_t ANIM_ASSOC_GROUPS = 0xB4EA34;

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

// a ped's walkstyle before we overrode it, per ped-pool slot
struct PedWalkstyle {
    unsigned char id;
    bool active;
    bool jogging;      // we swapped the run slot rather than the whole walkstyle
    int ourGroup;
    int savedGroup;
    PedWalkstyle() : id(0), active(false), jogging(false), ourGroup(0), savedGroup(0) {}
};

class LeedsMoveset {
public:
    static std::vector<PedWalkstyle> pedWalk;
    static std::set<int> jogWeapons;      // force-jog (priority ON), by weapon type or model id
    static std::set<int> noJogWeapons;    // force-no-jog (priority OFF), overrides everything
    static std::set<int> jogSlots;        // weapon.dat slots that jog by default (fallback)
    static std::set<int> fireExtWeapons;
    static std::set<int> pcRocketWeapons; // player-only carry styles
    static std::set<int> pcRifleWeapons;
    static std::set<int> pcHeavyWeapons;
    static std::set<int> aiRocketWeapons; // ped-only lists (AI walkstyles)
    static std::set<int> aiRifleWeapons;
    static std::set<int> aiBatWeapons;
    static std::set<int> aiHeavyWeapons;
    static std::set<int> aiRifleSlots;
    static std::set<int> aiJogWeapons;   // ped-only jog, independent of the combo
    static std::set<int> aiIgnoreWeapons; // ped-only: leave the ped's own walkstyle alone
    static std::set<int> aiJogPedTypes;   // ped types allowed to jog; empty = all
    static std::set<int> aiIgnorePedTypes; // ped types the mod never touches
    static bool playerWalkstyles; // [PlayerWalkstyles], its own switch
    static bool aiWalkstyles;
    static bool aiCombo;
    static bool aiGroupSprint;
    static bool noArmedHandSignals;
    static bool noGangTaunts;
    static bool debugLog;
    static int logTimer;
    static int pedLogTimer;
    static int jogLogTimer;
    static std::vector<unsigned> nearSig;
    static bool detonatorAnim;
    static bool detonatorFiring;
    static int detLastWeapon;
    static int detDelayMs;
    static unsigned int detFireAt;
    static bool unarmedListed; // does any list actually name weapon 0 / slot 0
    static bool sprintAnywhere;
    static bool sprintPatched;
    static bool groupSprintPatched;
    static bool noFat;
    static bool noMuscle;
    static bool noSkinny;
    static bool fireExtFix;
    static bool patched;
    static std::string iniPath;
    static std::string iniSection;
    static int reloadTimer;
    static FILETIME iniStamp;

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

    // this mod used to be called SA.StoriesSprinting - keep reading an old INI, and an
    // old section inside a renamed one, so nobody's settings vanish on upgrade
    static void ResolveIni() {
        static const char* OLD_NAME = "SA.StoriesSprinting";
        static const char* NEW_NAME = "SA.LeedsMoveset";
        std::string folder = AsiFolder();
        iniPath = folder + NEW_NAME + ".ini";
        if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::string legacy = folder + OLD_NAME + ".ini";
            if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) iniPath = legacy;
        }
        iniSection = NEW_NAME;
        const char* f = iniPath.c_str();
        if (GetPrivateProfileIntA(NEW_NAME, "NoFat1Armed", -1, f) == -1
            && GetPrivateProfileIntA(OLD_NAME, "NoFat1Armed", -1, f) != -1)
            iniSection = OLD_NAME;
    }

    // LoadConfig reads ~25 keys straight off disk, which is the heaviest thing here.
    // Live editing still works, it just costs one attribute check instead of 25 reads
    // on every frame that comes round.
    static bool IniChanged() {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(iniPath.c_str(), GetFileExInfoStandard, &fad)) return false;
        if (fad.ftLastWriteTime.dwLowDateTime == iniStamp.dwLowDateTime
            && fad.ftLastWriteTime.dwHighDateTime == iniStamp.dwHighDateTime) return false;
        iniStamp = fad.ftLastWriteTime;
        return true;
    }

    static void LoadConfig() {
        const char* f = iniPath.c_str();
        noFat = GetPrivateProfileIntA(iniSection.c_str(), "NoFat1Armed", 0, f) != 0;
        noMuscle = GetPrivateProfileIntA(iniSection.c_str(), "NoMuscle1Armed", 0, f) != 0;
        noSkinny = GetPrivateProfileIntA(iniSection.c_str(), "NoSkinny1Armed", 0, f) != 0;
        fireExtFix = GetPrivateProfileIntA(iniSection.c_str(), "FireExtinguisherWalkstyleFix", 1, f) != 0;

        char buf[2048];
        GetPrivateProfileStringA("JogWeapons", "Weapons",
            "2,8,10,12,14,15,22,23,24,26,28,29,32", buf, sizeof(buf), f);
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

        GetPrivateProfileStringA("PlayerWalkstyles", "RocketWeapons", "", buf, sizeof(buf), f);
        ParseIds(buf, pcRocketWeapons);

        GetPrivateProfileStringA("PlayerWalkstyles", "RifleWeapons", "", buf, sizeof(buf), f);
        ParseIds(buf, pcRifleWeapons);

        GetPrivateProfileStringA("PlayerWalkstyles", "HeavyWeapons", "", buf, sizeof(buf), f);
        ParseIds(buf, pcHeavyWeapons);

        playerWalkstyles = GetPrivateProfileIntA(iniSection.c_str(), "PlayerWeaponWalkstyles", 0, f) != 0;
        sprintAnywhere = GetPrivateProfileIntA(iniSection.c_str(), "SprintOnAnySurface", 0, f) != 0;
        detonatorAnim = GetPrivateProfileIntA(iniSection.c_str(), "DetonatorAnimation", 0, f) != 0;
        detDelayMs = GetPrivateProfileIntA(iniSection.c_str(), "DetonatorAnimationDelay", 300, f);
        aiWalkstyles = GetPrivateProfileIntA(iniSection.c_str(), "AIWeaponWalkstyles", 0, f) != 0;
        aiCombo = GetPrivateProfileIntA(iniSection.c_str(), "AIStoriesSprintingCombo", 0, f) != 0;
        aiGroupSprint = GetPrivateProfileIntA(iniSection.c_str(), "AIGroupSprintFix", 0, f) != 0;
        noArmedHandSignals = GetPrivateProfileIntA(iniSection.c_str(), "NoArmedHandSignals", 0, f) != 0;
        noGangTaunts = GetPrivateProfileIntA(iniSection.c_str(), "NoGangTaunts", 0, f) != 0;
        debugLog = GetPrivateProfileIntA(iniSection.c_str(), "DebugLog", 0, f) != 0;

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

        GetPrivateProfileStringA("AIWalkstyles", "JogWeapons", "", buf, sizeof(buf), f);
        ParseIds(buf, aiJogWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "IgnoreWeapons", "", buf, sizeof(buf), f);
        ParseIds(buf, aiIgnoreWeapons);

        GetPrivateProfileStringA("AIWalkstyles", "JogPedTypes", "6,7,8,9,10,11,12,13,14,15,16", buf, sizeof(buf), f);
        ParseIds(buf, aiJogPedTypes);

        GetPrivateProfileStringA("AIWalkstyles", "IgnorePedTypes", "", buf, sizeof(buf), f);
        ParseIds(buf, aiIgnorePedTypes);

        // ProcessPeds skips empty-handed peds outright, which is what nearly everyone
        // wants and saves the whole decision chain for most of the pool. Listing weapon
        // type 0 (or the unarmed slot) is a real way to ask for unarmed peds to jog
        // though, so work out once whether anything actually does.
        unarmedListed = jogWeapons.count(0) || noJogWeapons.count(0) || fireExtWeapons.count(0)
            || aiJogWeapons.count(0) || aiBatWeapons.count(0) || aiRocketWeapons.count(0)
            || aiRifleWeapons.count(0) || aiHeavyWeapons.count(0) || aiIgnoreWeapons.count(0)
            || jogSlots.count(0) || aiRifleSlots.count(0);
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

    // the same notion of two-handed the walkstyle lists use
    static bool IsTwoHanded(int type, int model, int slot) {
        if (InList(aiRocketWeapons, type, model)) return true;
        if (InList(aiRifleWeapons, type, model)) return true;
        if (InList(aiHeavyWeapons, type, model)) return true;
        return slot >= 0 && aiRifleSlots.count(slot) && !InList(jogWeapons, type, model);
    }

    // tauntOnly keeps the handshakes, smoking and leaning and drops just the gesture.
    // armed additionally drops the roadcross wave and the look-around idle.
    static void StopGangChatAnims(CPed* ped, bool tauntOnly, bool armed) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return;
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(clump); a; ) {
            CAnimBlendAssociation* next = RpAnimBlendGetNextAssociation(a);
            bool taunt = a->m_nAnimId >= GANG_TALK_FIRST && a->m_nAnimId <= GANG_TALK_LAST;
            bool gang = a->m_nAnimGroup == ANIM_GROUP_GANGS && (!tauntOnly || taunt);
            // any move group, not just the ped's current one - the association can predate
            // the walkstyle this mod gives them
            bool roadcross = armed && a->m_nAnimId == ROADCROSS_SLOT
                && IsMoveAnimGroup(a->m_nAnimGroup);
            bool lookAround = armed && a->m_nAnimGroup == ANIM_GROUP_DEFAULT
                && a->m_nAnimId >= IDLE_HBHB_FIRST && a->m_nAnimId <= IDLE_HBHB_LAST;
            if (gang || roadcross || lookAround) {
                // zero it as well as fading, otherwise a frame of it still shows
                a->m_fBlendAmount = 0.0f;
                a->m_fBlendDelta = CHAT_BLEND_OUT;
            }
            a = next;
        }
    }

    // DebugLog=1: what a two-handed ped is actually playing, so the groups can be checked
    static void LogPedAnims(CPed* ped, int type) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return;
        FILE* f = fopen((AsiFolder() + "SA.LeedsMoveset.log").c_str(), "a");
        if (!f) return;
        fprintf(f, "ped wep=%d anims:", type);
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(clump); a;
             a = RpAnimBlendGetNextAssociation(a))
            fprintf(f, " %d/%d(%.2f)", (int)a->m_nAnimGroup, (int)a->m_nAnimId, a->m_fBlendAmount);
        fprintf(f, "\n");
        fclose(f);
    }

    // gang signs and conversation gestures are tasks; the game picks handsignalL when the
    // ped is armed, so a rifle or minigun gets waved about mid-conversation. Abort both
    // and let them talk with the weapon held.
    static void ProcessHandSignals() {
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (!pool) return;

        for (int i = 0; i < pool->m_nSize; i++) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped->m_pPlayerData) continue;

            int type = (int)ped->GetWeapon()->m_eWeaponType;
            bool armed = false;
            if (noArmedHandSignals && type != 0) {
                int model = -1, slot = -1;
                if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD)) {
                    model = wi->m_nModelId;
                    slot = (int)wi->m_nSlot;
                }
                armed = IsTwoHanded(type, model, slot);
            }

            if (armed) {
                if (ped->IsPlayingHandSignal()) ped->StopPlayingHandSignal();
                StopGangChatAnims(ped, false, true);
                if (debugLog && --pedLogTimer <= 0) { pedLogTimer = 100; LogPedAnims(ped, type); }
            } else if (noGangTaunts) {
                StopGangChatAnims(ped, true, false);
            }
        }
    }

    // DebugLog=1: dump what nearby peds are playing, for identifying an anim by sight
    static void LogNearbyPedAnims() {
        if (!debugLog) return;
        CPlayerPed* player = FindPlayerPed();
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (!player || !pool) return;
        CVector pp = player->GetPosition();

        FILE* f = NULL;
        int shown = 0;
        for (int i = 0; i < pool->m_nSize && shown < 8; i++) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped->m_pPlayerData || !ped->m_pRwObject) continue;
            CVector d = ped->GetPosition() - pp;
            if (d.x * d.x + d.y * d.y + d.z * d.z > 400.0f) continue;

            unsigned sig = 0;
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation((RpClump*)ped->m_pRwObject);
                 a; a = RpAnimBlendGetNextAssociation(a))
                sig = sig * 131 + (unsigned)(a->m_nAnimGroup * 1000 + a->m_nAnimId);
            if ((int)nearSig.size() != pool->m_nSize) nearSig.assign(pool->m_nSize, 0);
            if (nearSig[i] == sig) continue;
            nearSig[i] = sig;

            if (!f) { f = fopen((AsiFolder() + "SA.LeedsMoveset.log").c_str(), "a"); if (!f) return; }
            // what the attached weapon object actually IS - the same read LogPlayer does.
            // miType 5 is a weapon model, anything else means the pointer is holding
            // something that is not a weapon (a carried prop, say)
            int miType = -1, miWeapon = -1;
            if (ped->m_pWeaponObject) {
                CBaseModelInfo* mi = (CBaseModelInfo*)((MiFromRwObject_t)MODELINFO_FROM_RWOBJECT)(ped->m_pWeaponObject);
                if (mi) {
                    miType = (int)mi->GetModelType();
                    if (miType == MODEL_INFO_WEAPON) miWeapon = (int)((CWeaponModelInfo*)mi)->m_weaponInfo;
                }
            }
            fprintf(f, "near type=%d wep=%d slot=%d group=%d wobj=%s miType=%d miWep=%d anims:",
                ped->m_nPedType, (int)ped->GetWeapon()->m_eWeaponType,
                (int)ped->m_nSelectedWepSlot,
                *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET),
                ped->m_pWeaponObject ? "yes" : "NULL", miType, miWeapon);
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation((RpClump*)ped->m_pRwObject);
                 a; a = RpAnimBlendGetNextAssociation(a))
                fprintf(f, " %d/%d(%.2f)", (int)a->m_nAnimGroup, (int)a->m_nAnimId, a->m_fBlendAmount);
            fprintf(f, "\n");
            shown++;
        }
        if (f) fclose(f);
    }

    static void SetSprintPatched(bool on) {
        if (on == sprintPatched) return;
        patch::SetRaw(CANT_SPRINT_ON, (void*)(on ? CANT_SPRINT_OFF : CANT_SPRINT_ORIG), 5);
        sprintPatched = on;
    }

    // Plays "bomber", the detonator animation still sitting unused in ped.ifp, and
    // syncs the charges to it. Runs straight after CPad::UpdatePads so the pad is fresh
    // and nothing has read it: the press is swallowed, the animation starts, and the
    // charges go off on our own clock at the point CJ's hand comes down. The game's own
    // fire handling never runs, so the spent detonator is taken off him here too.
    //
    // The approach is Cleomodlar's, from their "Unused Detonator" CLEO - drive both
    // halves yourself rather than waiting on the game, detonate through the same call
    // opcode 09D9 makes, then clear the detonator. A script gets in front of the fire
    // for free; from an ASI it takes the pad redirect above.
    static void ProcessDetonator() {
        CPlayerPed* ped = FindPlayerPed();
        CPad* pad = CPad::GetPad(0);
        if (!ped || !pad) { detFireAt = 0; return; }

        // mid-animation: keep the button down-pressed from reaching the game, and fire
        // the charges when the delay is up
        if (detFireAt) {
            pad->NewState.ButtonCircle = 0;
            if (CTimer::m_snTimeInMilliseconds >= detFireAt) {
                CProjectileInfo::RemoveDetonatorProjectiles();
                ped->ClearWeapon((eWeaponType)DETONATOR_TYPE);
                detFireAt = 0;
            }
            detonatorFiring = true;
            return;
        }

        int type = (int)ped->GetWeapon()->m_eWeaponType;
        bool fire = pad->NewState.ButtonCircle != 0;

        if (fire && !detonatorFiring && type == DETONATOR_TYPE && !ped->bInVehicle) {
            pad->NewState.ButtonCircle = 0; // the game never sees this press
            if (RpClump* clump = (RpClump*)ped->m_pRwObject)
                CAnimManager::BlendAnimation(clump, ANIM_GROUP_DEFAULT,
                    ANIM_DEFAULT_BOMBER, BOMBER_BLEND);
            detFireAt = CTimer::m_snTimeInMilliseconds
                + (unsigned int)(detDelayMs > 0 ? detDelayMs : 0);
        }
        detonatorFiring = fire;
    }

    // let peds in the player's group sprint from their own walkstyle group
    static void SetGroupSprintPatched(bool on) {
        if (on == groupSprintPatched) return;
        patch::SetRaw(GROUP_SPRINT_ADDR, (void*)(on ? GROUP_SPRINT_OWN : GROUP_SPRINT_ORIG), 2);
        groupSprintPatched = on;
    }

    // DebugLog=1: dump what the game and the mod each think the walkstyle should be
    static void LogPlayer(CPlayerPed* ped, int type, int model, int slot, int base) {
        if (!debugLog || --logTimer > 0) return;
        logTimer = 50;

        unsigned int flags = 0;
        if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD))
            flags = *(unsigned int*)((uintptr_t)wi + 0x18);

        void* wobj = *(void**)((uintptr_t)ped + WEAPON_OBJECT_OFFSET);
        int miType = -1, miWeapon = -1;
        if (wobj) {
            CBaseModelInfo* mi = (CBaseModelInfo*)((MiFromRwObject_t)MODELINFO_FROM_RWOBJECT)(wobj);
            if (mi) {
                miType = (int)mi->GetModelType();
                if (miType == MODEL_INFO_WEAPON) miWeapon = (int)((CWeaponModelInfo*)mi)->m_weaponInfo;
            }
        }

        FILE* f = fopen((AsiFolder() + "SA.LeedsMoveset.log").c_str(), "a");
        if (!f) return;
        fprintf(f, "wep=%d model=%d slot=%d flags=0x%X group=%d modBase=%d wobj=%s miType=%d miWep=%d "
                   "body=%d FAT=%d MUSC=%d patched=%d\n",
            type, model, slot, flags,
            *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET), base,
            wobj ? "yes" : "NULL", miType, miWeapon,
            ((BodyBaseGroup_t)BODY_BASE_GROUP)(),
            BlockLoaded("FAT") ? 1 : 0, BlockLoaded("MUSCULAR") ? 1 : 0, patched ? 1 : 0);
        fclose(f);
    }

    static void SetPatched(bool on) {
        if (on == patched) return;
        if (on) patch::Nop(WRITE_ADDR, 6);
        else patch::NopRestore(WRITE_ADDR);
        patched = on;
    }

    static void Process() {
        if (--reloadTimer <= 0) {
            reloadTimer = 100;
            if (IniChanged()) LoadConfig();
        }
        if (detonatorAnim) HookUpdatePads();
        ProcessPlayer();
        SetSprintPatched(sprintAnywhere);
        SetGroupSprintPatched(aiWalkstyles && aiGroupSprint);
        if (aiWalkstyles) ProcessPeds();
        else ReleasePeds();
        if (noArmedHandSignals || noGangTaunts) ProcessHandSignals();
        LogNearbyPedAnims();
    }

    static void ProcessPlayer() {
        CPlayerPed* ped = FindPlayerPed();
        if (!ped) return;
        // bInVehicle, not m_pVehicle (that lingers after you exit a car)
        if (ped->bInVehicle) { SetPatched(false); return; }

        // The jetpack has a walkstyle of its own - group 70 "playerjetpack", whose idle is
        // Jetpack_Idle, the pose where CJ holds the handles. Suppressing the game's write
        // at 0x609A4E and forcing a weapon group takes that away and leaves his arms at
        // his sides. Back off entirely while the jetpack task is running, the same as in
        // a vehicle, and the game puts him back on group 70 itself.
        if (ped->m_pIntelligence && ped->m_pIntelligence->GetTaskJetPack()) {
            SetPatched(false);
            return;
        }

        int type = (int)ped->GetWeapon()->m_eWeaponType;
        int model = -1, slot = -1;
        if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD)) {
            model = wi->m_nModelId;
            slot = (int)wi->m_nSlot;
        }

        // [PlayerWalkstyles] has its own switch, nothing to do with the AI settings, and is
        // an explicit "carry it like this", so it wins outright;
        // after that the INI lists win and the weapon.dat slot is the fallback:
        //   PlayerWalkstyles Rocket > Rifle > Heavy > NoJogWeapons (off) > JogWeapons (on)
        //   > FireExtWeapons > JogSlots fallback > nothing.
        int base = -1;
        bool jogStyle = false; // the fat/muscular opt-outs below are about the jog only
        if (playerWalkstyles && InList(pcRocketWeapons, type, model)) base = ROCKET_BASE;
        else if (playerWalkstyles && InList(pcRifleWeapons, type, model)) base = RIFLE_BASE;
        else if (playerWalkstyles && InList(pcHeavyWeapons, type, model)) base = FIREEXT_BASE;
        else if (InList(noJogWeapons, type, model)) base = -1;
        else if (InList(jogWeapons, type, model)) { base = JOG_BASE; jogStyle = true; }
        else if (fireExtFix && InList(fireExtWeapons, type, model)) { base = FIREEXT_BASE; jogStyle = true; }
        else if (slot >= 0 && jogSlots.count(slot)) { base = JOG_BASE; jogStyle = true; }

        LogPlayer(ped, type, model, slot, base);

        if (base < 0) { SetPatched(false); return; }

        float fat = CStats::GetStatValue(STAT_FAT);
        float musc = CStats::GetStatValue(STAT_MUSCLE);
        int group = base;
        if (fat > 500.0f && fat >= musc) {
            if ((!jogStyle || !noFat) && BlockLoaded("FAT")) group = base + 1;
        } else if (musc > 500.0f && musc >= fat) {
            if ((!jogStyle || !noMuscle) && BlockLoaded("MUSCULAR")) group = base + 2;
        }

        // skinny = neither variant applied, i.e. CJ is on the plain ped anims
        if (jogStyle && noSkinny && group == base) { SetPatched(false); return; }

        SetPatched(true);
        *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET) = group;
        ((ReloadMoveAnims_t)RELOAD_MOVE_ANIMS)(ped);
    }

    // back to what the ped had, not its model default - opcode 0245 and model swaps
    // change a ped's walkstyle at runtime
    static void RestorePed(PedWalkstyle& rec, CPed* ped) {
        if (!rec.active) return;
        int* cur = (int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
        if (*cur == rec.ourGroup) { *cur = rec.savedGroup; ReblendMoveAnims(ped); }
        rec.active = false;
    }

    static void ReblendMoveAnims(CPed* ped) {
        if (ped->m_pRwObject) ((ReloadMoveAnims_t)RELOAD_MOVE_ANIMS)(ped);
    }

    // a group's static association, bounds-checked - CAnimBlendAssocGroup::GetAnimation
    // indexes into the array without checking anything itself
    static CAnimBlendStaticAssociation* StaticAssoc(int group, int slot) {
        if (group < 0 || group >= CAnimManager::ms_numAnimAssocDefinitions) return NULL;
        CAnimBlendAssocGroup* groups = *(CAnimBlendAssocGroup**)ANIM_ASSOC_GROUPS;
        if (!groups) return NULL;
        CAnimBlendAssocGroup& g = groups[group];
        if (!g.m_pAssociations) return NULL;
        int i = slot - g.m_nIdOffset;
        if (i < 0 || i >= (int)g.m_nNumAnimations) return NULL;
        return &g.m_pAssociations[i];
    }

    static bool IsWalkstyleGroup(int g) {
        return IsMoveAnimGroup(g) && g != JETPACK_GROUP && g != SWIM_GROUP;
    }

    // Give a ped the jog without taking its walkstyle away. Writing 63 into 0x4D4 hands it
    // CJ's walk, sprint and idle as well, which is not what the jog is for; instead point
    // the ped's OWN group's run slot at the jog just long enough for ReApplyMoveAnims to
    // rebuild that one association. That call only rebuilds a slot whose anim differs and
    // carries the old blend amount across, so the other slots are left alone and the swap
    // does not pop. Init copies the nodes out of m_pSequenceArray, not the hierarchy, so
    // the whole animation half of the association has to move - the id and group stay put
    // and the rebuilt association still reports the ped's real walkstyle.
    static bool ApplyJogRun(CPed* ped) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return false;
        CAnimBlendStaticAssociation* jog = StaticAssoc(JOG_BASE, RUN_SLOT);
        if (!jog || !jog->m_pHeirarchy) return false;

        // associations are looked up by anim id alone, so check the group before touching one
        CAnimBlendAssociation* live = RpAnimBlendClumpGetAssociation(clump, RUN_SLOT);
        if (!live || !IsMoveAnimGroup(live->m_nAnimGroup)) return true;
        if (live->m_pHierarchy == jog->m_pHeirarchy) return true;

        int group = *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
        if (!IsWalkstyleGroup(group)) return false;
        CAnimBlendStaticAssociation* own = StaticAssoc(group, RUN_SLOT);
        if (!own) return false;

        unsigned short nodes = own->m_nNumBlendNodes, flags = own->m_nFlags;
        CAnimBlendSequence** seq = own->m_pSequenceArray;
        CAnimBlendHierarchy* hier = own->m_pHeirarchy;

        own->m_nNumBlendNodes = jog->m_nNumBlendNodes;
        own->m_nFlags = jog->m_nFlags;
        own->m_pSequenceArray = jog->m_pSequenceArray;
        own->m_pHeirarchy = jog->m_pHeirarchy;

        ((ReloadMoveAnims_t)RELOAD_MOVE_ANIMS)(ped);

        own->m_nNumBlendNodes = nodes;
        own->m_nFlags = flags;
        own->m_pSequenceArray = seq;
        own->m_pHeirarchy = hier;
        return true;
    }

    // DebugLog=1: why a ped picked for the jog did or did not get it
    static void LogJog(CPed* ped, int type, bool applied) {
        if (!debugLog || --jogLogTimer > 0) return;
        jogLogTimer = 100;
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        CAnimBlendStaticAssociation* jog = StaticAssoc(JOG_BASE, RUN_SLOT);
        int group = *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
        CAnimBlendStaticAssociation* own = StaticAssoc(group, RUN_SLOT);
        CAnimBlendAssociation* live = clump ? RpAnimBlendClumpGetAssociation(clump, RUN_SLOT) : NULL;
        FILE* f = fopen((AsiFolder() + "SA.LeedsMoveset.log").c_str(), "a");
        if (!f) return;
        fprintf(f, "jog wep=%d group=%d applied=%d jogAssoc=%p ownAssoc=%p live=%p "
                   "liveGroup=%d liveHier=%p jogHier=%p\n",
            type, group, applied ? 1 : 0, (void*)jog, (void*)own, (void*)live,
            live ? (int)live->m_nAnimGroup : -1,
            live ? (void*)live->m_pHierarchy : NULL,
            jog ? (void*)jog->m_pHeirarchy : NULL);
        fclose(f);
    }

    // put the ped's own run back - its run slot no longer matches its group, so the same
    // call rebuilds it from the walkstyle the ped actually has
    static void ClearJogRun(CPed* ped) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return;
        CAnimBlendStaticAssociation* jog = StaticAssoc(JOG_BASE, RUN_SLOT);
        CAnimBlendAssociation* live = RpAnimBlendClumpGetAssociation(clump, RUN_SLOT);
        if (!jog || !live || live->m_pHierarchy != jog->m_pHeirarchy) return;
        ((ReloadMoveAnims_t)RELOAD_MOVE_ANIMS)(ped);
    }

    // drop both kinds of override at once
    static void ReleasePed(PedWalkstyle& rec, CPed* ped) {
        if (rec.jogging) { ClearJogRun(ped); rec.jogging = false; }
        RestorePed(rec, ped);
    }

    // called when the feature gets switched off mid-game, so nobody stays overridden
    static void ReleasePeds() {
        if (pedWalk.empty()) return;
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (pool) {
            int n = pool->m_nSize < (int)pedWalk.size() ? pool->m_nSize : (int)pedWalk.size();
            for (int i = 0; i < n; i++) {
                CPed* ped = pool->GetAt(i);
                if (ped && pool->GetIdAt(i) == pedWalk[i].id) ReleasePed(pedWalk[i], ped);
            }
        }
        pedWalk.clear();
    }

    // give peds the player's weapon walkstyles; no fat/muscular variant for peds
    static void ProcessPeds() {
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (!pool) return;
        if ((int)pedWalk.size() != pool->m_nSize) pedWalk.assign(pool->m_nSize, PedWalkstyle());

        for (int i = 0; i < pool->m_nSize; i++) {
            PedWalkstyle& rec = pedWalk[i];
            CPed* ped = pool->GetAt(i);
            if (!ped) { rec.active = false; rec.jogging = false; continue; }
            // the pool reuses slots; its id byte changes, so any saved group is stale
            unsigned char id = pool->GetIdAt(i);
            if (rec.id != id) { rec.id = id; rec.active = false; rec.jogging = false; }
            if (ped->m_pPlayerData) { rec.active = false; rec.jogging = false; continue; }

            // Most peds are empty-handed and get nothing from the lists below, so bail
            // before the ped-type set, the task lookup and the whole decision chain -
            // that is most of what this loop would otherwise cost. Only skipped when a
            // list really does name weapon 0 or the unarmed slot.
            int type = (int)ped->GetWeapon()->m_eWeaponType;
            if (type == 0 && !unarmedListed) { ReleasePed(rec, ped); continue; }

            // ped types to leave completely alone, for scripted peds that should keep
            // whatever the mission gave them
            if (aiIgnorePedTypes.count(ped->m_nPedType)) { ReleasePed(rec, ped); continue; }

            // A ped carrying an object has both hands on the prop, and the weapon it is
            // still "holding" is not drawn with it - the triads in "A Home in the Hills"
            // carry a para_pack and their M4 never appears, so the rifle carry stood them
            // in idle_armed for the whole plane ride. CPedIntelligence::GetTaskHold finds the carry task
            // (TASK_SIMPLE_HOLD_ENTITY, 0x133) in the secondary slot or the primary tree.
            // This lasts exactly as long as the carry does: the script drops the prop
            // before they jump, the task ends, and the weapon walkstyle comes straight
            // back for the fight on the ground.
            if (ped->m_pIntelligence && ped->m_pIntelligence->GetTaskHold(false)) {
                ReleasePed(rec, ped);
                continue;
            }

            int model = -1, slot = -1;
            if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD)) {
                model = wi->m_nModelId;
                slot = (int)wi->m_nSlot;
            }

            //   IgnoreWeapons > RocketWeapons > RifleWeapons > HeavyWeapons > JogWeapons >
            //   [combo] BatWeapons + JogWeapons > [combo] FireExtWeapons > RifleSlots
            //   fallback > [combo] JogSlots fallback > the ped's own walkstyle.
            // The carry styles are whole groups; the jog is only the run slot, so a ped
            // that jogs still walks, sprints and idles on the walkstyle it came with.
            bool mayJog = aiJogPedTypes.empty() || aiJogPedTypes.count(ped->m_nPedType) != 0;

            int group = -1;
            bool jogRun = false;
            if (InList(aiIgnoreWeapons, type, model)) group = -1;
            else if (InList(aiRocketWeapons, type, model)) group = ROCKET_BASE;
            else if (InList(aiRifleWeapons, type, model)) group = RIFLE_BASE;
            else if (InList(aiHeavyWeapons, type, model)) group = FIREEXT_BASE;
            else if (mayJog && !InList(noJogWeapons, type, model)
                && InList(aiJogWeapons, type, model)) jogRun = true;
            else if (aiCombo && mayJog && !InList(noJogWeapons, type, model)
                && (InList(aiBatWeapons, type, model) || InList(jogWeapons, type, model)))
                jogRun = true;
            else if (aiCombo && fireExtFix && InList(fireExtWeapons, type, model)) group = FIREEXT_BASE;
            // [JogWeapons] declares a weapon one-handed - keeps the sawn-off out of the
            // rifle carry, its slot is 3 but it animates from the colt45 block
            else if (slot >= 0 && aiRifleSlots.count(slot)
                && !InList(jogWeapons, type, model)) group = RIFLE_BASE;
            else if (aiCombo && mayJog && slot >= 0 && jogSlots.count(slot)
                && !InList(noJogWeapons, type, model)) jogRun = true;

            // hand a carry group back before jogging, so the run swap sits on the ped's own
            if (jogRun) {
                RestorePed(rec, ped);
                rec.jogging = ApplyJogRun(ped);
                LogJog(ped, type, rec.jogging);
                continue;
            }
            if (rec.jogging) { ClearJogRun(ped); rec.jogging = false; }

            int* cur = (int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
            if (group < 0) { RestorePed(rec, ped); continue; }

            // anything but our own last write is the ped's real walkstyle
            if (!rec.active || *cur != rec.ourGroup) {
                rec.savedGroup = *cur;
                rec.active = true;
            }
            rec.ourGroup = group;
            if (*cur != group) { *cur = group; ReblendMoveAnims(ped); }
        }
    }
};

std::set<int> LeedsMoveset::jogWeapons;
std::set<int> LeedsMoveset::noJogWeapons;
std::set<int> LeedsMoveset::jogSlots;
std::set<int> LeedsMoveset::fireExtWeapons;
std::set<int> LeedsMoveset::pcRocketWeapons;
std::set<int> LeedsMoveset::pcRifleWeapons;
std::set<int> LeedsMoveset::pcHeavyWeapons;
std::set<int> LeedsMoveset::aiRocketWeapons;
std::set<int> LeedsMoveset::aiRifleWeapons;
std::set<int> LeedsMoveset::aiBatWeapons;
std::set<int> LeedsMoveset::aiHeavyWeapons;
std::set<int> LeedsMoveset::aiRifleSlots;
std::set<int> LeedsMoveset::aiJogWeapons;
std::set<int> LeedsMoveset::aiIgnoreWeapons;
std::set<int> LeedsMoveset::aiJogPedTypes;
std::set<int> LeedsMoveset::aiIgnorePedTypes;
std::vector<PedWalkstyle> LeedsMoveset::pedWalk;
bool LeedsMoveset::playerWalkstyles = false;
bool LeedsMoveset::aiWalkstyles = false;
bool LeedsMoveset::aiCombo = false;
bool LeedsMoveset::aiGroupSprint = false;
bool LeedsMoveset::noArmedHandSignals = false;
bool LeedsMoveset::noGangTaunts = false;
bool LeedsMoveset::debugLog = false;
int LeedsMoveset::logTimer = 0;
int LeedsMoveset::pedLogTimer = 0;
int LeedsMoveset::jogLogTimer = 0;
std::vector<unsigned> LeedsMoveset::nearSig;
bool LeedsMoveset::detonatorAnim = false;
bool LeedsMoveset::detonatorFiring = false;
int LeedsMoveset::detLastWeapon = 0;
int LeedsMoveset::detDelayMs = 300;
unsigned int LeedsMoveset::detFireAt = 0;
bool LeedsMoveset::unarmedListed = false;
bool LeedsMoveset::sprintAnywhere = false;
bool LeedsMoveset::sprintPatched = false;
bool LeedsMoveset::groupSprintPatched = false;
bool LeedsMoveset::noFat = false;
bool LeedsMoveset::noMuscle = false;
bool LeedsMoveset::noSkinny = false;
bool LeedsMoveset::fireExtFix = true;
bool LeedsMoveset::patched = false;
std::string LeedsMoveset::iniPath;
std::string LeedsMoveset::iniSection;
int LeedsMoveset::reloadTimer = 0;
FILETIME LeedsMoveset::iniStamp = { 0, 0 };

static void __cdecl HookedUpdatePads() {
    nextUpdatePads();
    if (LeedsMoveset::detonatorAnim) LeedsMoveset::ProcessDetonator();
}

// goes in the first time the detonator is switched on, and is re-armed if the site ever
// reads as the stock call again. SkyUI hooks the same call with a one-shot initialiser and
// puts the original call back once it has run, which takes our redirect with it - and its
// own routine is inert by then, so the pointer we chained onto must be dropped too.
// A restore is the ONLY case worth re-arming for: any other target means a mod is chained
// there, and grabbing that would put the two of us in a loop.
static void HookUpdatePads() {
    const unsigned char* site = (const unsigned char*)UPDATE_PADS_CALL;
    if (site[0] != 0xE8) return;
    UpdatePads_t target = (UpdatePads_t)(UPDATE_PADS_CALL + 5 + *(const int*)(site + 1));
    if (target == HookedUpdatePads) return;
    if (nextUpdatePads && target != (UpdatePads_t)UPDATE_PADS) return;
    nextUpdatePads = target;
    patch::RedirectCall(UPDATE_PADS_CALL, HookedUpdatePads);
}

class LeedsMovesetPlugin {
public:
    LeedsMovesetPlugin() {
        LeedsMoveset::ResolveIni();
        LeedsMoveset::LoadConfig();
        LeedsMoveset::ApplyAnimPatches();
        if (LeedsMoveset::detonatorAnim) HookUpdatePads();
        Events::gameProcessEvent += [] { LeedsMoveset::Process(); };
    }
} leedsMovesetPlugin;
