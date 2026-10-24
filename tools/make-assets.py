"""Generate the .uasset materials this port needs -- from TEXT, headlessly.

Run it through `tools/make-assets.sh`, which drives `UnrealEditor-Cmd` with
`-ExecutePythonScript`. Output lands in `Content/`, which is gitignored exactly like the
engine art: the .uassets are DERIVED STATE and this script is the source of truth.

WHY THIS EXISTS
---------------
The port recorded, in its own README, that a material "is a .uasset, so a port that has no
editor session cannot make one" -- and every visual compromise in the project descends from
that sentence. Windows are opaque pale panels because real glass needs a translucent
material. Every primitive in the world shares `BasicShapeMaterial` because a bespoke material
was unmakeable. Characters carry the engine's own materials because a skin or hair material
was unmakeable.

Measured 2026-10-09: that sentence is false. `PythonScriptPlugin` ships with the engine, and
`UnrealEditor-Cmd -unattended -NullRHI -ExecutePythonScript=...` can create a material,
author its expressions, connect them to its properties and SAVE it to disk with no display
and no human. So the no-Blueprints rule still holds -- nothing here is hand-authored in a GUI
-- but "no editor session" no longer implies "no assets".

Changes here are invisible until the module is rebuilt only if the game references them by
path; a renamed asset is a silent nullptr at runtime, so every generated asset is checked for
existence at the end and the count is printed.
"""

import unreal

# ---- paths. /Game/... is Content/, i.e. Content/HIGenerated/...
PKG = "/Game/HIGenerated"


def _fresh(name):
	"""Delete any previous version, so a re-run is deterministic rather than additive."""
	full = "%s/%s" % (PKG, name)
	if unreal.EditorAssetLibrary.does_asset_exist(full):
		unreal.EditorAssetLibrary.delete_asset(full)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	return tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew()), full


def _param(mat, kind, name, value, x, y):
	expr = unreal.MaterialEditingLibrary.create_material_expression(mat, kind, x, y)
	expr.set_editor_property("parameter_name", name)
	expr.set_editor_property("default_value", value)
	return expr


def _set_usage_flags(mat, wants=("INSTANCED", "SKELETAL")):
	"""Declare which mesh kinds will use this material, or the engine will refuse it.

	MEASURED, and it cost a capture: a generated material that does not carry the usage flag
	for the component it is put on is SILENTLY REPLACED at runtime by the engine's Default
	Material -- which renders as a grey-and-dark CHECKERBOARD. The log line is a Warning, not
	an Error:

	    LogMaterial: Warning: Material /Game/HIGenerated/M_HIGlass missing usage flag
	    InstancedStaticMeshes! Default Material will be used in game.

	In the editor those flags are set automatically when the material is assigned; a headless
	generator gets no such pass, so it has to ask. The enum member names are discovered rather
	than hard-coded, because getting one wrong is a silent no-op and this file's job is to not
	have those.
	"""
	usage = getattr(unreal, "MaterialUsage", None)
	if usage is None:
		print("HI_ASSET: WARNING -- unreal.MaterialUsage not exposed; usage flags unset")
		return
	lib = unreal.MaterialEditingLibrary
	for member in dir(usage):
		upper = member.upper()
		if member.startswith("_") or not any(w in upper for w in wants):
			continue
		try:
			lib.set_material_usage(mat, getattr(usage, member))
			print("HI_ASSET:   usage flag set: %s" % member)
		except Exception as err:
			print("HI_ASSET:   usage flag %s refused: %s" % (member, err))


def make_glass():
	"""Translucent glazing. Fix-list item 5's last half.

	The windows have had frames, sills, mullions and curtains since the port began, and the
	pane itself has been an OPAQUE pale panel -- acknowledged in the README as the one part of
	item 5 that a .uasset was needed for.

	UNLIT, and that is a measured decision rather than a shortcut. The first version was a lit
	translucent material, and a captured frame showed the pane as a DITHERED CHECKERBOARD:
	Lumen's translucent lighting resolves over several frames of temporal accumulation, and a
	single offscreen frame does not have them. It looked worse than the opaque panel it
	replaced. A window pane does not need to be lit -- it needs to be see-through and slightly
	tinted -- so this is Unlit + Translucent, which is a plain alpha blend with nothing to
	accumulate. Worth re-testing as a lit material if the game is ever captured as a running
	sequence rather than one frame at a time.
	"""
	mat, full = _fresh("M_HIGlass")
	if mat is None:
		print("HI_ASSET: FAILED to create M_HIGlass")
		return None

	colour = _param(mat, unreal.MaterialExpressionVectorParameter, "GlassColor",
		unreal.LinearColor(0.55, 0.66, 0.72, 1.0), -600, 0)
	opacity = _param(mat, unreal.MaterialExpressionScalarParameter, "Opacity",
		0.30, -600, 200)

	lib = unreal.MaterialEditingLibrary
	# UNLIT reads its colour from EMISSIVE, not from Base Color -- connecting the wrong one is
	# a silently black pane, which is the failure mode this whole file exists to avoid.
	lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
	lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

	# Two-sided matters: a window is seen from both the lawn and the kitchen, and a one-sided
	# plane is invisible from one of them.
	mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
	mat.set_editor_property("two_sided", True)
	mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

	# Without this the pane renders as the engine's checkerboard, not as glass.
	_set_usage_flags(mat)

	lib.recompile_material(mat)
	unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False)
	print("HI_ASSET: glass -> %s" % full)
	return full


