#!/usr/bin/env python3
"""Offline diagnostic, not robot safety validation. No third-party modules."""
import argparse
import csv
import json
import math
import statistics


def wrap(d):
    return (d + 180) % 360 - 180


def angle_stats(rows, field, circular):
    values = [float(r[field]) for r in rows]
    deltas = [(wrap(b-a) if circular else b-a) for a, b in zip(values, values[1:])]
    index = max(range(len(deltas)), key=lambda i: abs(deltas[i])) if deltas else None
    return dict(min=min(values), max=max(values),
                max_delta=max(map(abs, deltas), default=0),
                max_delta_at_frame=int(rows[index+1]["frame_id"]) if index is not None else None,
                rms_delta=math.sqrt(sum(d*d for d in deltas)/max(1,len(deltas))),
                wrap_frames=[int(rows[i+1]["frame_id"]) for i,(a,b) in
                             enumerate(zip(values,values[1:])) if circular and abs(b-a)>180])


def depth_sensitivity(rows):
    """Perturb camera-depth difference by +/-2% forearm length, holding XY.
    Evaluates geometry only, not a measured noise/error distribution."""
    maxima = [0.0, 0.0]
    for r in rows:
        if not int(r["elbow_roll_observable"]):
            continue
        f = [float(r["wrist"+s])-float(r["elbow"+s]) for s in ("_x3d","_y3d","_z")]
        axes = [[float(r[f"body_{a}_{b}"]) for b in "xyz"] for a in "xyz"]
        def angles(v):
            x,y,z = [sum(a*b for a,b in zip(v,axis)) for axis in axes]
            return math.degrees(math.atan2(x,z)), math.degrees(math.atan2(y,math.hypot(x,z)))
        baseline=angles(f)
        length=math.sqrt(sum(a*a for a in f))
        for sign in (-1,1):
            perturbed=angles([f[0],f[1],f[2]+sign*0.02*length])
            for i in range(2):
                maxima[i]=max(maxima[i],abs(wrap(perturbed[i]-baseline[i])))
    return dict(camera_dz_perturbation="+/-2% forearm length (synthetic, not measured)",
                max_elbow_roll_change=maxima[0],max_elbow_pitch_change=maxima[1])


def transition_diagnostics(rows, field, count=3):
    """Measure frame/forearm changes at largest output steps; not a claim
    that estimated depth changes are necessarily reconstruction errors."""
    def dot(a,b):
        return sum(x*y for x,y in zip(a,b))
    def unit(v):
        length=math.sqrt(dot(v,v))
        return [x/length for x in v]
    def axes(r):
        return [[float(r[f"body_{a}_{b}"]) for b in "xyz"] for a in "xyz"]
    def forearm(r):
        return [float(r["wrist"+s])-float(r["elbow"+s]) for s in ("_x3d","_y3d","_z")]
    def azimuth(f,basis):
        return math.degrees(math.atan2(dot(f,basis[0]),dot(f,basis[2])))
    def angle(cosine):
        return math.degrees(math.acos(max(-1,min(1,cosine))))
    pairs=sorted(zip(rows,rows[1:]),key=lambda pair:abs(wrap(float(pair[1][field])-float(pair[0][field]))),reverse=True)
    results=[]
    for a,b in pairs[:count]:
        aa,bb=axes(a),axes(b)
        fa,fb=forearm(a),forearm(b)
        ua,ub=unit(fa),unit(fb)
        results.append(dict(from_frame=int(a["frame_id"]),to_frame=int(b["frame_id"]),
                            output_delta=wrap(float(b[field])-float(a[field])),
                            body_rotation_deg=angle((sum(dot(x,y) for x,y in zip(aa,bb))-1)/2),
                            forearm_camera_rotation_deg=angle(dot(ua,ub)),
                            horizontal_before=math.hypot(dot(ua,aa[0]),dot(ua,aa[2])),
                            horizontal_after=math.hypot(dot(ub,bb[0]),dot(ub,bb[2])),
                            observable_before=int(a["elbow_roll_observable"]),
                            observable_after=int(b["elbow_roll_observable"]),
                            azimuth_change_fixed_previous_body=wrap(azimuth(fb,aa)-azimuth(fa,aa)),
                            azimuth_change_due_to_body_basis=wrap(azimuth(fb,bb)-azimuth(fb,aa)),
                            camera_dz_before=fa[2],camera_dz_after=fb[2],
                            hand_fresh_before=int(a["hand_fresh"]),hand_fresh_after=int(b["hand_fresh"])))
    return results


