// Auto Play - the "brain". Plain C++ with no game headers, so it can be tested outside the game.
//
// Everything is in level units (one block = 30), y up, the ground surface is at y = 90.
// The game side hands over the player's state and the real rectangles of every object near the player;
// the brain answers "press now", "release now" or "do nothing".
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace brain {

enum Mode { CUBE, SHIP, BALL, UFO, WAVE, ROBOT, SPIDER, SWING };

enum Kind {
    K_HAZARD,      // touching it kills
    K_SOLID,       // can be stood on, kills when hit from the side
    K_ORB,         // val = jump strength compared to a normal jump
    K_ORB_BLUE,    // flips gravity
    K_ORB_GREEN,   // flips gravity and jumps
    K_ORB_BLACK,   // shoots towards the ground
    K_PAD,         // val = jump strength
    K_PAD_GRAV,    // flips gravity
    K_GRAVITY,     // portal: val +1 normal, -1 upside down, 0 toggle
    K_MODE,        // gamemode portal: val = Mode
    K_SPEED,       // speed portal: val = units per second
};

struct Ob {
    float x0, y0, x1, y1;
    int kind;
    float val;
};

struct Arc {
    float v0;   // jump start speed (units / s)
    float G;    // gravity (units / s^2)
};

struct State {
    int mode = CUBE;
    float x = 0, y = 0, vy = 0;
    int g = 1;                  // +1 gravity pulls down, -1 gravity pulls up
    bool onGround = false;
    bool mini = false;
    float speed = 311.58f;      // units / s
    float hw = 15, hh = 15;     // half size of the hitbox
    float floorY = 90;
    float ceilY = 1e9f;         // 1e9 = no ceiling
    Arc arc{603.7f, 2794.f};
    float heldFor = 0;          // the button is down right now and stays down this long
};

constexpr float SIM_DT = 1.f / 120.f;
constexpr float HAZARD_MARGIN = 1.5f;   // hazards are treated as a little bigger than they are

struct Result {
    bool dead;
    float t;    // time of death, or the horizon
    int acted;  // how many presses actually did something (jump, orb, flip)
};

// Plays the level forward for `horizon` seconds with up to 2 button presses at the given times.
inline Result simulate(const State& s, const std::vector<Ob>& obs, float horizon, const float* press, int nPress, float hold) {
    static std::vector<uint8_t> used;
    used.assign(obs.size(), 0);

    float x = s.x, y = s.y, vy = s.vy, speed = s.speed;
    int g = s.g;
    bool ground = s.onGround;
    const float hw = s.hw, hh = s.hh;
    const float v0 = s.arc.v0, G = s.arc.G * (s.mode == BALL ? 0.6f : 1.f);
    const float vRobot = v0 * 0.5f;
    bool held = s.heldFor > 0, click = false, boosting = false;
    float releaseAt = s.heldFor, boostT = 0;
    int pi = 0, acted = 0;
    size_t lo = 0;
    const size_t n = obs.size();

    for (float t = 0; t < horizon; t += SIM_DT) {
        if (pi < nPress && t >= press[pi]) { held = true; click = true; releaseAt = t + hold; pi++; }
        if (held && t >= releaseAt) { held = false; click = false; boosting = false; }

        if (held && ground) {
            if (s.mode == CUBE) { vy = g * v0; ground = false; if (click) acted++; click = false; }
            else if (s.mode == ROBOT && click) { vy = g * vRobot; boosting = true; boostT = 0; ground = false; click = false; acted++; }
            else if (s.mode == BALL && click) { g = -g; vy = -g * 150.f; ground = false; click = false; acted++; }
            else if (s.mode == SPIDER && click) {
                // jump straight to the next surface in the opposite direction
                float best = g > 0 ? s.ceilY : s.floorY;
                bool hazard = false;
                for (size_t i = lo; i < n && obs[i].x0 <= x + hw; i++) {
                    const Ob& o = obs[i];
                    if (o.x1 < x - hw || (o.kind != K_SOLID && o.kind != K_HAZARD)) continue;
                    if (g > 0 && o.y0 >= y + hh - 2 && o.y0 < best) { best = o.y0; hazard = o.kind == K_HAZARD; }
                    if (g < 0 && o.y1 <= y - hh + 2 && o.y1 > best) { best = o.y1; hazard = o.kind == K_HAZARD; }
                }
                if (hazard || best > 1e8f) return {true, t, acted};
                y = g > 0 ? best - hh : best + hh;
                g = -g; vy = 0; click = false; acted++;
            }
        }

        if (boosting) {
            if (held && boostT < 0.25f) { vy = g * vRobot; boostT += SIM_DT; }
            else boosting = false;
        }
        if (!boosting) vy -= g * G * SIM_DT;
        vy = std::clamp(vy, -900.f, 900.f);
        float prevY = y;
        y += vy * SIM_DT;
        x += speed * SIM_DT;
        ground = false;

        if (y - hh <= s.floorY) { y = s.floorY + hh; if (vy < 0) vy = 0; if (g > 0) ground = true; }
        if (y + hh >= s.ceilY)  { y = s.ceilY - hh;  if (vy > 0) vy = 0; if (g < 0) ground = true; }
        if (y > s.y + 900.f) return {true, t, acted};   // fell into the sky

        while (lo < n && obs[lo].x1 < x - hw - 40.f) lo++;
        for (size_t i = lo; i < n && obs[i].x0 <= x + hw + 2.f; i++) {
            const Ob& o = obs[i];
            float m = o.kind == K_HAZARD ? HAZARD_MARGIN : 0.f;
            float mx = o.kind == K_SOLID ? speed * SIM_DT : m;   // walls are met one step early
            if (o.x1 + m < x - hw || o.x0 - mx > x + hw || o.y1 + m < y - hh || o.y0 - m > y + hh) continue;
            switch (o.kind) {
                case K_HAZARD: return {true, t, acted};
                case K_SOLID: {
                    // stricter than the game, so a landing the brain counts on really is one
                    bool fromAbove = prevY - hh >= o.y1 - 3.f || y - hh >= o.y1 - 5.f;
                    bool fromBelow = prevY + hh <= o.y0 + 3.f || y + hh <= o.y0 + 5.f;
                    bool canBump = s.mode == BALL || s.mode == SPIDER;
                    if (g > 0) {
                        if (fromAbove) { y = o.y1 + hh; if (vy < 0) vy = 0; ground = true; }
                        else if (fromBelow && canBump) { y = o.y0 - hh; if (vy > 0) vy = 0; }
                        else return {true, t, acted};
                    } else {
                        if (fromBelow) { y = o.y0 - hh; if (vy > 0) vy = 0; ground = true; }
                        else if (fromAbove && canBump) { y = o.y1 + hh; if (vy < 0) vy = 0; }
                        else return {true, t, acted};
                    }
                    break;
                }
                case K_ORB:       if (click && !used[i]) { used[i] = 1; click = false; acted++; boosting = false; vy = g * v0 * o.val; ground = false; } break;
                case K_ORB_BLUE:  if (click && !used[i]) { used[i] = 1; click = false; acted++; boosting = false; g = -g; vy = -g * v0 * 0.4f; ground = false; } break;
                case K_ORB_GREEN: if (click && !used[i]) { used[i] = 1; click = false; acted++; boosting = false; g = -g; vy = g * v0; ground = false; } break;
                case K_ORB_BLACK: if (click && !used[i]) { used[i] = 1; click = false; acted++; boosting = false; vy = -g * v0 * 1.3f; } break;
                case K_PAD:       if (!used[i]) { used[i] = 1; boosting = false; vy = g * v0 * o.val; ground = false; } break;
                case K_PAD_GRAV:  if (!used[i]) { used[i] = 1; boosting = false; g = -g; vy = -g * v0 * 0.4f; ground = false; } break;
                case K_GRAVITY: {
                    int ng = o.val > 0.5f ? 1 : (o.val < -0.5f ? -1 : (used[i] ? g : -g));
                    used[i] = 1;
                    if (ng != g) { g = ng; vy *= 0.5f; ground = false; }
                    break;
                }
                case K_MODE:  if ((int)o.val != s.mode) return {false, horizon, acted}; break;
                case K_SPEED: speed = o.val; break;
            }
        }
    }
    return {false, horizon, acted};
}

// ---------------------------------------------------------------------------------------------
// Cube / robot / ball / spider: find when to press
// ---------------------------------------------------------------------------------------------
struct JumpPlan {
    bool needed = false;    // false: nothing has to be pressed
    bool found = false;     // a press that survives exists
    float first = 0;        // earliest delay that works
    float last = 0;         // latest delay of the same window
    float hold = 0.05f;
    float bestT = 0;        // how long the best try survives (when nothing works)
    float bestDelay = 0;
    float baseT = 0;        // how long doing nothing survives
};

inline bool pressWorks(const State& s, const std::vector<Ob>& obs, float horizon, float d, float hold, float& survived) {
    float p[2] = {d, 0};
    Result r = simulate(s, obs, horizon, p, 1, hold);
    if (r.acted == 0) { survived = 0; return false; }   // a press that does nothing is not a plan
    survived = r.t;
    if (!r.dead) return true;
    // one more press afterwards
    for (float e = d + hold + 0.04f; e < r.t; e += 1.f / 30.f) {
        p[1] = e;
        Result r2 = simulate(s, obs, horizon, p, 2, hold);
        if (r2.t > survived) survived = r2.t;
        if (!r2.dead) return true;
    }
    return false;
}

inline JumpPlan planJump(const State& s, const std::vector<Ob>& obs, float horizon) {
    JumpPlan plan;
    Result base = simulate(s, obs, horizon, nullptr, 0, 0);
    plan.baseT = base.t;
    if (!base.dead) return plan;
    plan.needed = true;
    plan.bestT = base.t;
    plan.bestDelay = -1;

    const float holds[2] = {0.05f, 0.27f};
    int nHolds = s.mode == ROBOT ? 2 : 1;
    float bestWidth = -1;
    const float step = 1.f / 120.f;
    for (int h = 0; h < nHolds; h++) {
        float first = -1, last = -1;
        for (float d = 0; d <= base.t + step; d += step) {
            float survived;
            bool ok = pressWorks(s, obs, horizon, d, holds[h], survived);
            if (survived > plan.bestT + 0.001f) { plan.bestT = survived; plan.bestDelay = d; }
            if (ok) { if (first < 0) first = d; last = d; }
            else if (first >= 0) break;     // end of the first window
        }
        if (first >= 0 && last - first > bestWidth) {
            bestWidth = last - first;
            plan.found = true; plan.first = first; plan.last = last; plan.hold = holds[h];
        }
    }
    return plan;
}

// ---------------------------------------------------------------------------------------------
// Ship / ufo / wave / swing: find the height to aim for
// ---------------------------------------------------------------------------------------------
// Free space in the column around x. Picks the gap closest to yRef that the player fits through.
inline bool gapAt(const std::vector<Ob>& obs, float x, float halfW, float yRef, float need, float floorY, float ceilY, float& lo, float& hi) {
    static std::vector<std::pair<float, float>> blocked;
    blocked.clear();
    for (const Ob& o : obs) {
        if (o.kind != K_HAZARD && o.kind != K_SOLID) continue;
        if (o.x1 < x - halfW || o.x0 > x + halfW) continue;
        float m = o.kind == K_HAZARD ? 3.f : 0.f;
        blocked.push_back({o.y0 - m, o.y1 + m});
    }
    std::sort(blocked.begin(), blocked.end());
    float bestLo = 0, bestHi = 0, bestDist = 1e18f;
    bool any = false;
    float cur = floorY;
    auto consider = [&](float a, float b) {
        if (b - a < need) return;
        float c = std::clamp(yRef, a + need / 2, b - need / 2);
        float d = std::fabs(c - yRef);
        if (d < bestDist) { bestDist = d; bestLo = a; bestHi = b; any = true; }
    };
    for (auto& b : blocked) {
        if (b.first > cur) consider(cur, std::min(b.first, ceilY));
        cur = std::max(cur, b.second);
        if (cur >= ceilY) break;
    }
    if (cur < ceilY) consider(cur, ceilY);
    lo = bestLo; hi = bestHi;
    return any;
}

// Height to fly at. Looks at several columns ahead; the closest ones win.
// keep: the previous target (pass the player's y the first time), so that open space does not make it drift.
inline float flyTarget(const State& s, const std::vector<Ob>& obs, float ahead, float keep) {
    float need = s.hh * 2 + 2;
    float ceil = std::min(s.ceilY, s.floorY + 2000.f);
    if (std::fabs(keep - s.y) > 90.f) keep = s.y;
    float target = keep;
    const int cols = 5;
    float ref = keep;
    float refs[cols];
    float los[cols], his[cols];
    bool ok[cols];
    // walk outwards so that each column picks the gap that continues the previous one
    for (int i = 0; i < cols; i++) {
        float x = s.x + s.speed * ahead * (i + 1) / cols;
        ok[i] = gapAt(obs, x, s.hw + 3, ref, need, s.floorY, ceil, los[i], his[i]);
        if (ok[i]) ref = std::clamp(ref, los[i] + need / 2, his[i] - need / 2);
        refs[i] = ref;
    }
    target = refs[cols - 1];
    // then pull the target back into the nearer gaps, with as much margin as the gap allows
    for (int i = cols - 1; i >= 0; i--) {
        if (!ok[i]) continue;
        float margin = std::min((his[i] - los[i]) / 2, s.hh + (s.mode == UFO ? 22.f : 12.f));
        target = std::clamp(target, los[i] + margin, his[i] - margin);
    }
    return target;
}

// ---------------------------------------------------------------------------------------------
// Learns the real jump from what the player actually does (jump height and gravity differ a little per speed)
// ---------------------------------------------------------------------------------------------
struct ArcLearner {
    bool havePrev = false, prevAir = false, hasV = false, tracking = false;
    float prevY = 0, prevV = 0, startY = 0, maxRel = 0;

    void cancel() { tracking = false; }
    void reset() { havePrev = false; prevAir = false; hasV = false; tracking = false; }

    // call once per frame while in cube mode. jumpStarted: this frame the cube left the ground because of a plain jump
    void frame(float y, bool ground, float dt, int g, bool jumpStarted, Arc& arc, const Arc& def) {
        if (!havePrev || dt <= 0) { havePrev = true; prevY = y; prevAir = !ground; hasV = false; return; }
        float v = (y - prevY) / dt * g;
        if (!ground && prevAir && hasV) {
            float G = (prevV - v) / dt;
            if (G > def.G * 0.7f && G < def.G * 1.3f) arc.G += 0.25f * (G - arc.G);
        }
        if (jumpStarted) { tracking = true; startY = prevY; maxRel = 0; }
        if (tracking) {
            maxRel = std::max(maxRel, (y - startY) * g);
            if (ground) tracking = false;
            else if (v < 0) {
                float v0 = std::sqrt(2 * arc.G * maxRel);
                if (v0 > def.v0 * 0.8f && v0 < def.v0 * 1.25f) arc.v0 += 0.5f * (v0 - arc.v0);
                tracking = false;
            }
        }
        hasV = !ground && prevAir ? true : !ground;
        prevV = v; prevY = y; prevAir = !ground;
    }
};

// What the button should do in a flying mode. sinceTap: seconds since the last tap (ufo / swing).
struct FlyOut { bool hold; bool tap; };
inline FlyOut flyControl(const State& s, float target, bool curHeld, float sinceTap) {
    FlyOut out{false, false};
    bool flipped = s.g < 0;
    switch (s.mode) {
        case SHIP: {
            bool up = s.y + s.vy * 0.12f < target;
            out.hold = up != flipped;
            break;
        }
        case WAVE: {
            bool up = curHeld != flipped;
            if (s.y < target - 1.5f) up = true;
            else if (s.y > target + 1.5f) up = false;
            out.hold = up != flipped;
            break;
        }
        case UFO: {
            float ahead = s.y + s.vy * 0.10f;
            bool low = flipped ? ahead > target + 8.f : ahead < target - 8.f;
            out.tap = low && sinceTap > 0.06f;
            break;
        }
        case SWING: {
            float pull = flipped ? 1.f : -1.f;          // direction gravity pulls in
            float err = target - (s.y + s.vy * 0.12f);
            out.tap = err * pull < 0 && std::fabs(err) > 6.f && sinceTap > 0.1f;
            break;
        }
        default: break;
    }
    return out;
}

// Wave: tries every zig-zag for the next moments and keeps a first move that does not crash.
// preferUp is what the simple "aim for the middle" rule would do; it wins when both moves are fine.
inline bool waveClear(const State& s, const std::vector<Ob>& obs, float x, float y, int depth, bool up, float seg) {
    float slope = s.mini ? 2.f : 1.f;
    float dy = (up ? 1.f : -1.f) * s.speed * slope * seg;
    const int samples = 4;
    for (int k = 1; k <= samples; k++) {
        float px = x + s.speed * seg * k / samples, py = y + dy * k / samples;
        if (py - s.hh < s.floorY || py + s.hh > s.ceilY) return false;
        for (const Ob& o : obs) {
            if (o.x0 > px + s.hw + 2) break;
            if (o.kind != K_HAZARD && o.kind != K_SOLID) continue;
            if (o.x1 + 2 < px - s.hw || o.y1 + 2 < py - s.hh || o.y0 - 2 > py + s.hh) continue;
            return false;
        }
    }
    if (depth <= 1) return true;
    float nx = x + s.speed * seg, ny = y + dy;
    return waveClear(s, obs, nx, ny, depth - 1, up, seg) || waveClear(s, obs, nx, ny, depth - 1, !up, seg);
}

inline bool waveDecide(const State& s, const std::vector<Ob>& obs, bool preferUp) {
    for (int depth = 9; depth >= 2; depth -= 2) {
        bool a = waveClear(s, obs, s.x, s.y, depth, preferUp, 1.f / 30.f);
        if (a) return preferUp;
        bool b = waveClear(s, obs, s.x, s.y, depth, !preferUp, 1.f / 30.f);
        if (b) return !preferUp;
    }
    return preferUp;
}

} // namespace brain
