# US-T964 design: Light Society foundation in SpaceGod (architect pass, 2026-11-28)

Paper summary: docs/research/LightSociety-paper.md.

## Shape
- Game-agnostic library: Cpp/Source/SpaceGodCore/LightSociety/, namespace MindAttic::Society, plain C++20 + std only.
  RULE: files under LightSociety/ include only <std> headers and "LightSociety/..." (no Cs/Cs.h, no cs:: types, no Unreal, no SPACEGODCORE_API); nothing exported. Enforced by tools/check-lightsociety-boundary.ps1 (grep includes).
  SpaceGodCore.Build.cs already gives no-PCH, /fp:precise, exceptions, no unity; tools/build-core.ps1 compiles new folders automatically.
- SpaceGod adapter: Populace at Cpp/Source/SpaceGodCore/Systems/Civilization/Populace.*, last module group of Civilization.
  Why: Civilization is created only by the game (UnitySpaceApp.Influence.cpp:79-81), bench only Find()s it (Galaxy.cpp:258) -> bench digests unchanged; Civilization::Update walks whole days (Civilization.cpp:362-395); SocialEvent feed (Civilization.h:130-158; Witness turns raids/battles/conquests/pirate holds into events); Civilization::Stir (:353) and Publish (:191-219) are the doors back.
- Threading: pipeline on a worker. At top of each Civilization::Update (BEFORE the early return at Civilization.cpp:373): wait for previous batch (normally done), apply results, run due days' gather, launch next batch. Worker step = pure function of (state, inputs, seed). Counter-based RNG hash, fixed chunk boundaries independent of core count, combine in chunk order -> lockstep/rewind deterministic. >7 days due -> 7-day strides. Target <=2 ms worker per day at 64k agents.
- Rejected: separate UE module now; host-shipped state for co-op; hooking World::Update (bench path).

## Library API
LightSociety/Types.h      AgentId(u32), GroupId(u32), Profile{externalId, group, household, business, age,cls,edu u8, big5[5] u8, traits u16},
                          TopicSpec{id, priorMean, priorSpread, stubbornness}, Edge{a,b,kind u8,weight u8},
                          EnvEvent{time f64, priority u8, kind u16, topic u8, push f32, magnitude f32, scope{All|Group|Agent}, target u32, source u32},
                          Config{seed u64, slotsPerDay=10, memoryK=4, topics<=8, chunk=4096, policy, surrogateWeight}
LightSociety/Rng.h        Hash(seed, agent, round, op) -> u64 (SplitMix64 finaliser), Unit(), Roll()
LightSociety/AgentStore.* columns: static(profile), internal(stance[T] i16 q15, conf[T] u8, mem ring K x {kind u16,topic u8,valence i8,day u16}, trust u8, express u8),
                          external(group, CSR row); active bitset + free list (join/leave)
LightSociety/Graph.*      CSR (offsets u32, targets u32, weight u8), BuildFrom(edges) sorted; Generators: BarabasiAlbert(m), Clique(family/business), CrossGroup
LightSociety/EventQueue.* min-heap over (time, priority, seq), seq assigned at push; PopBatch() pops all same-(time,priority), stable-grouped by kind
LightSociety/Engine.*     Init(span<Profile>, span<Edge>, span<TopicSpec>) = f_I; Inject(span<EnvEvent>); StepTo(time);
                          ops f_P, f_pi, f_A (memory decay/ageing), f_E (drift), f_U; Readout per group x topic {mean, sup/neu/skep share, conf, var, changeRate}
