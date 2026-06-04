/*
  Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 protective tray case

  One-piece protective case for the camera version. Official dimensions are
  read from the rear-view mechanical drawing, then mirrored in X so the STL
  matches the board when the screen is facing the user.

  Kept openings:
  - 2 x USB-C on the side
  - 1 x camera lens on the back
  - 4 x mounting screws

  No GPIO, microphone, button, TF/SD, or extra rear ventilation openings.

  Official dimensions used:
  - Front glass: 114.40 x 66.80 mm
  - Visible LCD:  94.40 x 56.96 mm
  - PCB:          102.50 x 60.00 mm
*/

$fn = 56;
part = "case"; // case, fit_check, preview, reference
nfc_bump_enabled = false; // false: original case, true: NFC module clearance case
nfc_clearance_enabled = nfc_bump_enabled;

// Main board and display dimensions
glass_w = 114.40;
glass_h = 66.80;
lcd_w = 94.40;
lcd_h = 56.96;
pcb_w = 102.50;
pcb_h = 60.00;

// Official drawing offsets, measured from the rectangular glass/backplate.
// Right margin is marked 5.60 mm; left is the remaining width.
glass_to_pcb_left = 6.30;
glass_to_pcb_right = 5.60;
// The board on the real module sits lower in the rectangular glass/backplate:
// there is more blank area above the PCB/camera side than below it.
glass_to_pcb_top = 5.20;
glass_to_pcb_bottom = glass_h - pcb_h - glass_to_pcb_top;

// The Waveshare mechanical drawing/photo is a rear view. Keep this enabled so
// top-edge buttons/TF and side USB holes match a screen-facing fit check.
mirror_official_rear_x = true;

// Overall shell. The front is the screen side; the back is the camera side.
case_margin = 3.20;
outer_w = glass_w + 2 * case_margin;
outer_h = glass_h + 2 * case_margin;
outer_r = 6.00;
wall = 2.10;
back_thickness = 2.20;
inside_depth = 10.20;
total_depth = back_thickness + inside_depth;

// Front rectangular protection rim. Keep it above the slightly proud screen
// surface so the glass does not touch the table first.
front_rim_h = 1.70;
front_rim_w = 2.20;
// The earlier tight fit pressed the screen and caused visible spots.
// Restore the extra front glass clearance so the rim protects without clamping.
glass_clearance = 0.38;
glass_pocket_w = glass_w + 2 * glass_clearance;
glass_pocket_h = glass_h + 2 * glass_clearance;

// The board/display drops in from the front. This cavity is larger than glass
// and goes down to the back plate.
drop_in_w = glass_pocket_w;
drop_in_h = glass_pocket_h;
drop_in_r = 2.40;

// Fixing: four simple measured screw holes through the back cover. The camera
// and USB-C are on the short edges; measure the holes from the actual lower
// shell edges, not from the larger screen-side outer frame.
use_mount_screws = true;
mount_screw_clearance_d = nfc_clearance_enabled ? 3.00 : 2.50;
mount_screw_counterbore_d = 7.20;
mount_screw_counterbore_depth = 1.50;
mount_screw_bevel_d = 9.20;
mount_screw_bevel_depth = 0.85;
mount_long_edge_from_lower_edge = 9.00;
mount_camera_side_from_lower_edge = 11.00;
mount_usb_side_from_lower_edge = 8.00;

// Optional NFC/PN532 exterior bottom and recess. This keeps the printed/tested
// long clearance pocket: 8 mm in from the camera-side screw holes, 25 mm in
// from the USB-C-side screw holes, and full-width across the narrowed bottom.
// The NFC version lowers the whole bottom as one flat printable face, avoiding
// unsupported back-plate bridges around the screw and camera holes.
nfc_bump_depth = 6.00;
nfc_recess_camera_side_from_mount = 8.00;
nfc_recess_usb_side_from_mount = 25.00;
nfc_recess_edge_wall = 2.10;
nfc_recess_floor_thickness = back_thickness;
nfc_bump_overlap = 0.80;
nfc_bump_corner_r = 6.00;
nfc_recess_corner_r = 2.40;
nfc_bottom_slope_inset = 0.30;
nfc_continuous_taper_steps = 36;

