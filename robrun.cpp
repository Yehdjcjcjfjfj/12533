// robrun.cpp - ESP + silent aim + menu for ROB RUN
// external build:  cl /DROBRUN_INTERNAL=0 ...
// internal build:  cl /DROBRUN_INTERNAL=1 /LD ...
#include <windows.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef ROBRUN_INTERNAL
#define ROBRUN_INTERNAL 0
#endif

namespace offs {
    constexpr uintptr_t GameObjectManager  = 0x2230768;
    constexpr uintptr_t Updater_Entities   = 0x18;
    constexpr uintptr_t Updater_Battle     = 0x70;
    constexpr uintptr_t Updater_Views      = 0x110;
    constexpr uintptr_t Battle_Entities    = 0x90;
    constexpr uintptr_t Battle_Camera      = 0x358;
    constexpr uintptr_t Battle_AimTarget   = 0x198;
    constexpr uintptr_t Battle_World       = 0x518;
    constexpr uintptr_t Entities_Models    = 0x50;
    constexpr uintptr_t Entities_Slots     = 0x80;
    constexpr uintptr_t Slots_Dict         = 0x10;
    constexpr uintptr_t Slot_EntityId      = 0x20;
    constexpr uintptr_t Slot_Nickname      = 0x30;
    constexpr uintptr_t CameraModel_LookAt = 0x2C;

    constexpr uintptr_t World_Containers   = 0x20;
    constexpr uintptr_t World_Loots        = 0x38;
    constexpr uintptr_t World_VehicleLoots = 0x40;
    constexpr uintptr_t World_Doors        = 0x48;
    constexpr uintptr_t World_Cars         = 0x60;
    constexpr uintptr_t World_Helis        = 0x68;
    constexpr uintptr_t World_HideObjects  = 0x1B0;
    constexpr uintptr_t Model_InteractPos  = 0x28;
    constexpr uintptr_t PosModel_X         = 0x10;
    constexpr uintptr_t Car_Transform      = 0xC0;
    constexpr uintptr_t Heli_Transform     = 0x70;
    constexpr uintptr_t HideObj_Desc       = 0x18;
    constexpr uintptr_t HideDesc_Position  = 0x30;

    constexpr uintptr_t KlassName          = 0x10;
    constexpr uintptr_t GoComponents       = 0x10;
    constexpr uintptr_t GomFirstNode       = 0x8;
    constexpr uintptr_t NodeObject         = 0x0;
    constexpr uintptr_t NodeNext           = 0x8;
    constexpr uintptr_t TransformPos       = 0x38;
    constexpr uintptr_t TransformPosAlt    = 0x90;
    constexpr uintptr_t Mono_Dict_Entries  = 0x18;
    constexpr uintptr_t Mono_Dict_Count    = 0x20;
    constexpr uintptr_t Aim_CharacterId    = 0x10;
    constexpr uintptr_t Aim_TargetType     = 0x14;
}

struct Vec3 { float x, y, z; };

struct Config {
    bool esp = true, box = true, name = true, dist = true, health = true;
    bool loot = true, container = true, door = false, car = true, heli = true, hideobj = false;
    bool crosshair = true, fovCircle = true, streamproof = true;
    bool silent = false, mouseAim = false;
    int  aimKey = VK_XBUTTON1;
    float fov = 90.f, aimFov = 140.f, aimDist = 250.f, smooth = 0.35f;
    float playerDist = 600.f, worldDist = 150.f, eye = 1.65f, bone = 1.55f;
} cfg;

static void save_cfg() {
    FILE* f = nullptr; fopen_s(&f, "robrun.ini", "w"); if (!f) return;
    fprintf(f, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", cfg.esp, cfg.box, cfg.name, cfg.dist,
        cfg.health, cfg.loot, cfg.container, cfg.door, cfg.car, cfg.heli, cfg.hideobj,
        cfg.crosshair, cfg.fovCircle, cfg.streamproof);
    fprintf(f, "%d %d %d %f %f %f %f %f %f %f %f\n", cfg.silent, cfg.mouseAim, cfg.aimKey,
        cfg.fov, cfg.aimFov, cfg.aimDist, cfg.smooth, cfg.playerDist, cfg.worldDist, cfg.eye, cfg.bone);
    fclose(f);
}
static void load_cfg() {
    FILE* f = nullptr; fopen_s(&f, "robrun.ini", "r"); if (!f) return;
    fscanf_s(f, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d", &cfg.esp, &cfg.box, &cfg.name, &cfg.dist,
        &cfg.health, &cfg.loot, &cfg.container, &cfg.door, &cfg.car, &cfg.heli, &cfg.hideobj,
        &cfg.crosshair, &cfg.fovCircle, &cfg.streamproof);
    fscanf_s(f, "%d %d %d %f %f %f %f %f %f %f %f", &cfg.silent, &cfg.mouseAim, &cfg.aimKey,
        &cfg.fov, &cfg.aimFov, &cfg.aimDist, &cfg.smooth, &cfg.playerDist, &cfg.worldDist, &cfg.eye, &cfg.bone);
    fclose(f);
}

