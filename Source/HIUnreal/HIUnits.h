#pragma once

/**
 * Unreal's world unit is a CENTIMETRE; the sim is in METRES.
 *
 * Every conversion between the two goes through this one constant, and it is deliberately
 * not a magic 100 at each call site: the failure mode is a world that is internally
 * consistent and therefore looks fine in a test, but is a hundred times too small for the
 * engine's own assumptions -- camera near plane, default character size, gravity, and every
 * engine asset that has a size baked in. A house 16 m across becomes 16 cm, and the first
 * symptom is that the camera cannot see anything because the near plane is 10 cm.
 *
 * HISim stays unit-agnostic: it is a pure model, and the Godot original it was ported from
 * is also in metres. The conversion is the actor layer's job, because the actor layer is
 * the only part that knows what engine it is running in.
 */
constexpr float HI_METERS_TO_UU = 100.0f;