// Camera module location measured from the printed back cover.
// The camera is on the short edge, centered between the two long edges.
camera_center_from_camera_side = 11.00;
camera_center_y_offset_from_center = 0.00;
// Mini OV5647/Raspberry-Pi-style camera opening: small visible round hole,
// with a hidden square recess inside for the lens body.
camera_lens_hole_d = 5.00;
camera_lens_bevel_d = 10.20;
camera_lens_bevel_depth = 1.00;
camera_lens_relief_d = 10.80;
camera_pocket_w = 15.00;
camera_pocket_h = 15.00;
camera_pocket_depth = 1.60;
camera_guard_h = 0.00;

// Real-fit feedback: the screen/glass is larger than the PCB, so a full-size
// lower shell hides the recessed USB-C plugs. Keep the screen-side rim large,
// but make the whole lower/back shell smaller with a sloped transition.
lower_shell_usb_inset = 4.50;
lower_shell_opposite_inset = 4.50;
lower_shell_long_edge_inset = 3.00;
lower_shell_taper_h = 11.20;
lower_shell_taper_steps = 18;
lower_shell_profile_roundness = 0.04;
lower_shell_corner_r = 6.00;
lower_shell_bottom_round_h = 0.50;
lower_shell_bottom_edge_inset = 0.30;
pcb_cavity_clearance = 0.60;
lower_shell_min_wall = 1.60;

// Only necessary side openings.
edge_cut_depth = wall + case_margin + 4.0;
// Edge connectors and buttons sit on the rear PCB, not near the glass. Keep the
// openings biased toward the back plate so less material is removed near the
// screen face.
port_z_center = back_thickness + 3.30;
usb_cutout_y1 = 11.50;
usb_cutout_y2 = -12.50;
usb_cutout_w = 15.00;
usb_cutout_h = 8.60;
// USB-C side cutout corrections from first print:
// - leave only a 3 mm bridge between the two ports;
// - fill 4 mm on both outer ends;
// - first print filled 1.5 mm on the screen/front side of both openings;
// - second print feedback needs 2.0 mm more screen-side plug clearance.
usb_center_separator_w = 3.00;
usb_outer_side_fill = 4.00;
usb_screen_side_fill = 1.50;
usb_screen_side_extra_clearance = 2.00;
usb_bottom_side_extra_clearance = nfc_clearance_enabled ? 2.00 : 0.00;
usb_cutout_pos_y_min = usb_center_separator_w / 2;
usb_cutout_pos_y_max = usb_cutout_y1 + usb_cutout_w / 2 - usb_outer_side_fill;
usb_cutout_neg_y_min = usb_cutout_y2 - usb_cutout_w / 2 + usb_outer_side_fill;
usb_cutout_neg_y_max = -usb_center_separator_w / 2;
usb_cutout_z_min = port_z_center - usb_cutout_h / 2 -
                   usb_bottom_side_extra_clearance;
usb_cutout_z_max = port_z_center + usb_cutout_h / 2 -
                   usb_screen_side_fill + usb_screen_side_extra_clearance;

// Official rear-view top-edge positions. The side buttons are intentionally
// sealed in the printable case; these positions are kept only for reference.
tf_slot_x_from_pcb_left = 52.00;
tf_slot_w = 22.50;
tf_slot_h = 5.40;

button_slot_w = 7.20;
button_slot_h = 5.20;
button_pin_hole_d = 1.00;
power_x_from_pcb_left = pcb_w - 23.00;
boot_x_from_pcb_left = power_x_from_pcb_left - 8.35;
reset_x_from_pcb_left = boot_x_from_pcb_left - 8.35;

eps = 0.02;

function rear_pcb_left() = -glass_w / 2 + glass_to_pcb_left;
function case_x_from_rear_x(x) = mirror_official_rear_x ? -x : x;
function rear_from_pcb_left(x) = rear_pcb_left() + x;
function pcb_left() = pcb_center_x() - pcb_w / 2;
function pcb_top() = glass_h / 2 - glass_to_pcb_top;
function pcb_center_x() = from_pcb_left(pcb_w / 2);
function pcb_center_y() = pcb_top() - pcb_h / 2;
function from_pcb_left(x) = case_x_from_rear_x(rear_from_pcb_left(x));
function from_pcb_top(y) = pcb_top() - y;
function camera_x() = lower_shell_x_min() + camera_center_from_camera_side;
function camera_y() = lower_shell_y() + camera_center_y_offset_from_center;
function usb_edge_sign() = mirror_official_rear_x ? 1 : -1;
function clamp(v, lo, hi) = min(max(v, lo), hi);
function lerp(a, b, t) = a + (b - a) * t;
function smoothstep(t) = t * t * (3 - 2 * t);
function taper_profile(t) =
    lerp(t, smoothstep(t), lower_shell_profile_roundness);