// ------------------------------------------------------------- memory layer
static DWORD g_pid = 0;
static uintptr_t g_game = 0, g_unity = 0;
static bool g_run = true, g_menu = false;
static HINSTANCE g_self = nullptr;
#if !ROBRUN_INTERNAL
static HANDLE g_proc = nullptr;
#endif

static bool vread(uintptr_t a, void* buf, size_t n) {
    if (!a) return false;
#if ROBRUN_INTERNAL
    __try { memcpy(buf, (void*)a, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return ReadProcessMemory(g_proc, (LPCVOID)a, buf, n, nullptr) != 0;
#endif
}
static bool vwrite(uintptr_t a, const void* buf, size_t n) {
    if (!a) return false;
#if ROBRUN_INTERNAL
    __try { memcpy((void*)a, buf, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return WriteProcessMemory(g_proc, (LPVOID)a, buf, n, nullptr) != 0;
#endif
}

template <class T> static bool rd(uintptr_t a, T& o) { return vread(a, &o, sizeof(T)); }
template <class T> static T rdv(uintptr_t a) { T v{}; rd(a, v); return v; }
template <class T> static void wr(uintptr_t a, T v) { vwrite(a, &v, sizeof(T)); }

static bool plausible(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && fabsf(v.x) < 100000.f && fabsf(v.y) < 100000.f;
}

static uintptr_t module_base(const wchar_t* n) {
#if ROBRUN_INTERNAL
    char a[64]{}; WideCharToMultiByte(CP_ACP, 0, n, -1, a, 63, nullptr, nullptr);
    return (uintptr_t)GetModuleHandleA(a);
#else
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
    if (s == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) }; uintptr_t out = 0;
    if (Module32FirstW(s, &me)) do { if (!_wcsicmp(me.szModule, n)) { out = (uintptr_t)me.modBaseAddr; break; } } while (Module32NextW(s, &me));
    CloseHandle(s); return out;
#endif
}

static bool attach() {
#if !ROBRUN_INTERNAL
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof(pe) }; bool ok = false;
    if (Process32FirstW(s, &pe)) do {
        if (!_wcsicmp(pe.szExeFile, L"ROB RUN.exe")) { g_pid = pe.th32ProcessID; ok = true; break; }
    } while (Process32NextW(s, &pe));
    CloseHandle(s); if (!ok) return false;
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, g_pid);
    if (!g_proc) return false;
#endif
    g_game = module_base(L"GameAssembly.dll");
    g_unity = module_base(L"UnityPlayer.dll");
    return g_game && g_unity;
}

// ------------------------------------------------------------- il2cpp
static bool class_is(uintptr_t obj, const char* want) {
    uintptr_t k = rdv<uintptr_t>(obj); if (!k) return false;
    char b[64]{}; if (!vread(k + offs::KlassName, b, 63)) return false;
    return strstr(b, want) != nullptr;
}

template <class F> static void for_each_go(F fn) {
    uintptr_t gom = rdv<uintptr_t>(g_unity + offs::GameObjectManager); if (!gom) return;
    uintptr_t node = rdv<uintptr_t>(gom + offs::GomFirstNode);
    for (int g = 0; node && g < 60000; ++g) {
        uintptr_t go = rdv<uintptr_t>(node + offs::NodeObject);
        if (go) fn(go);
        uintptr_t nx = rdv<uintptr_t>(node + offs::NodeNext);
        if (nx == node) break; node = nx;
    }
}

static uintptr_t find_component(uintptr_t go, const char* cls) {
    uintptr_t arr = rdv<uintptr_t>(go + offs::GoComponents); if (!arr) return 0;
    uintptr_t data = rdv<uintptr_t>(arr + 0x8); int32_t n = rdv<int32_t>(arr + 0x18);
    if (!data || n <= 0 || n > 96) return 0;
    for (int i = 0; i < n; ++i) {
        uintptr_t c = rdv<uintptr_t>(data + (size_t)i * 0x10 + 0x8);
        if (c && class_is(c, cls)) return c;
    }
    return 0;
}

static uintptr_t transform_of(uintptr_t go) {
    uintptr_t arr = rdv<uintptr_t>(go + offs::GoComponents); if (!arr) return 0;
    uintptr_t data = rdv<uintptr_t>(arr + 0x8); int32_t n = rdv<int32_t>(arr + 0x18);
    if (!data || n <= 0 || n > 96) return 0;
    for (int i = 0; i < n; ++i) {
        uintptr_t p = data + (size_t)i * 0x10;
        if (rdv<int32_t>(p) == 4) return rdv<uintptr_t>(p + 0x8);
    }
    return 0;
}

static bool go_pos(uintptr_t go, Vec3& o) {
    uintptr_t t = transform_of(go); if (!t) return false;
    if (rd(t + offs::TransformPos, o) && plausible(o)) return true;
    return rd(t + offs::TransformPosAlt, o) && plausible(o);
}

template <class F> static void for_each_dict(uintptr_t d, F fn) {
    if (!d) return;
    uintptr_t e = rdv<uintptr_t>(d + offs::Mono_Dict_Entries);
    int32_t c = rdv<int32_t>(d + offs::Mono_Dict_Count);
    if (!e || c <= 0 || c > 8192) return;
    for (int32_t i = 0; i < c; ++i) {
        uintptr_t n = e + 0x10 + (size_t)i * 0x18;
        int32_t k = rdv<int32_t>(n + 0x8); uintptr_t v = rdv<uintptr_t>(n + 0x10);
        if (k > 0 && v) fn(k, v);
    }
}

static std::string read_str(uintptr_t s) {
    int32_t l = rdv<int32_t>(s + 0x10); if (l <= 0 || l > 40) return {};
    std::wstring w(l, L'\0');
    if (!vread(s + 0x14, &w[0], (size_t)l * 2)) return {};
    char b[96]{}; WideCharToMultiByte(CP_UTF8, 0, w.c_str(), l, b, 95, nullptr, nullptr);
    return b;
}

// ------------------------------------------------------------- state
struct State { uintptr_t updater{}, battle{}, entities{}, views{}, cam{}, world{}; };
struct Ent { int id; std::string name; Vec3 pos; float dist; bool self; };
struct World { Vec3 pos; float dist; int kind; };
struct Cam { Vec3 pos; float yaw, pitch, fov; };

static bool resolve(State& s) {
    for_each_go([&](uintptr_t go) { if (!s.updater) { uintptr_t c = find_component(go, "BattleEntitiesUpdater"); if (c) s.updater = c; } });
    if (!s.updater) return false;
    s.battle = rdv<uintptr_t>(s.updater + offs::Updater_Battle);
    s.entities = rdv<uintptr_t>(s.updater + offs::Updater_Entities);
    s.views = rdv<uintptr_t>(s.updater + offs::Updater_Views);
    if (!s.battle || !s.entities) return false;
    s.cam = rdv<uintptr_t>(s.battle + offs::Battle_Camera);
    s.world = rdv<uintptr_t>(s.battle + offs::Battle_World);
    return s.cam != 0;
}

static void collect(const State& s, std::vector<Ent>& out, std::unordered_map<int, uintptr_t>& views) {
    std::unordered_map<int, std::string> names;
    uintptr_t sm = rdv<uintptr_t>(s.entities + offs::Entities_Slots);
    uintptr_t sd = sm ? rdv<uintptr_t>(sm + offs::Slots_Dict) : 0;
    for_each_dict(sd, [&](int, uintptr_t slot) {
        int32_t id = rdv<int32_t>(slot + offs::Slot_EntityId);
        uintptr_t nm = rdv<uintptr_t>(slot + offs::Slot_Nickname);
        if (id > 0 && nm) names[id] = read_str(nm);
    });
    for_each_dict(s.views, [&](int id, uintptr_t v) { views[id] = v; });
    for_each_dict(rdv<uintptr_t>(s.entities + offs::Entities_Models), [&](int id, uintptr_t) {
        auto it = views.find(id); if (it == views.end()) return;
        Ent e{ id, {}, {}, 0, false };
        if (!go_pos(it->second, e.pos)) return;
        auto n = names.find(id);
        e.name = (n != names.end() && !n->second.empty()) ? n->second : "[" + std::to_string(id) + "]";
        out.push_back(std::move(e));
    });
}

static void collect_world(const State& s, std::vector<World>& out) {
    auto pos_model = [](uintptr_t m) {
        Vec3 v{}; uintptr_t p = rdv<uintptr_t>(m + offs::Model_InteractPos);
        if (p) { rd(p + offs::PosModel_X, v.x); rd(p + offs::PosModel_X + 4, v.y); rd(p + offs::PosModel_X + 8, v.z); }
        return v; };
    auto push = [&](const Vec3& p, int k) { if (plausible(p)) out.push_back({ p, 0, k }); };

    if (cfg.loot) {
        for_each_dict(rdv<uintptr_t>(s.world + offs::World_Loots), [&](int, uintptr_t m) { push(pos_model(m), 0); });
        for_each_dict(rdv<uintptr_t>(s.world + offs::World_VehicleLoots), [&](int, uintptr_t m) { push(pos_model(m), 0); });
    }
    if (cfg.container) for_each_dict(rdv<uintptr_t>(s.world + offs::World_Containers), [&](int, uintptr_t m) { push(pos_model(m), 1); });
    if (cfg.door)      for_each_dict(rdv<uintptr_t>(s.world + offs::World_Doors), [&](int, uintptr_t m) { push(pos_model(m), 2); });
    if (cfg.car)       for_each_dict(rdv<uintptr_t>(s.world + offs::World_Cars), [&](int, uintptr_t m) {
        Vec3 v{}; uintptr_t t = rdv<uintptr_t>(m + offs::Car_Transform);
        if (t) { rd(t + 0x38, v.x); rd(t + 0x3C, v.y); rd(t + 0x40, v.z); } push(v, 3); });
    if (cfg.heli)      for_each_dict(rdv<uintptr_t>(s.world + offs::World_Helis), [&](int, uintptr_t m) {
        Vec3 v{}; uintptr_t t = rdv<uintptr_t>(m + offs::Heli_Transform);
        if (t) { rd(t + 0x38, v.x); rd(t + 0x3C, v.y); rd(t + 0x40, v.z); } push(v, 4); });
    if (cfg.hideobj)   for_each_dict(rdv<uintptr_t>(s.world + offs::World_HideObjects), [&](int, uintptr_t m) {
        Vec3 v{}; uintptr_t d = rdv<uintptr_t>(m + offs::HideObj_Desc);
        if (d) { rd(d + offs::HideDesc_Position, v.x); rd(d + offs::HideDesc_Position + 4, v.y); rd(d + offs::HideDesc_Position + 8, v.z); }
        push(v, 5); });
}

static bool camera(const State& s, std::vector<Ent>& ents, Cam& c) {
    if (ents.empty()) return false;
    int li = 0;
    if (plausible(c.pos)) {
        float bd = 1e18f;
        for (size_t i = 0; i < ents.size(); ++i) {
            float dx = ents[i].pos.x - c.pos.x, dy = ents[i].pos.y - c.pos.y, dz = ents[i].pos.z - c.pos.z;
            float d = dx * dx + dy * dy + dz * dz;
            if (d < bd) { bd = d; li = (int)i; }
        }
    }
    ents[li].self = true;
    c.pos = ents[li].pos; c.pos.y += cfg.eye;
    Vec3 look{};
    if (!rd(s.cam + offs::CameraModel_LookAt, look)) return false;
    if (!std::isfinite(look.x) || (look.x == 0.f && look.y == 0.f)) return false;
    c.pitch = look.x; c.yaw = look.y; c.fov = cfg.fov;
    for (auto& e : ents) {
        float dx = e.pos.x - c.pos.x, dy = e.pos.y - c.pos.y, dz = e.pos.z - c.pos.z;
        e.dist = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    return true;
}

static int g_w, g_h;

static bool w2s(const Vec3& wp, const Cam& c, float& sx, float& sy) {
    float dx = wp.x - c.pos.x, dy = wp.y - c.pos.y, dz = wp.z - c.pos.z;
    float cy = cosf(-c.yaw), syw = sinf(-c.yaw);
    float rx = dx * cy - dz * syw, ry = dy, rz = dx * syw + dz * cy;
    float cp = cosf(-c.pitch), sp = sinf(-c.pitch);
    float vx = rx, vy = ry * cp - rz * sp, vz = ry * sp + rz * cp;
    if (vz < 0.1f) return false;
    float f = (g_h * 0.5f) / tanf(c.fov * 0.5f * 3.14159265f / 180.f);
    sx = g_w * 0.5f + (vx * f) / vz; sy = g_h * 0.5f - (vy * f) / vz;
    return true;
}

static void do_aim(const State& s, const std::vector<Ent>& ents, const Cam& c) {
    bool held = (GetAsyncKeyState(cfg.aimKey) & 0x8000) != 0;
    uintptr_t aim = s.battle ? rdv<uintptr_t>(s.battle + offs::Battle_AimTarget) : 0;
    if ((!cfg.silent && !cfg.mouseAim) || !held || ents.empty()) {
        if (cfg.silent && aim) { wr<int>(aim + offs::Aim_CharacterId, 0); wr<int>(aim + offs::Aim_TargetType, 0); }
        return;
    }
    int best = 0; float bd = 1e9f, tx = 0, ty = 0;
    for (const auto& e : ents) {
        if (e.self || e.dist > cfg.aimDist) continue;
        float sx, sy;
        if (!w2s({ e.pos.x, e.pos.y + cfg.bone, e.pos.z }, c, sx, sy)) continue;
        float d = hypotf(sx - g_w / 2.f, sy - g_h / 2.f);
        if (d < cfg.aimFov && d < bd) { bd = d; best = e.id; tx = sx; ty = sy; }
    }
    if (!best) return;
    if (cfg.silent && aim) { wr<int>(aim + offs::Aim_CharacterId, best); wr<int>(aim + offs::Aim_TargetType, 3); }
    if (cfg.mouseAim) {
        POINT p{}; GetCursorPos(&p);
        SetCursorPos((int)(p.x + (tx - p.x) * cfg.smooth), (int)(p.y + (ty - p.y) * cfg.smooth));
    }
}

// ------------------------------------------------------------- overlay
static HDC g_dc; static HBITMAP g_bmp; static HDC g_sdc;
static const COLORREF KEY = RGB(1, 1, 1);

struct UI {
    HDC dc{}; int mx = 0, my = 0; bool down = false, prev = false; int rowY = 0, drag = -1;
    int px = 24, py = 24, pw = 320;

    void begin(HDC d) {
        dc = d; POINT p; GetCursorPos(&p); mx = p.x; my = p.y;
        down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }
    bool hit(int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; }
    void end() { prev = down; if (!down) drag = -1; }

    void text(const char* s, int x, int y, COLORREF col, int size = 14) {
        SetTextColor(dc, col); SetBkMode(dc, TRANSPARENT);
        HFONT f = CreateFontA(size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
        HGDIOBJ o = SelectObject(dc, f); TextOutA(dc, x, y, s, (int)strlen(s));
        SelectObject(dc, o); DeleteObject(f);
    }
    void box(int x, int y, int w, int h, COLORREF fill, COLORREF border) {
        HBRUSH b = CreateSolidBrush(fill); RECT r{ x, y, x + w, y + h };
        FillRect(dc, &r, b); DeleteObject(b);
        HPEN p = CreatePen(PS_SOLID, 1, border); HGDIOBJ o = SelectObject(dc, p);
        MoveToEx(dc, x, y, nullptr); LineTo(dc, x + w, y); LineTo(dc, x + w, y + h); LineTo(dc, x, y + h); LineTo(dc, x, y);
        SelectObject(dc, o); DeleteObject(p);
    }
    void check(const char* label, bool& v) {
        int y = rowY;
        if (hit(px + 8, y, pw - 16, 20) && down && !prev) v = !v;
        box(px + 10, y + 3, 13, 13, v ? RGB(40, 190, 110) : RGB(28, 28, 34), RGB(90, 90, 100));
        text(label, px + 32, y, v ? RGB(235, 255, 240) : RGB(175, 175, 185));
        rowY += 23;
    }
    void slider(const char* label, float& v, float lo, float hi, int id, const char* fmt = "%.0f") {
        int y = rowY; char b[96], val[24];
        snprintf(val, sizeof val, fmt, v);
        snprintf(b, sizeof b, "%s: %s", label, val);
        text(b, px + 10, y, RGB(215, 215, 225));
        int tx = px + 10, ty = y + 19, tw = pw - 24;
        box(tx, ty, tw, 5, RGB(34, 34, 42), RGB(70, 70, 80));
        int k = (int)((v - lo) / (hi - lo) * tw);
        box(tx + k - 2, ty - 4, 5, 13, RGB(60, 210, 130), RGB(60, 210, 130));
        if (hit(tx, ty - 6, tw, 18) && down) drag = id;
        if (drag == id) v = lo + std::clamp((float)(mx - tx) / tw, 0.f, 1.f) * (hi - lo);
        rowY += 44;
    }
};

static void outlined(HDC dc, int x, int y, const std::string& s, COLORREF col) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0));
    TextOutA(dc, x + 1, y, s.c_str(), (int)s.size()); TextOutA(dc, x - 1, y, s.c_str(), (int)s.size());
    TextOutA(dc, x, y + 1, s.c_str(), (int)s.size()); TextOutA(dc, x, y - 1, s.c_str(), (int)s.size());
    SetTextColor(dc, col); TextOutA(dc, x, y, s.c_str(), (int)s.size());
}

