========================================================================
 Leeds Moveset
========================================================================

Sprint with every weapon (chainsaw, RPG, rifles, etc.) using the native
LCS/VCS animations, plus the one-handed "jog" run animation for melee
weapons, pistols and SMGs - with proper fat and muscular variations.

This edition replaces the original mod's CLEO script AND the "eASIer"
(sprint.asi) plugin with a single ASI. The weapon list is now fully
configurable, including fastman92 add-on weapons. It also absorbs
"AI Walkstyles Fix", so peds can use the weapon walkstyles too.


------------------------------------------------------------------------
 What's in this version
------------------------------------------------------------------------

- One plugin (SA.LeedsMoveset.asi) - no CLEO Library required, no
  separate memory-patcher plugin.
- Configurable weapon list via SA.LeedsMoveset.ini - add your own
  weapons (including fastman92 add-on weapons) to the jog animation.
- The heavy-weapon sprint patches (formerly sprint.txt) are built in.
- Settings reload live while you play - edit the INI and it applies.
- AI Weapon Walkstyles (optional): peds carry rifles and rocket launchers
  properly instead of holding them one-handed.
- Player Weapon Walkstyles (optional): choose how CJ carries a weapon -
  its walk, run and armed idle - so an add-on weapon such as a large SMG
  can be held like an assault rifle instead of one-handed.
- Sprint on any surface (optional): removes the surfinfo.dat restriction
  that stops CJ sprinting indoors and in a few other spots.
- Detonator animation (optional): restores the unused "bomber" animation
  for the satchel detonator, with the explosion synced to it.
- Fixed: peds carrying a prop no longer stand as if holding their weapon
  two-handed, and the jetpack keeps its own idle - CJ holds the handles
  again instead of leaving his arms at his sides.
- Fixed: with AIStoriesSprintingCombo on, peds took the whole player
  walkstyle and so walked, sprinted and idled like CJ. The jog now
  replaces only a ped's running animation - it keeps the walk, sprint
  and idle of the walkstyle it spawned with.
- Fixed: the mod was reading each weapon's model id and weapon.dat slot
  from the wrong entry, which put the sniper, rocket launcher and some
  melee weapons on the wrong walkstyle. Add-on weapons were hit hardest.
- Fixed: the mod redirected a shared game function - the per-frame pad
  update - without preserving whatever was already hooked there, so any
  other plugin using that same call was silently dropped. Most visibly
  SkyUI, whose pause menu lost its Brief, Map and Stats tabs. It now
  chains onto the existing hook instead, and only installs one at all
  when the detonator animation is turned on.


------------------------------------------------------------------------
 Requirements
------------------------------------------------------------------------

