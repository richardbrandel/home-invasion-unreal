# Lobby and networked play — design and plan

Status: **DEFERRED 2026-10-29.** Steam and Epic deployment are on hold at Richard's request, and
this plan is for a networked lobby — **do not start any phase of it until that changes.** Nothing
here has been built. Written 2026-10-29, after inspecting what this project
and this engine build actually provide. Read this before writing a line of the network layer:
the shape below is chosen to fit what is already here, and the first phase is deliberately
testable without Steam, an App ID, or a second machine.

## What the game is today

- **Single-player and local.** There is no networking code of any kind: `grep -riE
  "NetDriver|Replicat|OnlineSubsystem|Session" Source/` finds only incidental matches in tests
  and prose. Nothing replicates, nothing connects.
- **The simulation is a pure function library.** `FHISim` (`Source/HISim/`) owns the round:
  player state, intruders, loot, events. `FHIRound::Step(FHIInput, Dt)` advances it at a fixed
  1/60 s. It is deterministic-ish and has no knowledge of actors, rendering or the engine.
- **The game layer is a view.** `AHIGameMode` reads input into `FHISim::FHIInput`, steps the
  round, and the actors (`AHIIntruder`, `AHIPlayerCharacter`, `AHILootProp`, `AHIMarks`)
  follow the sim's state. This is a very good starting point for host-authoritative play: the
  sim is already the authority and the actors are already downstream of it.
- **The HUD is C++ canvas** (`AHIHUD`), not a UMG widget, because this project cannot author
  `.uasset` widgets. **A lobby screen is therefore buildable in text.**
- **The engine has what Steam needs**: `Engine/Plugins/Online/OnlineSubsystemSteam` and the
  Steamworks SDK (`Engine/Binaries/ThirdParty/Steamworks/Steamv164`) are both present in this
  source build. Neither is enabled for this project yet.

## The target

- A **lobby before the round**: create or join, see who is in, start when the host says so.
- **One human home defender, always.** That is not optional; the game is home invasion.
- **Humans can replace robot intruders.** A lobby of four is one defender and up to three
  humans on the intruder side; unfilled slots stay AI, so the game is playable with 1-4 players.

## The architecture, and why

**Host-authoritative, sim on the host, actors downstream.**

The host runs `FHISim` exactly as today. Remote clients do **not** run the sim; they send input
and receive state. That choice follows the code that exists:

- The sim is already a self-contained `Step(Input, Dt)`. A client-side sim would have to be kept
  in lockstep with the host's, which is a much harder problem than sending 20 snapshots a
  second.
- Intruder AI, theft, loot and round rules all live in the sim. Duplicating them on clients is
  duplication of the hardest code in the project.
- `FHIInput` is already a small POD struct, so it is already the right wire format for a client
  input packet.

The cost of this choice: **no client-side prediction**, so remote players are a round trip
behind. On a LAN or a typical home connection that is 20-60 ms, which is acceptable for a
co-operative home-defence game where the intruder's own movement is not competitive. If it ever
feels bad, prediction is a later layer, not a redesign.

### The four moving parts

1. **Session layer** — `IOnlineSession` create/find/join, lobby members, host start. One class,
   `FHISession`, used by the lobby screen. Platform-agnostic: works with Steam, and with the
   `Null` online subsystem for local testing.
2. **Lobby screen** — a state drawn by `AHIHUD` in canvas: `Main → Host/Join → Lobby → Playing`.
   No `.uasset` needed. Keys, not mouse, first; a mouse-driven version can follow.
3. **Intruder slots become claimable.** Today `AHIIntruder` is always sim-driven. It gains a
   `bHumanControlled` path: when a human claims slot N, the sim stops stepping that intruder's
   AI and instead applies the remote input for it. **The sim needs one new input field**
   (`TArray<FHIThiefInput> RemoteThieves`) and one branch where the AI thinks.
4. **Replication** — the host sends, ~20 Hz, the parts of `FHIRound` the actors need: player
   pose, intruder poses/states, loot states, round state, events. Clients send input ~30 Hz for
   their claimed slot. UE's actor replication can carry the poses; the round state is easier as
   one explicit struct than as network-flagged fields scattered across actors.

