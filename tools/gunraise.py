#!/usr/bin/env python3
"""gun-raise math: turn the -HIProbe bone dump into exact rotation data.

Richard 2026-10-15: "Move the gun up a little bit, about 6 inches."

Two candidate mechanisms, both computed here from MEASURED bone transforms
(no guessing Mixamo axis conventions):

  A. GUN-ONLY OFFSET: the bone-local vector v with R_hand * v = (0, 0, +15.24cm),
     feedable to -HIGunLoc=x,y,z. Risk: the hands stay at the waist while the
     gun floats -- the frames decide whether that reads broken.

  B. ARM RAISE (import-time rewrite of the shoulder tracks): rotate each Arm
     bone about the character's lateral axis through its OWN joint, so the
     shoulder stays attached and the wrist (and the gun parented to it) rises.
     Output: the constant LOCAL-space pre-multiply delta for each side,
     delta = P^-1 * R_c * P, P = the clavicle's world rotation at frame 0.

Usage: gunraise.py <logfile>   (reads "HI: probe bone ..." lines; metres/quats)
Units in, units out: positions metres, offsets UE units (cm), angles degrees.
"""
import math
import re
import sys

RAISE_CM = 15.24  # 6 inches, in UE units (cm)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def qconj(a):
    return (-a[0], -a[1], -a[2], a[3])


def qrot(q, v):
    """Rotate vector v by unit quaternion q (x,y,z,w)."""
    qv = (v[0], v[1], v[2], 0.0)
    r = qmul(qmul(q, qv), qconj(q))
    return (r[0], r[1], r[2])


def qaxis_angle(axis, deg):
    """Unit quaternion for a rotation of `deg` degrees about `axis` (xyz)."""
    n = math.sqrt(sum(c * c for c in axis))
    ax = tuple(c / n for c in axis)
    half = math.radians(deg) / 2.0
    s = math.sin(half)
    return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(half))


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vnorm(a):
    return math.sqrt(vdot(a, a))


