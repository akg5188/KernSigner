/*
  Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 protective tray case

  One-piece protective case for the camera version. Official dimensions are
  read from the rear-view mechanical drawing, then mirrored in X so the STL
  matches the board when the screen is facing the user.

  Kept openings:
  - 2 x USB-C on the left
  - 1 x microSD/TF card slot on the top
  - 3 x buttons on the top
  - 1 x camera lens on the back

  No GPIO, microphone, or extra rear ventilation openings.

  Official dimensions used:
  - Front glass: 114.40 x 66.80 mm
  - Visible LCD:  94.40 x 56.96 mm
  - PCB:          102.50 x 60.00 mm
*/

$fn = 56;
part = "case"; // case, fit_check, reference

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
outer_r = 3.00;
wall = 2.10;
back_thickness = 2.20;
inside_depth = 10.20;
total_depth = back_thickness + inside_depth;

// Front rectangular protection rim. It is intentionally only slightly higher
// than the glass so touch use remains comfortable.
front_rim_h = 1.20;
front_rim_w = 2.20;
// First print showed the front screen/glass opening about 0.5 mm too loose.
// Tighten only this front rim pocket; side ports and rear holes stay unchanged.
glass_clearance = 0.12;
glass_pocket_w = glass_w + 2 * glass_clearance;
glass_pocket_h = glass_h + 2 * glass_clearance;

// The board/display drops in from the front. This cavity is larger than glass
// and goes down to the back plate.
drop_in_w = glass_pocket_w;
drop_in_h = glass_pocket_h;
drop_in_r = 2.40;

// Fixing: recommended method is four longer screws through the case back into
// the board's existing corner mounting holes. This avoids relying on a hard
// snap-fit around the glass.
use_mount_screws = true;
mount_screw_clearance_d = 2.80; // M2.5 clearance; use 2.4 for M2.
mount_screw_slot_x = 5.20;
mount_screw_slot_y = 8.00;
mount_screw_head_d = 5.80;
mount_screw_head_slot_x = 8.20;
mount_screw_head_slot_y = 10.60;
mount_screw_head_h = 1.25;
mount_inner_relief_d = 6.40;

// Hand-measured from the real unit: mounting-hole centers to the outer glass
// edge. These override the vertical hole positions inferred from the PCB size.
mount_top_from_glass_top = 15.00;
mount_bottom_from_glass_bottom = 10.00;
mount_l_x = 5.25;
mount_r_x = pcb_w - 5.60;
mount_t_y = 5.00;
mount_b_y = pcb_h - 5.00;

// Camera module location measured from the printed back cover.
// Back view: the button edge is +Y. The camera is near the left short edge.
camera_center_x_from_left_edge = 15.00;
camera_center_y_from_button_edge = 37.00;
camera_center_y_from_plain_edge_reference = 36.00;
// Mini OV5647/Raspberry-Pi-style camera opening: small visible round hole,
// with a hidden square recess inside for the lens body.
camera_lens_hole_d = 7.00;
camera_lens_relief_d = 10.80;
camera_pocket_w = 15.00;
camera_pocket_h = 15.00;
camera_pocket_depth = 1.60;
camera_guard_h = 0.00;

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
// - fill 1.5 mm on the screen/front side of both openings.
usb_center_separator_w = 3.00;
usb_outer_side_fill = 4.00;
usb_screen_side_fill = 1.50;
usb_cutout_pos_y_min = usb_center_separator_w / 2;
usb_cutout_pos_y_max = usb_cutout_y1 + usb_cutout_w / 2 - usb_outer_side_fill;
usb_cutout_neg_y_min = usb_cutout_y2 - usb_cutout_w / 2 + usb_outer_side_fill;
usb_cutout_neg_y_max = -usb_center_separator_w / 2;
usb_cutout_z_min = port_z_center - usb_cutout_h / 2;
usb_cutout_z_max = port_z_center + usb_cutout_h / 2 - usb_screen_side_fill;

// Official rear-view top-edge positions.
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
function camera_x() = -outer_w / 2 + camera_center_x_from_left_edge;
function camera_y() = outer_h / 2 - camera_center_y_from_button_edge;

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

