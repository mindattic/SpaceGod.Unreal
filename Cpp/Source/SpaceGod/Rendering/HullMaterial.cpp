// US-T850: the hull master material at run time. See HullMaterial.h.
#include "Rendering/HullMaterial.h"
#include "Rendering/Materials.h"
#include "SpaceGodLog.h"

#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

#include <algorithm>
#include <unordered_map>

namespace SpaceGod::Rendering
{
	using namespace SpaceGod::UnityCompat;

	namespace
	{
		TAutoConsoleVariable<int32> CVarHullMaster(TEXT("sg.HullMaster"), 1,
			TEXT("US-T850: 1 draws the pack hulls with M_HullMaster (panels, wear, window lights, battle damage) where its "
				"MI_..._Hull instances are imported; 0 the plain lit instances. Read when the first hull is built."));
		TAutoConsoleVariable<float> CVarPanelMetres(TEXT("sg.HullMaster.PanelMetres"), 6.0f,
			TEXT("US-T850: how long one hull panel is, metres."));
	}

	bool HullMaterial::Enabled()
	{
		static int state = -1;
		if (state < 0) state = CVarHullMaster.GetValueOnGameThread() != 0 ? 1 : 0;
		return state == 1;
	}

	cs::String HullMaterial::Variant(const cs::String& path)
	{
		if (!Enabled()) return path;
		static std::unordered_map<cs::String, cs::String> known;
		auto it = known.find(path);
		if (it != known.end()) return it->second;
		cs::String hull = path + "_Hull";
		auto slash = hull.rfind('/');
		cs::String leaf = slash == cs::String::npos ? hull : hull.substr(slash + 1);
		bool ok = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.%s"), UTF8_TO_TCHAR(hull.c_str()), UTF8_TO_TCHAR(leaf.c_str())), nullptr,
			LOAD_NoWarn | LOAD_Quiet) != nullptr;
		if (!ok) UE_LOG(LogSpaceGod, Verbose, TEXT("[US-T850] No %s yet (run tools/build-hull-material.ps1): drawn with the lit instance"), UTF8_TO_TCHAR(hull.c_str()));
		return known.emplace(path, ok ? hull : path).first->second;
	}

	void HullMaterial::SetSpan(MaterialPropertyBlock& block, float stern, float bow, int keelAxis, int sections, double lengthM)
	{
		block.SetVector("HullSpan", Vector4(stern, bow, static_cast<float>(std::clamp(sections, 1, 8)), static_cast<float>(std::clamp(keelAxis, -3, 3))));
		double panel = std::max(0.5, static_cast<double>(CVarPanelMetres.GetValueOnGameThread()));
		block.SetFloat("PanelCells", static_cast<float>(std::clamp(lengthM / panel, 4.0, 512.0)));
	}

	void HullMaterial::SetDamage(MaterialPropertyBlock& block, const std::vector<float>& damage)
	{
		block.SetFloatArray("SectionDamage", damage);
	}

	cs::Ref<Material> HullMaterial::Shield()
	{
		static constexpr const char* Path = "/Game/SpaceGod/Materials/M_HullShield";
		static bool looked = false;
		static cs::Ref<Material> shield;
		if (looked) return shield;
		looked = true;
		if (LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.M_HullShield"), UTF8_TO_TCHAR(Path)), nullptr, LOAD_NoWarn | LOAD_Quiet) == nullptr)
		{
			UE_LOG(LogSpaceGod, Verbose, TEXT("[US-T881] No %s yet (run tools/build-hull-material.ps1): shields drawn as patches"), UTF8_TO_TCHAR(Path));
			return shield;
		}
		shield = Materials::MarkShared(cs::New<Material>(Shader::Find(Path), "HullShield"));
		return shield;
	}

	void HullMaterial::SetShield(MaterialPropertyBlock& block, float strength, Color color)
	{
		block.SetFloat("ShieldStrength", strength);
		block.SetColor("ShieldColor", color);
	}
}
