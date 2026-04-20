// build: g++ BH.cpp version.o -std=c++17 -static -mwindows -lgdi32 -lole32 -lwindowscodecs -o BH.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <random>

static const int SCREEN_W = 960;
static const int SCREEN_H = 720;
static const float FIXED_DT = 1.0f / 60.0f;
static const int MAX_BULLETS = 65535;
static const int MAX_PLAYER_SHOTS = 512;
static const int MAX_LASERS = 128;
static const int MAX_ENEMIES = 64;
static const int MAX_BMP_DIM = 2048;
static const size_t MAX_SCRIPT_LINES = 2048;
static const size_t MAX_PATH_LEN = 260;
static const float PI = 3.1415926535f;
static IWICImagingFactory* gWicFactory = nullptr;

static inline float deg2rad(float d) { return d * (PI / 180.0f); }
static inline float rad2deg(float r) { return r * (180.0f / PI); }
static inline float clampf(float v, float a, float b) { return (v < a) ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return (v < a) ? a : (v > b ? b : v); }
static inline uint32_t rgb(int r, int g, int b) {
    r = clampi(r, 0, 255);
    g = clampi(g, 0, 255);
    b = clampi(b, 0, 255);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};
static inline Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
static inline Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
static inline Vec2 operator*(const Vec2& a, float s) { return { a.x * s, a.y * s }; }
static inline Vec2 operator/(const Vec2& a, float s) { return { a.x / s, a.y / s }; }
static inline float len2(const Vec2& v) { return v.x * v.x + v.y * v.y; }
static inline float len(const Vec2& v) { return std::sqrt(len2(v)); }
static inline Vec2 normalize(const Vec2& v) {
    float l = len(v);
    if (l <= 0.00001f) return { 0.0f, 0.0f };
    return v / l;
}
static inline float dist2(const Vec2& a, const Vec2& b) { return len2(a - b); }

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string toLowerCopy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool safeFileSize(const std::string& path, size_t maxBytes) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto pos = f.tellg();
    if (pos <= 0) return false;
    return (size_t)pos <= maxBytes;
}
static bool toIntSafe(const std::string& s, int& out) {
    try {
        size_t idx = 0;
        long long v = std::stoll(trim(s), &idx, 10);
        if (idx != trim(s).size()) return false;
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
        out = (int)v;
        return true;
    } catch (...) { return false; }
}
static bool toFloatSafe(const std::string& s, float& out) {
    try {
        size_t idx = 0;
        float v = std::stof(trim(s), &idx);
        if (idx != trim(s).size()) return false;
        if (!std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}
static bool parseColorToken(const std::string& s, int& r, int& g, int& b) {
    std::string t = trim(s);
    if (t.empty()) return false;
    if (t[0] == '#') t = t.substr(1);
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
    if (t.find(',') != std::string::npos) {
        char c1 = 0, c2 = 0;
        std::stringstream ss(t);
        int rr = 0, gg = 0, bb = 0;
        if (ss >> rr >> c1 >> gg >> c2 >> bb && c1 == ',' && c2 == ',') {
            r = rr; g = gg; b = bb;
            return true;
        }
        return false;
    }
    if (t.size() == 6) {
        unsigned int v = 0;
        std::stringstream ss;
        ss << std::hex << t;
        ss >> v;
        r = (v >> 16) & 255;
        g = (v >> 8) & 255;
        b = v & 255;
        return true;
    }
    return false;
}
static uint32_t parseColor(const std::string& s, uint32_t fallback) {
    int r = 0, g = 0, b = 0;
    if (!parseColorToken(s, r, g, b)) return fallback;
    return rgb(r, g, b);
}
static std::vector<std::string> splitTokens(const std::string& s) {
    std::stringstream ss(s);
    std::vector<std::string> out;
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}
static bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

static bool isAllowedGamePath(const std::string& p) {
    std::string lower = toLowerCopy(p);
    return startsWith(lower, "stages/") ||
           startsWith(lower, "ply/") ||
           startsWith(lower, "scripts/") ||
           startsWith(lower, "assets/");
}

static bool isSafeRelativePath(std::string p) {
    p = trim(p);
    if (p.empty() || p.size() > MAX_PATH_LEN) return false;
    for (char& ch : p) if (ch == '\\') ch = '/';
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.find(':') != std::string::npos) return false;
    if (p.find("..") != std::string::npos) return false;
    if (p.find("//") != std::string::npos) return false;
    for (unsigned char ch : p) {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/') continue;
        return false;
    }
    if (!isAllowedGamePath(p)) return false;
    return true;
}

#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};
struct BMPInfoHeader {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

struct Sprite {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> px;
    bool empty() const { return w <= 0 || h <= 0 || px.empty(); }
};

static bool loadBMP(const std::string& rawPath, Sprite& out) {
    out = Sprite();
    std::string path = trim(rawPath);
    if (!isSafeRelativePath(path)) return false;
    if (!safeFileSize(path, 64u * 1024u * 1024u)) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    BMPFileHeader fh{};
    BMPInfoHeader ih{};
    f.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    f.read(reinterpret_cast<char*>(&ih), sizeof(ih));
    if (!f || fh.bfType != 0x4D42 || ih.biPlanes != 1) return false;
    if (ih.biCompression != 0) return false;
    if (ih.biBitCount != 24 && ih.biBitCount != 32) return false;

    int w = ih.biWidth;
    int h = std::abs(ih.biHeight);
    bool topDown = ih.biHeight < 0;
    if (w <= 0 || h <= 0 || w > MAX_BMP_DIM || h > MAX_BMP_DIM) return false;

    int bytesPerPixel = ih.biBitCount / 8;
    size_t rowStride = (((size_t)w * (size_t)bytesPerPixel + 3u) / 4u) * 4u;
    if (rowStride == 0 || rowStride > (1u << 26)) return false;

    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * (size_t)h, 0);

    f.seekg((std::streamoff)fh.bfOffBits, std::ios::beg);
    if (!f) return false;

    std::vector<uint8_t> row(rowStride);
    for (int y = 0; y < h; ++y) {
        int dstY = topDown ? y : (h - 1 - y);
        f.read(reinterpret_cast<char*>(row.data()), (std::streamsize)rowStride);
        if (!f) return false;

        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &row[(size_t)x * (size_t)bytesPerPixel];
            uint8_t b = p[0], g = p[1], r = p[2], a = 255;
            if (bytesPerPixel == 4) a = p[3];
            out.px[(size_t)dstY * (size_t)w + (size_t)x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    return true;
}


static bool utf8ToWide(const std::string& s, std::wstring& out) {
    if (s.empty()) { out.clear(); return true; }
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (needed <= 0) return false;
    out.assign((size_t)needed, L' ');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &out[0], needed) > 0;
}

static bool endsWithIgnoreCase(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = (char)std::tolower((unsigned char)s[off + i]);
        char b = (char)std::tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

static bool loadPNG(const std::string& rawPath, Sprite& out) {
    out = Sprite();
    std::string path = trim(rawPath);
    if (!isSafeRelativePath(path)) return false;
    if (!(endsWithIgnoreCase(path, ".png") || endsWithIgnoreCase(path, ".jpg") || endsWithIgnoreCase(path, ".jpeg"))) return false;
    if (!safeFileSize(path, 64u * 1024u * 1024u)) return false;

    std::wstring wpath;
    if (!utf8ToWide(path, wpath)) return false;

    if (!gWicFactory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT hr = S_OK;

    hr = gWicFactory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) return false;

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        return false;
    }

    hr = gWicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        return false;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return false;
    }

    UINT w = 0, h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > (UINT)MAX_BMP_DIM || h > (UINT)MAX_BMP_DIM) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return false;
    }

    const size_t stride = (size_t)w * 4u;
    std::vector<uint8_t> pixels((size_t)h * stride);
    hr = converter->CopyPixels(nullptr, (UINT)stride, (UINT)pixels.size(), pixels.data());
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return false;
    }

    out.w = (int)w;
    out.h = (int)h;
    out.px.assign((size_t)out.w * (size_t)out.h, 0);
    for (int y = 0; y < out.h; ++y) {
        const uint8_t* row = &pixels[(size_t)y * stride];
        for (int x = 0; x < out.w; ++x) {
            const uint8_t* p = &row[(size_t)x * 4u];
            out.px[(size_t)y * (size_t)out.w + (size_t)x] = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    return true;
}

static bool loadSpriteFile(const std::string& path, Sprite& out) {
    if (endsWithIgnoreCase(path, ".png") || endsWithIgnoreCase(path, ".jpg") || endsWithIgnoreCase(path, ".jpeg")) return loadPNG(path, out);
    if (endsWithIgnoreCase(path, ".bmp")) return loadBMP(path, out);
    return loadBMP(path, out);
}

struct FrameBuffer {
    std::vector<uint32_t> px;
    BITMAPINFO bmi{};

    void init() {
        px.assign((size_t)SCREEN_W * (size_t)SCREEN_H, 0);
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = SCREEN_W;
        bmi.bmiHeader.biHeight = -SCREEN_H;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }

    void clear(uint32_t c) { std::fill(px.begin(), px.end(), c); }

    inline bool inside(int x, int y) const { return x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H; }

    inline void put(int x, int y, uint32_t c) {
        if (!inside(x, y)) return;
        px[(size_t)y * (size_t)SCREEN_W + (size_t)x] = c;
    }

    inline void blend(int x, int y, uint32_t srcBGRA) {
        if (!inside(x, y)) return;
        uint8_t sa = (uint8_t)((srcBGRA >> 24) & 255);
        if (sa == 0) return;

        uint32_t& dst = px[(size_t)y * (size_t)SCREEN_W + (size_t)x];
        if (sa == 255) {
            dst = (((srcBGRA >> 16) & 255) << 16) | (((srcBGRA >> 8) & 255) << 8) | (srcBGRA & 255);
            return;
        }

        float a = sa / 255.0f;
        uint8_t sr = (uint8_t)((srcBGRA >> 16) & 255);
        uint8_t sg = (uint8_t)((srcBGRA >> 8) & 255);
        uint8_t sb = (uint8_t)(srcBGRA & 255);

        uint8_t dr = (uint8_t)((dst >> 16) & 255);
        uint8_t dg = (uint8_t)((dst >> 8) & 255);
        uint8_t db = (uint8_t)(dst & 255);

        int rr = (int)(dr + (sr - dr) * a);
        int rg = (int)(dg + (sg - dg) * a);
        int rb = (int)(db + (sb - db) * a);
        dst = rgb(rr, rg, rb);
    }

    void rect(int x, int y, int w, int h, uint32_t c) {
        int x0 = std::max(0, x);
        int y0 = std::max(0, y);
        int x1 = std::min(SCREEN_W, x + w);
        int y1 = std::min(SCREEN_H, y + h);
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx)
                put(xx, yy, c);
    }

    void line(int x0, int y0, int x1, int y1, uint32_t c) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            put(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void circle(int cx, int cy, int r, uint32_t c, bool filled = true) {
        if (r <= 0) return;
        int rr = r * r;
        for (int y = -r; y <= r; ++y) {
            for (int x = -r; x <= r; ++x) {
                int d = x * x + y * y;
                if (filled) {
                    if (d <= rr) put(cx + x, cy + y, c);
                } else {
                    if (std::abs(d - rr) <= r) put(cx + x, cy + y, c);
                }
            }
        }
    }

    void diamond(int cx, int cy, int r, uint32_t c, bool filled = true) {
        if (r <= 0) return;
        for (int y = -r; y <= r; ++y) {
            int span = r - std::abs(y);
            if (filled) {
                for (int x = -span; x <= span; ++x) put(cx + x, cy + y, c);
            } else {
                put(cx - span, cy + y, c);
                put(cx + span, cy + y, c);
            }
        }
    }

    void blit(const Sprite& s, int cx, int cy, bool centered = true) {
        if (s.empty()) return;
        int ox = centered ? (cx - s.w / 2) : cx;
        int oy = centered ? (cy - s.h / 2) : cy;
        for (int y = 0; y < s.h; ++y) {
            for (int x = 0; x < s.w; ++x) {
                uint32_t p = s.px[(size_t)y * (size_t)s.w + (size_t)x];
                blend(ox + x, oy + y, p);
            }
        }
    }

    void blitTiled(const Sprite& s) {
        if (s.empty()) return;
        for (int y = 0; y < SCREEN_H; ++y) {
            int sy = y % s.h;
            for (int x = 0; x < SCREEN_W; ++x) {
                int sx = x % s.w;
                uint32_t p = s.px[(size_t)sy * (size_t)s.w + (size_t)sx];
                blend(x, y, p);
            }
        }
    }

    void present(HWND hwnd, const std::string& hud) {
        if (!hwnd) return;
        HDC dc = GetDC(hwnd);
        if (!dc) return;
        StretchDIBits(dc, 0, 0, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H, px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 10, 10, hud.c_str(), (int)hud.size());
        ReleaseDC(hwnd, dc);
    }
};
static FrameBuffer gFrame;

static float pointSegmentDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    float ab2 = len2(ab);
    if (ab2 <= 0.00001f) return len(p - a);
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / ab2;
    t = clampf(t, 0.0f, 1.0f);
    Vec2 q = { a.x + ab.x * t, a.y + ab.y * t };
    return len(p - q);
}
static std::string baseNameFromPath(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

struct PowerProfile {
    int level = 0;
    std::string name = "Ply";
    std::string playerSpritePath;
    std::string bombSpritePath;
    std::string shotSpritePath;
    std::string bombShape = "ring";
    uint32_t shotColor = rgb(255, 220, 120);
    uint32_t bombColor = rgb(100, 220, 255);
    int fireCooldown = 6;
    int shotCount = 2;
    float shotSpeed = 12.0f;
    float shotSpreadDeg = 10.0f;
    float shotDamage = 1.0f;
    float hitRadius = 4.0f;
    float grazeRadius = 16.0f;
    int bombRadius = 96;
    int bombDuration = 60;
    int bombInvuln = 120;
    int bombClearRadius = 120;
};

static bool loadPowerProfile(int level, PowerProfile& out) {
    out = PowerProfile();
    out.level = level;
    std::ostringstream oss;
    oss << "ply/power" << level << "/config.txt";
    std::string path = oss.str();
    if (!isSafeRelativePath(path)) return false;
    if (!safeFileSize(path, 1024u * 1024u)) return false;
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));

        if (key == "name") out.name = val;
        else if (key == "player_sprite") { if (isSafeRelativePath(val)) out.playerSpritePath = val; }
        else if (key == "bomb_sprite") { if (isSafeRelativePath(val)) out.bombSpritePath = val; }
        else if (key == "shot_sprite") { if (isSafeRelativePath(val)) out.shotSpritePath = val; }
        else if (key == "bomb_shape") out.bombShape = val;
        else if (key == "shot_color") out.shotColor = parseColor(val, out.shotColor);
        else if (key == "bomb_color") out.bombColor = parseColor(val, out.bombColor);
        else if (key == "fire_cooldown") { int v; if (toIntSafe(val, v)) out.fireCooldown = std::max(1, v); }
        else if (key == "shot_count") { int v; if (toIntSafe(val, v)) out.shotCount = std::max(1, v); }
        else if (key == "shot_speed") { float v; if (toFloatSafe(val, v)) out.shotSpeed = v; }
        else if (key == "shot_spread_deg") { float v; if (toFloatSafe(val, v)) out.shotSpreadDeg = v; }
        else if (key == "shot_damage") { float v; if (toFloatSafe(val, v)) out.shotDamage = v; }
        else if (key == "hit_radius") { float v; if (toFloatSafe(val, v)) out.hitRadius = v; }
        else if (key == "graze_radius") { float v; if (toFloatSafe(val, v)) out.grazeRadius = v; }
        else if (key == "bomb_radius") { int v; if (toIntSafe(val, v)) out.bombRadius = std::max(16, v); }
        else if (key == "bomb_duration") { int v; if (toIntSafe(val, v)) out.bombDuration = std::max(1, v); }
        else if (key == "bomb_invuln") { int v; if (toIntSafe(val, v)) out.bombInvuln = std::max(1, v); }
        else if (key == "bomb_clear_radius") { int v; if (toIntSafe(val, v)) out.bombClearRadius = std::max(16, v); }
    }
    return true;
}

