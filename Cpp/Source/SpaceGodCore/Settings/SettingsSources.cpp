// Not a C# file (US-T21): the explicit stand-in for SettingsRegistry.DiscoverDeclarations, which in C# runs the static
// initialiser of every class marked [SettingsSource] by reflection (see docs/MIGRATION.md, "Conventions").
//
// Each entry touches one C# [SettingsSource] class: its RunClassConstructor() where the port has one (it builds every
// static field in textual order, as C#'s type initialiser does), otherwise each setting accessor in the C# textual
// order of the fields. The list is in C#'s discovery order: Assembly.GetTypes() returns types in metadata order, which
// is the order the compiler was given the files (MSBuild's **/*.cs glob: the folder's own files, then each subfolder,
// alphabetically) and, within a file, declaration order. The order decides only where a setting not yet touched by
// anything else lands in SettingsRegistry.All (the menu's order within a group).
//
// Adding a [SettingsSource] class to Core means adding it here; the engine side registers its own (Runtime's
// SpaceCamera, EngineSettings, RenderPipelineSelection, UrpPostProcessSettings, UnitySpaceApp counterparts) with
// SettingsRegistry::AddDeclarationSource before the first Find.

#include "Settings/SettingsRegistry.h"

#include "Difficulty.h" // US-T253
#include "RenderBudget.h"
#include "RenderLod.h"
#include "ShipLod.h"
#include "SimClock.h"
#include "Streaming.h"
#include "Combat/AlphaStrike.h"
#include "Combat/ArmourAngling.h"
#include "Combat/BattleTuning.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/BreacherBays.h"
#include "Combat/Broadsides.h"
#include "Combat/BulkFreighters.h"
#include "Combat/CombatTuning.h"
#include "Combat/Corvettes.h"
#include "Combat/Dogfighting.h"
#include "Combat/Drift.h"
#include "Combat/ElectronicWarfare.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/FireCones.h"
#include "Combat/FleetCoordination.h"
#include "Combat/FleetPatterns.h"
#include "Combat/FlightDecks.h"
#include "Combat/FlightModels.h"
#include "Combat/GateBreakers.h"
#include "Combat/GatePickets.h"
#include "Combat/LaneInterdictors.h"
#include "Combat/MarineHabitats.h"
#include "Combat/NaniteRepairs.h"
#include "Combat/Radiators.h"
#include "Combat/RearLine.h"
#include "Combat/ScreenWeapons.h"
#include "Combat/ShieldEmitters.h"
#include "Combat/SpinalMounts.h"
#include "Combat/TachyonLances.h"
#include "Combat/WeakPatches.h"
#include "Economy/TradeDesk.h"
#include "Law/Law.h"
#include "Rendering/Skyboxes.h"
#include "Sensors/Sensors.h"
#include "Settings/DevFlags.h"
#include "Systems/CruiseDrive.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/FtlNavigation.h"
#include "Systems/FtlRules.h"
#include "Systems/GalaxyStreaming.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/StarDiscovery.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"