function nfc_outer_profile(t) = smoothstep(t);
function lower_shell_inset_pos_x() =
    usb_edge_sign() > 0 ? lower_shell_usb_inset : lower_shell_opposite_inset;
function lower_shell_inset_neg_x() =
    usb_edge_sign() > 0 ? lower_shell_opposite_inset : lower_shell_usb_inset;
function lower_shell_w() =
    outer_w - lower_shell_inset_pos_x() - lower_shell_inset_neg_x();
function lower_shell_h() =
    outer_h - 2 * lower_shell_long_edge_inset;
function pcb_cavity_w() = pcb_w + 2 * pcb_cavity_clearance;
function pcb_cavity_h() = pcb_h + 2 * pcb_cavity_clearance;
function pcb_cavity_x_min() = pcb_center_x() - pcb_cavity_w() / 2;
function pcb_cavity_x_max() = pcb_center_x() + pcb_cavity_w() / 2;
function pcb_cavity_y_min() = pcb_center_y() - pcb_cavity_h() / 2;
function pcb_cavity_y_max() = pcb_center_y() + pcb_cavity_h() / 2;
function lower_shell_requested_x() =
    (lower_shell_inset_neg_x() - lower_shell_inset_pos_x()) / 2;
function lower_shell_requested_y() = 0;
function lower_shell_x() =
    clamp(
        lower_shell_requested_x(),
        pcb_cavity_x_max() + lower_shell_min_wall - lower_shell_w() / 2,
        pcb_cavity_x_min() - lower_shell_min_wall + lower_shell_w() / 2
    );
function lower_shell_y() =
    clamp(
        lower_shell_requested_y(),
        pcb_cavity_y_max() + lower_shell_min_wall - lower_shell_h() / 2,
        pcb_cavity_y_min() - lower_shell_min_wall + lower_shell_h() / 2
    );
function lower_shell_x_min() = lower_shell_x() - lower_shell_w() / 2;
function lower_shell_x_max() = lower_shell_x() + lower_shell_w() / 2;
function lower_shell_y_min() = lower_shell_y() - lower_shell_h() / 2;
function lower_shell_y_max() = lower_shell_y() + lower_shell_h() / 2;
function lower_shell_taper_start_z() =
    total_depth - lower_shell_taper_h;
function cavity_transition_start_z() =
    max(back_thickness, lower_shell_taper_start_z());
function nfc_outer_start_z() = nfc_bump_enabled ? -nfc_bump_depth : 0;
function nfc_outer_taper_h() = total_depth - nfc_outer_start_z();
function mount_camera_x() = lower_shell_x_min() + mount_camera_side_from_lower_edge;
function mount_usb_x() = lower_shell_x_max() - mount_usb_side_from_lower_edge;
function mount_top_y() = lower_shell_y_max() - mount_long_edge_from_lower_edge;
function mount_bottom_y() = lower_shell_y_min() + mount_long_edge_from_lower_edge;
function nfc_bump_x_min() =
    max(lower_shell_x_min(), mount_camera_x() + nfc_recess_camera_side_from_mount);
function nfc_bump_x_max() =
    min(lower_shell_x_max(), mount_usb_x() - nfc_recess_usb_side_from_mount);
function nfc_bump_y_min() = lower_shell_y_min();
function nfc_bump_y_max() = lower_shell_y_max();
function nfc_bump_w() = nfc_bump_x_max() - nfc_bump_x_min();
function nfc_bump_h() = nfc_bump_y_max() - nfc_bump_y_min();
function nfc_bump_x() = (nfc_bump_x_min() + nfc_bump_x_max()) / 2;
function nfc_bump_y() = (nfc_bump_y_min() + nfc_bump_y_max()) / 2;
function nfc_recess_x_min() =
    nfc_bump_x_min() + nfc_recess_edge_wall;
function nfc_recess_x_max() =
    nfc_bump_x_max() - nfc_recess_edge_wall;
function nfc_recess_y_min() =
    nfc_bump_y_min() + nfc_recess_edge_wall;
function nfc_recess_y_max() =
    nfc_bump_y_max() - nfc_recess_edge_wall;
