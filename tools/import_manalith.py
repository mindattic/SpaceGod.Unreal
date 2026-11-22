"""Headless import of the Manalith into /Game/SpaceGod/Manalith (US-T458, spec docs/MANALITH_VISUAL_SPEC.md).

Run through tools/import-manalith.ps1, which first prepares the model with Blender (tools/manalith_prepare.py) and then runs
    UnrealEditor-Cmd Cpp/SpaceGod.uproject -run=pythonscript -script=<abs>/tools/import_manalith.py
with MANALITH_PIECES set to the prepared folder. Re-running is safe: everything is re-imported or rebuilt in place.

What it builds (the owner's own model, from Meshy, so it is committed):
  manalith-satellite-00                  the mesh, centred, metres -> cm (60k faces)
  manalith-base-00, -metallic-00, -roughness-00, -normal-00    its PBR maps (2048)
  M_ManalithLit      Default Lit PBR from the maps as they are (normal strength 1; the Meshy normal is OpenGL-style, so its
                  green is flipped for Unreal) and the spec's one emission: a Fresnel rim in mana blue (exponent 3,
                  intensity 1.5) plus the faint mana-blue rim light (0.4), exposed as the game's other emissives are.
  M_ManalithShard    Masked Default Lit for the falling shards: BaseColor, a Glow colour x GlowIntensity, and Fade (0..1)
                  dithered out, all set per shard from code.
"""

import os

import unreal

ROOT = "/Game/SpaceGod/Manalith"
MPC = "/SpaceGodRender/Materials/MPC_SpaceGod"
# The shared spec's <MANA_BLUE> #7590C8, as linear colour.
MANA = (0.178, 0.279, 0.578)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def _log(msg):
    unreal.log(f"MANALITH: {msg}")