LightSociety/Backend.h    IBackend::Run(const OpBatch&, OpOut&) -- batch of feature rows -> class probs / continuous deltas
LightSociety/Router.*     Policy{AllTeacher, AllSurrogate, Weighted(w)}: per-sample Roll(Hash(seed,agent,round,op)) < w
LightSociety/LruCache.h   exact-key cache: 64-bit hash of quantized feature bytes + full-key verify (hits change speed, never results)
LightSociety/Teacher.*    rule/utility policies: bounded-confidence persuasion (edu/income weights, reactance; change mostly via neutral), trust/dictator/ultimatum utility (inequity aversion, reciprocity)
LightSociety/Surrogate.*  softmax regression; MLP(64,32 ReLU, dropout .1 via Rng); fixed-order SGD
LightSociety/Distill.*    teacher sample capture, macro-F1 shortlist (within eps of best), then smallest change-rate gap on held-out rollout
LightSociety/Lookup.*     table [profileBucket x profileBucket x stance x stance] -> probs, built from the selected surrogate
LightSociety/Scenarios/*  Diffusion (BA m=3, top 20% degree influencers, inter-influencer edges dropped, 1A1N/1D1N/Random, 1% fire per round as one batch),
                          Continuous (speak/heard/reflect, s in [-1,1], c in [0,1], 10 slots/day, 2 speakers/slot, end-of-day reflect), Games (trust, dictator, repeated x7, ultimatum)
LightSociety/Worker.*     one std::thread job runner + fixed-partition chunk pool (precedent: ProceduralSky.cpp:74,103)
LightSociety/Codec.*      versioned little-endian blob of the whole engine (store, CSR, queue, models, pending readout)
Distillation runs in the first job after init; router uses Teacher until init day + tune.light.distillDays (keyed to sim day, never thread finish).

## Populace (adapter)
- f_I: every living citizen = anchor agent (house, business, persona traits, ties: Society.h:70-131, Civilization.h:96-127); each standing settlement topped up with synthetic agents to tune.light.agentsPerSettlement (64) up to tune.light.maxAgents (65,536; cap 2^20). Synthetic profiles hashed from settlement Civics (Learning, Hawk, Liberty, Faith: Civilization.h:161-188); wealth class from its houses. Edges: BA within settlement, cliques per house and business, existing citizen Ties, sparse cross-settlement links along business branches. Births/deaths/fallen worlds -> join/leave events. Iterate list order only (Houses, Polities->Settlements()), never hashed containers.
- Topics: PolitySupport, PirateFear, GodFaith, TradeTrust, WarHawk.
- Perception (Populace.Perceive.cpp): SocialKind -> EnvEvent table for events with Id > saved cursor (Publish numbers them, Civilization.cpp:193): PirateRaid/PirateHold fear+ support-; HavenStormed fear- support+; BattleFought/WorldConquered/Scandal support-; Steered faith+. Bounty kills by diffing outlaw fates daily (Law/Outlaws.h:101-107 Fate, FateBy). Lane robberies: Society::Robberies (Society.h:343) + trip notes. Other systems' events delayed by Galaxy.Traffic->News->RelayDays(from,to,jd) (InterstellarTraffic.h:303, News.h:223). Ignore own published events.
- Feedback (Populace.Feedback.cpp), daily on game thread at wait-apply: support -> Civilization::Stir(s, dUnrest, dApproval), gain knob, cap 0.01/day; monthly poll as SocialKind::PollPublished (Civilization.h:65) Amount = support share; threshold crossings publish NEW kinds OpinionShift, Panic, TrustCrisis, Revival via Publish; TradeTrust (monthly trust/ultimatum rounds between business owners) -> term in Society::Books (Society.cpp:715) and lowers trip planning through pirate-held systems. All read Populace::Mood(settlement, topic), neutral when no Populace.
  RISK: Witness::IsWitnessed is k >= WorldConquered && k < Count_ (Witness.cpp:98): change bound to <= Embargo when appending kinds.
- Not in epic: Economy prices, contracts (per-World code) -> proposed follow-up; GodFaith -> SpaceGodInfluence (Embodiment/Influence.h:45) owner decision.
- Save: Civilization gets SG_OPT(Populace); Populace registers b.FieldWith<LightStateCodec>("State", &Self::state) (ObjectGraph.h:1073); codec Write waits for worker, encodes blob, writes ONE base64 string node (StringCodec, ObjectGraph.h:452-464). NOT a byte array (56-byte Value per element). Old saves: no Populace, built on first day. Rewind keyframes encode the blob (RewindRing.cpp:318): report encode time/size.
- Co-op: lockstep, all peers run jobs; knobs Tuning::Knob(..., TuningScope::Simulation, ...) (Tuning.h:15-43); hand verb `sway <topic> <settlement> <push>` in CivHands travels as a "civ" entry (UnitySpaceApp.Society.inl:15-21); `light <readout hash>` line in CoopDigestText (UnitySpaceApp.Coop.cpp:702).
- UI: CivHands view `opinion` (CivHandCommands.cpp:71) -> Society screen page (SocietyScreen.h:77); OPINION section in Society screen world detail; GalaxyMapScreen::MapMode::Opinion appended (GalaxyMapScreen.h:96), cycles topics, colours systems by mean stance; sg.LightSociety console command.
- Bench: no change to Bench/expected-digests.json; all entry points behind Civilization::Find / Populace::Mood (null/neutral in bench). sg.LightSociety selftest: two sandbox runs same seed -> same blob hash; 1-thread vs N-thread same.
- Thread lifetime: destroying a Populace (load/new game) must join its worker.
- Shared checkout: UnitySpaceApp.Coop.cpp, UnitySpaceApp.h, HUD files are dirty from others: small hunks only, never touch UnitySpaceApp.h; console command uses per-file RunningApp() convention (SocietyCommand.cpp:34-44).

## Library lift (for Prose)
Prose is .NET 10 / C#, local ASP.NET Core Hub. Lift path: flat C ABI (ls_create, ls_add_agents, ls_add_edges, ls_inject, ls_step, ls_readout, ls_save, ls_load) -> mindattic_society.dll via P/Invoke. Keep boundary now (cheap); lift to its own repo (2-3 tasks) when Prose has a consumer; SpaceGod vendors it (no sibling reads, BIBLE LAW-5). C# port rejected.
