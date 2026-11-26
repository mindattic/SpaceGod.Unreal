"""Headless build of the hull master material and its shield shell (US-T849).

Run through tools/build-hull-material.ps1, which runs
    UnrealEditor-Cmd Cpp/SpaceGod.uproject -run=pythonscript -script=<abs>/tools/hull_material.py
Re-running is safe: the materials are rebuilt in place and the instances re-parented, so references to them survive.

What it builds:
  /Game/SpaceGod/Materials/M_HullMaster     Default Lit, US-T172's lit ship shading (tools/import_usc.py's
                                            M_USCShipLit, same texture and tint parameters) plus procedural panel
                                            lines, wear, grime, window lights, faction paint and per-section battle
                                            damage along the keel
  /Game/SpaceGod/Materials/M_HullShield     translucent additive Fresnel shell drawn over a hull
  /Game/ThirdParty/<TAG>/Materials/MI_<TAG>_<Atlas>_<Colour>_Hull
                                            one beside each MI_<TAG>_<Atlas>_<Colour>_Lit already imported (USC and
                                            SSC), parented to M_HullMaster with the same maps

The hull's keel runs along one object-space axis, which ShipView::SetHullSpan picks per hull (the mesh-local axis that
points along the ship's nose; the pack meshes' nose is their native +Y). HullSpan is (stern, bow, keel section count,
keel axis) in the mesh's own units: the stern and bow are positions along that axis, and the axis is 1, 2 or 3 for x, y
or z, negated when the nose runs down it (0 reads as +z);
panel cells are sized from that length, so a frigate and a carrier carry the same number of panels per hull length.
SectionDamage is the 8 keel sections' damage (0 = stern, 0..1); ShaderBinding pushes a SetFloatArray("SectionDamage")
as the vectors SectionDamage_0 (sections 0-3) and SectionDamage_1 (sections 4-7).
"""

import unreal

GAME_MATERIALS = "/Game/SpaceGod/Materials"
MASTER = "M_HullMaster"
SHIELD = "M_HullShield"
PACKS = ["USC", "SSC"]
MPC = "/SpaceGodRender/Materials/MPC_SpaceGod"
INCLUDE = "/Plugin/SpaceGodRender/SpaceGodCommon.ush"
TEXTURE_PARAMS = ["BaseColor", "Metallic", "Roughness", "Normal", "Emission"]

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
failures = []


def _log(msg):
    unreal.log(f"Hull: {msg}")


def _fail(msg):
    failures.append(msg)
    unreal.log_error(f"Hull: {msg}")


# Every hull node starts from the same frame: t along the keel (0 stern, 1 bow), the panel grid on the face's two
# in-plane object axes (the face normal from the position's screen derivatives, so no tangent frame is needed), a hash
# per panel, and the battle damage of the keel section under the pixel, blended across section boundaries.
HULL_COMMON = r"""
float ka = abs(HullSpan.w);
float3 keel = ka < 0.5 ? float3(0, 0, 1) : float3(ka < 1.5, ka >= 1.5 && ka < 2.5, ka >= 2.5) * sign(HullSpan.w);
float hullLen = HullSpan.y - HullSpan.x;
if (abs(hullLen) < 1.0) hullLen = 1.0;
float t = saturate((dot(P, keel) - HullSpan.x) / hullLen);
float3 q = P / abs(hullLen) * PanelCells;
float3 fn = cross(ddy(P), ddx(P));
fn = (dot(fn, fn) > 1e-12 && !any(isnan(fn))) ? normalize(fn) : float3(0, 0, 1);
float3 an = abs(fn);
bool side = an.x >= an.y && an.x >= an.z;
float2 uv = side ? q.yz : (an.y >= an.z ? q.xz : q.xy);
float2 cell = floor(uv);
float2 edge = 0.5 - abs(frac(uv) - 0.5);
float2 lw = max(PanelLineWidth, 0.0) + fwidth(uv);
float seam = 1.0 - smoothstep(0.0, 1.0, min(edge.x / lw.x, edge.y / lw.y));
float h = frac(sin(dot(cell + (side ? 0.0 : 31.0), float2(12.9898, 78.233))) * 43758.5453);
float sd[8] = { D0.x, D0.y, D0.z, D0.w, D1.x, D1.y, D1.z, D1.w };
float sections = clamp(round(HullSpan.z), 1.0, 8.0);
float fs = clamp(t * sections - 0.5, 0.0, sections - 1.0);
int s0 = (int)floor(fs);
int s1 = min(s0 + 1, (int)sections - 1);
float dmg = saturate(lerp(sd[s0], sd[s1], fs - s0));
float sn = frac(sin(dot(floor(uv * 3.0), float2(127.1, 311.7))) * 43758.5453);
float scorch = saturate(dmg * (0.6 + 0.8 * sn));
float grime = saturate(Grime) * saturate(seam * 1.5 + (1.0 - h) * 0.35);
"""