def make_textured():
	"""A surface material that samples an IMPORTED texture, tiled.

	Nine textures from the Godot repository are imported to /Game/HIImported/textures. Until
	this material existed there was nothing in the project that could sample one: every surface
	was `BasicShapeMaterial` with a flat colour, because a .uasset could not be made. Both
	halves of that are gone now.

	Tiling is a PARAMETER rather than baked UVs, because the same material has to cover a 16 m
	wall, an 11 m floor and a 5 km lawn -- and the engine's cube has 0..1 UVs on every face, so
	without a tiling multiplier a brick texture would be stretched once across the whole house.
	"""
	mat, full = _fresh("M_HITextured")
	if mat is None:
		print("HI_ASSET: FAILED to create M_HITextured")
		return None

	lib = unreal.MaterialEditingLibrary
	try:
		tex = lib.create_material_expression(
			mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
		tex.set_editor_property("parameter_name", "BaseTexture")
		# A visible default, so a forgotten parameter is obvious rather than black.
		tex.set_editor_property("texture", unreal.load_asset(
			"/Game/HIImported/textures/ceiling_plaster.ceiling_plaster"))

		uv = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
		tiling = _param(mat, unreal.MaterialExpressionScalarParameter, "UVTiling", 8.0, -1100, 180)
		mul = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -850, 0)

		lib.connect_material_expressions(uv, "", mul, "A")
		lib.connect_material_expressions(tiling, "", mul, "B")
		lib.connect_material_expressions(mul, "", tex, "Coordinates")

		# ---- the colour, and this is a FIX rather than a feature (2026-10-23).
		#
		# Until this graph existed the image went straight into base colour with no colour
		# input at all, so every `FHIMaterials::Tint()` on a textured component was discarded
		# the moment the texture replaced it. Measured in a frame: the 5 km lawn rendered the
		# source scan's own dry tan while `Grass` said deep green, and every interior wall
		# rendered the plaster scan's olive blotches while `Wall` said warm off-white -- which
		# is why the rooms read as sandstone and the yard as desert. The tint is a MULTIPLY,
		# so a white default is a no-op and an existing caller that passes nothing is unchanged.
		#
		# `TextureStrength` blends the image against WHITE before the tint: 1.0 is the
		# photograph (a lawn), 0.3 is a grain over paint (a drywall wall). Lerping toward white
		# rather than toward the tint keeps the two parameters doing one job each.
		white = lib.create_material_expression(
			mat, unreal.MaterialExpressionConstant3Vector, -620, 260)
		white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
		strength = _param(
			mat, unreal.MaterialExpressionScalarParameter, "TextureStrength", 1.0, -1100, 340)
		blend = lib.create_material_expression(
			mat, unreal.MaterialExpressionLinearInterpolate, -380, 120)
		lib.connect_material_expressions(white, "", blend, "A")
		lib.connect_material_expressions(tex, "RGB", blend, "B")
		lib.connect_material_expressions(strength, "", blend, "Alpha")

		tint = _param(mat, unreal.MaterialExpressionVectorParameter, "Tint",
			unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -380, -160)
		tinted = lib.create_material_expression(
			mat, unreal.MaterialExpressionMultiply, -160, 0)
		lib.connect_material_expressions(blend, "", tinted, "A")
		lib.connect_material_expressions(tint, "", tinted, "B")
		lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

		# Matte. A wall and a lawn are the two least shiny things in the game, and the default
		# roughness of a fresh material is 0.5, which makes plaster look like plastic under
		# the sun. Connected as a constant so it is visible in the material, not just set.
		rough = _param(mat, unreal.MaterialExpressionScalarParameter, "Roughness", 0.9, -600, 320)
		lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
	except Exception as err:
		print("HI_ASSET: textured material FAILED while authoring: %s" % err)
		return None

	_set_usage_flags(mat)
	lib.recompile_material(mat)
	unreal.EditorAssetLibrary.save_asset(full, only_if_is_dirty=False)
	print("HI_ASSET: textured -> %s" % full)
	return full


def main():
	print("HI_ASSET: generating materials from text")
	glass = make_glass()
	textured = make_textured()

	# ---- verify rather than assume: a generated asset that did not save is a nullptr in the
	# game and a silently grey pane on screen, which looks like the change did nothing.
	for path in [glass, textured]:
		if path is None:
			continue
		ok = unreal.EditorAssetLibrary.does_asset_exist(path)
		print("HI_ASSET: %s exists=%s" % (path, ok))
	print("HI_ASSET: done")


main()
