#ifndef ICONS_H
#define ICONS_H

// Font Awesome symbol definitions (UTF-8 encoded).
// The generated 16/24/36 px icon fonts must include these codepoints.
#define LV_SYMBOL_OK "\xEF\x80\x8C" // FontAwesome U+F00C = check
#define LV_SYMBOL_POWER "\xEF\x80\x91" // FontAwesome U+F011 = power-off
#define LV_SYMBOL_SETTINGS "\xEF\x80\x93" // FontAwesome U+F013 = gear
#define LV_SYMBOL_LEFT "\xEF\x81\x93" // FontAwesome U+F053 = chevron-left
#define LV_SYMBOL_RIGHT "\xEF\x81\x94" // FontAwesome U+F054 = chevron-right
#define LV_SYMBOL_EYE_OPEN "\xEF\x81\xAE" // FontAwesome U+F06E = eye
#define LV_SYMBOL_EYE_CLOSE "\xEF\x81\xB0" // FontAwesome U+F070 = eye-slash
#define LV_SYMBOL_CHARGE "\xEF\x83\xA7" // FontAwesome U+F0E7 = bolt
#define LV_SYMBOL_BATTERY_FULL "\xEF\x89\x80" // FontAwesome U+F240 = battery-full
#define LV_SYMBOL_BATTERY_3 "\xEF\x89\x81" // FontAwesome U+F241 = battery-three-quarters
#define LV_SYMBOL_BATTERY_2 "\xEF\x89\x82" // FontAwesome U+F242 = battery-half
#define LV_SYMBOL_BATTERY_1 "\xEF\x89\x83" // FontAwesome U+F243 = battery-quarter
#define LV_SYMBOL_BATTERY_EMPTY "\xEF\x89\x84" // FontAwesome U+F244 = battery-empty
#define LV_SYMBOL_TRASH "\xEF\x8B\xAD" // FontAwesome U+F2ED = trash-can
#define ICON_BITCOIN "\xEE\x82\xB4" // FontAwesome U+E0B4 = bitcoin-sign
#define ICON_QR_CODE "\xEF\x80\xA9" // FontAwesome U+F029 = qrcode
#define ICON_HELP "\xEF\x81\x99" // FontAwesome U+F059 = circle-question
#define ICON_DERIVATION "\xEF\x84\xA6" // FontAwesome U+F126 = code-branch
#define ICON_FINGERPRINT "\xEF\x95\xB7" // FontAwesome U+F577 = fingerprint
#define LV_SYMBOL_BACKSPACE "\xEF\x95\x9A" // FontAwesome U+F55A = delete-left

// Backward-compatible aliases for call sites that used size-named symbols.
#define ICON_QRCODE_36 ICON_QR_CODE
#define ICON_HELP_36 ICON_HELP
#define ICON_FINGERPRINT_36 ICON_FINGERPRINT

#endif // ICONS_H
