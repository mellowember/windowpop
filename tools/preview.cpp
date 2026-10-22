// Renders WindowPop over synthetic moving footage to PPM files, without After Effects.
// Usage: preview <outDir> [style 0-3] [look 0-1]
#include "../src/core/WindowPop.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace wp;

static void makeFootage(Image<Px8>& img, double t) {
    // Warm gradient with a bright moving blob, roughly like a lit dancer shot.
    const double bx = 0.5 + 0.25 * std::sin(t * 1.7), by = 0.55 + 0.15 * std::cos(t * 2.3);
    for (int y = 0; y < img.height; ++y) {
        Px8* row = img.row(y);
        for (int x = 0; x < img.width; ++x) {
            const double u = double(x) / img.width, v = double(y) / img.height;
            const double d = std::hypot((u - bx) * 1.6, v - by);
            const double blob = std::exp(-d * d * 30);
            const double stripes = 0.5 + 0.5 * std::sin((u * 12 + v * 4) - t * 3);
            row[x].a = 255;
            row[x].r = static_cast<uint8_t>(std::min(255.0, 70 + 140 * v + 120 * blob));
            row[x].g = static_cast<uint8_t>(std::min(255.0, 30 + 40 * stripes + 200 * blob));
            row[x].b = static_cast<uint8_t>(std::min(255.0, 60 + 80 * (1 - v) + 210 * blob));
        }
    }
}

static void writePPM(const std::string& path, const Image<Px8>& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", img.width, img.height);
    for (int y = 0; y < img.height; ++y) {
        const Px8* row = img.row(y);
        for (int x = 0; x < img.width; ++x) {
            const unsigned char px[3] = {row[x].r, row[x].g, row[x].b};
            std::fwrite(px, 1, 3, f);
        }
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    Settings s;
    if (argc > 2) s.style = static_cast<Style>(std::atoi(argv[2]));
    if (argc > 3) s.look = static_cast<Look>(std::atoi(argv[3]));
    s.focusX = 0.5;
    s.focusY = 0.5;

    const int W = 960, H = 540;
    const double fps = 24;
    auto makeImage = [&](std::vector<Px8>& buf) {
        buf.assign(size_t(W) * H, Px8{});
        Image<Px8> img;
        img.data = buf.data();
        img.width = W;
        img.height = H;
        img.rowBytes = W * sizeof(Px8);
        return img;
    };

    std::vector<Px8> curBuf, outBuf;
    Image<Px8> cur = makeImage(curBuf), out = makeImage(outBuf);

    for (int f : {12, 36, 60}) {
        const double t = f / fps;
        makeFootage(cur, t);
        const auto wins = activeWindows(s, t, W, H);

        // Frozen frames: snap each spawn time to the frame grid, render once per unique frame.
        std::vector<std::vector<Px8>> bufs(wins.size());
        std::vector<Image<Px8>> imgs(wins.size());
        std::vector<const Image<Px8>*> frames(wins.size());
        for (size_t i = 0; i < wins.size(); ++i) {
            imgs[i] = makeImage(bufs[i]);
            makeFootage(imgs[i], std::floor(wins[i].spawnTime * fps) / fps);
            frames[i] = &imgs[i];
        }
        render(s, wins, frames, cur, out);
        const std::string path = outDir + "/frame_" + std::to_string(f) + ".ppm";
        writePPM(path, out);
        std::printf("%s: %zu windows\n", path.c_str(), wins.size());
    }
    return 0;
}