function nfc_recess_x() = (nfc_recess_x_min() + nfc_recess_x_max()) / 2;
function nfc_recess_y() = (nfc_recess_y_min() + nfc_recess_y_max()) / 2;
function nfc_recess_w() = nfc_recess_x_max() - nfc_recess_x_min();
function nfc_recess_h() = nfc_recess_y_max() - nfc_recess_y_min();
function nfc_recess_floor_z() = -nfc_bump_depth + nfc_recess_floor_thickness;
function nfc_outer_cut_start_z() = nfc_bump_enabled ? -nfc_bump_depth - eps : -eps;

module rounded_rect(size, r) {
    w = size[0];
    h = size[1];
    rr = min(r, min(w, h) / 2 - 0.01);
    hull() {
        for (x = [-w / 2 + rr, w / 2 - rr])
            for (y = [-h / 2 + rr, h / 2 - rr])
                translate([x, y]) circle(r = rr);
    }
}

module rounded_prism(size, r) {
    linear_extrude(height = size[2])
        rounded_rect([size[0], size[1]], r);
}

module mount_positions() {
    for (x = [mount_camera_x(), mount_usb_x()])
        for (y = [mount_top_y(), mount_bottom_y()])
            translate([x, y, 0])
                children();
}

function side_cut_x0() =
    mirror_official_rear_x ? outer_w / 2 - edge_cut_depth - eps : -outer_w / 2 - eps;
function top_cut_y0() = outer_h / 2 - edge_cut_depth - eps;

module side_usb_cutout(y_min, y_max) {
    translate([side_cut_x0(), y_min, usb_cutout_z_min])
        cube([edge_cut_depth + 2 * eps, y_max - y_min, usb_cutout_z_max - usb_cutout_z_min]);
}

module top_cutout(x, w, h) {
    translate([x - w / 2, top_cut_y0(), port_z_center - h / 2])
        cube([w, edge_cut_depth + 2 * eps, h]);
}

module top_pin_hole(x) {
    translate([x, top_cut_y0(), port_z_center])
        rotate([-90, 0, 0])
            cylinder(d = button_pin_hole_d, h = edge_cut_depth + 2 * eps);
}

module camera_hole() {
    camera_entry_z = nfc_bump_enabled ? -eps : -eps;

    if (nfc_bump_enabled) {
        translate([camera_x(), camera_y(), nfc_outer_cut_start_z()])
            cylinder(
                d = camera_lens_relief_d,
                h = -nfc_outer_cut_start_z() + 2 * eps
            );
    }

    translate([camera_x(), camera_y(), camera_entry_z])
        cylinder(
            d1 = camera_lens_bevel_d,
            d2 = camera_lens_hole_d,
            h = camera_lens_bevel_depth + eps
        );
    translate([camera_x(), camera_y(), camera_entry_z])
        cylinder(
            d = camera_lens_hole_d,
            h = back_thickness - camera_entry_z + 2 * eps
        );

    // Inner square recess for the camera module/lens body. The outside remains
    // flat; only the circular optical hole is visible from the back.
    translate([
        camera_x() - camera_pocket_w / 2,
        camera_y() - camera_pocket_h / 2,
        back_thickness - camera_pocket_depth
    ]) {
        cube([camera_pocket_w, camera_pocket_h, camera_pocket_depth + eps]);
        translate([camera_pocket_w / 2, camera_pocket_h / 2, -eps])
            cylinder(d = camera_lens_relief_d, h = camera_pocket_depth + 2 * eps);
    }
}

module mount_screw_cuts() {
    mount_positions() {
        translate([0, 0, nfc_outer_cut_start_z()])
            cylinder(
                d1 = mount_screw_bevel_d,
                d2 = mount_screw_counterbore_d,
                h = mount_screw_bevel_depth + eps
            );
        translate([0, 0, nfc_outer_cut_start_z()])
            cylinder(
                d = mount_screw_counterbore_d,
                h = mount_screw_counterbore_depth + eps
            );
        translate([0, 0, nfc_outer_cut_start_z()])
            cylinder(
                d = mount_screw_clearance_d,
                h = total_depth + front_rim_h - nfc_outer_cut_start_z() + 2 * eps
            );
    }
}

module thin_rounded_plate(w, h, r) {
    linear_extrude(height = eps)
        rounded_rect([w, h], r);
}