static void corners(HDC dc, int x, int y, int w, int h, COLORREF c) {
    HPEN p = CreatePen(PS_SOLID, 1, c); HGDIOBJ o = SelectObject(dc, p);
    MoveToEx(dc, x, y, nullptr); LineTo(dc, x + w / 4, y);
    MoveToEx(dc, x + w * 3 / 4, y, nullptr); LineTo(dc, x + w, y);
    MoveToEx(dc, x + w, y, nullptr); LineTo(dc, x + w, y + h / 4);
    MoveToEx(dc, x + w, y + h * 3 / 4, nullptr); LineTo(dc, x + w, y + h);
    MoveToEx(dc, x + w, y + h, nullptr); LineTo(dc, x + w * 3 / 4, y + h);
    MoveToEx(dc, x + w / 4, y + h, nullptr); LineTo(dc, x, y + h);
    MoveToEx(dc, x, y + h, nullptr); LineTo(dc, x, y + h * 3 / 4);
    MoveToEx(dc, x, y + h / 4, nullptr); LineTo(dc, x, y);
    SelectObject(dc, o); DeleteObject(p);
}

static void render(HDC dc, const std::vector<Ent>& ents, const std::vector<World>& world, const Cam& c) {
    HFONT f = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    HGDIOBJ of = SelectObject(dc, f);

    if (cfg.esp) {
        for (const auto& e : ents) {
            if (e.self || e.dist > cfg.playerDist) continue;
            float hx, hy, fx, fy;
            if (!w2s({ e.pos.x, e.pos.y + 1.75f, e.pos.z }, c, hx, hy)) continue;
            if (!w2s(e.pos, c, fx, fy)) continue;
            int bh = (int)(fy - hy); if (bh < 6) continue;
            int bw = std::max(4, bh / 2), bx = (int)hx - bw / 2, by = (int)hy;
            if (cfg.box) corners(dc, bx, by, bw, bh, RGB(0, 255, 120));
            if (cfg.name) outlined(dc, bx, by - 17, e.name, RGB(235, 235, 240));
            if (cfg.dist) outlined(dc, bx, by + bh + 2, std::to_string((int)e.dist) + "m", RGB(160, 255, 200));
        }
        for (const auto& w : world) {
            if (w.dist > cfg.worldDist) continue;
            float sx, sy; if (!w2s(w.pos, c, sx, sy)) continue;
            static const COLORREF cols[] = { RGB(255, 215, 60), RGB(90, 200, 255), RGB(150, 150, 255), RGB(255, 120, 200), RGB(255, 150, 60), RGB(180, 255, 120) };
            static const char* names[] = { "loot", "stash", "door", "car", "heli", "hide" };
            outlined(dc, (int)sx, (int)sy, names[w.kind], cols[w.kind]);
        }
    }
    if (cfg.crosshair && !g_menu) {
        HPEN p = CreatePen(PS_SOLID, 1, RGB(0, 255, 120)); HGDIOBJ o = SelectObject(dc, p);
        MoveToEx(dc, g_w / 2 - 6, g_h / 2, nullptr); LineTo(dc, g_w / 2 - 2, g_h / 2);
        MoveToEx(dc, g_w / 2 + 2, g_h / 2, nullptr); LineTo(dc, g_w / 2 + 6, g_h / 2);
        MoveToEx(dc, g_w / 2, g_h / 2 - 6, nullptr); LineTo(dc, g_w / 2, g_h / 2 - 2);
        MoveToEx(dc, g_w / 2, g_h / 2 + 2, nullptr); LineTo(dc, g_w / 2, g_h / 2 + 6);
        SelectObject(dc, o); DeleteObject(p);
    }
    if (cfg.fovCircle && (cfg.silent || cfg.mouseAim) && !g_menu) {
        HPEN p = CreatePen(PS_DOT, 1, RGB(60, 200, 130)); HGDIOBJ o = SelectObject(dc, p);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, (int)(g_w / 2 - cfg.aimFov), (int)(g_h / 2 - cfg.aimFov),
                    (int)(g_w / 2 + cfg.aimFov), (int)(g_h / 2 + cfg.aimFov));
        SelectObject(dc, ob); SelectObject(dc, o); DeleteObject(p);
    }
    SelectObject(dc, of); DeleteObject(f);
}