namespace SpaceGod::Core
{
	int SettingsRegistry::DiscoverLoadedDeclarations()
	{
		int found = 0;
		// ---- Core/ (the folder's own files)
		Difficulty::Level(); Difficulty::CustomDamageTaken(); Difficulty::CustomAccuracy(); Difficulty::CustomLockTime(); Difficulty::CustomAggression();
		Difficulty::CustomPay(); Difficulty::CustomReaction(); found++; // US-T253 (not a C# source)
		RenderLod::SimplifyBelowPx(); RenderLod::CullBelowPx(); RenderLod::AggregatePx(); found++;
		RenderBudget::FullCap(); RenderBudget::SimplifiedCap(); RenderBudget::IconCap(); RenderBudget::Hysteresis(); found++; // US-T719 (not a C# source)
		ShipLod::IconBelowPx(); ShipLod::IconNames(); found++;
		SimClock::StartingWarp(); found++;
		Streaming::FrameBudgetMs(); Streaming::LeadSeconds(); Streaming::GateCoverMaxSeconds(); found++;
		// ---- Core/Combat/
		AlphaStrike::RunClassConstructor(); found++;
		ArmourAngling::RunClassConstructor(); found++;
		BattleTuning::EscapeKm(); BattleTuning::HuntPlayerKm(); BattleTuning::MineTriggerKm(); BattleTuning::FireCallWeightKm();
		BattleTuning::FallBackStandKm(); BattleTuning::CrippleCallWeightKm(); BattleTuning::WithdrawCoverKm(); BattleTuning::MergeKm();
		BattleTuning::ExtendKm(); BattleTuning::TailKm(); found++; // US-T540 (not a C# source)
		BoardingShuttles::TeamSize(); BoardingShuttles::LatchSeconds(); BoardingShuttles::RangeKm(); BoardingShuttles::GateSeizeDays(); found++;
		BreacherBays::TeamSize(); BreacherBays::ReadySeconds(); BreacherBays::CycleSeconds(); BreacherBays::RoundSpeedKmS(); BreacherBays::SteerKmS2();
		BreacherBays::RangeKm(); BreacherBays::ImpactDamage(); found++;
		Broadsides::RunClassConstructor(); found++;
		BulkFreighters::LaneTollShare(); found++;
		CombatTuning::WeaponDamage(); CombatTuning::WeaponRange(); CombatTuning::RateOfFire(); CombatTuning::ProjectileSpeed();
		CombatTuning::ShieldStrength(); CombatTuning::ShieldRegen(); CombatTuning::HullStrength(); found++; // US-T539 (not a C# source)
		Corvettes::PerNavy(); found++;
		BoostPool::ChargeSeconds(); BoostPool::RunnerChargeSeconds(); BoostPool::RefillSeconds(); BoostPool::TopSpeedSway(); found++; // US-T738 (not a C# source)
		AssistThrottle::ToTop(FlightFamily::LightFighter); AssistThrottle::ToStop(FlightFamily::LightFighter); AssistThrottle::LightFighterRoll();
		AssistThrottle::SpinUp(FlightFamily::LightFighter); AssistThrottle::BoostKick(); AssistThrottle::BoostKickShare(); AssistThrottle::BoostBleed(); found++; // US-T818, US-T828, US-T820 (not a C# source)
		Drift::SpinUp(); Drift::Turn(); Drift::BoostOut(); Drift::BoostOutReach(); Drift::BoostOutReachShare(); found++; // US-T829 (not a C# source)
		Countermeasures::Effectiveness(); Countermeasures::AiSpoofLocks(); found++;                         // Combat/Dogfighting.cs
		WeakPoints::Damage(); WeakPoints::AiGoesForThem(); found++;          // Combat/Dogfighting.cs
		StrikeTorpedoes::BareHullDamage(); found++;                         // Combat/Dogfighting.cs
		ElectronicWarfare::RunClassConstructor(); found++;
		EnergyWeapons::RunClassConstructor(); found++;
		FireCones::RunClassConstructor(); found++;
		FleetCoordination::RunClassConstructor(); found++;
		FleetPatterns::RunClassConstructor(); found++;
		FlightDecks::ServiceSeconds(); FlightDecks::AiRearms(); FlightDecks::ServiceHull(); FlightDecks::LaunchTubes(); found++;
		LightFighterSettings::TurnPenalty(); found++;                       // Combat/FlightModels.cs
		GateBreakers::TeamMarines(); GateBreakers::FieldRepair(); GateBreakers::FortFocus(); GateBreakers::PerNavy(); found++;
		GatePickets::PerGate(); GatePickets::JumpWatchAu(); found++;
		LaneInterdictors::RechargeDays(); LaneInterdictors::PicketChance(); found++;
		MarineHabitats::DrillDays(); MarineHabitats::DrillBonus(); found++;
		NaniteRepairs::RunClassConstructor(); found++;                      // US-T102 (not a C# source)
		Radiators::RunClassConstructor(); found++;
		RearLine::Standoff(); RearLine::GuardKm(); RearLine::RearBias(); RearLine::CapitalBias(); found++;
		ScreenWeapons::RunClassConstructor(); found++;
		ShieldEmitters::RunClassConstructor(); found++;
		SpinalMounts::RunClassConstructor(); found++;
		TachyonLances::RunClassConstructor(); found++;
		WeakPatches::AiRolls(); WeakPatches::Protection(); found++;
		// ---- Core/Economy/
		TradeRules::RunClassConstructor(); found++;                         // Economy/TradeDesk.cs
		// ---- Core/Law/
		Law::PirateKillPayShare(); Law::PirateKillStanding(); found++;      // US-T958 (not a C# source)
		// ---- Core/Rendering/
		SkyboxSettings::DefaultSky(); SkyboxSettings::Brightness(); SkyboxSettings::NebulaResolution(); found++; // Rendering/Skyboxes.cs
		// ---- Core/Sensors/
		Signatures::DriveFactor(); Signatures::WeaponsFire(); Signatures::SilentFactor(); Signatures::HulkFactor(); Signatures::Battle();
		Signatures::Haven(); SensorSuite::ScannerRangeShare(); SensorSuite::RangeScale(); SensorSuite::ArrayScanners(); found++; // US-T542 (not a C# source)
		// ---- Core/Settings/
		DevFlags::All(); found++;                                           // US-T524 (not a C# source)
		// ---- Core/Systems/
		CruiseDrives::SpeedScale(); CruiseDrives::WellLimit(); found++;     // Systems/CruiseDrive.cs
		FleetDoctrine::StrikeGroups(); FleetDoctrine::ReserveShare(); FleetDoctrine::StrikeRangeLy(); FleetDoctrine::StrikeDays(); found++;
		FtlNavigation::TripRealSeconds(); found++;
		FtlRules::WellGravity(); FtlRules::SpoolScale(); FtlRules::CooldownScale(); FtlRules::FuelScale(); FtlRules::CrossingSpeedKmS(); found++;
		GalaxyStreaming::ChartRadius(); GalaxyStreaming::SystemRadius(); GalaxyStreaming::MaxCharted(); found++;
		InterstellarTraffic::TradersPerRoute(); found++;
		LaneCondition::DecayPerDay(); LaneCondition::TendPerDay(); LaneCondition::FailAt(); LaneCondition::SlowestSpeed(); LaneCondition::WorstReliability();
		LaneCondition::StallDays(); found++;                                // Systems/LaneUpkeep.cs
		LaneUpkeep::TendersPerLane(); LaneUpkeep::TenderValue(); LaneUpkeep::TenderFocus(); LaneUpkeep::HuntDays(); LaneUpkeep::RaiderHuntChance();
		LaneUpkeep::DwellDays(); found++;                                   // Systems/LaneUpkeep.cs
		StarDiscovery::Range(); found++;
		Statecraft::AiDiplomacy(); Statecraft::OpinionPace(); Statecraft::TradePactIncome(); Statecraft::VassalLevy(); found++;
		StrategicCommand::ResearchPace(); found++;
		// ---- outside Core (C#: the other loaded assemblies that reference Core, after it)
		auto extra = ExtraSources().ToArray();
		for (const auto& touch : extra)
		{
			touch();
			found++;
		}
		return found;
	}
}
