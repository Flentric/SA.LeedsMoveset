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
// slot 4 of a move anim group - the arms-out wave peds also throw after a kill
static const int ROADCROSS_SLOT = 4;

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
    int ourGroup;
    int savedGroup;
    PedWalkstyle() : id(0), active(false), ourGroup(0), savedGroup(0) {}
};

class StoriesSprinting {
public:
    static std::vector<PedWalkstyle> pedWalk;
    static std::set<int> jogWeapons;      // force-jog (priority ON), by weapon type or model id
    static std::set<int> noJogWeapons;    // force-no-jog (priority OFF), overrides everything
    static std::set<int> jogSlots;        // weapon.dat slots that jog by default (fallback)
    static std::set<int> fireExtWeapons;
    static std::set<int> aiRocketWeapons; // ped-only lists (AI walkstyles)
    static std::set<int> aiRifleWeapons;
    static std::set<int> aiBatWeapons;
    static std::set<int> aiHeavyWeapons;
    static std::set<int> aiRifleSlots;
    static std::set<int> aiJogWeapons;   // ped-only jog, independent of the combo
    static std::set<int> aiIgnoreWeapons; // ped-only: leave the ped's own walkstyle alone
    static std::set<int> aiJogPedTypes;   // ped types allowed to jog; empty = all
    static bool aiWalkstyles;
    static bool aiCombo;
    static bool aiGroupSprint;
    static bool noArmedHandSignals;
    static bool noGangTaunts;
    static bool debugLog;
    static int logTimer;
    static int pedLogTimer;
    static std::vector<unsigned> nearSig;
    static bool groupSprintPatched;
    static bool noFat;
    static bool noMuscle;
    static bool noSkinny;
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
        noSkinny = GetPrivateProfileIntA("SA.StoriesSprinting", "NoSkinny1Armed", 0, f) != 0;
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
        aiGroupSprint = GetPrivateProfileIntA("SA.StoriesSprinting", "AIGroupSprintFix", 1, f) != 0;
        noArmedHandSignals = GetPrivateProfileIntA("SA.StoriesSprinting", "NoArmedHandSignals", 0, f) != 0;
        noGangTaunts = GetPrivateProfileIntA("SA.StoriesSprinting", "NoGangTaunts", 0, f) != 0;
        debugLog = GetPrivateProfileIntA("SA.StoriesSprinting", "DebugLog", 0, f) != 0;

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

        GetPrivateProfileStringA("AIWalkstyles", "JogPedTypes", "", buf, sizeof(buf), f);
        ParseIds(buf, aiJogPedTypes);
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