static void draw_menu(UI& ui, const State& st, size_t nent, size_t nworld, int fps) {
    static int tab = 0;
    ui.begin(g_dc);
    ui.box(ui.px, ui.py, ui.pw, 470, RGB(16, 16, 21), RGB(60, 60, 72));
    ui.text(ROBRUN_INTERNAL ? "ROB RUN  [internal]" : "ROB RUN  [external]", ui.px + 14, ui.py + 12, RGB(90, 240, 150), 17);
    ui.text("insert to close", ui.px + ui.pw - 116, ui.py + 15, RGB(110, 110, 125), 12);

    const char* tabs[] = { "VISUALS", "AIM", "MISC" };
    for (int i = 0; i < 3; ++i) {
        int tx = ui.px + 12 + i * 96; bool on = tab == i;
        ui.box(tx, ui.py + 42, 90, 24, on ? RGB(30, 120, 75) : RGB(26, 26, 32), RGB(60, 60, 72));
        if (ui.hit(tx, ui.py + 42, 90, 24) && ui.down && !ui.prev) tab = i;
        ui.text(tabs[i], tx + 14, ui.py + 47, on ? RGB(240, 255, 245) : RGB(160, 160, 175));
    }
    ui.rowY = ui.py + 78;

    if (tab == 0) {
        ui.check("master esp", cfg.esp); ui.check("boxes", cfg.box); ui.check("names", cfg.name);
        ui.check("distance", cfg.dist); ui.check("loot", cfg.loot); ui.check("stashes", cfg.container);
        ui.check("vehicles", cfg.car); ui.check("helicopters", cfg.heli); ui.check("hide objects", cfg.hideobj);
        ui.slider("player range", cfg.playerDist, 50, 1200, 1);
        ui.slider("world range", cfg.worldDist, 20, 500, 2);
    } else if (tab == 1) {
        ui.check("silent aim", cfg.silent); ui.check("mouse aim", cfg.mouseAim);
        ui.slider("aim fov", cfg.aimFov, 20, 400, 3); ui.slider("aim range", cfg.aimDist, 20, 600, 4);
        ui.slider("smoothing", cfg.smooth, 0.05f, 1.f, 5, "%.2f");
        ui.slider("aim height", cfg.bone, 0.8f, 2.f, 6, "%.2f");
        ui.slider("camera fov", cfg.fov, 60, 120, 7); ui.slider("eye height", cfg.eye, 0.6f, 2.4f, 8, "%.2f");
        ui.check("fov circle", cfg.fovCircle);
        ui.text(cfg.aimKey == VK_XBUTTON1 ? "aim key: mouse4" : "aim key: mouse5", ui.px + 10, ui.rowY, RGB(150, 150, 165));
        if (ui.hit(ui.px + 10, ui.rowY, ui.pw - 20, 20) && ui.down && !ui.prev)
            cfg.aimKey = cfg.aimKey == VK_XBUTTON1 ? VK_XBUTTON2 : VK_XBUTTON1;
        ui.rowY += 24;
    } else {
        ui.check("crosshair", cfg.crosshair); ui.check("stream proof", cfg.streamproof);
        ui.rowY += 6;
        char b[128];
        snprintf(b, sizeof b, "players  %zu", nent);  ui.text(b, ui.px + 10, ui.rowY, RGB(150, 150, 165)); ui.rowY += 18;
        snprintf(b, sizeof b, "world    %zu", nworld); ui.text(b, ui.px + 10, ui.rowY, RGB(150, 150, 165)); ui.rowY += 18;
        snprintf(b, sizeof b, "fps      %d", fps);    ui.text(b, ui.px + 10, ui.rowY, RGB(150, 150, 165)); ui.rowY += 18;
        snprintf(b, sizeof b, "battle   %s", st.battle ? "ok" : "none");
        ui.text(b, ui.px + 10, ui.rowY, st.battle ? RGB(120, 230, 160) : RGB(240, 150, 120)); ui.rowY += 26;
        if (ui.hit(ui.px + 10, ui.rowY, 120, 24) && ui.down && !ui.prev) save_cfg();
        ui.box(ui.px + 10, ui.rowY, 120, 24, RGB(30, 120, 75), RGB(60, 60, 72));
        ui.text("save config", ui.px + 26, ui.rowY + 4, RGB(240, 255, 245)); ui.rowY += 30;
        if (ui.hit(ui.px + 10, ui.rowY, 120, 24) && ui.down && !ui.prev) load_cfg();
        ui.box(ui.px + 10, ui.rowY, 120, 24, RGB(40, 40, 50), RGB(60, 60, 72));
        ui.text("load config", ui.px + 26, ui.rowY + 4, RGB(220, 220, 230));
    }
    ui.end();

    POINT p{}; GetCursorPos(&p);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 255, 170)); HGDIOBJ op = SelectObject(g_dc, pen);
    MoveToEx(g_dc, p.x, p.y, nullptr); LineTo(g_dc, p.x + 11, p.y + 7);
    LineTo(g_dc, p.x + 6, p.y + 8); LineTo(g_dc, p.x + 4, p.y + 15); LineTo(g_dc, p.x, p.y);
    SelectObject(g_dc, op); DeleteObject(pen);
}