module lower_shell_body() {
    hull() {
        translate([lower_shell_x(), lower_shell_y(), 0])
            thin_rounded_plate(
                lower_shell_w() - 2 * lower_shell_bottom_edge_inset,
                lower_shell_h() - 2 * lower_shell_bottom_edge_inset,
                lower_shell_corner_r
            );
        translate([lower_shell_x(), lower_shell_y(), lower_shell_bottom_round_h])
            thin_rounded_plate(lower_shell_w(), lower_shell_h(), lower_shell_corner_r);
    }

    translate([lower_shell_x(), lower_shell_y(), lower_shell_bottom_round_h])
        rounded_prism([
            lower_shell_w(),
            lower_shell_h(),
            lower_shell_taper_start_z() - lower_shell_bottom_round_h + eps
        ], lower_shell_corner_r);
}

module smooth_outer_transition() {
    for (i = [0 : lower_shell_taper_steps - 1]) {
        t0 = i / lower_shell_taper_steps;
        t1 = (i + 1) / lower_shell_taper_steps;
        s0 = taper_profile(t0);
        s1 = taper_profile(t1);

        hull() {
            translate([
                lerp(lower_shell_x(), 0, s0),
                lerp(lower_shell_y(), 0, s0),
                lower_shell_taper_start_z() + lower_shell_taper_h * t0
            ])
                thin_rounded_plate(
                    lerp(lower_shell_w(), outer_w, s0),
                    lerp(lower_shell_h(), outer_h, s0),
                    lerp(lower_shell_corner_r, outer_r, s0)
                );
            translate([
                lerp(lower_shell_x(), 0, s1),
                lerp(lower_shell_y(), 0, s1),
                lower_shell_taper_start_z() + lower_shell_taper_h * t1
            ])
                thin_rounded_plate(
                    lerp(lower_shell_w(), outer_w, s1),
                    lerp(lower_shell_h(), outer_h, s1),
                    lerp(lower_shell_corner_r, outer_r, s1)
                );
        }
    }
}

module nfc_bump_outer() {
    if (nfc_bump_enabled) {
        for (i = [0 : nfc_continuous_taper_steps - 1]) {
            t0 = i / nfc_continuous_taper_steps;
            t1 = (i + 1) / nfc_continuous_taper_steps;
            s0 = nfc_outer_profile(t0);
            s1 = nfc_outer_profile(t1);

            hull() {
                translate([
                    lerp(lower_shell_x(), 0, s0),
                    lerp(lower_shell_y(), 0, s0),
                    nfc_outer_start_z() + nfc_outer_taper_h() * t0
                ])
                    thin_rounded_plate(
                        lerp(
                            lower_shell_w() - 2 * nfc_bottom_slope_inset,
                            outer_w,
                            s0
                        ),
                        lerp(
                            lower_shell_h() - 2 * nfc_bottom_slope_inset,
                            outer_h,
                            s0
                        ),
                        lerp(lower_shell_corner_r, outer_r, s0)
                    );
                translate([
                    lerp(lower_shell_x(), 0, s1),
                    lerp(lower_shell_y(), 0, s1),
                    nfc_outer_start_z() + nfc_outer_taper_h() * t1
                ])
                    thin_rounded_plate(
                        lerp(
                            lower_shell_w() - 2 * nfc_bottom_slope_inset,
                            outer_w,
                            s1
                        ),
                        lerp(
                            lower_shell_h() - 2 * nfc_bottom_slope_inset,
                            outer_h,
                            s1
                        ),
                        lerp(lower_shell_corner_r, outer_r, s1)
                    );
            }
        }
    }
}

module outer_body() {
    union() {
        if (nfc_bump_enabled) {
            nfc_bump_outer();
        } else {
            // Smaller lower shell: this is the PCB-side body that exposes the
            // recessed USB-C plugs while still leaving a printable side wall.
            lower_shell_body();

            // Smooth multi-step transition from the smaller lower shell to the
            // full-size screen-side rim. This avoids the previous square-looking
            // straight chamfer.
            smooth_outer_transition();
        }

        translate([0, 0, total_depth])
            rounded_prism([outer_w, outer_h, front_rim_h], outer_r);
    }
}

module nfc_recess_cut() {
    if (nfc_bump_enabled) {
        translate([nfc_recess_x(), nfc_recess_y(), nfc_recess_floor_z()])
            rounded_prism([
                nfc_recess_w(),
                nfc_recess_h(),
                back_thickness - nfc_recess_floor_z() + eps
            ], nfc_recess_corner_r);
    }
}

