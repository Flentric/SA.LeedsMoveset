========================================================================
 Stories Sprinting - ASI Edition
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

- One plugin (SA.StoriesSprinting.asi) - no CLEO Library required, no
  separate memory-patcher plugin.
- Configurable weapon list via SA.StoriesSprinting.ini - add your own
  weapons (including fastman92 add-on weapons) to the jog animation.
- The heavy-weapon sprint patches (formerly sprint.txt) are built in.
- Settings reload live while you play - edit the INI and it applies.
- AI Weapon Walkstyles (optional): peds carry rifles and rocket launchers
  properly instead of holding them one-handed.
- Fixed: the mod was reading each weapon's model id and weapon.dat slot
  from the wrong entry, which put the sniper, rocket launcher and some
  melee weapons on the wrong walkstyle. Add-on weapons were hit hardest.


------------------------------------------------------------------------
 Requirements
------------------------------------------------------------------------

- GTA San Andreas v1.0 US (HOODLUM).
- An ASI loader (Silent's ASI Loader / Ultimate ASI Loader).
- Mod Loader (recommended install method below).


------------------------------------------------------------------------
 Installation (Mod Loader - recommended)
------------------------------------------------------------------------

1. Copy the "SA.StoriesSprinting" folder from inside "modloader" into your
   game's "modloader" folder.

That's it. The folder contains everything:
   scripts\  -> the ASI and INI
   anim\, data\, models\  -> the animations and weapon data


------------------------------------------------------------------------
 Installation (manual, without Mod Loader)
------------------------------------------------------------------------

- scripts\SA.StoriesSprinting.asi + SA.StoriesSprinting.ini -> game root.
- anim\ped.ifp                     -> replace in gta3.img (or modloader).
- models\gta3.img\fat.ifp          -> replace in gta3.img.
- models\gta3.img\muscular.ifp     -> replace in gta3.img.
- data\SA.StoriesSprintingWeaponData.txt -> merge into your weapon.dat.


------------------------------------------------------------------------
 Configuring weapons (SA.StoriesSprinting.ini)
------------------------------------------------------------------------

[JogWeapons]     - weapons that use the one-handed jog run animation.
[FireExtWeapons] - weapons that use the chainsaw/heavy one-handed style.
[JogSlots]       - fallback: any weapon whose weapon.dat slot is listed
                   jogs, so add-on weapons work without being listed.
[NoJogWeapons]   - weapons that must never jog. Overrides everything.

You can list either a weapon TYPE id (vanilla weapons) or a weapon's
MODEL id (fastman92 add-on weapons) - both are matched. Separate ids with
commas or spaces.

Other options:
  NoFat1Armed / NoMuscle1Armed        - disable the fat/muscular jog
                                        variations (for BeSlim, etc.).
  NoSkinny1Armed                      - disable the jog when CJ is on the
                                        plain ped anims.
  FireExtinguisherWalkstyleFix        - 1 = on (default).


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

Other options:
  AIStoriesSprintingCombo  - peds also use the one-handed jog for the
                             weapons in [JogWeapons]. Off by default.
  AIGroupSprintFix         - peds in your group sprint from their own
                             walkstyle instead of the plain civilian
                             sprint the game forces. Off by default.
  NoArmedHandSignals       - stop peds throwing gang signs and chatting
                             gestures while holding a two-handed weapon.
                             They still talk. Off by default.
  DebugLog                 - writes SA.StoriesSprinting.log next to the
                             ASI, for tracking down walkstyle reports.


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
- ASI conversion + fastman92 add-on weapon support - this edition