- GTA San Andreas v1.0 US (HOODLUM).
- An ASI loader (Silent's ASI Loader / Ultimate ASI Loader).
- Mod Loader (recommended install method below).


------------------------------------------------------------------------
 Installation (Mod Loader - recommended)
------------------------------------------------------------------------

1. Copy the "SA.LeedsMoveset" folder from inside "modloader" into your
   game's "modloader" folder.

That's it. The folder contains everything:
   scripts\  -> the ASI and INI
   anim\, data\, models\  -> the animations and weapon data


------------------------------------------------------------------------
 Installation (manual, without Mod Loader)
------------------------------------------------------------------------

- scripts\SA.LeedsMoveset.asi + SA.LeedsMoveset.ini -> game root.
- anim\ped.ifp                     -> replace in gta3.img (or modloader).
- models\gta3.img\fat.ifp          -> replace in gta3.img.
- models\gta3.img\muscular.ifp     -> replace in gta3.img.
- data\SA.LeedsMovesetWeaponData.txt -> merge into your weapon.dat.


------------------------------------------------------------------------
 Configuring weapons (SA.LeedsMoveset.ini)
------------------------------------------------------------------------

[JogWeapons]     - weapons that use the one-handed jog run animation.
[FireExtWeapons] - weapons that use the chainsaw/heavy one-handed style.
[JogSlots]       - fallback: any weapon whose weapon.dat slot is listed
                   jogs, so add-on weapons work without being listed.
[NoJogWeapons]   - weapons that must never jog. Overrides everything.
[PlayerWalkstyles] - how CJ CARRIES a weapon: its walk, run and armed
                   idle. RifleWeapons holds it like an assault rifle,
                   RocketWeapons like an RPG, HeavyWeapons like a
                   chainsaw. Wins over the lists above. Needs
                   PlayerWeaponWalkstyles=1. CJ only, and unrelated to
                   the AI settings - peds have their own equivalent
                   under [AIWalkstyles]. Note this changes how a weapon
                   is carried, not how it is aimed or fired; that comes
                   from its anim group in weapon.dat.

You can list either a weapon TYPE id (vanilla weapons) or a weapon's
MODEL id (fastman92 add-on weapons) - both are matched. Separate ids with
commas or spaces.

Other options:
  NoFat1Armed / NoMuscle1Armed        - disable the fat/muscular jog
                                        variations (for BeSlim, etc.).
  NoSkinny1Armed                      - disable the jog when CJ is on the
                                        plain ped anims.
  FireExtinguisherWalkstyleFix        - 1 = on (default).
  SprintOnAnySurface                  - lifts the "can't sprint on" flag
                                        that surfinfo.dat sets on some
                                        surfaces, so CJ can sprint indoors
                                        and anywhere else it was blocked.
                                        Done at runtime, so surfinfo.dat
                                        is left alone and no other mod
                                        that edits it is disturbed.
                                        Off by default.
  DetonatorAnimation                  - plays "bomber", the detonator
                                        animation left unused in ped.ifp,
                                        and sets the charges off in time
                                        with it. DetonatorAnimationDelay
                                        tunes when the bang lands.
                                        Off by default.


------------------------------------------------------------------------
 AI Weapon Walkstyles (peds)
------------------------------------------------------------------------

Set AIWeaponWalkstyles=1 to give peds the same weapon walkstyles the
player gets - two-handed for rifles and shotguns, the launcher stance for
rockets, the chainsaw stance for the flamethrower and minigun. Off by
default.

The lists live in [AIWalkstyles] and take the same TYPE or MODEL ids:

  RocketWeapons / RifleWeapons / HeavyWeapons / BatWeapons
                       - weapons for each walkstyle.
  RifleSlots           - fallback by weapon.dat slot, so add-on long guns
                         are covered without listing them.
  JogWeapons           - peds only: force the one-handed jog on these,
                         without turning the whole combo on.
  IgnoreWeapons        - peds only: leave the ped's own walkstyle alone.
  JogPedTypes          - which peds may jog at all. Ships as cops and
                         every gang, so ordinary civilians keep their own
                         run. Set it EMPTY for every ped. Gates the jog
                         only, never the rifle or rocket carry styles.

Other options:
  AIStoriesSprintingCombo  - peds also use the one-handed jog for the
                             weapons in [JogWeapons]. Only their running
                             animation changes. Use JogPedTypes above to
                             choose which peds it applies to. Off by
                             default.
  AIGroupSprintFix         - peds in your group sprint from their own
                             walkstyle instead of the plain civilian
                             sprint the game forces. Off by default.
  NoArmedHandSignals       - stop peds throwing gang signs and chatting
                             gestures while holding a two-handed weapon.
                             They still talk. Off by default.
  IgnorePedTypes           - ped types to leave completely alone, for
                             scripted peds that should keep whatever the
                             mission gave them. Empty by default; peds
                             carrying a prop are already skipped on their
                             own.
  DebugLog                 - writes SA.LeedsMoveset.log next to the
                             ASI, for tracking down walkstyle reports.


------------------------------------------------------------------------
 Debug Menu support (optional)
------------------------------------------------------------------------

If you have aap's debugmenu installed (debugmenu.dll next to the exe -
the same menu SilentPatch and SkyGfx use), every on/off setting in this
mod shows up in it under "Leeds Moveset", split into Player, Peds and
Detonator pages. Toggling one takes effect immediately, exactly as
editing the INI does.

Two commands sit at the top of the page:

- Reload INI - re-reads the file. Use this after editing the weapon
  lists by hand; those are not in the menu, because a debug menu is no
  place to type a list of weapon ids.
- Save settings to INI - writes the current on/off states back to your
  INI so they survive a restart. Your comments and weapon lists are
  left untouched.

Menu changes are live only until you save them. Nothing here is
required - without debugmenu installed the mod behaves exactly as
before.


------------------------------------------------------------------------
 If your ped.ifp / fat.ifp / muscular.ifp are already modified
------------------------------------------------------------------------

Use the files in "If Your IFPs Are Modified" with GTA Anim Manager to
merge the new animations into your existing IFPs instead of replacing
them:

  Open your modified ped.ifp, right-click CustomPed.ifp, "Open with
  Options...", OK, then save. Repeat for fat.ifp with CustomFat.ifp and
  muscular.ifp with CustomMuscular.ifp.


------------------------------------------------------------------------
 Optional Extras
------------------------------------------------------------------------

Minigun Sprinting - lets you jump and sprint with the Minigun. Merge the
"data" file inside it into your weapon.dat (or drop it into the mod's
modloader folder).


------------------------------------------------------------------------
 Credits
------------------------------------------------------------------------

- Original "Stories Sprinting"      - H-G
- Animations                        - SlingShot753
- "eASIer" memory patcher (absorbed into this ASI) - HackMan128
- "AI Walkstyles Fix" (absorbed into this ASI) - H-G
- Detonator animation timing (approach from the "Unused
  Detonator" CLEO)                    - Cleomodlar
- debugmenu, and its client header (MIT) - aap
- ASI conversion + fastman92 add-on weapon support - this edition
