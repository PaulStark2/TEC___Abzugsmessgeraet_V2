from pathlib import Path

root = Path(__file__).resolve().parent

fields = [
    ("v_approach_testpoints", "f32"),
    ("v_fine_testpoints", "f32"),
    ("v_unclamp", "f32"),
    ("v_homing", "f32"),
    ("a_testpoints", "f32"),
    ("corner_testpoints", "f32"),
    ("a_homing", "f32"),
    ("force_filter", "f32"),
    ("xy_tolerance_N", "f32"),
    ("xy_correction_step", "f32"),
    ("xy_max_correction", "f32"),
    ("insertion_buffer", "f32"),
    ("collision_approach_N", "f32"),
    ("collision_retract_N", "f32"),
    ("collision_clamp_N", "f32"),
    ("collision_unclamp_N", "f32"),
    ("collision_contact_N", "f32"),
    ("collision_measure_N", "f32"),
    ("spreader_extend_time_s", "f32"),
    ("spreader_retract_time_s", "f32"),
    ("force_tolerance_N", "f32"),
    ("v_search", "f32"),
    ("home_offset_x", "f32"),
    ("home_offset_y", "f32"),
    ("home_offset_z", "f32"),
    ("tool_offset_x", "f32"),
    ("tool_offset_y", "f32"),
    ("tool_offset_z", "f32"),
    ("safe_z_buffer", "f32"),
    ("sample_timeout_s", "f32"),
    ("heartbeat_timeout_s", "f32"),
    ("xy_correct_timeout_s", "f32"),
    ("approach_distance", "f32"),
    ("probe_v_approach", "f32"),
    ("probe_wall_force_N", "f32"),
    ("probe_floor_force_N", "f32"),
    ("probe_xy_search_limit", "f32"),
    ("probe_z_search_limit", "f32"),
    ("probe_z_lift_mm", "f32"),
    ("probe_bore_timeout_s", "f32"),
    ("probe_bore_refine_passes", "u8"),
    ("insert_search_distance", "f32"),
    ("phase1_v_extract", "f32"),
    ("phase1_v_insert", "f32"),
    ("phase1_a", "f32"),
    ("phase1_corner", "f32"),
    ("phase3_v_extract", "f32"),
    ("phase3_v_insert", "f32"),
    ("phase3_a", "f32"),
    ("phase3_corner", "f32"),
    ("probe_center_x", "f32"),
    ("probe_center_y", "f32"),
    ("probe_center_z", "f32"),
    ("force_range_N", "f32"),
    ("force_hard_limit_N", "f32"),
    ("a_manual", "f32"),
    ("force_hard_limit_x_N", "f32"),
    ("force_hard_limit_y_N", "f32"),
    ("collision_approach_x_N", "f32"),
    ("collision_approach_y_N", "f32"),
    ("collision_retract_x_N", "f32"),
    ("collision_retract_y_N", "f32"),
    ("collision_clamp_x_N", "f32"),
    ("collision_clamp_y_N", "f32"),
    ("collision_unclamp_x_N", "f32"),
    ("collision_unclamp_y_N", "f32"),
    ("collision_contact_x_N", "f32"),
    ("collision_contact_y_N", "f32"),
    ("collision_measure_x_N", "f32"),
    ("collision_measure_y_N", "f32"),
    ("xy_search_limit", "f32"),
    ("preclamp_confirm", "u8"),
    ("v_bore_search", "f32"),
    ("bore_refine_passes", "u8"),
    ("v_plate_bore_search", "f32"),
    ("plate_wall_force_N", "f32"),
    ("plate_floor_force_N", "f32"),
    ("plate_xy_search_limit", "f32"),
    ("plate_z_search_limit", "f32"),
    ("plate_z_lift_mm", "f32"),
    ("plate_bore_timeout_s", "f32"),
    ("plate_bore_refine_passes", "u8"),
    ("coord_tolerance_mm", "f32"),
    ("test_lateral_force_N", "f32"),
    ("z_limit_pos_mm", "f32"),
    ("z_limit_neg_mm", "f32"),
    ("anomaly_skip", "u8"),
    ("tool_offset_change_tolerance_mm", "f32"),
]

