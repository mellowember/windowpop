#include "WindowPop.h"

#include <algorithm>
#include <cmath>

namespace wp {

namespace {

// ---------------------------------------------------------------------------
// Deterministic randomness: hash(seed, index, salt) -> [0,1)

uint64_t mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double rnd(uint32_t seed, int64_t index, uint32_t salt) {
    uint64_t h = mix64(static_cast<uint64_t>(seed) * 0x100000001B3ull ^ mix64(static_cast<uint64_t>(index)) ^
                       (static_cast<uint64_t>(salt) << 48));
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

constexpr int kMaxWindows = 256;

}  // namespace

// ---------------------------------------------------------------------------
// Simulation

std::vector<Window> activeWindows(const Settings& s, double t, int frameW, int frameH,
                                  const MotionPicker& motion) {
    std::vector<Window> out;
    if (s.windowsPerSecond <= 0 || s.lifetime <= 0 || frameW <= 0 || frameH <= 0) return out;

    const double rate = s.windowsPerSecond;
    // Window k spawns in slot k, jittered inside it so spawns don't look metronomic.
    auto spawnOf = [&](int64_t k) { return (static_cast<double>(k) + 0.85 * rnd(s.seed, k, 1)) / rate; };

    int64_t kFirst = static_cast<int64_t>(std::floor((t - s.lifetime) * rate)) - 1;
    int64_t kLast = static_cast<int64_t>(std::floor(t * rate));
    kFirst = std::max<int64_t>(kFirst, kLast - kMaxWindows);
    kFirst = std::max<int64_t>(kFirst, 0);  // nothing spawns before the layer starts

    const double fw = frameW, fh = frameH;

    for (int64_t k = kFirst; k <= kLast; ++k) {
        const double spawn = spawnOf(k);
        const double age = t - spawn;
        if (age < 0 || age >= s.lifetime) continue;

        Window w;
        w.index = k;
        w.spawnTime = spawn;
        w.age = age;

        if (s.look == Look::TimeTrail) {
            // Classic error-cascade: full-frame shrunk windows marching diagonally.
            const double scale = 0.35 + 0.5 * clamp01(s.windowSize);
            const double ww = fw * scale, wh = fh * scale;
            const int steps = 12;
            const double step = (k % steps) / static_cast<double>(steps - 1);
            const double x0 = (fw - ww) * step;
            const double y0 = (fh - wh) * step;
            w.content = {x0, y0, x0 + ww, y0 + wh};
            w.source = {0, 0, fw, fh};
            w.title = "trail#" + std::to_string(10000 + k);
        } else {
            w.focus = s.useFocus && (k % 4 == 0);
            const double baseH = fh * s.windowSize * (w.focus ? 1.6 : 1.0);
            const double aspect = 0.65 + 0.8 * rnd(s.seed, k, 2);  // width / height
            const double sizeJitter = 0.7 + 0.6 * rnd(s.seed, k, 3);
            const double wh = baseH * sizeJitter;
            const double ww = wh * aspect;

            double nx, ny;
            if (w.focus) {
                // Focus windows hug the focus point with a small wobble.
                nx = s.focusX + (rnd(s.seed, k, 4) - 0.5) * 0.08;
                ny = s.focusY + (rnd(s.seed, k, 5) - 0.5) * 0.08;
            } else if (!(motion && motion(spawn, rnd(s.seed, k, 6), nx, ny))) {
                // No motion data: scatter, loosely pulled toward the focus point.
                nx = 0.5 * rnd(s.seed, k, 4) + 0.5 * (s.focusX + (rnd(s.seed, k, 7) - 0.5) * 0.9);
                ny = 0.5 * rnd(s.seed, k, 5) + 0.5 * (s.focusY + (rnd(s.seed, k, 8) - 0.5) * 0.9);
            }
            const double cx = clamp01(nx) * fw, cy = clamp01(ny) * fh;
            w.content = {cx - ww / 2, cy - wh / 2, cx + ww / 2, cy + wh / 2};
            w.source = w.content;  // the window shows the footage underneath it
            w.title = (w.focus ? "focus#" : "motion#") + std::to_string(w.focus ? 100 + k : 10000 + k);
        }
        out.push_back(std::move(w));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Drawing helpers

namespace {

struct Color {
    float r, g, b, a;
};

Color lerp(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

constexpr Color rgb(float r, float g, float b) { return {r, g, b, 1.0f}; }

// 5x7 bitmap font, MSB (bit 4) is the leftmost column. Lowercase, digits, '#', '.'.
const uint8_t* glyph(char c) {
    static const uint8_t kDigits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
    static const uint8_t kLower[26][7] = {
        {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},
        {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},
        {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08},
        {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},
        {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C},
        {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}, {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11},
        {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10},
        {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01}, {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
        {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}, {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06},
        {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}, {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
        {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}};
    static const uint8_t kHash[7] = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A};
    static const uint8_t kDot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    if (c >= 'a' && c <= 'z') return kLower[c - 'a'];
    if (c == '#') return kHash;
    if (c == '.') return kDot;
    return nullptr;
}

struct StyleSpec {
    Color frame;          // outer border
    Color frameLight;     // bevel highlight (Classic) / inner line
    Color frameDark;      // bevel shadow
    Color titleTop, titleBottom;
    Color text;
    Color button, buttonGlyph, closeButton;
    int border;           // px at 1080p
    int titleBar;         // px at 1080p
    bool bevel;
    bool boldText;
    bool macButtons;      // traffic lights on the left, centred title
};

StyleSpec styleSpec(Style st) {
    switch (st) {
        case Style::Classic90s:
            return {rgb(0.75f, 0.75f, 0.75f), rgb(1, 1, 1), rgb(0.35f, 0.35f, 0.35f),
                    rgb(0.0f, 0.0f, 0.5f), rgb(0.06f, 0.52f, 0.82f), rgb(1, 1, 1),
                    rgb(0.75f, 0.75f, 0.75f), rgb(0, 0, 0), rgb(0.75f, 0.75f, 0.75f),
                    4, 22, true, true, false};
        case Style::ModernDark:
            return {rgb(0.17f, 0.17f, 0.18f), rgb(0.30f, 0.30f, 0.32f), rgb(0.08f, 0.08f, 0.09f),
                    rgb(0.20f, 0.20f, 0.21f), rgb(0.16f, 0.16f, 0.17f), rgb(0.86f, 0.86f, 0.88f),
                    rgb(0.30f, 0.30f, 0.32f), rgb(0.9f, 0.9f, 0.9f), rgb(0.99f, 0.37f, 0.34f),
                    1, 28, false, false, true};
        case Style::ModernLight:
            return {rgb(0.86f, 0.86f, 0.87f), rgb(0.98f, 0.98f, 0.98f), rgb(0.70f, 0.70f, 0.72f),
                    rgb(0.95f, 0.95f, 0.96f), rgb(0.90f, 0.90f, 0.91f), rgb(0.20f, 0.20f, 0.22f),
                    rgb(0.80f, 0.80f, 0.82f), rgb(0.2f, 0.2f, 0.2f), rgb(0.99f, 0.37f, 0.34f),
                    1, 28, false, false, true};
        case Style::Glossy2000s:
        default:
            return {rgb(0.04f, 0.30f, 0.86f), rgb(0.35f, 0.62f, 1.0f), rgb(0.0f, 0.16f, 0.55f),
                    rgb(0.30f, 0.58f, 1.0f), rgb(0.0f, 0.33f, 0.88f), rgb(1, 1, 1),
                    rgb(0.15f, 0.45f, 0.98f), rgb(1, 1, 1), rgb(0.88f, 0.30f, 0.16f),
                    4, 24, false, true, false};
    }
}

template <typename P>
struct Canvas {
    Image<P>& img;
    float alpha;  // global opacity for the window being drawn (fade entrance)

    static constexpr float kMax = PixelTraits<P>::kMax;

    void blendPixel(P& d, const Color& c) const {
        using T = decltype(d.r);
        const float a = c.a * alpha;
        if (a <= 0) return;
        const float ia = 1.0f - a;
        d.r = static_cast<T>(c.r * a * kMax + d.r * ia);
        d.g = static_cast<T>(c.g * a * kMax + d.g * ia);
        d.b = static_cast<T>(c.b * a * kMax + d.b * ia);
        d.a = static_cast<T>(a * kMax + d.a * ia);
    }

    void fill(int x0, int y0, int x1, int y1, const Color& c) const {
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, img.width);
        y1 = std::min(y1, img.height);
        for (int y = y0; y < y1; ++y) {
            P* row = img.row(y);
            for (int x = x0; x < x1; ++x) blendPixel(row[x], c);
        }
    }

    void vgradient(int x0, int y0, int x1, int y1, const Color& top, const Color& bottom) const {
        const int h = std::max(1, y1 - y0);
        for (int y = y0; y < y1; ++y) fill(x0, y, x1, y + 1, lerp(top, bottom, float(y - y0) / h));
    }

    void disc(int cx, int cy, int r, const Color& c) const {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r) fill(cx + x, cy + y, cx + x + 1, cy + y + 1, c);
    }

    // Returns the text width in px.
    int text(int x, int y, const std::string& s, int px, bool bold, const Color& c, bool draw = true) const {
        const int advance = (bold ? 7 : 6) * px;
        if (draw) {
            for (size_t i = 0; i < s.size(); ++i) {
                const uint8_t* g = glyph(s[i]);
                if (!g) continue;
                const int gx = x + static_cast<int>(i) * advance;
                for (int row = 0; row < 7; ++row) {
                    // Bold smears each row one column to the right (6 columns wide).
                    const unsigned bits = bold ? (g[row] << 1 | g[row]) : (g[row] << 1);
                    for (int col = 0; col < 6; ++col) {
                        if ((bits >> (5 - col)) & 1) fill(gx + col * px, y + row * px, gx + (col + 1) * px, y + (row + 1) * px, c);
                    }
                }
            }
        }
        return static_cast<int>(s.size()) * advance;
    }
};

double easeOutBack(double x) {
    const double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}

}  // namespace

// ---------------------------------------------------------------------------
// Rendering

template <typename P>
void render(const Settings& s, const std::vector<Window>& windows, const std::vector<const Image<P>*>& frames,
            const Image<P>& current, Image<P>& out) {
    using T = decltype(out.data->r);
    constexpr float kMax = PixelTraits<P>::kMax;
    const int W = out.width, H = out.height;

    // 1. Background
    for (int y = 0; y < H; ++y) {
        P* d = out.row(y);
        const P* src = current.row(y);
        for (int x = 0; x < W; ++x) {
            switch (s.background) {
                case Background::Original: d[x] = src[x]; break;
                case Background::Dimmed: {
                    const float k = static_cast<float>(1.0 - clamp01(s.dimAmount));
                    d[x].a = src[x].a;
                    d[x].r = static_cast<T>(src[x].r * k);
                    d[x].g = static_cast<T>(src[x].g * k);
                    d[x].b = static_cast<T>(src[x].b * k);
                    break;
                }
                case Background::Solid:
                    d[x].a = static_cast<T>(kMax);
                    d[x].r = static_cast<T>(s.solidColor[0] * kMax);
                    d[x].g = static_cast<T>(s.solidColor[1] * kMax);
                    d[x].b = static_cast<T>(s.solidColor[2] * kMax);
                    break;
                case Background::Transparent: d[x] = P{}; break;
            }
        }
    }

    // 2. Windows, oldest first
    const StyleSpec st = styleSpec(s.style);
    const double ui = s.interfaceScale * H / 1080.0;
    const int border = std::max(1, static_cast<int>(std::lround(st.border * ui)));
    const int titleH = std::max(8, static_cast<int>(std::lround(st.titleBar * ui)));
    const int fontPx = std::max(1, static_cast<int>(std::lround(titleH / 11.0)));

    for (size_t i = 0; i < windows.size(); ++i) {
        const Window& w = windows[i];
        const Image<P>& src = frames[i] ? *frames[i] : current;

        // Entrance animation
        double scale = 1.0;
        float alpha = 1.0f;
        const double inDur = 0.18;
        if (s.entrance == Entrance::Pop && w.age < inDur) scale = std::max(0.05, easeOutBack(w.age / inDur));
        if (s.entrance == Entrance::Fade && w.age < inDur) alpha = static_cast<float>(w.age / inDur);

        // Outer frame rect around content, scaled about the window centre
        const double ox0 = w.content.x0 - border, oy0 = w.content.y0 - border - titleH;
        const double ox1 = w.content.x1 + border, oy1 = w.content.y1 + border;
        const double cx = (ox0 + ox1) / 2, cy = (oy0 + oy1) / 2;
        auto sx = [&](double x) { return static_cast<int>(std::lround(cx + (x - cx) * scale)); };
        auto sy = [&](double y) { return static_cast<int>(std::lround(cy + (y - cy) * scale)); };

        Canvas<P> cv{out, alpha};
        const int fx0 = sx(ox0), fy0 = sy(oy0), fx1 = sx(ox1), fy1 = sy(oy1);
        const int b = std::max(1, static_cast<int>(std::lround(border * scale)));
        const int tb = std::max(1, static_cast<int>(std::lround(titleH * scale)));

        // Frame
        cv.fill(fx0, fy0, fx1, fy1, st.frame);
        if (st.bevel) {
            cv.fill(fx0, fy0, fx1, fy0 + 1, st.frameLight);
            cv.fill(fx0, fy0, fx0 + 1, fy1, st.frameLight);
            cv.fill(fx0, fy1 - 1, fx1, fy1, st.frameDark);
            cv.fill(fx1 - 1, fy0, fx1, fy1, st.frameDark);
        } else {
            cv.fill(fx0, fy0, fx1, fy0 + 1, st.frameLight);
            cv.fill(fx0, fy1 - 1, fx1, fy1, st.frameDark);
        }

        // Title bar
        const int tx0 = fx0 + b, tx1 = fx1 - b, ty0 = fy0 + b, ty1 = ty0 + tb;
        cv.vgradient(tx0, ty0, tx1, ty1, st.titleTop, st.titleBottom);

        // Content: nearest-neighbour map from the scaled content rect back to the source rect
        const int cx0 = tx0, cx1 = tx1, cy0 = ty1, cy1 = fy1 - b;
        if (cx1 > cx0 && cy1 > cy0) {
            const double kx = w.source.w() / (cx1 - cx0), ky = w.source.h() / (cy1 - cy0);
            const int dx0 = std::max(cx0, 0), dx1 = std::min(cx1, W);
            const int dy0 = std::max(cy0, 0), dy1 = std::min(cy1, H);
            for (int y = dy0; y < dy1; ++y) {
                int syy = static_cast<int>(w.source.y0 + (y - cy0 + 0.5) * ky);
                syy = std::min(std::max(syy, 0), src.height - 1);
                const P* srow = src.row(syy);
                P* drow = out.row(y);
                for (int x = dx0; x < dx1; ++x) {
                    int sxx = static_cast<int>(w.source.x0 + (x - cx0 + 0.5) * kx);
                    sxx = std::min(std::max(sxx, 0), src.width - 1);
                    if (alpha >= 1.0f) {
                        drow[x] = srow[sxx];
                        drow[x].a = static_cast<T>(kMax);
                    } else {
                        const P& sp = srow[sxx];
                        cv.blendPixel(drow[x], {sp.r / kMax, sp.g / kMax, sp.b / kMax, 1.0f});
                    }
                }
            }
        }

        if (scale < 0.6) continue;  // too small mid-pop for legible chrome

        // Buttons and title
        const int bs = tb - 2 * std::max(1, tb / 8);  // button size
        const int by0 = ty0 + (tb - bs) / 2;
        const int pad = std::max(1, tb / 8);
        const int fpx = std::max(1, static_cast<int>(std::lround(fontPx * scale)));
        const int textY = ty0 + (tb - 7 * fpx) / 2;

        int textX0, textX1;
        if (st.macButtons) {
            const int r = std::max(2, bs / 3);
            const Color lights[3] = {rgb(0.99f, 0.37f, 0.34f), rgb(0.99f, 0.74f, 0.18f), rgb(0.16f, 0.78f, 0.25f)};
            int bx = tx0 + pad * 2 + r;
            for (int k = 0; k < 3; ++k, bx += r * 3) cv.disc(bx, ty0 + tb / 2, r, lights[k]);
            textX0 = bx;
            textX1 = tx1 - pad * 2;
        } else {
            int bx = tx1 - pad - bs;
            for (int k = 0; k < 3; ++k) {  // close, maximise, minimise from the right
                const Color face = (k == 0) ? st.closeButton : st.button;
                cv.fill(bx, by0, bx + bs, by0 + bs, face);
                if (st.bevel) {
                    cv.fill(bx, by0, bx + bs, by0 + 1, st.frameLight);
                    cv.fill(bx, by0, bx + 1, by0 + bs, st.frameLight);
                    cv.fill(bx, by0 + bs - 1, bx + bs, by0 + bs, st.frameDark);
                    cv.fill(bx + bs - 1, by0, bx + bs, by0 + bs, st.frameDark);
                } else {
                    // thin light outline
                    cv.fill(bx, by0, bx + bs, by0 + 1, {1, 1, 1, 0.8f});
                    cv.fill(bx, by0 + bs - 1, bx + bs, by0 + bs, {1, 1, 1, 0.8f});
                    cv.fill(bx, by0, bx + 1, by0 + bs, {1, 1, 1, 0.8f});
                    cv.fill(bx + bs - 1, by0, bx + bs, by0 + bs, {1, 1, 1, 0.8f});
                }
                const int g0 = bs / 4, g1 = bs - bs / 4, lw = std::max(1, bs / 8);
                if (k == 0) {  // X
                    for (int d = g0; d < g1; ++d) {
                        cv.fill(bx + d, by0 + d, bx + d + lw, by0 + d + lw, st.buttonGlyph);
                        cv.fill(bx + bs - 1 - d - lw + 1, by0 + d, bx + bs - d, by0 + d + lw, st.buttonGlyph);
                    }
                } else if (k == 1) {  // maximise box
                    cv.fill(bx + g0, by0 + g0, bx + g1, by0 + g0 + lw * 2, st.buttonGlyph);
                    cv.fill(bx + g0, by0 + g1 - lw, bx + g1, by0 + g1, st.buttonGlyph);
                    cv.fill(bx + g0, by0 + g0, bx + g0 + lw, by0 + g1, st.buttonGlyph);
                    cv.fill(bx + g1 - lw, by0 + g0, bx + g1, by0 + g1, st.buttonGlyph);
                } else {  // minimise bar
                    cv.fill(bx + g0, by0 + g1 - lw * 2, bx + g1, by0 + g1, st.buttonGlyph);
                }
                bx -= bs + std::max(1, pad / 2);
            }
            textX0 = tx0 + pad * 2;
            textX1 = bx + bs - pad;
        }

        // Title, truncated with "..." when it doesn't fit
        std::string title = w.title;
        const int avail = textX1 - textX0;
        while (!title.empty() && cv.text(0, 0, title, fpx, st.boldText, st.text, false) > avail) {
            if (title.size() <= 4) { title.clear(); break; }
            title = title.substr(0, title.size() - (title.size() >= 3 && title.compare(title.size() - 3, 3, "...") == 0 ? 4 : 1)) + "...";
        }
        int textX = textX0;
        if (st.macButtons) textX = textX0 + std::max(0, (avail - cv.text(0, 0, title, fpx, st.boldText, st.text, false)) / 2);
        cv.text(textX, textY, title, fpx, st.boldText, st.text);
    }
}

template void render<Px8>(const Settings&, const std::vector<Window>&, const std::vector<const Image<Px8>*>&,
                          const Image<Px8>&, Image<Px8>&);
template void render<Px16>(const Settings&, const std::vector<Window>&, const std::vector<const Image<Px16>*>&,
                           const Image<Px16>&, Image<Px16>&);
template void render<PxF>(const Settings&, const std::vector<Window>&, const std::vector<const Image<PxF>*>&,
                          const Image<PxF>&, Image<PxF>&);

}  // namespace wp