static inline bool isIdentStart(char c) {
    return std::isalpha((unsigned char)c) || c == '_';
}

static inline bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

struct PlayerShot {
    Vec2 pos;
    Vec2 vel;
    float radius = 4.0f;
    float damage = 1.0f;
    bool alive = false;
};
struct Bullet {
    Vec2 pos;
    Vec2 vel;
    float radius = 5.0f;
    uint32_t color = rgb(255, 100, 80);
    bool grazed = false;
    bool alive = false;
};
struct Laser {
    Vec2 origin;
    float angle = 0.0f;
    float growSpeed = 8.0f;
    float maxLength = 200.0f;
    float width = 8.0f;
    int life = 60;
    float currentLength = 0.0f;
    uint32_t color = rgb(120, 255, 120);
    bool grazed = false;
    bool alive = false;
};

struct PatternSystemRuntimeView {
    Vec2 enemyPos{ 0.0f, 0.0f };
    Vec2 playerPos{ 0.0f, 0.0f };
    float hp = 1.0f;
    float maxHp = 1.0f;
    int phase = 0;
    float time = 0.0f;
    bool playerSlow = false;
    bool playerInvuln = false;
    int playerBombTimer = 0;
    int playerLives = 3;
    int playerBombs = 3;
    int score = 0;
    int graze = 0;
    float difficulty = 1.0f;
};

struct BulletSpec {
    Vec2 pos;
    Vec2 vel;
    float radius = 5.0f;
    uint32_t color = rgb(255, 100, 80);
};
struct LaserSpec {
    Vec2 origin;
    float angleDeg = 0.0f;
    float growSpeed = 8.0f;
    float maxLength = 200.0f;
    int duration = 60;
    float width = 8.0f;
    uint32_t color = rgb(120, 255, 120);
};
struct PatternSystemCallbacks {
    std::function<void(const BulletSpec&)> emitBullet;
    std::function<void(const LaserSpec&)> emitLaser;
    std::function<void(const Vec2&, float)> moveEnemy;
};

struct ExprEnv {
    std::function<float(const std::string&)> resolveVar;
    std::function<float(const std::string&, const std::vector<float>&)> callFunc;
};
struct ExprNode {
    virtual ~ExprNode() = default;
    virtual float eval(const ExprEnv& env) const = 0;
};
struct NumberNode final : ExprNode {
    float value;
    explicit NumberNode(float v) : value(v) {}
    float eval(const ExprEnv&) const override { return value; }
};
struct VarNode final : ExprNode {
    std::string name;
    explicit VarNode(std::string n) : name(std::move(n)) {}
    float eval(const ExprEnv& env) const override { return env.resolveVar ? env.resolveVar(name) : 0.0f; }
};
struct UnaryNode final : ExprNode {
    char op;
    std::unique_ptr<ExprNode> expr;
    UnaryNode(char o, std::unique_ptr<ExprNode> e) : op(o), expr(std::move(e)) {}
    float eval(const ExprEnv& env) const override {
        float v = expr ? expr->eval(env) : 0.0f;
        switch (op) {
        case '+': return v;
        case '-': return -v;
        case '!': return (v == 0.0f) ? 1.0f : 0.0f;
        default: return v;
        }
    }
};
struct BinaryNode final : ExprNode {
    char op;
    std::unique_ptr<ExprNode> lhs, rhs;
    BinaryNode(char o, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r) : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
    float eval(const ExprEnv& env) const override {
        float a = lhs ? lhs->eval(env) : 0.0f;
        float b = rhs ? rhs->eval(env) : 0.0f;
        switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return (std::fabs(b) <= 1e-8f) ? 0.0f : a / b;
        case '%': return (std::fabs(b) <= 1e-8f) ? 0.0f : std::fmod(a, b);
        case '^': return std::pow(a, b);
        case '<': return (a < b) ? 1.0f : 0.0f;
        case '>': return (a > b) ? 1.0f : 0.0f;
        case 'L': return (a <= b) ? 1.0f : 0.0f;
        case 'G': return (a >= b) ? 1.0f : 0.0f;
        case 'E': return (a == b) ? 1.0f : 0.0f;
        case 'N': return (a != b) ? 1.0f : 0.0f;
        case '&': return (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f;
        case '|': return (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f;
        default: return 0.0f;
        }
    }
};
struct FuncNode final : ExprNode {
    std::string name;
    std::vector<std::unique_ptr<ExprNode>> args;
    FuncNode(std::string n, std::vector<std::unique_ptr<ExprNode>> a) : name(std::move(n)), args(std::move(a)) {}
    float eval(const ExprEnv& env) const override {
        std::vector<float> values;
        values.reserve(args.size());
        for (const auto& a : args) values.push_back(a ? a->eval(env) : 0.0f);
        return env.callFunc ? env.callFunc(name, values) : 0.0f;
    }
};

struct Expression {
    std::unique_ptr<ExprNode> root;
    Expression() = default;
    explicit Expression(std::unique_ptr<ExprNode> n) : root(std::move(n)) {}
    bool valid() const { return (bool)root; }
    float eval(const ExprEnv& env) const { return root ? root->eval(env) : 0.0f; }
};

class ExprParser {
public:
    explicit ExprParser(std::string src) : s(std::move(src)) {}
    Expression parse() {
        pos = 0;
        auto node = parseOr();
        skipSpaces();
        if (pos != s.size()) return Expression();
        return Expression(std::move(node));
    }
private:
    std::string s;
    size_t pos = 0;

    void skipSpaces() { while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos; }
    bool consume(const std::string& t) {
        skipSpaces();
        if (s.compare(pos, t.size(), t) == 0) {
            pos += t.size();
            return true;
        }
        return false;
    }