def import_file(path, name, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", ROOT)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    asset = unreal.load_asset(f"{ROOT}/{name}")
    if asset is None:
        raise RuntimeError(f"import of {path} made no {ROOT}/{name}")
    return asset


def import_textures(pieces):
    out = {}
    for key, srgb, normal in (("base", True, False), ("metallic", False, False), ("roughness", False, False), ("normal", False, True)):
        name = f"manalith-{key}-00"
        t = import_file(os.path.join(pieces, name + ".png"), name)
        t.set_editor_property("srgb", srgb)
        if normal:
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif not srgb:
            t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        eal.save_asset(f"{ROOT}/{name}", only_if_is_dirty=False)
        out[key] = t
    return out


def import_mesh(pieces):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    sm = ui.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("import_uniform_scale", 1.0)
    sm.set_editor_property("convert_scene", True)
    sm.set_editor_property("convert_scene_unit", True)
    mesh = import_file(os.path.join(pieces, "manalith-satellite-00.fbx"), "manalith-satellite-00", ui)
    box = mesh.get_bounding_box()
    _log(f"mesh box min {box.min} max {box.max}")
    return mesh


EMISSIVE_CODE = r"""
float2 nxy = N.xy;
nxy.y *= FlipGreen;
float3 tn = normalize(float3(nxy * NormalStrength, sqrt(saturate(1.0 - dot(N.xy, N.xy)))));
float3 n = mul(tn, (float3x3)Parameters.TangentToWorld);
if (!(dot(n, n) > 1e-8) || any(isnan(n))) n = Parameters.TangentToWorld[2];
n = normalize(n);
float facing = 1.0 - saturate(dot(n, Parameters.CameraVector));
float3 col = RimColor * (pow(facing, RimExponent) * RimIntensity);
col += RimColor * (pow(facing, 4.0) * RimLight * saturate(Albedo.b * 2.0 + 0.25));
col += Albedo * AmbientFloor;
col = any(isnan(col)) ? float3(0, 0, 0) : col;
return SG_Expose3(col, Exposure, SceneLuminance);
"""

NORMAL_CODE = r"""
float2 nxy = N.xy;
nxy.y *= FlipGreen;
nxy *= NormalStrength;
float3 tn = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
return any(isnan(tn)) ? float3(0, 0, 1) : normalize(tn);
"""

DITHER_CODE = r"""
float ign = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))));
return saturate(Fade) - ign + (1.0 / 3.0);
"""

SHARD_EMISSIVE_CODE = r"""
float3 col = Glow * GlowIntensity * Fade + Albedo * AmbientFloor;
return SG_Expose3(col, Exposure, SceneLuminance);
"""


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0
        self.mpc = unreal.load_asset(MPC)
        if self.mpc is None:
            raise RuntimeError(f"missing {MPC} (the SpaceGodRender plugin)")

    def node(self, cls, x=-1600):
        e = mel.create_material_expression(self.mat, cls, x, self.y)
        self.y += 140
        return e

    def sample(self, name, texture, sampler):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("texture", texture)
        e.set_editor_property("sampler_type", sampler)
        e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        return e

    def scalar(self, name, value):
        e = self.node(unreal.MaterialExpressionScalarParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", value)
        return e, ""

    def vec(self, name, rgb):
        e = self.node(unreal.MaterialExpressionVectorParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        mask = self.node(unreal.MaterialExpressionComponentMask, -1300)
        for c in ("r", "g", "b"):
            mask.set_editor_property(c, True)
        mask.set_editor_property("a", False)
        mel.connect_material_expressions(e, "", mask, "")
        return mask, ""

    def exposure(self):
        ex = self.node(unreal.MaterialExpressionEyeAdaptation)
        lum = self.node(unreal.MaterialExpressionCollectionParameter)
        lum.set_editor_property("collection", self.mpc)
        lum.set_editor_property("parameter_name", "_UnitySpaceSceneLuminance")
        return (ex, ""), (lum, "")

    def custom(self, desc, code, out_type, inputs, include=False):
        c = mel.create_material_expression(self.mat, unreal.MaterialExpressionCustom, -700, self.y)
        self.y += 140
        c.set_editor_property("description", desc)
        c.set_editor_property("code", code)
        c.set_editor_property("output_type", out_type)
        if include:
            c.set_editor_property("include_file_paths", ["/Plugin/SpaceGodRender/SpaceGodCommon.ush"])
        cis = []
        for n, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            cis.append(ci)
        c.set_editor_property("inputs", cis)
        for n, (source, out) in inputs:
            if not mel.connect_material_expressions(source, out, c, n):
                raise RuntimeError(f"{desc}: could not connect input {n}")
        return c


def fresh_material(name):
    path = f"{ROOT}/{name}"
    mat = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if mat is None:
        mat = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("two_sided", False)
    return mat, path


def build_lit(textures):
    mat, path = fresh_material("M_ManalithLit")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    g = Graph(mat)
    S = unreal.MaterialSamplerType
    F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3
    base = g.sample("BaseColor", textures["base"], S.SAMPLERTYPE_COLOR)
    metallic = g.sample("Metallic", textures["metallic"], S.SAMPLERTYPE_MASKS)
    rough = g.sample("Roughness", textures["roughness"], S.SAMPLERTYPE_MASKS)
    normal = g.sample("Normal", textures["normal"], S.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(metallic, "R", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    flip, strength = g.scalar("FlipGreen", -1.0), g.scalar("NormalStrength", 1.0)
    tangent = g.custom("Manalith Normal", NORMAL_CODE, F3, [("N", (normal, "RGB")), ("FlipGreen", flip), ("NormalStrength", strength)])
    mel.connect_material_property(tangent, "", unreal.MaterialProperty.MP_NORMAL)
    exposure, luminance = g.exposure()
    glow = g.custom("Manalith Rim", EMISSIVE_CODE, F3, [
        ("N", (normal, "RGB")), ("FlipGreen", flip), ("NormalStrength", strength), ("Albedo", (base, "RGB")),
        ("RimColor", g.vec("RimColor", MANA)), ("RimExponent", g.scalar("RimExponent", 3.0)),
        ("RimIntensity", g.scalar("RimIntensity", 1.5)), ("RimLight", g.scalar("RimLight", 0.4)),
        ("AmbientFloor", g.scalar("AmbientFloor", 0.012)), ("Exposure", exposure), ("SceneLuminance", luminance)], include=True)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")


def build_shard():
    mat, path = fresh_material("M_ManalithShard")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    g = Graph(mat)
    F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3
    base = g.vec("BaseColor", (0.034, 0.027, 0.055))
    mel.connect_material_property(base[0], "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = g.scalar("Roughness", 0.85)
    mel.connect_material_property(rough[0], "", unreal.MaterialProperty.MP_ROUGHNESS)
    fade = g.scalar("Fade", 1.0)
    # Screen-door fade (interleaved gradient noise against the clip value, 1/3): DitherTemporalAA is a material
    # function, not an expression the Python API can make.
    dither = g.custom("Shard Fade", DITHER_CODE, F1, [("Fade", fade)])
    mel.connect_material_property(dither, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    exposure, luminance = g.exposure()
    glow = g.custom("Shard Glow", SHARD_EMISSIVE_CODE, F3, [
        ("Glow", g.vec("Glow", MANA)), ("GlowIntensity", g.scalar("GlowIntensity", 0.0)), ("Fade", fade),
        ("Albedo", base), ("AmbientFloor", g.scalar("AmbientFloor", 0.012)), ("Exposure", exposure), ("SceneLuminance", luminance)],
        include=True)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")


def main():
    pieces = os.environ.get("MANALITH_PIECES")
    if not pieces or not os.path.isdir(pieces):
        raise RuntimeError(f"MANALITH_PIECES ({pieces}) must be the prepared folder (run tools/import-manalith.ps1)")
    textures = import_textures(pieces)
    import_mesh(pieces)
    build_lit(textures)
    build_shard()
    eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    _log("SUMMARY done")


main()