HULL_BASE_CODE = HULL_COMMON + r"""
float3 albedo = Base * Tint * _Tint;
albedo *= 1.0 - seam * saturate(PanelLineDepth);
albedo *= 1.0 + (h - 0.5) * 0.25 * saturate(Wear);
albedo *= 1.0 - 0.6 * grime;
float stripe = PaintStripeWidth > 0.0 ? 1.0 - smoothstep(PaintStripeWidth * 0.5, PaintStripeWidth * 0.5 + fwidth(t), abs(t - PaintStripeAt)) : 0.0;
float paint = saturate(max(Mask, stripe)) * saturate(PaintColor.a) * (1.0 - 0.5 * saturate(Wear) * h);
albedo = lerp(albedo, PaintColor.rgb, paint);
albedo *= 1.0 - 0.85 * scorch;
return max(albedo, 0.0);
"""

HULL_METAL_CODE = HULL_COMMON + r"""
return saturate(M * MetallicScale) * (1.0 - 0.7 * scorch);
"""

HULL_ROUGH_CODE = HULL_COMMON + r"""
float r = lerp(R, 1.0 - R, InvertRoughness) * RoughnessScale;
r += (h - 0.5) * 0.15 * saturate(Wear) + 0.3 * grime + 0.6 * scorch;
return clamp(r, 0.12, 1.0);
"""

# import_usc.py's LIT_NORMAL_CODE: the packs' OpenGL-style normal maps into Unreal's tangent frame.
HULL_NORMAL_CODE = r"""
float2 nxy = N.xy;
nxy.y *= FlipGreen;
nxy *= NormalStrength;
float3 tn = float3(nxy, sqrt(saturate(1.0 - dot(N.xy, N.xy))));
return any(isnan(tn)) ? float3(0, 0, 1) : normalize(tn);
"""

# import_usc.py's LIT_EMISSIVE_CODE (pack glow, highlight rim, cool rim, ambient floor) plus window lights on the hull's
# flanks (object X faces) and embers glowing through the scorch of a badly damaged section.
HULL_EMISSIVE_CODE = HULL_COMMON + r"""
float2 nxy = N.xy;
nxy.y *= FlipGreen;
float3 tn = normalize(float3(nxy * NormalStrength, sqrt(saturate(1.0 - dot(N.xy, N.xy)))));
float3 n = mul(tn, (float3x3)Parameters.TangentToWorld);
if (!(dot(n, n) > 1e-8) || any(isnan(n))) n = Parameters.TangentToWorld[2];
n = normalize(n);
float fres = pow(1.0 - saturate(dot(n, Parameters.CameraVector)), 4.0);
float3 col = E * EmissiveIntensity * (0.55 + 0.45 * saturate(_EngineGlow)) * _Tint;
col += float3(0.35, 0.7, 1.0) * fres * _Highlight * 0.6;
col += RimColor * (fres * RimStrength) + Albedo * AmbientFloor;
float2 wq = uv * float2(4.0, 2.0);
float2 wf = frac(wq);
float wh = frac(sin(dot(floor(wq), float2(269.5, 183.3))) * 43758.5453);
float win = (side ? 1.0 : 0.0) * step(wh, saturate(WindowDensity)) * step(abs(wf.x - 0.5), 0.3) * step(abs(wf.y - 0.5), 0.18);
col += WindowColor * (WindowGlow * win * (1.0 - seam) * (1.0 - scorch));
float crack = smoothstep(0.55, 1.0, frac(sin(dot(floor(uv * 9.0), float2(41.3, 289.1))) * 43758.5453));
col += EmberColor * (EmberGlow * dmg * dmg * (0.25 + crack) * scorch);
col = any(isnan(col)) ? float3(0, 0, 0) : col;
return SG_Expose3(col, Exposure, SceneLuminance);
"""