defaults = {
    "force_range_N": "30.0f",
    "force_hard_limit_N": "30.0f",
    "a_manual": "500.0f",
    "force_hard_limit_x_N": "30.0f",
    "force_hard_limit_y_N": "30.0f",
    "collision_approach_x_N": "20.0f",
    "collision_approach_y_N": "20.0f",
    "collision_retract_x_N": "30.0f",
    "collision_retract_y_N": "30.0f",
    "collision_clamp_x_N": "20.0f",
    "collision_clamp_y_N": "20.0f",
    "collision_unclamp_x_N": "20.0f",
    "collision_unclamp_y_N": "20.0f",
    "collision_contact_x_N": "30.0f",
    "collision_contact_y_N": "30.0f",
    "collision_measure_x_N": "30.0f",
    "collision_measure_y_N": "30.0f",
    "xy_search_limit": "5.0f",
    "preclamp_confirm": "1",
    "v_bore_search": "1.0f",
    "bore_refine_passes": "0",
    "v_plate_bore_search": "1.0f",
    "plate_wall_force_N": "0.5f",
    "plate_floor_force_N": "0.5f",
    "plate_xy_search_limit": "5.0f",
    "plate_z_search_limit": "10.0f",
    "plate_z_lift_mm": "2.0f",
    "plate_bore_timeout_s": "60.0f",
    "plate_bore_refine_passes": "0",
    "coord_tolerance_mm": "0.5f",
    "test_lateral_force_N": "5.0f",
    "z_limit_pos_mm": "10.0f",
    "z_limit_neg_mm": "15.0f",
    "anomaly_skip": "0",
    "tool_offset_change_tolerance_mm": "0.5f",
    "probe_v_approach": "10.0f",
    "probe_wall_force_N": "0.5f",
    "probe_floor_force_N": "0.5f",
    "probe_xy_search_limit": "5.0f",
    "probe_z_search_limit": "10.0f",
    "probe_z_lift_mm": "2.0f",
    "probe_bore_timeout_s": "60.0f",
    "probe_bore_refine_passes": "0",
}

nbytes = sum(4 if t == "f32" else 2 if t == "u16" else 1 for _, t in fields)
print("WIRE", nbytes)

struct_lines = []
for name, t in fields:
    ctype = "float" if t == "f32" else "uint16_t" if t == "u16" else "uint8_t"
    if name in defaults:
        struct_lines.append(f"    {ctype} {name} = {defaults[name]};")
    else:
        struct_lines.append(f"    {ctype} {name};")

enc = []
for name, t in fields:
    if t == "f32":
        enc.append(f"        writeF32LE(out, capacity, offset, payload.{name}) &&")
    elif t == "u16":
        enc.append(f"        writeU16LE(out, capacity, offset, payload.{name}) &&")
    else:
        enc.append(f"        writeU8(out, capacity, offset, payload.{name}) &&")
enc[-1] = enc[-1].replace(" &&", "")

dec = []
for name, t in fields:
    if t == "f32":
        dec.append(f"        readF32LE(data, length, offset, out.{name}, true) &&")
    elif t == "u16":
        dec.append(f"        readU16LE(data, length, offset, out.{name}) &&")
    else:
        dec.append(f"        readU8(data, length, offset, out.{name}) &&")
dec[-1] = dec[-1].replace(" &&", "")

js_defaults = {
    "force_range_N": 30,
    "force_hard_limit_N": 30,
    "a_manual": 500,
    "force_hard_limit_x_N": 30,
    "force_hard_limit_y_N": 30,
    "collision_approach_x_N": 20,
    "collision_approach_y_N": 20,
    "collision_retract_x_N": 30,
    "collision_retract_y_N": 30,
    "collision_clamp_x_N": 20,
    "collision_clamp_y_N": 20,
    "collision_unclamp_x_N": 20,
    "collision_unclamp_y_N": 20,
    "collision_contact_x_N": 30,
    "collision_contact_y_N": 30,
    "collision_measure_x_N": 30,
    "collision_measure_y_N": 30,
    "xy_search_limit": 5,
    "preclamp_confirm": 1,
    "v_bore_search": 1,
    "bore_refine_passes": 0,
    "v_plate_bore_search": 1,
    "plate_wall_force_N": 0.5,
    "plate_floor_force_N": 0.5,
    "plate_xy_search_limit": 5,
    "plate_z_search_limit": 10,
    "plate_z_lift_mm": 2,
    "plate_bore_timeout_s": 60,
    "plate_bore_refine_passes": 0,
    "coord_tolerance_mm": 0.5,
    "test_lateral_force_N": 5,
    "z_limit_pos_mm": 10,
    "z_limit_neg_mm": 15,
    "anomaly_skip": 0,
    "tool_offset_change_tolerance_mm": 0.5,
    "probe_v_approach": 10,
    "probe_wall_force_N": 0.5,
    "probe_floor_force_N": 0.5,
    "probe_xy_search_limit": 5,
    "probe_z_search_limit": 10,
    "probe_z_lift_mm": 2,
    "probe_bore_timeout_s": 60,
    "probe_bore_refine_passes": 0,
}
opt_zero = {"probe_center_x", "probe_center_y", "probe_center_z"}
js = []
for name, t in fields:
    extra = ""
    if name in opt_zero:
        extra = ", optionalZero: true"
    elif name in js_defaults:
        extra = f", defaultValue: {js_defaults[name]}"
    js.append(f"  {{ key: '{name}', type: '{t}'{extra} }},")

(root / "_wire_gen.txt").write_text(
    f"WIRE={nbytes}\n\nSTRUCT:\n"
    + "\n".join(struct_lines)
    + "\n\nENC:\n"
    + "\n".join(enc)
    + "\n\nDEC:\n"
    + "\n".join(dec)
    + "\n\nJS:\n"
    + "\n".join(js)
    + "\n",
    encoding="utf-8",
)
print("wrote _wire_gen.txt")