## Phases, in the order they can actually be tested

| phase | deliverable | testable how | rough size |
|---|---|---|---|
| **1** | Session layer + lobby screen, no players yet | `Null` subsystem, one process | 1 session |
| **2** | Listen server: host + one client, both see the same round | two instances on this box, `-server`/`-client`, localhost | 1-2 sessions |
| **3** | Human intruders: claim a slot, replace that AI | two instances, one claims a thief | 1-2 sessions |
| **4** | Steam: enable the subsystem, App ID, lobby over Steam | needs Steam running + App ID | 1 session + your Steam setup |
| **5** | Polish: join in progress, drop handling, rejoin, lobby chat | — | open-ended |

Phase 4 is the only one that cannot be developed or verified on this box alone. Phases 1-3 use
the `Null` subsystem and two local processes, so the whole architecture can be built and proven
before Steam is in the loop — which is exactly what you want, because Steam failures are
platform failures, not logic failures, and mixing the two costs days.

## Steam prerequisites (yours to provide, when we reach phase 4)

- A **Steamworks App ID**. For development, `480` (Spacewar) is the public test app and works
  for lobbies without owning anything.
- Steam client running and logged in on the test machine.
- Config, once we get there: `[/Script/Engine.GameEngine]` NetDriverDefinitions,
  `[/Script/OnlineSubsystemSteam.OnlineSubsystemSteam] bEnabled=true` + `SteamDevAppId=480`,
  `DefaultPlatformService=Steam`, and `OnlineSubsystemSteam` enabled in the `.uproject`.
  **None of this is committed yet** — it is deliberately left until phase 4 so that phases 1-3
  are not blocked on a Steam install.
- Two Steam accounts to test a lobby properly (one host, one joiner). One account can run two
  instances with the Null subsystem, but not with Steam.

## Open decisions, for Richard

1. **Intruder count vs lobby size.** A lobby of 4 = defender + 3 intruders. Should the lobby
   cap at 4, or allow more humans than intruder slots (spectators)? Default assumption: cap 4.
2. **Who can start the round?** Host only, assumed.
3. **Can humans join mid-round?** Assumed no for phase 3; join-in-progress is phase 5.
4. **Friendly fire between intruders?** Not a networking question but it becomes one the moment
   there are two humans on the intruder side. Current sim has no notion of it.
5. **Theft authority.** The defender's shots and the intruders' grabs both live in the sim. With
   host authority, a client's grab is one round trip late. Acceptable, or does it need
   prediction? Assumed acceptable until it is seen.

## Characters: two female intruders

Separate from networking, and **blocked on an asset** (see the session notes): the cast is
already `{james, brute, martha}` — `FHICharAnim::IntruderTagForId` — so one woman is already in
the round. A *second* woman needs a second rigged female character, and this box cannot obtain
one unattended:

- `tools/fetch-sketchfab.py` needs `SKETCHFAB_TOKEN` in the environment (an API token from a
  free Sketchfab account). Not set on this machine.
- Mixamo needs an Adobe account; nothing here can automate it.
- The existing characters arrive as `SourceAssets/characters/<tag>/idle.fbx` — a **rigged FBX
  with animation clips**, not a static glb. `tools/import-assets.py` expects that layout, and
  normalises every clip against the character's own skeleton.

So the deliverable for "two women" is: one asset (either a Sketchfab token, or a rigged FBX
dropped at `SourceAssets/characters/<name>/idle.fbx`), then the cast list becomes
`{brute, martha, <name>}` and every intruder slot is visible in a frame. Without that asset the
honest options are a duplicate of Martha (two identical women, which reads as a bug) or leaving
it at one.

"Prettier" is art direction, not a code change: these are photogrammetry scans of real people.
Making them prettier means choosing a **stylised, game-ready female character** instead of a
scan — which is a taste decision worth making explicitly, because it changes the whole game's
look from "scanned reality" toward "stylised". Worth deciding before the asset is bought or
downloaded, not after.