SHIELD_CODE = r"""
float3 n = normalize(Parameters.TangentToWorld[2]);
float fres = pow(1.0 - saturate(abs(dot(n, Parameters.CameraVector))), max(FresnelPower, 0.1));
float3 col = ShieldColor * (max(ShieldStrength, 0.0) * (fres + max(ShieldHit, 0.0) * (0.25 + fres)));
col = any(isnan(col)) ? float3(0, 0, 0) : col;
return SG_Expose3(col, Exposure, SceneLuminance);
"""


def _engine_texture(*paths):
    for p in paths:
        t = unreal.load_asset(p)
        if t is not None:
            return t
    return None


def _black():
    return _engine_texture("/Engine/EngineResources/Black", "/Engine/EngineMaterials/BlackSquareTexture",
                           "/Engine/EngineResources/BlackSquareTexture")


def lit_instances():
    """[(tag, path)] of every MI_<TAG>_*_Lit the USC/SSC imports left (tools/import_usc.py)."""
    out = []
    for tag in PACKS:
        folder = f"/Game/ThirdParty/{tag}/Materials"
        if not eal.does_directory_exist(folder):
            continue
        for p in eal.list_assets(folder, recursive=False, include_folder=False):
            name = p.split("/")[-1].split(".")[0]
            if name.startswith(f"MI_{tag}_") and name.endswith("_Lit"):
                out.append((tag, p.split(".")[0]))
    return sorted(out)


def _material(name, folder):
    path = f"{folder}/{name}"
    mat = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if mat is None:
        mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    return path, mat


