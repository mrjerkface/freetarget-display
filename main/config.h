// Build-time settings. Edit here, rebuild, flash.
#pragma once

// --- Which WiFi to join -------------------------------------------------------
// Empty FET_SSID = automatic: scan and join the strongest network whose name
// starts with FET_SSID_PREFIX. freETarget names its network "FET-<name>"
// (default "FET-TARGET"). Set FET_SSID to pin one target when several are in range.
#define FET_SSID_PREFIX   "FET-"
#define FET_SSID          ""
// freETarget's network is open unless WIFI_PWD was set on the target.
#define FET_PASSWORD      ""

// --- Where the target listens -------------------------------------------------
// Empty FET_HOST = use the WiFi gateway (the target itself, 192.168.10.9 when it
// runs its own network). Set an IP here if the target joined a club router.
#define FET_HOST          ""
#define FET_PORT          1090

// --- Target face ---------------------------------------------------------------
// Index into TARGETS[]:
//   0 = ISSF 50 m smallbore rifle, 1 = NSRA 25 yard, 2 = ISSF 10 m air rifle,
//   3 = ISSF 10 m air pistol
// Used until the target reports its own "target_name", or until a face is
// picked with the BOOT button (that choice is saved and always wins).
#define FET_DEFAULT_TARGET 0
// Faces the BOOT button steps through, in order.
#define FET_FACE_MENU      { 0, 2, 3 }

// --- Match these to the freETarget PC app's settings ---------------------------
// The PC app changes every shot with these before scoring and drawing, so the
// CYD must use the same values or its scores will differ from the PC's.
//
// "Distance to target (%)" in App Config (100 = full distance). Received x/y
// are scaled by 100 / this value.
#define FET_DISTANCE_PERCENT 100.0
// Calibration offsets (mm) and rotation (degrees) from the PC app's
// calibration screen. 0 = none.
#define FET_CAL_X_MM      0.0
#define FET_CAL_Y_MM      0.0
#define FET_CAL_ANGLE_DEG 0.0
// Bullet diameter of the PC app's event, mm. 0 = the face's default
// (5.6 for smallbore, 4.5 for air).
#define FET_CALIBER_MM    0.0
// Decimal scoring as set on the PC app's event: 1 = on, 0 = off, -1 = the
// face's default (on for ISSF faces, off for NSRA).
#define FET_DECIMAL       (-1)

// Shots kept for one session (the target holds about the same).
#define FET_MAX_SHOTS     120