// ------------------------------------------------------------- entry
static DWORD WINAPI run(LPVOID) {
    SetProcessDPIAware();
    load_cfg();
    if (!attach()) return 1;

    g_w = GetSystemMetrics(SM_CXSCREEN); g_h = GetSystemMetrics(SM_CYSCREEN);
    WNDCLASSA wc{ 0 }; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = g_self ? g_self : GetModuleHandleA(nullptr);
    wc.lpszClassName = "rbov"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        "rbov", "robrun", WS_POPUP, 0, 0, g_w, g_h, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;
    SetLayeredWindowAttributes(hwnd, KEY, 255, LWA_COLORKEY);
    if (cfg.streamproof) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(hwnd, SW_SHOW);

    g_sdc = GetDC(hwnd); g_dc = CreateCompatibleDC(g_sdc);
    g_bmp = CreateCompatibleBitmap(g_sdc, g_w, g_h); SelectObject(g_dc, g_bmp);

    State st; std::vector<Ent> ents; std::vector<World> world;
    std::unordered_map<int, uintptr_t> views; Cam cam{};
    auto last = std::chrono::steady_clock::now(), fpsT = last;
    int frames = 0, fps = 0;
    UI ui; bool lastMenu = false, lastStream = cfg.streamproof;

    while (g_run) {
        if (GetAsyncKeyState(VK_END) & 1) g_run = false;
        if (GetAsyncKeyState(VK_INSERT) & 1) g_menu = !g_menu;

        if (g_menu != lastMenu) {
            LONG_PTR ex = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
            ex = g_menu ? (ex & ~WS_EX_TRANSPARENT) | WS_EX_NOACTIVATE : ex | WS_EX_TRANSPARENT;
            SetWindowLongPtrA(hwnd, GWL_EXSTYLE, ex);
            if (g_menu) { ClipCursor(nullptr); ShowCursor(TRUE); } else ShowCursor(FALSE);
            lastMenu = g_menu;
        }
        if (cfg.streamproof != lastStream) {
            SetWindowDisplayAffinity(hwnd, cfg.streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
            lastStream = cfg.streamproof;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() > 500) {
            last = now;
            if (!st.updater || !rdv<uintptr_t>(st.updater + offs::Updater_Entities)) { State f{}; resolve(f); st = f; }
        }
        if (++frames, std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsT).count() > 500) {
            fps = frames * 2; frames = 0; fpsT = now;
        }

        ents.clear(); views.clear(); world.clear();
        bool ok = st.updater != 0;
        if (ok) collect(st, ents, views);
        bool camOk = ok && !ents.empty() && camera(st, ents, cam);
        if (ok && cfg.esp) collect_world(st, world);
        for (auto& w : world) {
            float dx = w.pos.x - cam.pos.x, dy = w.pos.y - cam.pos.y, dz = w.pos.z - cam.pos.z;
            w.dist = sqrtf(dx * dx + dy * dy + dz * dz);
        }

        PatBlt(g_dc, 0, 0, g_w, g_h, WHITENESS);
        if (camOk) render(g_dc, ents, world, cam);
        if (g_menu) draw_menu(ui, st, ents.size(), world.size(), fps);
        BitBlt(g_sdc, 0, 0, g_w, g_h, g_dc, 0, 0, SRCCOPY);

        if (camOk && !g_menu) do_aim(st, ents, cam);
        Sleep(1);
    }

    save_cfg();
#if !ROBRUN_INTERNAL
    CloseHandle(g_proc);
#endif
    return 0;
}

#if ROBRUN_INTERNAL
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h; DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    }
    return TRUE;
}
#else
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    g_self = h;
    if (!attach()) { MessageBoxA(nullptr, "ROB RUN.exe is not running.", "robrun", MB_ICONERROR); return 1; }
    run(nullptr);
    return 0;
}
#endif