module drop_in_cavity() {
    // PCB-side cavity. It is smaller than the glass pocket so the shortened
    // lower shell still has side walls.
    translate([pcb_center_x(), pcb_center_y(), back_thickness])
        rounded_prism([
            pcb_cavity_w(),
            pcb_cavity_h(),
            cavity_transition_start_z() - back_thickness + eps
        ], drop_in_r);

    // Internal transition where the larger screen/glass area sits above the
    // smaller PCB. This avoids the old full-size cavity cutting through the
    // shortened lower shell.
    hull() {
        translate([pcb_center_x(), pcb_center_y(), cavity_transition_start_z()])
            thin_rounded_plate(pcb_cavity_w(), pcb_cavity_h(), drop_in_r);
        translate([0, 0, total_depth])
            thin_rounded_plate(drop_in_w, drop_in_h, drop_in_r);
    }

    translate([0, 0, total_depth])
        rounded_prism([drop_in_w, drop_in_h, front_rim_h + eps], drop_in_r);
}

module protective_case() {
    difference() {
        outer_body();
        drop_in_cavity();
        nfc_recess_cut();

        // Only the requested edge openings.
        side_usb_cutout(usb_cutout_pos_y_min, usb_cutout_pos_y_max);
        side_usb_cutout(usb_cutout_neg_y_min, usb_cutout_neg_y_max);
        // Camera opening on the back.
        camera_hole();

        // Bottom/back screw holes for the recommended fixed assembly.
        if (use_mount_screws)
            mount_screw_cuts();
    }
}

module reference() {
    color([0.03, 0.16, 0.80, 0.35])
        translate([pcb_center_x(), pcb_center_y(), back_thickness + 1.0])
            linear_extrude(height = 1.2)
                square([pcb_w, pcb_h], center = true);

    color([0.02, 0.02, 0.02, 0.25])
        translate([0, 0, total_depth])
            linear_extrude(height = 0.6)
                rounded_rect([glass_w, glass_h], 1.4);

    color([0.00, 0.00, 0.00, 0.45])
        translate([0, 0, total_depth + 0.65])
            linear_extrude(height = 0.4)
                rounded_rect([lcd_w, lcd_h], 0.8);

    color([1.0, 0.45, 0.0, 0.65])
        translate([camera_x(), camera_y(), -0.3])
            cylinder(d = camera_lens_hole_d, h = 0.6);

    color([0.10, 0.10, 0.10, 0.90])
        protective_case();
}

module fit_check_plate() {
    // Thin, quick print for calibration. Lay it over the real unit and mark the
    // offset before committing to the full-height case.
    difference() {
        rounded_prism([outer_w, outer_h, 1.20], outer_r);

        translate([0, 0, -eps])
            rounded_prism([glass_w - 1.50, glass_h - 1.50, 1.20 + 2 * eps], 2.0);

        // Mounting holes.
        mount_positions()
            translate([0, 0, -eps])
                cylinder(d = mount_screw_clearance_d, h = 1.20 + 2 * eps);

        // Camera hole marker.
        translate([camera_x(), camera_y(), -eps])
            cylinder(d = camera_lens_hole_d, h = 1.20 + 2 * eps);

        // Edge-opening markers.
        translate([side_cut_x0(), usb_cutout_pos_y_min, -eps])
            cube([edge_cut_depth + 2 * eps, usb_cutout_pos_y_max - usb_cutout_pos_y_min, 1.20 + 2 * eps]);
        translate([side_cut_x0(), usb_cutout_neg_y_min, -eps])
            cube([edge_cut_depth + 2 * eps, usb_cutout_neg_y_max - usb_cutout_neg_y_min, 1.20 + 2 * eps]);

        translate([from_pcb_left(tf_slot_x_from_pcb_left) - 3.00, top_cut_y0(), -eps])
            cube([6.00, edge_cut_depth + 2 * eps, 1.20 + 2 * eps]);
    }
}

module printable_case() {
    // Screen-facing orientation: USB-C openings are mirrored from the official
    // rear drawing and appear on the right when the display faces the user.
    protective_case();
}

module printable_preview() {
    color([1.00, 0.34, 0.04, 1.00])
        protective_case();
}

module printable_reference() {
    reference();
}

if (part == "reference") {
    printable_reference();
} else if (part == "preview") {
    printable_preview();
} else if (part == "fit_check") {
    fit_check_plate();
} else {
    printable_case();
}