def analyze(path, expected=None):
    with open(path, newline="", encoding="utf-8") as f:
        rows=list(csv.DictReader(f))
    if expected is not None and len(rows)!=expected:
        raise ValueError(f"Expected {expected} frames; got {len(rows)}")
    if not rows or any(not math.isfinite(float(v)) for r in rows for v in r.values()):
        raise ValueError("Empty CSV or non-finite/malformed numeric data")
    valid=[r for r in rows if int(r["target_valid"])]
    fresh=[r for r in valid if int(r["update_ret"])==1]
    if not fresh:
        raise ValueError("No fresh valid targets")
    for r in fresh:
        for field in ("elbow_roll_deg", "wrist_pitch_deg", "wrist_roll_deg"):
            if not -180 <= float(r[field]) < 180:
                raise ValueError(f"Out of range {field}: {r[field]}")
        if not -90 <= float(r["elbow_pitch_deg"]) <= 90 or float(r["gripper_norm"]) not in (0,1):
            raise ValueError("Pitch/gripper contract violation")
        if r["target_frame_id"] != r["frame_id"]:
            raise ValueError("Fresh target frame_id mismatch")
    fields=("elbow_roll_deg","elbow_pitch_deg","wrist_pitch_deg","wrist_roll_deg")
    stats={k:angle_stats(fresh,k,k!="elbow_pitch_deg") for k in fields}
    raw={k:angle_stats(fresh,k,k=="raw_elbow_roll_deg") for k in ("raw_elbow_roll_deg","raw_elbow_pitch_deg")}
    result=dict(frames=len(rows), fresh=len(fresh), invalid=len(rows)-len(valid),
                hold=sum(int(r["update_ret"])==0 for r in rows),
                elbow_roll_unobservable=sum(not int(r["elbow_roll_observable"]) for r in fresh),
                singular_frames=[int(r["frame_id"]) for r in fresh if not int(r["elbow_roll_observable"])],
                hand_held=sum(not int(r["hand_fresh"]) for r in fresh),
                angles=stats, before_angle_ema=raw,
                gripper_transitions=sum(a["gripper_norm"]!=b["gripper_norm"] for a,b in zip(valid,valid[1:])),
                depth_sensitivity=depth_sensitivity(fresh),
                largest_elbow_roll_steps=transition_diagnostics(fresh,"elbow_roll_deg"),
                largest_wrist_roll_steps=transition_diagnostics(fresh,"wrist_roll_deg",1))
    # Image-side-view proxy only: small shoulder Camera-X span / 3D span.
    # Occlusion and monocular reconstruction prevent treating it as true torso yaw.
    side=[]
    for r in fresh:
        d=[float(r["shoulder_r"+s])-float(r["shoulder_l"+s]) for s in ("_x3d","_y3d","_z")]
        if abs(d[0])/max(1e-9,math.sqrt(sum(v*v for v in d))) < 0.35:
            side.append(r)
    result["side_view_proxy"] = dict(criterion="abs(shoulder Camera-X span) / 3D span < 0.35; NOT measured torso yaw",
                                     frames=len(side), depth_sensitivity=depth_sensitivity(side))
    dt=[float(b["time_sec"])-float(a["time_sec"]) for a,b in zip(rows,rows[1:])]
    if dt:
        result["timestamp_dt_sec"]=dict(min=min(dt),median=statistics.median(dt),max=max(dt))
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_csv")
    parser.add_argument("--expect-frames",type=int)
    args=parser.parse_args()
    print(json.dumps(analyze(args.result_csv,args.expect_frames),indent=2))