module slot_2d(size, r) {
    rounded_rect(size, r);
}

module mount_positions() {
    mount_left_x = from_pcb_left(mount_l_x);
    mount_right_x = from_pcb_left(mount_r_x);
    mount_top_y = glass_h / 2 - mount_top_from_glass_top;
    mount_bottom_y = -glass_h / 2 + mount_bottom_from_glass_bottom;

    for (x = [mount_left_x, mount_right_x])
        for (y = [mount_top_y, mount_bottom_y])
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
    translate([camera_x(), camera_y(), -eps])
        cylinder(d = camera_lens_hole_d, h = back_thickness + 2 * eps);

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
        translate([0, 0, -eps])
            linear_extrude(height = back_thickness + inside_depth + front_rim_h + 2 * eps)
                slot_2d([mount_screw_slot_x, mount_screw_slot_y], mount_screw_clearance_d / 2);
        translate([0, 0, -eps])
            linear_extrude(height = mount_screw_head_h + eps)
                slot_2d([mount_screw_head_slot_x, mount_screw_head_slot_y], mount_screw_head_d / 2);
        translate([0, 0, back_thickness - eps])
            cylinder(d = mount_inner_relief_d, h = 1.40 + eps);
    }
}

module protective_case() {
    difference() {
        union() {
            difference() {
                // Main tray body, back at z=0 and front rim at z=total_depth.
                rounded_prism([outer_w, outer_h, total_depth + front_rim_h], outer_r);

                // Drop-in cavity. It leaves the back plate and side walls intact.
                translate([0, 0, back_thickness])
                    rounded_prism([drop_in_w, drop_in_h, inside_depth + front_rim_h + eps], drop_in_r);

                // The raised outer wall is the screen protection: the glass
                // pocket is lower than this rim, so face-down impact hits the
                // printed edge first.
            }

        }

        // Only the requested edge openings.
        side_usb_cutout(usb_cutout_pos_y_min, usb_cutout_pos_y_max);
        side_usb_cutout(usb_cutout_neg_y_min, usb_cutout_neg_y_max);
        top_pin_hole(from_pcb_left(reset_x_from_pcb_left));
        top_pin_hole(from_pcb_left(boot_x_from_pcb_left));
        top_pin_hole(from_pcb_left(power_x_from_pcb_left));

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

        // Mounting slots.
        mount_positions()
            translate([0, 0, -eps])
                linear_extrude(height = 1.20 + 2 * eps)
                    slot_2d([mount_screw_slot_x, mount_screw_slot_y], mount_screw_clearance_d / 2);

        // Camera hole marker.
        translate([camera_x(), camera_y(), -eps])
            cylinder(d = camera_lens_hole_d, h = 1.20 + 2 * eps);

        // Edge-opening markers.
        translate([side_cut_x0(), usb_cutout_pos_y_min, -eps])
            cube([edge_cut_depth + 2 * eps, usb_cutout_pos_y_max - usb_cutout_pos_y_min, 1.20 + 2 * eps]);
        translate([side_cut_x0(), usb_cutout_neg_y_min, -eps])
            cube([edge_cut_depth + 2 * eps, usb_cutout_neg_y_max - usb_cutout_neg_y_min, 1.20 + 2 * eps]);

        for (x = [
            from_pcb_left(tf_slot_x_from_pcb_left),
            from_pcb_left(reset_x_from_pcb_left),
            from_pcb_left(boot_x_from_pcb_left),
            from_pcb_left(power_x_from_pcb_left)
        ])
            translate([x - 3.00, top_cut_y0(), -eps])
                cube([6.00, edge_cut_depth + 2 * eps, 1.20 + 2 * eps]);
    }
}

module printable_case() {
    // Screen-facing orientation: USB-C openings are mirrored from the official
    // rear drawing and appear on the right when the display faces the user.
    protective_case();
}

module printable_reference() {
    reference();
}

if (part == "reference") {
    printable_reference();
} else if (part == "fit_check") {
    fit_check_plate();
} else {
    printable_case();
}