class Graph:
    """The node helpers of import_usc.py's build_lit_material, for one material."""

    def __init__(self, mat, label):
        self.mat, self.label, self.y = mat, label, 0

    def node(self, cls, x=-1600):
        e = mel.create_material_expression(self.mat, cls, x, self.y)
        self.y += 140
        return e

    def sample(self, name, texture, sampler):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property("parameter_name", name)
        if texture is not None:
            e.set_editor_property("texture", texture)
        e.set_editor_property("sampler_type", sampler)
        e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        return e

    def _vector(self, name, rgba):
        e = self.node(unreal.MaterialExpressionVectorParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(*rgba))
        return e

    def rgb(self, source, output=""):
        m = mel.create_material_expression(self.mat, unreal.MaterialExpressionComponentMask, -1300, self.y)
        for c, on in (("r", True), ("g", True), ("b", True), ("a", False)):
            m.set_editor_property(c, on)
        mel.connect_material_expressions(source, output, m, "")
        return m

    def vec(self, name, rgb):
        return self.rgb(self._vector(name, (rgb[0], rgb[1], rgb[2], 1.0))), ""

    def vec4(self, name, rgba):
        """A float4 parameter: its RGB with its alpha appended, whatever width the node's default output has."""
        e = self._vector(name, rgba)
        a = mel.create_material_expression(self.mat, unreal.MaterialExpressionAppendVector, -1300, self.y)
        mel.connect_material_expressions(self.rgb(e), "", a, "A")
        mel.connect_material_expressions(e, "A", a, "B")
        return a, ""

    def scalar(self, name, value):
        e = self.node(unreal.MaterialExpressionScalarParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", value)
        return e, ""

    def custom(self, desc, code, out_type, inputs, x=-900, include=True):
        c = mel.create_material_expression(self.mat, unreal.MaterialExpressionCustom, x, self.y)
        self.y += 140
        c.set_editor_property("description", desc)
        c.set_editor_property("code", code)
        c.set_editor_property("output_type", out_type)
        if include:
            c.set_editor_property("include_file_paths", [INCLUDE])
        cis = []
        for n, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            cis.append(ci)
        c.set_editor_property("inputs", cis)
        for n, (source, out) in inputs:
            if not mel.connect_material_expressions(source, out, c, n):
                raise RuntimeError(f"{self.label}: could not connect {desc} input {n}")
        return c

    def exposure(self):
        mpc = unreal.load_asset(MPC)
        if mpc is None:
            raise RuntimeError(f"missing {MPC} (the SpaceGodRender plugin)")
        exposure = self.node(unreal.MaterialExpressionEyeAdaptation)
        luminance = self.node(unreal.MaterialExpressionCollectionParameter)
        luminance.set_editor_property("collection", mpc)
        luminance.set_editor_property("parameter_name", "_UnitySpaceSceneLuminance")
        return [("Exposure", (exposure, "")), ("SceneLuminance", (luminance, ""))]


def default_textures(instances):
    """The master's parameter defaults (a texture parameter needs one of its sampler type): the first lit instance's
    maps, else neutral engine stand-ins for the normal and emission."""
    out = {}
    for _, path in instances:
        mi = unreal.load_asset(path)
        if mi is None:
            continue
        for param in TEXTURE_PARAMS:
            if param not in out:
                t = mel.get_material_instance_texture_parameter_value(mi, param)
                if t is not None:
                    out[param] = t
    out.setdefault("Normal", _engine_texture("/Engine/EngineMaterials/FlatNormal", "/Engine/EngineMaterials/DefaultNormal"))
    out.setdefault("Emission", _black())
    missing = [p for p in TEXTURE_PARAMS if out.get(p) is None]
    if missing:
        raise RuntimeError(f"no default for {', '.join(missing)}: import a ship pack first (tools/import-usc.ps1)")
    return out


def build_master(defaults):
    path, mat = _material(MASTER, GAME_MATERIALS)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)
    g = Graph(mat, MASTER)
    S = unreal.MaterialSamplerType
    F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3

    base = g.sample("BaseColor", defaults["BaseColor"], S.SAMPLERTYPE_COLOR)
    metallic = g.sample("Metallic", defaults["Metallic"], S.SAMPLERTYPE_MASKS)
    rough = g.sample("Roughness", defaults["Roughness"], S.SAMPLERTYPE_MASKS)
    normal = g.sample("Normal", defaults["Normal"], S.SAMPLERTYPE_NORMAL)
    emission = g.sample("Emission", defaults["Emission"], S.SAMPLERTYPE_COLOR)
    mask = g.sample("PaintMask", _black(), S.SAMPLERTYPE_COLOR)
    tint, tint_ = g.vec("Tint", (1, 1, 1)), g.vec("_Tint", (1, 1, 1))
    flip, strength = g.scalar("FlipGreen", -1.0), g.scalar("NormalStrength", 1.0)
    position = g.node(unreal.MaterialExpressionLocalPosition)
    hull = [
        ("P", (position, "")),
        ("HullSpan", g.vec4("HullSpan", (-1000.0, 1000.0, 8.0, 3.0))),
        ("PanelCells", g.scalar("PanelCells", 24.0)),
        ("PanelLineWidth", g.scalar("PanelLineWidth", 0.03)),
        ("PanelLineDepth", g.scalar("PanelLineDepth", 0.35)),
        ("Wear", g.scalar("Wear", 0.3)),
        ("Grime", g.scalar("Grime", 0.2)),
        ("D0", g.vec4("SectionDamage_0", (0.0, 0.0, 0.0, 0.0))),
        ("D1", g.vec4("SectionDamage_1", (0.0, 0.0, 0.0, 0.0))),
    ]

    albedo = g.custom("Hull/BaseColor", HULL_BASE_CODE, F3, hull + [
        ("Base", (base, "RGB")), ("Tint", tint), ("_Tint", tint_), ("Mask", (mask, "R")),
        ("PaintColor", g.vec4("PaintColor", (0.55, 0.08, 0.06, 0.0))),
        ("PaintStripeAt", g.scalar("PaintStripeAt", 0.72)), ("PaintStripeWidth", g.scalar("PaintStripeWidth", 0.04))])
    mel.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    metal = g.custom("Hull/Metallic", HULL_METAL_CODE, F1, hull + [
        ("M", (metallic, "R")), ("MetallicScale", g.scalar("MetallicScale", 0.7))])
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    roughness = g.custom("Hull/Roughness", HULL_ROUGH_CODE, F1, hull + [
        ("R", (rough, "R")), ("InvertRoughness", g.scalar("InvertRoughness", 0.0)),
        ("RoughnessScale", g.scalar("RoughnessScale", 1.0))])
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    tangent = g.custom("Hull/Normal", HULL_NORMAL_CODE, F3, [
        ("N", (normal, "RGB")), ("FlipGreen", flip), ("NormalStrength", strength)], include=False)
    mel.connect_material_property(tangent, "", unreal.MaterialProperty.MP_NORMAL)
    glow = g.custom("Hull/Emissive", HULL_EMISSIVE_CODE, F3, hull + [
        ("E", (emission, "RGB")), ("EmissiveIntensity", g.scalar("EmissiveIntensity", 3.0)),
        ("_EngineGlow", g.scalar("_EngineGlow", 0.6)), ("_Tint", tint_), ("_Highlight", g.scalar("_Highlight", 0.0)),
        ("N", (normal, "RGB")), ("FlipGreen", flip), ("NormalStrength", strength), ("Albedo", (albedo, "")),
        ("RimColor", g.vec("RimColor", (0.42, 0.56, 0.8))), ("RimStrength", g.scalar("RimStrength", 0.07)),
        ("AmbientFloor", g.scalar("AmbientFloor", 0.012)),
        ("WindowGlow", g.scalar("WindowGlow", 2.5)), ("WindowColor", g.vec("WindowColor", (1.0, 0.82, 0.55))),
        ("WindowDensity", g.scalar("WindowDensity", 0.35)),
        ("EmberColor", g.vec("EmberColor", (1.0, 0.35, 0.08))), ("EmberGlow", g.scalar("EmberGlow", 6.0)),
    ] + g.exposure(), x=-500)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")
    return mat


