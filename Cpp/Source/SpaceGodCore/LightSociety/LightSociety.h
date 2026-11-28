#pragma once
// US-T964: Light Society, a game-agnostic agent-society simulation after arXiv 2506.12078 (docs/research/LightSociety-paper.md,
// docs/research/LightSociety-design.md). Namespace MindAttic::Society, C++20 and the standard library only: files under
// LightSociety/ include nothing but <std> headers and "LightSociety/..." (tools/check-lightsociety-boundary.ps1), so the
// library can be lifted out of SpaceGod whole.

#include "LightSociety/AgentStore.h"
#include "LightSociety/Codec.h"
#include "LightSociety/Graph.h"
#include "LightSociety/Rng.h"
#include "LightSociety/Types.h"