def main():
    path = sys.argv[1]
    bones = {}
    pat = re.compile(
        r"HI: probe bone (\w+) pos=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)m "
        r"quat=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)"
    )
    gunlocs = []
    gpat = re.compile(
        r"gunLoc=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)m")
    with open(path, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m and m.group(1) not in bones:
                bones[m.group(1)] = (
                    tuple(float(m.group(i)) for i in (2, 3, 4)),
                    tuple(float(m.group(i)) for i in (5, 6, 7, 8)),
                )
            g = gpat.search(line)
            if g:
                gunlocs.append(tuple(float(g.group(i)) for i in (1, 2, 3)))

    # The FIRST probe firing is the frame-1 bChanged path, which runs BEFORE the
    # anim system has evaluated a single pose -- its gunLoc is the reference
    # T-pose (hands out at the sides, Z 1.39 m against the in-pose 1.09 m).
    # Drop it and average the mid-loop samples that remain.
    if len(gunlocs) >= 2:
        gunlocs = gunlocs[1:]
    gunloc = tuple(sum(p[i] for p in gunlocs) / len(gunlocs) for i in range(3))

    missing = [b for b in ("RightHand", "LeftHand", "RightArm", "LeftArm",
                           "RightShoulder", "LeftShoulder") if b not in bones]
    if missing:
        print(f"MISSING BONES: {missing}")
        sys.exit(1)

    if gunloc:
        print(f"baseline gunLoc (world, m): ({gunloc[0]:.3f}, {gunloc[1]:.3f}, {gunloc[2]:.3f})")
        print(f"target   gunLoc Z after raise: {gunloc[2] + RAISE_CM / 100.0:.3f} m")
    print()

    # ---- A. gun-only offset: v_local = R_hand^-1 * (0,0,RAISE_CM) ----
    rhand_pos, rhand_q = bones["RightHand"]
    v_local = qrot(qconj(rhand_q), (0.0, 0.0, RAISE_CM))
    back = qrot(rhand_q, v_local)
    print("A. GUN-ONLY OFFSET (-HIGunLoc, bone-local, cm):")
    print(f"   -HIGunLoc={v_local[0]:.3f},{v_local[1]:.3f},{v_local[2]:.3f}")
    print(f"   check: R_hand * v = ({back[0]:.4f}, {back[1]:.4f}, {back[2]:.4f}) cm world (want 0,0,{RAISE_CM})")
    print()

    # ---- B. arm raise: rotate about the lateral axis through each shoulder joint ----
    # lateral = RightShoulder - LeftShoulder (a character's right is clavicle_l -> clavicle_r)
    lat = vsub(bones["RightShoulder"][0], bones["LeftShoulder"][0])
    lat_n = tuple(c / vnorm(lat) for c in lat)
    print(f"B. ARM RAISE (import-time shoulder-track rotation)")
    print(f"   lateral axis (world, unit): ({lat_n[0]:.4f}, {lat_n[1]:.4f}, {lat_n[2]:.4f})")
    for side in ("Right", "Left"):
        sh_pos, sh_q = bones[f"{side}Shoulder"]        # clavicle joint + world rotation (P)
        arm_pos, arm_q = bones[f"{side}Arm"]           # shoulder joint
        hand_pos, _ = bones[f"{side}Hand"]             # wrist
        r = vsub(hand_pos, arm_pos)                    # shoulder-joint -> wrist, metres
        # exact solve: find theta with (R(theta, lat) * r).z - r.z == RAISE (metres).
        # The rise is NOT monotone in signed theta -- which way the wrist moves first
        # depends on sign((lat x r).z), and the first draft assumed it and pinned
        # itself to the +90 deg bound, reporting a 6.5 cm DROP as its best answer.
        # Measure the direction, then bisect on [0, 89] along it.
        target = RAISE_CM / 100.0
        sgn = 1.0 if vcross(lat_n, r)[2] >= 0.0 else -1.0

        def rise_at(deg):
            return qrot(qaxis_angle(lat_n, sgn * deg), r)[2] - r[2]

        if rise_at(89.0) < target:
            print(f"   {side}: NOT REACHABLE: max rise is {rise_at(89.0)*100:+.1f} cm, want {RAISE_CM:+.1f} cm")
        lo, hi = 0.0, 89.0
        theta_mag = 0.0
        for _ in range(60):
            mid = (lo + hi) / 2.0
            if rise_at(mid) < target:
                lo = mid
            else:
                hi = mid
            theta_mag = mid
        theta = sgn * theta_mag
        rot_world = qaxis_angle(lat_n, theta)
        new_r = qrot(rot_world, r)
        # local-space constant pre-multiply: delta = P^-1 * R_c * P  (P = clavicle world rot)
        delta_local = qmul(qmul(qconj(sh_q), rot_world), sh_q)
        # axis-angle form of delta_local, which is what the C++ constant bakes
        w = max(-1.0, min(1.0, delta_local[3]))
        ang = 2.0 * math.acos(w)
        s = math.sin(ang / 2.0)
        if abs(s) < 1e-9:
            axis = (0.0, 0.0, 1.0)
        else:
            axis = (delta_local[0] / s, delta_local[1] / s, delta_local[2] / s)
        print(f"   {side}: r(shoulder->wrist)=({r[0]:.3f},{r[1]:.3f},{r[2]:.3f})m  |r|={vnorm(r):.3f}m")
        print(f"   {side}: world rotation about lateral axis: {theta:+.2f} deg")
        print(f"   {side}: rise check: wrist z {r[2]:.3f} -> {new_r[2]:.3f} m  (delta {new_r[2]-r[2]:+.4f} m, want {target:+.4f})")
        h_before = math.hypot(r[0], r[1])
        h_after = math.hypot(new_r[0], new_r[1])
        print(f"   {side}: horizontal reach {h_before:.3f} -> {h_after:.3f} m (wrist pulls {h_before-h_after:+.3f} m toward the body -- watch gunSight for re-occlusion)")
        print(f"   {side}: delta_local quat (x,y,z,w) = ({delta_local[0]:+.5f}, {delta_local[1]:+.5f}, {delta_local[2]:+.5f}, {delta_local[3]:+.5f})")
        print(f"   {side}: delta_local axis = ({axis[0]:+.5f}, {axis[1]:+.5f}, {axis[2]:+.5f}), angle = {math.degrees(ang):+.2f} deg")
    print()
    print("   NOTE: the local delta is exact for frame 0 (P taken from the dump);")
    print("   breathing moves P by a degree or two, so verify the rise over the whole")
    print("   8.3 s loop with the probe's gunLoc, not at one instant.")


if __name__ == "__main__":
    main()