    // tauntOnly keeps the handshakes, smoking and leaning and drops just the gesture
    static void StopGangChatAnims(CPed* ped, bool tauntOnly) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return;
        int moveGroup = *(int*)((uintptr_t)ped + WALK_GROUP_OFFSET);
        for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(clump); a; ) {
            CAnimBlendAssociation* next = RpAnimBlendGetNextAssociation(a);
            bool taunt = a->m_nAnimId >= GANG_TALK_FIRST && a->m_nAnimId <= GANG_TALK_LAST;
            bool gang = a->m_nAnimGroup == ANIM_GROUP_GANGS && (!tauntOnly || taunt);
            bool roadcross = a->m_nAnimGroup == moveGroup && a->m_nAnimId == ROADCROSS_SLOT;
            if ((gang || roadcross) && a->m_fBlendDelta >= 0.0f)
                a->m_fBlendDelta = CHAT_BLEND_OUT;
            a = next;
        }
    }

    // DebugLog=1: what a two-handed ped is actually playing, so the groups can be checked
    static void LogPedAnims(CPed* ped, int type) {
        RpClump* clump = (RpClump*)ped->m_pRwObject;
        if (!clump) return;
        FILE* f = fopen((AsiFolder() + "SA.StoriesSprinting.log").c_str(), "a");
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
                StopGangChatAnims(ped, false);
                if (debugLog && --pedLogTimer <= 0) { pedLogTimer = 100; LogPedAnims(ped, type); }
            } else if (noGangTaunts) {
                StopGangChatAnims(ped, true);
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

            // only groups above the movement ones are worth reporting
            bool interesting = false;
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation((RpClump*)ped->m_pRwObject);
                 a; a = RpAnimBlendGetNextAssociation(a))
                if (a->m_nAnimGroup > 0 && a->m_nAnimGroup < 54) { interesting = true; break; }
            if (!interesting) continue;

            unsigned sig = 0;
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation((RpClump*)ped->m_pRwObject);
                 a; a = RpAnimBlendGetNextAssociation(a))
                sig = sig * 131 + (unsigned)(a->m_nAnimGroup * 1000 + a->m_nAnimId);
            if ((int)nearSig.size() != pool->m_nSize) nearSig.assign(pool->m_nSize, 0);
            if (nearSig[i] == sig) continue;
            nearSig[i] = sig;

            if (!f) { f = fopen((AsiFolder() + "SA.StoriesSprinting.log").c_str(), "a"); if (!f) return; }
            fprintf(f, "near type=%d wep=%d anims:", ped->m_nPedType,
                (int)ped->GetWeapon()->m_eWeaponType);
            for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation((RpClump*)ped->m_pRwObject);
                 a; a = RpAnimBlendGetNextAssociation(a))
                fprintf(f, " %d/%d(%.2f)", (int)a->m_nAnimGroup, (int)a->m_nAnimId, a->m_fBlendAmount);
            fprintf(f, "\n");
            shown++;
        }
        if (f) fclose(f);
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

        FILE* f = fopen((AsiFolder() + "SA.StoriesSprinting.log").c_str(), "a");
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
        if (--reloadTimer <= 0) { LoadConfig(); reloadTimer = 100; }
        ProcessPlayer();
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

        int type = (int)ped->GetWeapon()->m_eWeaponType;
        int model = -1, slot = -1;
        if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD)) {
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

        LogPlayer(ped, type, model, slot, base);

        if (base < 0) { SetPatched(false); return; }

        float fat = CStats::GetStatValue(STAT_FAT);
        float musc = CStats::GetStatValue(STAT_MUSCLE);
        int group = base;
        if (fat > 500.0f && fat >= musc) {
            if (!noFat && BlockLoaded("FAT")) group = base + 1;
        } else if (musc > 500.0f && musc >= fat) {
            if (!noMuscle && BlockLoaded("MUSCULAR")) group = base + 2;
        }

        // skinny = neither variant applied, i.e. CJ is on the plain ped anims
        if (noSkinny && group == base) { SetPatched(false); return; }

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

    // called when the feature gets switched off mid-game, so nobody stays overridden
    static void ReleasePeds() {
        if (pedWalk.empty()) return;
        CPool<CPed, CCopPed>* pool = CPools::ms_pPedPool;
        if (pool) {
            int n = pool->m_nSize < (int)pedWalk.size() ? pool->m_nSize : (int)pedWalk.size();
            for (int i = 0; i < n; i++) {
                CPed* ped = pool->GetAt(i);
                if (ped && pool->GetIdAt(i) == pedWalk[i].id) RestorePed(pedWalk[i], ped);
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
            if (!ped) { rec.active = false; continue; }
            // the pool reuses slots; its id byte changes, so any saved group is stale
            unsigned char id = pool->GetIdAt(i);
            if (rec.id != id) { rec.id = id; rec.active = false; }
            if (ped->m_pPlayerData) { rec.active = false; continue; }

            int type = (int)ped->GetWeapon()->m_eWeaponType;
            int model = -1, slot = -1;
            if (CWeaponInfo* wi = CWeaponInfo::GetWeaponInfo((eWeaponType)type, WEAPON_SKILL_STD)) {
                model = wi->m_nModelId;
                slot = (int)wi->m_nSlot;
            }

            //   IgnoreWeapons > RocketWeapons > RifleWeapons > HeavyWeapons > JogWeapons >
            //   [combo] BatWeapons + JogWeapons > [combo] FireExtWeapons > RifleSlots
            //   fallback > [combo] JogSlots fallback > the ped's own walkstyle.
            bool mayJog = aiJogPedTypes.empty() || aiJogPedTypes.count(ped->m_nPedType) != 0;

            int group = -1;
            if (InList(aiIgnoreWeapons, type, model)) group = -1;
            else if (InList(aiRocketWeapons, type, model)) group = ROCKET_BASE;
            else if (InList(aiRifleWeapons, type, model)) group = RIFLE_BASE;
            else if (InList(aiHeavyWeapons, type, model)) group = FIREEXT_BASE;
            else if (mayJog && !InList(noJogWeapons, type, model)
                && InList(aiJogWeapons, type, model)) group = JOG_BASE;
            else if (aiCombo && mayJog && !InList(noJogWeapons, type, model)
                && (InList(aiBatWeapons, type, model) || InList(jogWeapons, type, model)))
                group = JOG_BASE;
            else if (aiCombo && fireExtFix && InList(fireExtWeapons, type, model)) group = FIREEXT_BASE;
            // [JogWeapons] declares a weapon one-handed - keeps the sawn-off out of the
            // rifle carry, its slot is 3 but it animates from the colt45 block
            else if (slot >= 0 && aiRifleSlots.count(slot)
                && !InList(jogWeapons, type, model)) group = RIFLE_BASE;
            else if (aiCombo && mayJog && slot >= 0 && jogSlots.count(slot)
                && !InList(noJogWeapons, type, model)) group = JOG_BASE;

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

std::set<int> StoriesSprinting::jogWeapons;
std::set<int> StoriesSprinting::noJogWeapons;
std::set<int> StoriesSprinting::jogSlots;
std::set<int> StoriesSprinting::fireExtWeapons;
std::set<int> StoriesSprinting::aiRocketWeapons;
std::set<int> StoriesSprinting::aiRifleWeapons;
std::set<int> StoriesSprinting::aiBatWeapons;
std::set<int> StoriesSprinting::aiHeavyWeapons;
std::set<int> StoriesSprinting::aiRifleSlots;
std::set<int> StoriesSprinting::aiJogWeapons;
std::set<int> StoriesSprinting::aiIgnoreWeapons;
std::set<int> StoriesSprinting::aiJogPedTypes;
std::vector<PedWalkstyle> StoriesSprinting::pedWalk;
bool StoriesSprinting::aiWalkstyles = false;
bool StoriesSprinting::aiCombo = false;
bool StoriesSprinting::aiGroupSprint = true;
bool StoriesSprinting::noArmedHandSignals = false;
bool StoriesSprinting::noGangTaunts = false;
bool StoriesSprinting::debugLog = false;
int StoriesSprinting::logTimer = 0;
int StoriesSprinting::pedLogTimer = 0;
std::vector<unsigned> StoriesSprinting::nearSig;
bool StoriesSprinting::groupSprintPatched = false;
bool StoriesSprinting::noFat = false;
bool StoriesSprinting::noMuscle = false;
bool StoriesSprinting::noSkinny = false;
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