    std::unique_ptr<ExprNode> parseOr() {
        auto lhs = parseAnd();
        while (true) {
            skipSpaces();
            if (consume("||")) {
                auto rhs = parseAnd();
                lhs = std::make_unique<BinaryNode>('|', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseAnd() {
        auto lhs = parseEq();
        while (true) {
            skipSpaces();
            if (consume("&&")) {
                auto rhs = parseEq();
                lhs = std::make_unique<BinaryNode>('&', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseEq() {
        auto lhs = parseRel();
        while (true) {
            skipSpaces();
            if (consume("==")) {
                auto rhs = parseRel();
                lhs = std::make_unique<BinaryNode>('E', std::move(lhs), std::move(rhs));
            } else if (consume("!=")) {
                auto rhs = parseRel();
                lhs = std::make_unique<BinaryNode>('N', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseRel() {
        auto lhs = parseAdd();
        while (true) {
            skipSpaces();
            if (consume("<=")) {
                auto rhs = parseAdd();
                lhs = std::make_unique<BinaryNode>('L', std::move(lhs), std::move(rhs));
            } else if (consume(">=")) {
                auto rhs = parseAdd();
                lhs = std::make_unique<BinaryNode>('G', std::move(lhs), std::move(rhs));
            } else if (consume("<")) {
                auto rhs = parseAdd();
                lhs = std::make_unique<BinaryNode>('<', std::move(lhs), std::move(rhs));
            } else if (consume(">")) {
                auto rhs = parseAdd();
                lhs = std::make_unique<BinaryNode>('>', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseAdd() {
        auto lhs = parseMul();
        while (true) {
            skipSpaces();
            if (consume("+")) {
                auto rhs = parseMul();
                lhs = std::make_unique<BinaryNode>('+', std::move(lhs), std::move(rhs));
            } else if (consume("-")) {
                auto rhs = parseMul();
                lhs = std::make_unique<BinaryNode>('-', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseMul() {
        auto lhs = parsePow();
        while (true) {
            skipSpaces();
            if (consume("*")) {
                auto rhs = parsePow();
                lhs = std::make_unique<BinaryNode>('*', std::move(lhs), std::move(rhs));
            } else if (consume("/")) {
                auto rhs = parsePow();
                lhs = std::make_unique<BinaryNode>('/', std::move(lhs), std::move(rhs));
            } else if (consume("%")) {
                auto rhs = parsePow();
                lhs = std::make_unique<BinaryNode>('%', std::move(lhs), std::move(rhs));
            } else break;
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parsePow() {
        auto lhs = parseUnary();
        skipSpaces();
        if (consume("^")) {
            auto rhs = parsePow();
            lhs = std::make_unique<BinaryNode>('^', std::move(lhs), std::move(rhs));
        }
        return lhs;
    }
    std::unique_ptr<ExprNode> parseUnary() {
        skipSpaces();
        if (consume("+")) return std::make_unique<UnaryNode>('+', parseUnary());
        if (consume("-")) return std::make_unique<UnaryNode>('-', parseUnary());
        if (consume("!")) return std::make_unique<UnaryNode>('!', parseUnary());
        return parsePrimary();
    }
    std::unique_ptr<ExprNode> parsePrimary() {
        skipSpaces();
        if (pos >= s.size()) return nullptr;
        if (s[pos] == '(') {
            ++pos;
            auto node = parseOr();
            skipSpaces();
            if (pos < s.size() && s[pos] == ')') ++pos;
            return node;
        }
        if (std::isdigit((unsigned char)s[pos]) || s[pos] == '.') {
            const char* begin = s.c_str() + pos;
            char* end = nullptr;
            double v = std::strtod(begin, &end);
            if (end == begin) return nullptr;
            pos += (size_t)(end - begin);
            return std::make_unique<NumberNode>((float)v);
        }
        if (isIdentStart(s[pos])) {
            std::string name = parseQualifiedIdentifier();
            skipSpaces();
            if (pos < s.size() && s[pos] == '(') {
                ++pos;
                std::vector<std::unique_ptr<ExprNode>> args;
                skipSpaces();
                if (pos < s.size() && s[pos] != ')') {
                    while (true) {
                        auto arg = parseOr();
                        if (!arg) return nullptr;
                        args.push_back(std::move(arg));
                        skipSpaces();
                        if (pos < s.size() && s[pos] == ',') {
                            ++pos;
                            continue;
                        }
                        break;
                    }
                }
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') ++pos;
                return std::make_unique<FuncNode>(name, std::move(args));
            }
            return std::make_unique<VarNode>(name);
        }
        return nullptr;
    }
    std::string parseIdentifier() {
        skipSpaces();
        size_t start = pos;
        while (pos < s.size() && isIdentChar(s[pos])) ++pos;
        return s.substr(start, pos - start);
    }

    std::string parseQualifiedIdentifier() {
        skipSpaces();
        size_t start = pos;
        if (pos >= s.size() || !isIdentStart(s[pos])) return "";
        while (pos < s.size() && isIdentChar(s[pos])) ++pos;

        while (pos + 1 < s.size() && s[pos] == '.' && isIdentStart(s[pos + 1])) {
            ++pos;
            while (pos < s.size() && isIdentChar(s[pos])) ++pos;
        }
        return s.substr(start, pos - start);
    }
};

class TouhouPatternSystemEngine {
public:

    enum class CmdKind {
        Nop, Wait, WaitRand, Set, Add, Sub, Mul, Div, RandSet,
        Move, MoveAuto,
        ShotCircle, ShotFan, ShotAimed, ShotSpiral, Laser,
        If, EndIf,
        Loop, EndLoop, For, EndFor,
        Label, Goto, Call, Return, Break, Continue, Jump, End
    };

    struct Command {
        CmdKind kind = CmdKind::Nop;
        std::vector<std::string> args;
        size_t line = 0;
    };

    struct LoopFrame {
        enum class Kind { Repeat, For } kind = Kind::Repeat;
        size_t startPc = 0;
        size_t endPc = 0;
        bool infinite = false;
        int remain = 0;
        std::string varName;
        float cur = 0.0f;
        float end = 0.0f;
        float step = 1.0f;
    };

    struct Options {
        int instructionsPerTick = 96;
        int maxLoopDepth = 64;
        int maxCallDepth = 64;
    };

    TouhouPatternSystemEngine() { patternRng.seed(std::random_device{}()); }

    void resetRuntime() {
        pc_ = 0;

        loopStack_.clear();
        callStack_.clear();
        vars_.clear();

        finished_ = false;
        enabled_ = false;
    }
    void resetAll() {
        program_.clear();
        labels_.clear();
        resetRuntime();
    }
    void enable() {
        if (!loaded_) return;
        resetRuntime();
        enabled_ = true;
        finished_ = false;
    }
    bool isEnabled() const { return enabled_; }

    void disable() {
        enabled_ = false;
    }

    bool ready() const {
        return loaded_ && enabled_ && !finished_;
    }

    bool finished() const {
        return finished_;
    }

    void setVar(const std::string& name, float v) { vars_[name] = v; }

    float getVar(const std::string& name, const PatternSystemRuntimeView& view) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) return it->second;
        return builtinVar(name, view);
    }

    bool loadFromString(const std::string& script) {
        resetAll();
        std::stringstream ss(script);
        std::string line;
        size_t lineNo = 0;
        while (std::getline(ss, line)) {
            ++lineNo;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            Command cmd;
            if (!parseCommand(line, lineNo, cmd)) continue;
            if (cmd.kind == CmdKind::Label && !cmd.args.empty()) {
                if (!labels_.count(cmd.args[0])) labels_[cmd.args[0]] = program_.size();
            }
            program_.push_back(std::move(cmd));
        }
        loaded_ = !program_.empty();
        enabled_ = false;
        finished_ = false;
        pc_ = 0;
        wait_ = 0;
        return loaded_;
    }

    bool loadFromFile(const std::string& rawPath) {
        resetAll();
        std::string path = trim(rawPath);
        if (!isSafeRelativePath(path)) return false;
        if (!safeFileSize(path, 1024u * 1024u)) return false;
        std::ifstream f(path);
        if (!f) return false;
        std::stringstream buffer;
        buffer << f.rdbuf();
        return loadFromString(buffer.str());
    }

    void tick(const PatternSystemRuntimeView& view, const PatternSystemCallbacks& cb, const Options& opt) {
        if (!ready()) return;
        view_ = view;
        int budget = std::max(1, opt.instructionsPerTick);
        for (int steps = 0; enabled_ && !finished_ && steps < budget; ++steps) {
            if (wait_ > 0) { --wait_; break; }
            if (pc_ >= program_.size()) { finished_ = true; break; }
            execute(program_[pc_], cb, opt);
            if (finished_) break;
        }
    }

private:
    static constexpr size_t npos = (size_t)-1;

    std::vector<Command> program_;
    std::unordered_map<std::string, size_t> labels_;
    std::unordered_map<std::string, float> vars_;
    std::vector<size_t> callStack_;
    std::vector<LoopFrame> loopStack_;
    PatternSystemRuntimeView view_{};
    size_t pc_ = 0;
    int wait_ = 0;
    bool loaded_ = false;
    bool enabled_ = false;
    bool finished_ = false;
    float spiralPhase_ = 0.0f;
    std::mt19937 patternRng;

    static bool isIdentStart(char c) {
        return std::isalpha((unsigned char)c) || c == '_';
    }
    static bool isIdentChar(char c) {
        return std::isalnum((unsigned char)c) || c == '_';
    }

    float randomFloat(float a, float b) {
        if (a > b) std::swap(a, b);
        std::uniform_real_distribution<float> dist(a, b);
        return dist(patternRng);
    }

    float builtinVar(const std::string& rawName, const PatternSystemRuntimeView& view) const {
        std::string name = toLowerCopy(trim(rawName));
        if (startsWith(name, "status.")) name = name.substr(7);
        if (startsWith(name, "math.")) name = name.substr(5);

        if (name == "x" || name == "enemyx" || name == "enemy_x" || name == "posx" || name == "px") return view.enemyPos.x;
        if (name == "y" || name == "enemyy" || name == "enemy_y" || name == "posy" || name == "py") return view.enemyPos.y;
        if (name == "playerx" || name == "player_x") return view.playerPos.x;
        if (name == "playery" || name == "player_y") return view.playerPos.y;
        if (name == "hp") return view.hp;
        if (name == "maxhp") return view.maxHp;
        if (name == "phase") return (float)view.phase;
        if (name == "time") return view.time;
        if (name == "slow") return view.playerSlow ? 1.0f : 0.0f;
        if (name == "invuln") return view.playerInvuln ? 1.0f : 0.0f;
        if (name == "bombtimer") return (float)view.playerBombTimer;
        if (name == "lives") return (float)view.playerLives;
        if (name == "bombs") return (float)view.playerBombs;
        if (name == "score") return (float)view.score;
        if (name == "graze") return (float)view.graze;
        if (name == "difficulty") return view.difficulty;
        if (name == "pi") return PI;
        if (name == "tau") return PI * 2.0f;
        return 0.0f;
    }

    float resolveVar(const std::string& name, const PatternSystemRuntimeView& view) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) return it->second;

        std::string lower = toLowerCopy(trim(name));
        if (startsWith(lower, "status.") || startsWith(lower, "math.")) {
            size_t dot = name.find('.');
            if (dot != std::string::npos && dot + 1 < name.size()) {
                std::string stripped = name.substr(dot + 1);
                auto it2 = vars_.find(stripped);
                if (it2 != vars_.end()) return it2->second;
            }
        }

        return builtinVar(name, view);
    }

    float builtinFunc(const std::string& rawName, const std::vector<float>& args) {
        std::string name = toLowerCopy(trim(rawName));
        if (startsWith(name, "math.")) name = name.substr(5);
        if (startsWith(name, "status.")) name = name.substr(7);
        if (name == "random") name = "rand";

        if (name == "rand") {
            if (args.empty()) return randomFloat(0.0f, 1.0f);
            if (args.size() == 1) return randomFloat(0.0f, args[0]);
            return randomFloat(args[0], args[1]);
        }
        if (name == "abs") return args.empty() ? 0.0f : std::fabs(args[0]);
        if (name == "min") {
            if (args.empty()) return 0.0f;
            float v = args[0];
            for (size_t i = 1; i < args.size(); ++i) v = std::min(v, args[i]);
            return v;
        }
        if (name == "max") {
            if (args.empty()) return 0.0f;
            float v = args[0];
            for (size_t i = 1; i < args.size(); ++i) v = std::max(v, args[i]);
            return v;
        }
        if (name == "clamp") {
            if (args.size() < 3) return args.empty() ? 0.0f : args[0];
            return clampf(args[0], args[1], args[2]);
        }
        if (name == "clamp01") return args.empty() ? 0.0f : clampf(args[0], 0.0f, 1.0f);
        if (name == "lerp") {
            if (args.size() < 3) return 0.0f;
            return args[0] + (args[1] - args[0]) * args[2];
        }
        if (name == "sin") return args.empty() ? 0.0f : std::sin(args[0]);
        if (name == "cos") return args.empty() ? 0.0f : std::cos(args[0]);
        if (name == "tan") return args.empty() ? 0.0f : std::tan(args[0]);
        if (name == "asin") return args.empty() ? 0.0f : std::asin(args[0]);
        if (name == "acos") return args.empty() ? 0.0f : std::acos(args[0]);
        if (name == "atan") return args.empty() ? 0.0f : std::atan(args[0]);
        if (name == "atan2") {
            if (args.size() < 2) return 0.0f;
            return std::atan2(args[0], args[1]);
        }
        if (name == "sqrt") return args.empty() ? 0.0f : std::sqrt(std::max(0.0f, args[0]));
        if (name == "floor") return args.empty() ? 0.0f : std::floor(args[0]);
        if (name == "ceil") return args.empty() ? 0.0f : std::ceil(args[0]);
        if (name == "sign") {
            if (args.empty()) return 0.0f;
            return (args[0] > 0.0f) ? 1.0f : (args[0] < 0.0f ? -1.0f : 0.0f);
        }
        if (name == "dist") {
            if (args.size() < 4) return 0.0f;
            float dx = args[2] - args[0];
            float dy = args[3] - args[1];
            return std::sqrt(dx * dx + dy * dy);
        }
        if (name == "angle") {
            if (args.size() < 4) return 0.0f;
            return rad2deg(std::atan2(args[3] - args[1], args[2] - args[0]));
        }
        return 0.0f;
    }

    Expression parseExpr(const std::string& s) { return ExprParser(s).parse(); }

    float evalExpr(const std::string& s) {
        Expression e = parseExpr(s);
        ExprEnv env;
        env.resolveVar = [&](const std::string& n) -> float { return resolveVar(n, view_); };
        env.callFunc = [&](const std::string& n, const std::vector<float>& a) -> float { return builtinFunc(n, a); };
        return e.valid() ? e.eval(env) : 0.0f;
    }
    bool evalBool(const std::string& s) { return evalExpr(s) != 0.0f; }

    static std::string joinTokens(const std::vector<std::string>& t, size_t start) {
        std::string out;
        for (size_t i = start; i < t.size(); ++i) {
            if (i > start) out.push_back(' ');
            out += t[i];
        }
        return out;
    }


    static bool looksLikeColorToken(const std::string& s) {
        std::string t = trim(s);
        if (t.empty()) return false;
        if (t[0] == '#' || t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) return true;
        if (t.find(',') != std::string::npos) return true;
        if (t.size() == 6) {
            for (char ch : t) {
                if (!std::isxdigit((unsigned char)ch)) return false;
            }
            return true;
        }
        return false;
    }

    static bool tryParseColor(const std::string& s, uint32_t& out) {
        int r = 0, g = 0, b = 0;
        if (!parseColorToken(s, r, g, b)) return false;
        out = rgb(r, g, b);
        return true;
    }

    static void parseOffsetColorTail(const std::vector<std::string>& args, size_t start,
                                     bool allowOffset, float& offsetDeg, uint32_t& color) {
        bool offsetSet = false;
        bool colorSet = false;
        for (size_t i = start; i < args.size(); ++i) {
            std::string key = toLowerCopy(trim(args[i]));
            if (allowOffset && (key == "offset" || key == "off" || key == "angle") && i + 1 < args.size()) {
                float v = 0.0f;
                if (toFloatSafe(args[i + 1], v)) {
                    offsetDeg = v;
                    offsetSet = true;
                    ++i;
                    continue;
                }
            }
            if ((key == "color" || key == "colour") && i + 1 < args.size()) {
                uint32_t c = color;
                if (tryParseColor(args[i + 1], c)) {
                    color = c;
                    colorSet = true;
                    ++i;
                    continue;
                }
            }

            if (allowOffset && !offsetSet && !looksLikeColorToken(args[i])) {
                float v = 0.0f;
                if (toFloatSafe(args[i], v)) {
                    offsetDeg = v;
                    offsetSet = true;
                    continue;
                }
            }
            if (!colorSet && looksLikeColorToken(args[i])) {
                uint32_t c = color;
                if (tryParseColor(args[i], c)) {
                    color = c;
                    colorSet = true;
                    continue;
                }
            }
        }
    }

    void adjustSpiralPhase(float step) {
        spiralPhase_ += step;
        if (spiralPhase_ > 2.0f * PI || spiralPhase_ < -2.0f * PI) {
            spiralPhase_ = std::fmod(spiralPhase_, 2.0f * PI);
        }
    }

    static bool parseCommand(const std::string& line, size_t lineNo, Command& out) {
        out = Command{};
        out.line = lineNo;
        auto t = splitTokens(line);
        if (t.empty()) return false;
        std::string c = toLowerCopy(t[0]);

        auto exprFrom = [&](size_t idx) -> std::string { return joinTokens(t, idx); };

        if (c == "wait" && t.size() >= 2) { out.kind = CmdKind::Wait; out.args = { exprFrom(1) }; return true; }
        if (c == "waitrand" && t.size() >= 3) { out.kind = CmdKind::WaitRand; out.args = { exprFrom(1), exprFrom(2) }; return true; }
        if (c == "set" && t.size() >= 3) { out.kind = CmdKind::Set; out.args = { t[1], exprFrom(2) }; return true; }
        if (c == "add" && t.size() >= 3) { out.kind = CmdKind::Add; out.args = { t[1], exprFrom(2) }; return true; }
        if (c == "sub" && t.size() >= 3) { out.kind = CmdKind::Sub; out.args = { t[1], exprFrom(2) }; return true; }
        if (c == "mul" && t.size() >= 3) { out.kind = CmdKind::Mul; out.args = { t[1], exprFrom(2) }; return true; }
        if (c == "div" && t.size() >= 3) { out.kind = CmdKind::Div; out.args = { t[1], exprFrom(2) }; return true; }
        if (c == "randset" && t.size() >= 4) { out.kind = CmdKind::RandSet; out.args = { t[1], exprFrom(2), exprFrom(3) }; return true; }
        if (c == "endif") { out.kind = CmdKind::EndIf; return true; }

        if (c == "move" && t.size() >= 2) {
            if (toLowerCopy(t[1]) == "auto") {
                out.kind = CmdKind::MoveAuto;
                out.args.push_back((t.size() >= 3) ? t[2] : "2");
                out.args.push_back((t.size() >= 4) ? t[3] : "220");
                return true;
            }
            if (t.size() >= 4) {
                out.kind = CmdKind::Move;
                out.args = { t[1], t[2], t[3] };
                return true;
            }
        }

        if (c == "shot" && t.size() >= 2) {
            std::string sub = toLowerCopy(t[1]);
            if ((sub == "circle" || sub == "ring" || sub == "burst") && t.size() >= 4) {
                out.kind = CmdKind::ShotCircle;
                out.args = { t[2], t[3] };
                if (t.size() >= 5) out.args.push_back(t[4]);
                return true;
            }
            if (sub == "fan" && t.size() >= 5) {
                out.kind = CmdKind::ShotFan;
                out.args = { t[2], t[3], t[4] };
                for (size_t i = 5; i < t.size(); ++i) out.args.push_back(t[i]);
                return true;
            }
            if (sub == "aimed" && t.size() >= 3) {
                out.kind = CmdKind::ShotAimed;
                out.args.push_back(t[2]);
                for (size_t i = 3; i < t.size(); ++i) out.args.push_back(t[i]);
                return true;
            }
            if (sub == "spiral" && t.size() >= 5) {
                out.kind = CmdKind::ShotSpiral;
                out.args = { t[2], t[3], t[4] };
                if (t.size() >= 6) out.args.push_back(t[5]);
                return true;
            }
        }

        if (c == "laser" && t.size() >= 2) {
            std::string sub = toLowerCopy(t[1]);
            if (sub == "aimed" && t.size() >= 6) {
                out.kind = CmdKind::Laser;
                out.args.push_back("aimed");
                out.args.push_back(t[2]);
                out.args.push_back(t[3]);
                out.args.push_back(t[4]);
                out.args.push_back(t[5]);
                for (size_t i = 6; i < t.size(); ++i) out.args.push_back(t[i]);
                return true;
            }
            if (t.size() >= 6) {
                out.kind = CmdKind::Laser;
                out.args = { t[1], t[2], t[3], t[4], t[5] };
                if (t.size() >= 7) out.args.push_back(t[6]);
                return true;
            }
        }

        if (c == "if" && t.size() >= 2) { out.kind = CmdKind::If; out.args = { exprFrom(1) }; return true; }
        if (c == "loop" && t.size() >= 2) { out.kind = CmdKind::Loop; out.args = { exprFrom(1) }; return true; }
        if (c == "endloop") { out.kind = CmdKind::EndLoop; return true; }
        if (c == "for" && t.size() >= 4) { out.kind = CmdKind::For; out.args = { t[1], t[2], t[3] }; if (t.size() >= 5) out.args.push_back(t[4]); return true; }
        if (c == "endfor") { out.kind = CmdKind::EndFor; return true; }
        if (c == "label" && t.size() >= 2) { out.kind = CmdKind::Label; out.args = { t[1] }; return true; }
        if (c == "goto" && t.size() >= 2) { out.kind = CmdKind::Goto; out.args = { t[1] }; return true; }
        if (c == "call" && t.size() >= 2) { out.kind = CmdKind::Call; out.args = { t[1] }; return true; }
        if (c == "return") { out.kind = CmdKind::Return; return true; }
        if (c == "break") { out.kind = CmdKind::Break; return true; }
        if (c == "continue") { out.kind = CmdKind::Continue; return true; }
        if (c == "jump" && t.size() >= 2) { out.kind = CmdKind::Jump; out.args = { exprFrom(1) }; return true; }
        if (c == "end") { out.kind = CmdKind::End; return true; }
        return false;
    }

    size_t findMatchingEnd(size_t startPc, CmdKind beginKind, CmdKind endKind) const {
        int depth = 1;

        for (size_t i = startPc + 1; i < program_.size(); ++i) {
            if (program_[i].kind == beginKind) depth++;
            else if (program_[i].kind == endKind) {
                depth--;
                if (depth == 0) return i;
            }
        }

        return npos;
    }

    Vec2 chooseAutoMoveTarget(const PatternSystemRuntimeView& view, float distance) const {
        distance = clampf(distance, 120.0f, 360.0f);
        if (view.playerSlow) distance += 40.0f;
        if (view.playerBombTimer > 0 || view.playerInvuln) distance += 60.0f;
        if (view.playerLives <= 1) distance += 30.0f;
        if (view.playerBombs <= 0) distance += 20.0f;

        static const float angles[] = { 225.0f, 245.0f, 270.0f, 295.0f, 315.0f };
        Vec2 best = view.enemyPos;
        float bestScore = -1.0e30f;

        for (float ad : angles) {
            float a = deg2rad(ad);
            Vec2 cand{
                view.playerPos.x + std::cos(a) * distance,
                view.playerPos.y + std::sin(a) * distance
            };
            cand.x = clampf(cand.x, 32.0f, SCREEN_W - 32.0f);
            float safeTop = view.playerPos.y - 48.0f;
            if (safeTop < 32.0f) safeTop = 32.0f;
            cand.y = clampf(cand.y, 32.0f, safeTop);

            float awayScore = len(cand - view.enemyPos);
            float playerGap = len(cand - view.playerPos);
            float sideBonus = std::max(0.0f, 140.0f - std::fabs(cand.x - view.playerPos.x));
            float frontBonus = (cand.y <= view.playerPos.y - 48.0f) ? 300.0f : -1000.0f;

            float score = awayScore + playerGap * 0.25f + frontBonus - sideBonus;
            if (score > bestScore) {
                bestScore = score;
                best = cand;
            }
        }
        return best;
    }

    void execute(const Command& cmd, const PatternSystemCallbacks& cb, const Options& opt) {
        switch (cmd.kind) {
        case CmdKind::Nop: ++pc_; return;

        case CmdKind::Wait:
            wait_ = std::max(0, (int)std::lround(evalExpr(cmd.args[0])));
            ++pc_;
            return;

        case CmdKind::WaitRand: {
            float a = evalExpr(cmd.args[0]);
            float b = evalExpr(cmd.args[1]);
            if (a > b) std::swap(a, b);
            wait_ = std::max(0, (int)std::lround(randomFloat(a, b)));
            ++pc_;
            return;
        }

        case CmdKind::Set:
            vars_[cmd.args[0]] = evalExpr(cmd.args[1]);
            ++pc_;
            return;
        case CmdKind::Add:
            vars_[cmd.args[0]] = getVar(cmd.args[0], view_) + evalExpr(cmd.args[1]);
            ++pc_;
            return;
        case CmdKind::Sub:
            vars_[cmd.args[0]] = getVar(cmd.args[0], view_) - evalExpr(cmd.args[1]);
            ++pc_;
            return;
        case CmdKind::Mul:
            vars_[cmd.args[0]] = getVar(cmd.args[0], view_) * evalExpr(cmd.args[1]);
            ++pc_;
            return;
        case CmdKind::Div: {
            float v = evalExpr(cmd.args[1]);
            if (std::fabs(v) <= 1e-8f) {
                ++pc_;
                return;
            }
            vars_[cmd.args[0]] = getVar(cmd.args[0], view_) / v;
            ++pc_;
            return;
        }

        case CmdKind::RandSet: {
            float a = evalExpr(cmd.args[1]);
            float b = evalExpr(cmd.args[2]);
            vars_[cmd.args[0]] = randomFloat(a, b);
            ++pc_;
            return;
        }

        case CmdKind::Move:
            if (cb.moveEnemy) {
                Vec2 target{ evalExpr(cmd.args[0]), evalExpr(cmd.args[1]) };
                float speed = evalExpr(cmd.args[2]);
                cb.moveEnemy(target, speed);
            }
            ++pc_;
            return;

        case CmdKind::MoveAuto:
            if (cb.moveEnemy) {
                float speed = evalExpr(cmd.args[0]);
                float dist = (cmd.args.size() >= 2) ? evalExpr(cmd.args[1]) : 220.0f;
                Vec2 target = chooseAutoMoveTarget(view_, dist);
                cb.moveEnemy(target, speed);
            }
            ++pc_;
            return;

        case CmdKind::ShotCircle:
            if (cb.emitBullet) {
                int count = std::max(1, (int)std::lround(evalExpr(cmd.args[0])));
                float speed = evalExpr(cmd.args[1]);
                uint32_t color = 0xFF6644u;
                int r = 0, g = 0, b = 0;
                if (cmd.args.size() >= 3)
                    if (parseColorToken(cmd.args[2], r, g, b)) {
                        color = rgb(r, g, b);
                    }
                for (int i = 0; i < count; ++i) {
                    float a = (2.0f * PI * i) / (float)count;
                    BulletSpec b;
                    b.pos = view_.enemyPos;
                    b.vel = { std::cos(a) * speed, std::sin(a) * speed };
                    b.radius = 5.0f;
                    b.color = color;
                    cb.emitBullet(b);
                }
            }
            ++pc_;
            return;

        case CmdKind::ShotFan:
            if (cb.emitBullet) {
                int count = std::max(1, (int)std::lround(evalExpr(cmd.args[0])));
                float speed = evalExpr(cmd.args[1]);
                float spreadDeg = evalExpr(cmd.args[2]);
                float offsetDeg = 0.0f;
                uint32_t color = 0xFF6644u;
                parseOffsetColorTail(cmd.args, 3, true, offsetDeg, color);

                float base = std::atan2(view_.playerPos.y - view_.enemyPos.y, view_.playerPos.x - view_.enemyPos.x) + deg2rad(offsetDeg);
                float mid = (count - 1) * 0.5f;
                for (int i = 0; i < count; ++i) {
                    float a = base + deg2rad((i - mid) * spreadDeg);
                    BulletSpec b;
                    b.pos = view_.enemyPos;
                    b.vel = { std::cos(a) * speed, std::sin(a) * speed };
                    b.radius = 5.0f;
                    b.color = color;
                    cb.emitBullet(b);
                }
            }
            ++pc_;
            return;

        case CmdKind::ShotAimed:
            if (cb.emitBullet) {
                float speed = evalExpr(cmd.args[0]);
                float offsetDeg = 0.0f;
                uint32_t color = 0xFF6644u;
                parseOffsetColorTail(cmd.args, 1, true, offsetDeg, color);

                Vec2 dir = normalize(view_.playerPos - view_.enemyPos);
                float a = std::atan2(dir.y, dir.x) + deg2rad(offsetDeg);
                BulletSpec b;
                b.pos = view_.enemyPos;
                b.vel = { std::cos(a) * speed, std::sin(a) * speed };
                b.radius = 5.0f;
                b.color = color;
                cb.emitBullet(b);
            }
            ++pc_;
            return;

        case CmdKind::ShotSpiral:
            if (cb.emitBullet) {
                int count = std::max(1, (int)std::lround(evalExpr(cmd.args[0])));
                float speed = evalExpr(cmd.args[1]);
                float step = evalExpr(cmd.args[2]);
                uint32_t color = 0xFF6644u;
                if (cmd.args.size() >= 4) {
                    int r = 0, g = 0, b = 0;
                    if (parseColorToken(cmd.args[3], r, g, b)) color = rgb(r, g, b);
                }
                float phase = spiralPhase_;
                for (int i = 0; i < count; ++i) {
                    float a = phase + (2.0f * PI * i) / (float)count;
                    BulletSpec b;
                    b.pos = view_.enemyPos;
                    b.vel = { std::cos(a) * speed, std::sin(a) * speed };
                    b.radius = 5.0f;
                    b.color = color;
                    cb.emitBullet(b);
                }
                adjustSpiralPhase(step);
            }
            ++pc_;
            return;

        case CmdKind::Laser:
            if (cb.emitLaser) {
                LaserSpec l;
                l.origin = view_.enemyPos;
                uint32_t color = 0x00FF00u;

                if (!cmd.args.empty() && toLowerCopy(cmd.args[0]) == "aimed") {
                    l.growSpeed = evalExpr(cmd.args[1]);
                    l.maxLength = evalExpr(cmd.args[2]);
                    l.duration = std::max(1, (int)std::lround(evalExpr(cmd.args[3])));
                    l.width = std::max(1.0f, evalExpr(cmd.args[4]));

                    float offset = 0.0f;
                    if (cmd.args.size() >= 6) {
                        parseOffsetColorTail(cmd.args, 5, true, offset, color);
                    }

                    Vec2 dir = normalize(view_.playerPos - view_.enemyPos);
                    float a = std::atan2(dir.y, dir.x) + deg2rad(offset);
                    l.angleDeg = rad2deg(a);
                    l.color = color;
                    cb.emitLaser(l);
                } else {
                    l.angleDeg = evalExpr(cmd.args[0]);
                    l.growSpeed = evalExpr(cmd.args[1]);
                    l.maxLength = evalExpr(cmd.args[2]);
                    l.duration = std::max(1, (int)std::lround(evalExpr(cmd.args[3])));
                    l.width = std::max(1.0f, evalExpr(cmd.args[4]));
                    if (cmd.args.size() >= 6) {
                        int r = 0, g = 0, b = 0;
                        if (parseColorToken(cmd.args[5], r, g, b)) color = rgb(r, g, b);
                    }
                    l.color = color;
                    cb.emitLaser(l);
                }
            }
            ++pc_;
            return;

        case CmdKind::If:
            if (!evalBool(cmd.args[0])) {
                size_t endIf = findMatchingEnd(pc_, CmdKind::If, CmdKind::EndIf);
                if (endIf == npos) { finished_ = true; return; }
                pc_ = endIf + 1;
            } else {
                ++pc_;
            }
            return;

        case CmdKind::EndIf:
            ++pc_;
            return;

        case CmdKind::Loop: {
            int count = (int)std::lround(evalExpr(cmd.args[0]));
            size_t endLoop = findMatchingEnd(pc_, CmdKind::Loop, CmdKind::EndLoop);
            if (endLoop == npos) { finished_ = true; return; }
            if (count == 0) { pc_ = endLoop + 1; return; }
            if ((int)loopStack_.size() >= opt.maxLoopDepth) { finished_ = true; return; }
            LoopFrame f;
            f.kind = LoopFrame::Kind::Repeat;
            f.startPc = pc_ + 1;
            f.endPc = endLoop;
            f.infinite = (count < 0);
            f.remain = count;
            loopStack_.push_back(f);
            ++pc_;
            return;
        }

        case CmdKind::EndLoop:
            if (loopStack_.empty()) { ++pc_; return; }

            {
                LoopFrame& f = loopStack_.back();

                if (f.kind != LoopFrame::Kind::Repeat) {
                    loopStack_.pop_back();
                    ++pc_;
                    return;
                }

                if (f.infinite) {
                    pc_ = f.startPc;
                    return;
                }

                if (--f.remain > 0) {
                    pc_ = f.startPc;
                    return;
                }

                loopStack_.pop_back();
                ++pc_;
                return;
            }

        case CmdKind::For: {
            if ((int)loopStack_.size() >= opt.maxLoopDepth) { finished_ = true; return; }
            const std::string& varName = cmd.args[0];
            float start = evalExpr(cmd.args[1]);
            float end = evalExpr(cmd.args[2]);
            float step = 1.0f;
            if (cmd.args.size() >= 4) step = evalExpr(cmd.args[3]);
            if (!std::isfinite(step) || std::fabs(step) < 1e-6f) {
                step = (end >= start) ? 1.0f : -1.0f;
            }

            size_t endFor = findMatchingEnd(pc_, CmdKind::For, CmdKind::EndFor);
            if (endFor == npos) { finished_ = true; return; }

            bool run;
            if (step > 0.0f) run = start <= end;
            else run = start >= end;
            if (!run) { pc_ = endFor + 1; return; }

            vars_[varName] = start;
            LoopFrame f;
            f.kind = LoopFrame::Kind::For;
            f.startPc = pc_ + 1;
            f.endPc = endFor;
            f.varName = varName;
            f.cur = start;
            f.end = end;
            f.step = step;
            loopStack_.push_back(f);
            ++pc_;
            return;
        }

        case CmdKind::EndFor:
            if (loopStack_.empty()) { ++pc_; return; }

            {
                LoopFrame& f = loopStack_.back();

                if (f.kind != LoopFrame::Kind::For) {
                    loopStack_.pop_back();
                    ++pc_;
                    return;
                }

                f.cur += f.step;
                vars_[f.varName] = f.cur;

                bool cont = (f.step > 0.0f) ? (f.cur <= f.end) : (f.cur >= f.end);

                if (cont) {
                    pc_ = f.startPc;
                    return;
                }

                loopStack_.pop_back();
                ++pc_;
                return;
            }

        case CmdKind::Label:
            ++pc_;
            return;

        case CmdKind::Goto: {
            auto it = labels_.find(cmd.args[0]);
            if (it == labels_.end()) { ++pc_; return; }
            pc_ = it->second;
            return;
        }

        case CmdKind::Call: {
            if ((int)callStack_.size() >= opt.maxCallDepth) { finished_ = true; return; }

            auto it = labels_.find(cmd.args[0]);
            if (it == labels_.end()) { ++pc_; return; }

            callStack_.push_back(pc_ + 1);
            pc_ = it->second;
            return;
        }

        case CmdKind::Return:
            if (callStack_.empty()) {
                finished_ = true;
                return;
            }

            pc_ = callStack_.back();
            callStack_.pop_back();
            return;

        case CmdKind::Break:
            if (loopStack_.empty()) { ++pc_; return; }

            while (!loopStack_.empty()) {
                LoopFrame f = loopStack_.back();
                loopStack_.pop_back();

                pc_ = f.endPc + 1;

                if (f.kind == LoopFrame::Kind::Repeat ||
                    f.kind == LoopFrame::Kind::For)
                    break;
            }
            return;

        case CmdKind::Continue:
            if (loopStack_.empty()) { ++pc_; return; }
            {
                LoopFrame& f = loopStack_.back();
                if (f.kind == LoopFrame::Kind::For) {
                    f.cur += f.step;
                    vars_[f.varName] = f.cur;
                    bool cont = (f.step > 0.0f) ? (f.cur <= f.end) : (f.cur >= f.end);
                    if (!cont) {
                        loopStack_.pop_back();
                        pc_ = f.endPc + 1;
                        return;
                    }
                }
                pc_ = f.startPc;
                return;
            }

        case CmdKind::Jump: {
            int target = (int)std::lround(evalExpr(cmd.args[0]));
            if (target < 0 || target >= (int)program_.size()) { ++pc_; return; }
            pc_ = (size_t)target;
            return;
        }

        case CmdKind::End:
            finished_ = true;
            return;
        }
    }
};

struct Player {
    Vec2 pos{ SCREEN_W * 0.5f, SCREEN_H - 96.0f };
    int lives = 3;
    int bombs = 3;
    int powerLevel = 0;
    int score = 0;
    int graze = 0;
    int fireCooldown = 0;
    int invuln = 0;
    int bombTimer = 0;
    int bombCooldown = 0;
    bool alive = true;
    bool slow = false;
    PowerProfile profile;
    Sprite playerSprite;
    Sprite bombSprite;
    Sprite shotSprite;
};

struct Enemy {
    Vec2 pos;
    Vec2 target;
    bool moving = false;
    float moveSpeed = 0.0f;
    float radius = 16.0f;
    int hp = 1;
    int maxHp = 1;
    uint32_t color = rgb(255, 180, 180);
    bool alive = false;
    Sprite sprite;
    TouhouPatternSystemEngine script;
    std::string name;

    bool autoMove = false;
    float autoMoveSpeed = 0.0f;
    float autoMoveDistance = 220.0f;
    int autoMoveRepath = 0;
    int phase = 0;
};

static Player gPlayer;
static std::vector<PlayerShot> gPlayerShots(MAX_PLAYER_SHOTS);
static std::vector<Bullet> gBullets(MAX_BULLETS);
static std::vector<Laser> gLasers(MAX_LASERS);
static std::vector<Enemy> gEnemies(MAX_ENEMIES);
static uint32_t gBgColor = rgb(10, 10, 20);
static Sprite gStageBg;
static bool gGameOver = false;
static int gStageIndex = 1;
static float gGlobalTime = 0.0f;

static bool loadEnemySpriteOrFallback(const std::string& path, Enemy& e) {
    e.sprite = Sprite();
    if (path.empty()) return false;
    if (!isSafeRelativePath(path)) return false;
    return loadSpriteFile(path, e.sprite);
}
static void clearGameObjects() {
    for (auto& s : gPlayerShots) s.alive = false;
    for (auto& b : gBullets) b.alive = false;
    for (auto& l : gLasers) l.alive = false;
    for (auto& e : gEnemies) e.alive = false;
    gGameOver = false;
}
static bool loadStage(int stageIndex) {
    clearGameObjects();
    gStageBg = Sprite();
    gStageIndex = clampi(stageIndex, 0, 99);

    std::ostringstream oss;
    oss << "stages/stage" << gStageIndex << "/stage.txt";
    std::string stagePath = oss.str();
    if (!isSafeRelativePath(stagePath)) return false;
    if (!safeFileSize(stagePath, 1024u * 1024u)) return false;

    std::ifstream f(stagePath);
    if (!f) return false;

    std::string line;
    int enemyIndex = 0;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto t = splitTokens(line);
        if (t.empty()) continue;

        if ((t[0] == "bgimg" || t[0] == "bgimage" || t[0] == "background") && t.size() >= 2) {
            loadSpriteFile(t[1], gStageBg);
        }
        else if (t[0] == "bg" && t.size() >= 2) {
            gBgColor = parseColor(t[1], gBgColor);
        }
        else if (t[0] == "enemy" && t.size() >= 6 && enemyIndex < MAX_ENEMIES) {
            Enemy& e = gEnemies[enemyIndex++];
            e = Enemy();
            e.alive = true;
            e.pos = { 0.0f, 0.0f };
            e.target = e.pos;
            float fx = 0.0f, fy = 0.0f, fr = 16.0f;
            int hp = 1;
            if (!toFloatSafe(t[1], fx) || !toFloatSafe(t[2], fy) || !toIntSafe(t[3], hp) || !toFloatSafe(t[4], fr)) {
                e.alive = false;
                continue;
            }
            e.pos = { fx, fy };
            e.target = e.pos;
            e.hp = hp;
            e.maxHp = hp;
            e.radius = fr;
            e.name = baseNameFromPath(t[5]);
            e.color = rgb(255, 180, 180);
            if (e.script.loadFromFile(t[5])) {
                e.script.enable();
            } else {
                e.script.disable();
            }
            if (t.size() >= 7) loadEnemySpriteOrFallback(t[6], e);
            if (t.size() >= 8) e.color = parseColor(t[7], e.color);
        }
        else if (t[0] == "player" && t.size() >= 3) {
            float px = 0.0f, py = 0.0f;
            if (toFloatSafe(t[1], px) && toFloatSafe(t[2], py)) gPlayer.pos = { px, py };
        }
        else if (t[0] == "power" && t.size() >= 2) {
            int p = 0;
            if (toIntSafe(t[1], p)) gPlayer.powerLevel = clampi(p, 0, 8);
        }
    }
    return true;
}
static bool loadPlayerAssets() {
    if (!loadPowerProfile(gPlayer.powerLevel, gPlayer.profile)) {
        gPlayer.profile = PowerProfile();
        gPlayer.profile.level = gPlayer.powerLevel;
    }
    if (!gPlayer.profile.playerSpritePath.empty()) loadSpriteFile(gPlayer.profile.playerSpritePath, gPlayer.playerSprite);
    if (!gPlayer.profile.bombSpritePath.empty()) loadSpriteFile(gPlayer.profile.bombSpritePath, gPlayer.bombSprite);
    if (!gPlayer.profile.shotSpritePath.empty()) loadSpriteFile(gPlayer.profile.shotSpritePath, gPlayer.shotSprite);
    return true;
}

static PlayerShot* allocPlayerShot() { for (auto& s : gPlayerShots) if (!s.alive) return &s; return nullptr; }
static Bullet* allocBullet() { for (auto& b : gBullets) if (!b.alive) return &b; return nullptr; }

static void spawnPlayerShots() {
    int count = std::max(1, gPlayer.profile.shotCount + (gPlayer.powerLevel / 2));
    float spread = gPlayer.profile.shotSpreadDeg;
    float baseAngle = -90.0f;
    float mid = (count - 1) * 0.5f;

    for (int i = 0; i < count; ++i) {
        PlayerShot* s = allocPlayerShot();
        if (!s) return;
        float ang = deg2rad(baseAngle + (i - mid) * spread);
        Vec2 dir = { std::cos(ang), std::sin(ang) };
        s->alive = true;
        s->pos = gPlayer.pos + Vec2{ (i - mid) * 4.0f, -16.0f };
        s->vel = dir * gPlayer.profile.shotSpeed;
        s->radius = 4.0f;
        s->damage = gPlayer.profile.shotDamage;
    }
}
static void spawnBullet(const Vec2& pos, const Vec2& vel, float radius, uint32_t color) {
    Bullet* b = allocBullet();
    if (!b) return;
    b->alive = true;
    b->pos = pos;
    b->vel = vel;
    b->radius = radius;
    b->color = color;
    b->grazed = false;
}
static void shootCircle(const Vec2& pos, int count, float speed, uint32_t color = rgb(255, 120, 100)) {
    count = std::max(1, count);
    for (int i = 0; i < count; ++i) {
        float a = (2.0f * PI * i) / (float)count;
        Vec2 v = { std::cos(a) * speed, std::sin(a) * speed };
        spawnBullet(pos, v, 5.0f, color);
    }
}
static void shootAimed(const Vec2& pos, float speed, float offsetDeg = 0.0f, uint32_t color = rgb(255, 120, 100)) {
    Vec2 dir = normalize(gPlayer.pos - pos);
    float a = std::atan2(dir.y, dir.x) + deg2rad(offsetDeg);
    spawnBullet(pos, { std::cos(a) * speed, std::sin(a) * speed }, 5.0f, color);
}
static void shootSpiral(const Vec2& pos, int count, float speed, float phaseStep, float& spiralPhase, uint32_t color = rgb(255, 120, 100)) {
    count = std::max(1, count);
    float phase = spiralPhase;
    for (int i = 0; i < count; ++i) {
        float a = phase + (2.0f * PI * i) / (float)count;
        spawnBullet(pos, { std::cos(a) * speed, std::sin(a) * speed }, 5.0f, color);
    }
    spiralPhase += phaseStep;
}
static Laser* allocLaser() {
    for (auto& l : gLasers) {
        if (!l.alive) return &l;
    }

    gLasers.emplace_back();
    return &gLasers.back();
}

static void shootLaser(const Vec2& origin, float angleDeg, float growSpeed, float maxLen, int duration, float width, uint32_t color = rgb(120, 255, 120)) {
    Laser* l = allocLaser();
    if (!l) return;

    l->alive = true;
    l->origin = origin;
    l->angle = deg2rad(angleDeg);
    l->growSpeed = growSpeed;
    l->maxLength = maxLen;
    l->width = width;
    l->life = duration;
    l->currentLength = 0.0f;
    l->color = color;
    l->grazed = false;
}
static void shootLaserAimed(const Vec2& origin, float growSpeed, float maxLen, int duration, float width, float offsetDeg = 0.0f, uint32_t color = rgb(120, 255, 120)) {
    Vec2 d = normalize(gPlayer.pos - origin);
    float a = std::atan2(d.y, d.x) + deg2rad(offsetDeg);
    shootLaser(origin, rad2deg(a), growSpeed, maxLen, duration, width, color);
}
static void bombClear() {
    float r = (float)gPlayer.profile.bombClearRadius;
    float r2 = r * r;
    for (auto& b : gBullets) if (b.alive && dist2(b.pos, gPlayer.pos) <= r2) b.alive = false;
    for (auto& l : gLasers) if (l.alive) l.life = std::min(l.life, 10);
}
static void activateBomb() {
    if (gPlayer.bombs <= 0) return;
    if (gPlayer.bombTimer > 0) return;
    if (gPlayer.bombCooldown > 0) return;
    gPlayer.bombs--;
    gPlayer.bombTimer = gPlayer.profile.bombDuration;
    gPlayer.invuln = std::max(gPlayer.invuln, gPlayer.profile.bombInvuln);
    gPlayer.bombCooldown = 30;
    bombClear();
}
static void playerHit() {
    if (gPlayer.invuln > 0 || gPlayer.bombTimer > 0) return;
    gPlayer.lives--;
    gPlayer.invuln = 120;
    gPlayer.pos = { SCREEN_W * 0.5f, SCREEN_H - 96.0f };
    if (gPlayer.lives < 0) gGameOver = true;
}

static Vec2 chooseAutoMoveTargetForEnemy(const Enemy& e) {
    float dist = e.autoMoveDistance;
    if (gPlayer.slow) dist += 40.0f;
    if (gPlayer.bombTimer > 0 || gPlayer.invuln > 0) dist += 60.0f;
    if (gPlayer.lives <= 1) dist += 30.0f;
    if (gPlayer.bombs <= 0) dist += 20.0f;
    dist = clampf(dist, 120.0f, 360.0f);

    const float angleDegs[] = { 225.0f, 245.0f, 270.0f, 295.0f, 315.0f };
    Vec2 best = e.pos;
    float bestScore = -1.0e30f;

    for (float ad : angleDegs) {
        float a = deg2rad(ad);
        Vec2 cand{
            gPlayer.pos.x + std::cos(a) * dist,
            gPlayer.pos.y + std::sin(a) * dist
        };
        cand.x = clampf(cand.x, 32.0f, SCREEN_W - 32.0f);
        float safeTop = gPlayer.pos.y - 48.0f;
        if (safeTop < 32.0f) safeTop = 32.0f;
        cand.y = clampf(cand.y, 32.0f, safeTop);

        float awayScore = len(cand - e.pos);
        float playerGap = len(cand - gPlayer.pos);
        float sideBonus = std::max(0.0f, 140.0f - std::fabs(cand.x - gPlayer.pos.x));
        float frontBonus = (cand.y <= gPlayer.pos.y - 48.0f) ? 300.0f : -1000.0f;

        float score = awayScore + playerGap * 0.25f + frontBonus - sideBonus;
        if (score > bestScore) {
            bestScore = score;
            best = cand;
        }
    }
    return best;
}

static void updateEnemyMovement(Enemy& e) {
    if (!e.alive) return;
    if (e.autoMove) {
        if (e.autoMoveRepath <= 0 || dist2(e.pos, e.target) <= 20.0f * 20.0f) {
            e.target = chooseAutoMoveTargetForEnemy(e);
            e.moveSpeed = e.autoMoveSpeed;
            e.moving = true;
            e.autoMoveRepath = 20;
        }
        if (e.autoMoveRepath > 0) e.autoMoveRepath--;
    }
    if (!e.moving) return;
    Vec2 d = e.target - e.pos;
    float l = len(d);
    if (l <= e.moveSpeed || l <= 0.001f) {
        e.pos = e.target;
        e.moving = false;
        return;
    }
    e.pos = e.pos + normalize(d) * e.moveSpeed;
}

static void runPatternSystemOnEnemy(Enemy& e) {
    if (!e.alive) return;
    if (!e.script.ready()) return;

    PatternSystemRuntimeView view;
    view.enemyPos = e.pos;
    view.playerPos = gPlayer.pos;
    view.hp = (float)e.hp;
    view.maxHp = (float)e.maxHp;
    view.phase = e.phase;
    view.time = gGlobalTime;
    view.playerSlow = gPlayer.slow;
    view.playerInvuln = gPlayer.invuln > 0;
    view.playerBombTimer = gPlayer.bombTimer;
    view.playerLives = gPlayer.lives;
    view.playerBombs = gPlayer.bombs;
    view.score = gPlayer.score;
    view.graze = gPlayer.graze;
    view.difficulty = 1.0f;

    int execLimit = 96;
    int executed = 0;

    PatternSystemCallbacks cb;
    cb.emitBullet = [&](const BulletSpec& b) {
        if (++executed > execLimit) return;
        spawnBullet(b.pos, b.vel, b.radius, b.color);
    };

    cb.emitLaser = [&](const LaserSpec& l) {
        if (++executed > execLimit) return;
        shootLaser(l.origin, l.angleDeg, l.growSpeed, l.maxLength, l.duration, l.width, l.color);
    };

    cb.moveEnemy = [&](const Vec2& target, float speed) {
        e.target = target;
        e.moveSpeed = speed;
        e.moving = true;
    };

    e.script.tick(view, cb, TouhouPatternSystemEngine::Options{ 96, 64, 64 });
}

static void updatePlayer() {
    const bool keyUp = (GetAsyncKeyState(VK_UP) & 0x8000) || (GetAsyncKeyState('W') & 0x8000);
    const bool keyDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) || (GetAsyncKeyState('S') & 0x8000);
    const bool keyLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000);
    const bool keyRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000);
    const bool keyShot = (GetAsyncKeyState('Z') & 0x8000);
    const bool keyBomb = (GetAsyncKeyState('X') & 0x8000);
    const bool keySlow = (GetAsyncKeyState(VK_SHIFT) & 0x8000);

    gPlayer.slow = keySlow;
    float speed = gPlayer.slow ? 3.0f : 6.0f;

    Vec2 move{ 0, 0 };
    if (keyLeft) move.x -= 1.0f;
    if (keyRight) move.x += 1.0f;
    if (keyUp) move.y -= 1.0f;
    if (keyDown) move.y += 1.0f;
    if (len2(move) > 0.0f) move = normalize(move) * speed;
    gPlayer.pos = gPlayer.pos + move;

    gPlayer.pos.x = clampf(gPlayer.pos.x, 16.0f, SCREEN_W - 16.0f);
    gPlayer.pos.y = clampf(gPlayer.pos.y, 24.0f, SCREEN_H - 24.0f);

    if (gPlayer.fireCooldown > 0) gPlayer.fireCooldown--;
    if (gPlayer.invuln > 0) gPlayer.invuln--;
    if (gPlayer.bombTimer > 0) gPlayer.bombTimer--;
    if (gPlayer.bombCooldown > 0) gPlayer.bombCooldown--;

    if (keyBomb) activateBomb();
    if (keyShot && gPlayer.fireCooldown == 0 && !gGameOver) {
        spawnPlayerShots();
        gPlayer.fireCooldown = gPlayer.profile.fireCooldown;
    }
}
static void updatePlayerShots() {
    for (auto& s : gPlayerShots) {
        if (!s.alive) continue;
        s.pos = s.pos + s.vel;
        if (s.pos.y < -32.0f || s.pos.y > SCREEN_H + 32.0f || s.pos.x < -32.0f || s.pos.x > SCREEN_W + 32.0f) s.alive = false;
    }
}
static void updateBullets() {
    for (auto& b : gBullets) {
        if (!b.alive) continue;
        b.pos = b.pos + b.vel;
        if (b.pos.x < -64.0f || b.pos.x > SCREEN_W + 64.0f || b.pos.y < -64.0f || b.pos.y > SCREEN_H + 64.0f) b.alive = false;
    }
}
static void updateLasers() {
    for (auto& l : gLasers) {
        if (!l.alive) continue;
        l.life--;
        if (l.currentLength < l.maxLength) l.currentLength = std::min(l.maxLength, l.currentLength + l.growSpeed);
        if (l.life <= 0) l.alive = false;
    }
}
static void updateEnemies() {
    for (auto& e : gEnemies) {
        if (!e.alive) continue;

        updateEnemyMovement(e);

        if (!e.script.ready()) continue;

        runPatternSystemOnEnemy(e);

        if (e.hp <= 0) {
            e.alive = false;
            gPlayer.score += 1000;
        }
    }
}
static void playerBulletVsEnemy() {
    for (auto& s : gPlayerShots) {
        if (!s.alive) continue;
        for (auto& e : gEnemies) {
            if (!e.alive) continue;
            float rr = s.radius + e.radius;
            if (dist2(s.pos, e.pos) <= rr * rr) {
                e.hp -= (int)std::max(1.0f, s.damage);
                s.alive = false;
                gPlayer.score += 10;
                break;
            }
        }
    }
}

static const Enemy* getFirstAliveBoss() {
    for (const auto& e : gEnemies) {
        if (e.alive) return &e;
    }
    return nullptr;
}

static void renderBossHPBar() {
    const Enemy* boss = getFirstAliveBoss();
    if (!boss) return;

    const int barX = 120;
    const int barY = 18;
    const int barW = SCREEN_W - 240;
    const int barH = 14;

    float ratio = 0.0f;
    if (boss->maxHp > 0) {
        ratio = clampf((float)boss->hp / (float)boss->maxHp, 0.0f, 1.0f);
    }

    gFrame.rect(barX - 2, barY - 2, barW + 4, barH + 4, rgb(0, 0, 0));
    gFrame.rect(barX, barY, barW, barH, rgb(40, 40, 40));
    gFrame.rect(barX, barY, (int)(barW * ratio), barH, rgb(220, 60, 60));
}

static void enemyBulletVsPlayer() {
    float hitR = gPlayer.profile.hitRadius;
    float grazeR = gPlayer.profile.grazeRadius;
    float hitR2 = hitR * hitR;
    float grazeR2 = grazeR * grazeR;

    for (auto& b : gBullets) {
        if (!b.alive) continue;
        float d2 = dist2(b.pos, gPlayer.pos);
        if (d2 <= hitR2) {
            b.alive = false;
            playerHit();
            continue;
        }
        if (!b.grazed && d2 > hitR2 && d2 <= grazeR2) {
            b.grazed = true;
            gPlayer.graze++;
            gPlayer.score += 5;
        }
    }
}
static void laserVsPlayer() {
    float hitR = gPlayer.profile.hitRadius;
    float grazeR = gPlayer.profile.grazeRadius;

    for (auto& l : gLasers) {
        if (!l.alive) continue;

        Vec2 a{ l.origin.x, l.origin.y };
        Vec2 b{
            l.origin.x + std::cos(l.angle) * l.currentLength,
            l.origin.y + std::sin(l.angle) * l.currentLength
        };

        float d = pointSegmentDistance(gPlayer.pos, a, b);
        float core = l.width * 0.5f;

        if (d <= core + hitR) {
            playerHit();
        }
        else if (!l.grazed && d <= core + grazeR) {
            l.grazed = true;
            gPlayer.graze++;
            gPlayer.score += 3;
        }
    }
}
static void updateBomb() {
    if (gPlayer.bombTimer <= 0) return;
    float r = (float)gPlayer.profile.bombRadius;
    float r2 = r * r;
    for (auto& b : gBullets) {
        if (!b.alive) continue;
        if (dist2(b.pos, gPlayer.pos) <= r2) b.alive = false;
    }
}
static void updateGame() {
    if (gGameOver) return;
    updatePlayer();
    updatePlayerShots();
    updateEnemies();
    updateBullets();
    updateLasers();
    updateBomb();
    playerBulletVsEnemy();
    enemyBulletVsPlayer();
    laserVsPlayer();
    gGlobalTime += FIXED_DT;
}

static void renderBombShape() {
    if (gPlayer.bombTimer <= 0) return;
    int t = gPlayer.profile.bombDuration - gPlayer.bombTimer;
    float progress = clampf(t / (float)std::max(1, gPlayer.profile.bombDuration), 0.0f, 1.0f);
    int r = (int)(gPlayer.profile.bombRadius * (0.35f + 0.65f * progress));
    uint32_t c = gPlayer.profile.bombColor;

    if (gPlayer.profile.bombShape == "circle") {
        gFrame.circle((int)gPlayer.pos.x, (int)gPlayer.pos.y, r, c, true);
    } else if (gPlayer.profile.bombShape == "diamond") {
        gFrame.diamond((int)gPlayer.pos.x, (int)gPlayer.pos.y, r, c, false);
    } else if (gPlayer.profile.bombShape == "cross") {
        gFrame.line((int)gPlayer.pos.x - r, (int)gPlayer.pos.y, (int)gPlayer.pos.x + r, (int)gPlayer.pos.y, c);
        gFrame.line((int)gPlayer.pos.x, (int)gPlayer.pos.y - r, (int)gPlayer.pos.x, (int)gPlayer.pos.y + r, c);
    } else {
        gFrame.circle((int)gPlayer.pos.x, (int)gPlayer.pos.y, r, c, false);
    }

    if (!gPlayer.bombSprite.empty()) gFrame.blit(gPlayer.bombSprite, (int)gPlayer.pos.x, (int)gPlayer.pos.y, true);
}
static void renderPlayer() {
    bool blink = (gPlayer.invuln > 0) && ((gPlayer.invuln / 4) % 2 == 0);
    if (!blink) {
        if (!gPlayer.playerSprite.empty()) gFrame.blit(gPlayer.playerSprite, (int)gPlayer.pos.x, (int)gPlayer.pos.y, true);
        else gFrame.circle((int)gPlayer.pos.x, (int)gPlayer.pos.y, 8, rgb(100, 180, 255), true);
    }
    gFrame.circle((int)gPlayer.pos.x, (int)gPlayer.pos.y, (int)gPlayer.profile.hitRadius, rgb(255, 255, 255), true);
}
static void renderShots() {
    for (const auto& s : gPlayerShots) {
        if (!s.alive) continue;
        if (!gPlayer.shotSprite.empty()) gFrame.blit(gPlayer.shotSprite, (int)s.pos.x, (int)s.pos.y, true);
        else gFrame.circle((int)s.pos.x, (int)s.pos.y, (int)s.radius, gPlayer.profile.shotColor, true);
    }
}
static void renderBullets() {
    for (const auto& b : gBullets) if (b.alive) gFrame.circle((int)b.pos.x, (int)b.pos.y, (int)b.radius, b.color, true);
}
static void renderLasers() {
    for (const auto& l : gLasers) {
        if (!l.alive) continue;
        Vec2 a = l.origin;
        Vec2 b{ l.origin.x + std::cos(l.angle) * l.currentLength, l.origin.y + std::sin(l.angle) * l.currentLength };
        int steps = std::max(1, (int)l.width);
        for (int i = 0; i < steps; ++i) {
            float t = i / (float)steps;
            Vec2 p{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
            gFrame.circle((int)p.x, (int)p.y, (int)std::max(1.0f, l.width * 0.5f), l.color, true);
        }
        gFrame.line((int)a.x, (int)a.y, (int)b.x, (int)b.y, l.color);
    }
}
static void renderEnemies() {
    for (const auto& e : gEnemies) {
        if (!e.alive) continue;
        if (!e.sprite.empty()) gFrame.blit(e.sprite, (int)e.pos.x, (int)e.pos.y, true);
        else {
            gFrame.circle((int)e.pos.x, (int)e.pos.y, (int)e.radius, e.color, true);
            gFrame.rect((int)e.pos.x - 6, (int)e.pos.y - 2, 12, 4, rgb(80, 0, 0));
        }
    }
}


static void renderGameOver() {
    if (!gGameOver) return;
    gFrame.rect(0, SCREEN_H / 2 - 40, SCREEN_W, 80, rgb(0, 0, 0));
}
static std::string makeHUD() {
    std::ostringstream oss;
    oss << "Stage " << gStageIndex
        << " | Score " << gPlayer.score
        << " | Lives " << gPlayer.lives
        << " | Bombs " << gPlayer.bombs
        << " | Power " << gPlayer.powerLevel
        << " | Graze " << gPlayer.graze
        << " | Profile " << gPlayer.profile.name;
    if (gGameOver) oss << " | GAME OVER";
    if (const Enemy* boss = getFirstAliveBoss()) {
        float ratio = (boss->maxHp > 0) ? clampf((float)boss->hp / (float)boss->maxHp, 0.0f, 1.0f) : 0.0f;
        oss << " | Boss " << boss->name << " " << (int)(ratio * 100.0f) << "%";
    }
    return oss.str();
}
static void renderFrame(HWND hwnd) {
    gFrame.clear(gBgColor);
    gFrame.blitTiled(gStageBg);
    renderBossHPBar();   // 추가
    renderBombShape();
    renderEnemies();
    renderBullets();
    renderLasers();
    renderShots();
    renderPlayer();
    renderGameOver();
    gFrame.present(hwnd, makeHUD());
}

static HWND gHwnd = nullptr;
static bool gRunning = true;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        gRunning = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            gRunning = false;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_F1) {
            gPlayer.powerLevel = (gPlayer.powerLevel + 1) % 9;
            loadPlayerAssets();
            return 0;
        }
        if (wParam == VK_F5) {
            loadStage(gStageIndex);
            return 0;
        }
        if (wParam == VK_F6) {
            int next = gStageIndex + 1;
            if (next > 99) next = 1;
            loadStage(next);
            return 0;
        }
        if (wParam == VK_F7) {
            loadStage(0);
            return 0;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    struct ComScope {
        bool ok = false;
        ComScope() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
        ~ComScope() { if (ok) CoUninitialize(); }
    } comScope;

    HRESULT hrWic = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&gWicFactory)
    );

    gFrame.init();

    const char* clsName = "BULLET HELL";
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = clsName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rc{ 0, 0, SCREEN_W, SCREEN_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    gHwnd = CreateWindowA(
        clsName,
        "the BULLET HELL",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr
    );
    if (!gHwnd) return 0;

    ShowWindow(gHwnd, SW_SHOW);
    UpdateWindow(gHwnd);

    gPlayer.powerLevel = 0;
    loadPlayerAssets();
    loadStage(1);

    LARGE_INTEGER freq{};
    LARGE_INTEGER prev{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    double acc = 0.0;

    MSG msg{};
    while (gRunning) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) gRunning = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!gRunning) break;

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart;
        prev = now;
        acc += dt;

        while (acc >= FIXED_DT) {
            updateGame();
            acc -= FIXED_DT;
        }

        renderFrame(gHwnd);
        Sleep(1);
    }
    if (gWicFactory) {
        gWicFactory->Release();
        gWicFactory = nullptr;
    }
    return 0;
}