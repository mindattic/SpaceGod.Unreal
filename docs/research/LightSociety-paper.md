# Light Society (arXiv 2506.12078v2, "Modeling Earth-Scale Human-Like Societies with One Billion Agents") — the foundation

Source: arXiv 2506.12078v2 (owner copy on the Desktop; pdftotext; sections 2.1, 4.1, 4.2, 4.5, 4.7 matter most).

## Formalism
M := <D, T, S_A, S_E, V, Q, F>,  F := { f_I, f_P, f_pi, f_A, f_E, f_U } (+ readout)
- D seed dataset; T timeline; dynamic set of active agents (birth/death/join/leave).
- Agent state S_A = static profile (demographics, personality) + internal status (memory, beliefs/stances, confidence, emotion, goals) + external status (location, social links).
- Environment S_E = static (spatial layout, rules) + dynamic (weather, disasters; drift).
- Events V: timestamp, priority, sequence; initiators + targets; structured payload (message, resource transfer, intention). Queue Q = min-heap over (time, priority, sequence) -> deterministic.
- Operations: f_I initialization from seed dataset (rule-based / generated / augmented); f_P perception; f_pi policy (decides, emits new events); f_A agent evolution between events (memory decay, ageing); f_E environment evolution (drift); f_U update applies events to state. Readout aggregates trajectories of states+events into measurable outputs (charts/reports).

## Systems layer
- Event aggregation: same-(time, priority) events popped together as a concurrent batch, grouped by kind, dispatched as one vectorized call to the policy/update op. Asynchronous execution.
- Mixture-of-models engine: every operation request routed by a configurable policy (all-LLM, all-surrogate, per-sample weighted) to a registered backend table: full LLMs, distilled surrogate models (MLP etc.), precomputed lookup tables. Backends share an async batch interface.
- Prompt cache: exact (SHA-256-keyed LRU) + semantic cache.
- Knowledge distillation: train small surrogates (softmax regression, GBT, MLP(512,256,ReLU,dropout .1), transformer, small LM) on teacher interaction samples; select checkpoint by macro-F1 shortlist then smallest change-rate gap (aggregate fidelity vs teacher).
- Lookup table: surrogate precomputed over [influencer profile x target profile x stance x stance] -> each interaction is one array lookup.
- Storage: adjacency in CSR; agent state as a per-agent record store (small) or a columnar store (per-field arrays) for big runs; vectorized per-field updates.

## Scenarios validated
1. Trust Game (trustor sends N of 10, tripled; trustee returns some), Dictator, Repeated (7 rounds). Trust rises with social class & education; age gap sharpens with population size.
2. Influencer-influencee opinion diffusion on a Barabasi-Albert graph (m=3): top 20% by degree are influencers (inter-influencer edges dropped); stances {disagree, neutral, agree}; seeding schemes 1A1N / 1D1N / Random; each round 1% of influencers fire an influence event at all their influencees, same timestamp, aggregated batch, one vectorized lookup. Findings: topic prior matters; change goes almost only through neutral (U-shaped agree curve: reactance then cascade); education/income -> more persuasive and more resistant; long horizon settles to a stochastic equilibrium (no full consensus).
3. Continuous stance s in [-1,1] + confidence c in [0,1] (supporter s>0.2, skeptic s<-0.2); 10 slots/day, 2 speaker activations per slot (expression-desire heuristic), speak -> heard fan-out to neighbours (one batch), heard -> listener update; end of day reflective-update for every agent that heard something. Dense graphs polarize/drift, sparse keep spread.
4. Ultimatum game: bounded rationality (offers ~40%, low offers rejected; iterated offers decline, rejection peaks mid-run).
5. Memory-augmented free discussion: sliding window of recent conversations + daily reflective summary.

## What "self-contained in Unreal, not a billion" means for SpaceGod
No external LLM API at runtime (nothing may read outside the repo at run time). The "teacher" is an in-engine rich rule-based/utility policy; surrogates (tiny MLP / softmax regression with deterministic training, or shipped weights) and lookup tables are distilled from it; the mixture-of-models router routes per op. Scale: tens of thousands to ~1M agents, bounded per-frame cost, heavy work off the game thread (owner's rule: fast and reactive).
