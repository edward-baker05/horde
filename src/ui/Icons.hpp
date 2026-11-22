#pragma once

// Icons from Lucide (https://lucide.dev, ISC licence, assets/fonts/Lucide-LICENSE.txt)
// as UTF-8 strings, drawn in the "lucide" font family. assets/fonts/Lucide.ttf
// is lucide-static 1.75.0's font cut down to exactly these glyphs, so adding
// one means subsetting it again with the new codepoint:
//
//   python3 -m fontTools.subset lucide.ttf --unicodes=U+E13C,...
//       --drop-tables+=GSUB,GPOS,GDEF --layout-features='' --output-file=Lucide.ttf
//
// (one command, wrapped here).
//
// RML files spell the same glyphs as literal characters.

namespace horde::ui::icon {

inline constexpr const char* kPlay = "\xEE\x84\xBC"; // play, U+E13C
inline constexpr const char* kSettings = "\xEE\x85\x94"; // settings, U+E154
inline constexpr const char* kPower = "\xEE\x85\x80"; // power, U+E140
inline constexpr const char* kFlaskConical = "\xEE\x83\x95"; // flask-conical, U+E0D5
inline constexpr const char* kSparkles = "\xEE\x90\x92"; // sparkles, U+E412
inline constexpr const char* kPencilRuler = "\xEE\x93\xB1"; // pencil-ruler, U+E4F1
inline constexpr const char* kMousePointer2 = "\xEE\x87\x83"; // mouse-pointer-2, U+E1C3
inline constexpr const char* kSquare = "\xEE\x85\xA7"; // square, U+E167
inline constexpr const char* kTriangle = "\xEE\x86\x92"; // triangle, U+E192
inline constexpr const char* kCircle = "\xEE\x81\xB6"; // circle, U+E076
inline constexpr const char* kSpline = "\xEE\x8E\x8B"; // spline, U+E38B
inline constexpr const char* kArrowDownToLine = "\xEE\x91\x95"; // arrow-down-to-line, U+E455
inline constexpr const char* kArrowUpFromLine = "\xEE\x91\x9A"; // arrow-up-from-line, U+E45A
inline constexpr const char* kWind = "\xEE\x86\xB0"; // wind, U+E1B0
inline constexpr const char* kPaintbrush = "\xEE\x8B\xA7"; // paintbrush, U+E2E7
inline constexpr const char* kUndo2 = "\xEE\x8A\xA1"; // undo-2, U+E2A1
inline constexpr const char* kRedo2 = "\xEE\x8A\xA0"; // redo-2, U+E2A0
inline constexpr const char* kFlame = "\xEE\x83\x92"; // flame, U+E0D2
inline constexpr const char* kZap = "\xEE\x86\xB4"; // zap, U+E1B4
inline constexpr const char* kBomb = "\xEE\x8B\xBF"; // bomb, U+E2FF
inline constexpr const char* kRadar = "\xEE\x92\x97"; // radar, U+E497
inline constexpr const char* kConstruction = "\xEE\x8E\xB4"; // construction, U+E3B4
inline constexpr const char* kBrickWall = "\xEE\x96\x81"; // brick-wall, U+E581
inline constexpr const char* kCircleDot = "\xEE\x8D\x85"; // circle-dot, U+E345
inline constexpr const char* kCrosshair = "\xEE\x82\xAC"; // crosshair, U+E0AC
inline constexpr const char* kShirt = "\xEE\x87\x8A"; // shirt, U+E1CA
inline constexpr const char* kDisc3 = "\xEE\x92\x94"; // disc-3, U+E494
inline constexpr const char* kStore = "\xEE\x8F\xA4"; // store, U+E3E4
inline constexpr const char* kMusic = "\xEE\x84\xA2"; // music, U+E122
inline constexpr const char* kSpeaker = "\xEE\x85\xA6"; // speaker, U+E166
inline constexpr const char* kAudioWaveform = "\xEE\x95\x9B"; // audio-waveform, U+E55B
inline constexpr const char* kSprayCan = "\xEE\x92\x95"; // spray-can, U+E495
inline constexpr const char* kFence = "\xEE\x96\x82"; // fence, U+E582
inline constexpr const char* kWifi = "\xEE\x86\xAE"; // wifi, U+E1AE
inline constexpr const char* kChevronRight = "\xEE\x81\xAF"; // chevron-right, U+E06F
inline constexpr const char* kX = "\xEE\x86\xB2"; // x, U+E1B2
inline constexpr const char* kSave = "\xEE\x85\x8D"; // save, U+E14D
inline constexpr const char* kHouse = "\xEE\x83\xB5"; // house, U+E0F5
inline constexpr const char* kLogOut = "\xEE\x84\x8E"; // log-out, U+E10E

} // namespace horde::ui::icon
