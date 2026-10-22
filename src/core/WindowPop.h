// WindowPop core: host-independent window simulation and rendering.
//
// Everything here is stateless. The set of windows on screen at time t is a
// pure function of the settings, the seed and t, so frames can render in any
// order, on any thread, on any machine (Multi-Frame Rendering, render farms).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wp {

enum class Look { MotionWindows = 0, TimeTrail };
enum class Style { Glossy2000s = 0, Classic90s, ModernDark, ModernLight };
enum class Entrance { Pop = 0, Fade, None };
enum class Background { Original = 0, Dimmed, Solid, Transparent };

struct Settings {
    Look look = Look::MotionWindows;
    double windowsPerSecond = 8.0;  // "Amount"
    double lifetime = 1.5;          // "Trail Length", seconds each window stays up
    double windowSize = 0.30;       // base window height as a fraction of frame height
    uint32_t seed = 1;

    bool useFocus = true;           // every 4th window is a larger "focus" window
    double focusX = 0.5;            // normalised 0..1
    double focusY = 0.5;

    Style style = Style::Glossy2000s;
    double interfaceScale = 1.0;    // title bar / border scale, 1.0 = sized for 1080p
    Entrance entrance = Entrance::Pop;

    Background background = Background::Dimmed;
    double dimAmount = 0.6;
    float solidColor[3] = {0.04f, 0.04f, 0.06f};
};

// Picks a spawn point biased toward motion. Given the spawn time and a uniform
// random number in [0,1), return true and a normalised position, or false to
// fall back to a random position. Optional.
using MotionPicker = std::function<bool(double spawnTime, double rnd, double& nx, double& ny)>;

struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixels, x1/y1 exclusive
    double w() const { return x1 - x0; }
    double h() const { return y1 - y0; }
};

struct Window {
    int64_t index = 0;     // stable id, used for the title number and randomness
    double spawnTime = 0;  // seconds; the window shows the frame at this time
    double age = 0;        // seconds since spawn
    bool focus = false;
    Rect content;          // where the content area sits in the output frame
    Rect source;           // which part of the frozen frame fills the content area
    std::string title;
};

// Windows alive at time t, oldest first (draw in this order).
std::vector<Window> activeWindows(const Settings& s, double t, int frameW, int frameH,
                                  const MotionPicker& motion = nullptr);

// ---------------------------------------------------------------------------
// Pixel formats. Layouts match After Effects (ARGB, 8/16/32-bit).

struct Px8  { uint8_t  a, r, g, b; };
struct Px16 { uint16_t a, r, g, b; };
struct PxF  { float    a, r, g, b; };

template <typename P> struct PixelTraits;
template <> struct PixelTraits<Px8>  { static constexpr float kMax = 255.0f; };
template <> struct PixelTraits<Px16> { static constexpr float kMax = 32768.0f; };  // AE 16 bpc is 0..32768
template <> struct PixelTraits<PxF>  { static constexpr float kMax = 1.0f; };

template <typename P>
struct Image {
    P* data = nullptr;
    int width = 0;
    int height = 0;
    std::ptrdiff_t rowBytes = 0;

    P* row(int y) const { return reinterpret_cast<P*>(reinterpret_cast<char*>(data) + y * rowBytes); }
};

// Renders one output frame.
//   current : the source frame at the current time (for the background)
//   frames  : for each window, the frozen source frame at its spawn time
//             (may point to `current`); same size as `out`
template <typename P>
void render(const Settings& s, const std::vector<Window>& windows,
            const std::vector<const Image<P>*>& frames,
            const Image<P>& current, Image<P>& out);

}  // namespace wp