def build_shield():
    path, mat = _material(SHIELD, GAME_MATERIALS)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", False)
    g = Graph(mat, SHIELD)
    shell = g.custom("Hull/Shield", SHIELD_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, [
        ("ShieldColor", g.vec("ShieldColor", (0.3, 0.65, 1.0))), ("ShieldStrength", g.scalar("ShieldStrength", 0.6)),
        ("ShieldHit", g.scalar("ShieldHit", 0.0)), ("FresnelPower", g.scalar("FresnelPower", 3.0)),
    ] + g.exposure(), x=-500)
    mel.connect_material_property(shell, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")
    return mat


def build_instances(parent, instances):
    """MI_<TAG>_<Atlas>_<Colour>_Hull beside each _Lit instance, with its maps and roughness convention."""
    made = 0
    for tag, lit_path in instances:
        lit = unreal.load_asset(lit_path)
        if lit is None:
            _fail(f"could not load {lit_path}")
            continue
        folder, lit_name = lit_path.rsplit("/", 1)
        name = lit_name[:-len("_Lit")] + "_Hull"
        path = f"{folder}/{name}"
        mi = unreal.load_asset(path) if eal.does_asset_exist(path) else None
        if mi is None:
            mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant,
                                    unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, parent)
        for param in TEXTURE_PARAMS:
            t = mel.get_material_instance_texture_parameter_value(lit, param)
            if t is not None:
                mel.set_material_instance_texture_parameter_value(mi, param, t)
        mel.set_material_instance_scalar_parameter_value(
            mi, "InvertRoughness", mel.get_material_instance_scalar_parameter_value(lit, "InvertRoughness"))
        mel.update_material_instance(mi)
        eal.save_asset(path, only_if_is_dirty=False)
        made += 1
    _log(f"built {made} hull material instance(s)")
    return made


def main():
    instances = lit_instances()
    _log(f"{len(instances)} lit instance(s) found")
    made = 0
    try:
        master = build_master(default_textures(instances))
        made = build_instances(master, instances)
    except Exception as error:
        _fail(f"{MASTER}: {error}")
    try:
        build_shield()
    except Exception as error:
        _fail(f"{SHIELD}: {error}")
    _log(f"SUMMARY instances={made} failures={len(failures)}")
    for f in failures:
        unreal.log_error(f"Hull: FAILURE {f}")


main()
