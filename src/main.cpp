// Auto Play - plays a level by itself.
//
// Nothing is read from the screen: the mod asks the game for the real hitbox of every object near the player
// (spikes, blocks, orbs, pads, portals) and for the player's real gamemode, speed, size and gravity.
// So it does not matter whether hitboxes are shown, what the level looks like, or how it is decorated.
//
//   cube / robot / ball / spider : the next second is simulated with every possible press time,
//                                  and the button is pressed in the middle of the times that survive
//   ship / ufo / wave / swing    : the free gap ahead is found and the player is steered through it
//
// The decision code is in brain.hpp (no game headers, tested outside the game).
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include "brain.hpp"

using namespace geode::prelude;

namespace {

struct Entry {
    GameObject* obj;
    float x;
    int kind;
    float val;
};

std::vector<Entry> g_index;         // every object that matters, sorted by x
PlayLayer* g_owner = nullptr;       // the level the index belongs to
std::vector<brain::Ob> g_window;    // objects near the player, rebuilt every frame

bool g_enabled = true;
float g_lookAhead = 1.0f, g_flyAhead = 0.22f;
int g_settingsAge = 0;

bool g_down = false;                // is the bot holding the button
bool g_usedBot = false;             // did the bot press anything in this attempt
bool g_haveLast = false, g_haveTarget = false, g_wasGround = true, g_tapRelease = false;
float g_lastX = 0, g_lastY = 0;
float g_now = 0, g_releaseAt = 0, g_targetT = 0, g_lastPress = -9, g_lastTap = -9, g_flyKeep = 0;
int g_lastMode = -1;

const brain::Arc ARC_DEFAULT[2] = {{603.7f, 2794.f}, {483.f, 2794.f}};   // normal, mini
brain::Arc g_arc[2] = {ARC_DEFAULT[0], ARC_DEFAULT[1]};
brain::ArcLearner g_learn;

void loadSettings() {
    auto mod = Mod::get();
    g_enabled = mod->getSettingValue<bool>("enabled");
    g_lookAhead = static_cast<float>(mod->getSettingValue<double>("look-ahead"));
    g_flyAhead = static_cast<float>(mod->getSettingValue<double>("fly-ahead"));
    g_settingsAge = 0;
}

// what an object means for the bot. false = ignore it
bool classify(GameObject* o, int& kind, float& val) {
    using namespace brain;
    switch (o->m_objectID) {
        case 200:  kind = K_SPEED; val = 251.16f; return true;
        case 201:  kind = K_SPEED; val = 311.58f; return true;
        case 202:  kind = K_SPEED; val = 387.42f; return true;
        case 203:  kind = K_SPEED; val = 468.f;   return true;
        case 1334: kind = K_SPEED; val = 576.f;   return true;
        default: break;
    }
    val = 0;
    switch (o->m_objectType) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:       kind = K_HAZARD; return true;
        case GameObjectType::Solid:
        case GameObjectType::Slope:
        case GameObjectType::Breakable:            kind = K_SOLID; return true;
        case GameObjectType::YellowJumpRing:       kind = K_ORB; val = 1.0f;  return true;
        case GameObjectType::PinkJumpRing:         kind = K_ORB; val = 0.72f; return true;
        case GameObjectType::RedJumpRing:          kind = K_ORB; val = 1.38f; return true;
        case GameObjectType::GravityRing:          kind = K_ORB_BLUE;  return true;
        case GameObjectType::GreenRing:            kind = K_ORB_GREEN; return true;
        case GameObjectType::DropRing:             kind = K_ORB_BLACK; return true;
        case GameObjectType::YellowJumpPad:        kind = K_PAD; val = 1.43f; return true;
        case GameObjectType::PinkJumpPad:          kind = K_PAD; val = 0.94f; return true;
        case GameObjectType::RedJumpPad:           kind = K_PAD; val = 1.86f; return true;
        case GameObjectType::GravityPad:           kind = K_PAD_GRAV; return true;
        case GameObjectType::InverseGravityPortal: kind = K_GRAVITY; val = -1; return true;
        case GameObjectType::NormalGravityPortal:  kind = K_GRAVITY; val = 1;  return true;
        case GameObjectType::GravityTogglePortal:  kind = K_GRAVITY; val = 0;  return true;
        case GameObjectType::CubePortal:           kind = K_MODE; val = CUBE;   return true;
        case GameObjectType::ShipPortal:           kind = K_MODE; val = SHIP;   return true;
        case GameObjectType::BallPortal:           kind = K_MODE; val = BALL;   return true;
        case GameObjectType::UfoPortal:            kind = K_MODE; val = UFO;    return true;
        case GameObjectType::WavePortal:           kind = K_MODE; val = WAVE;   return true;
        case GameObjectType::RobotPortal:          kind = K_MODE; val = ROBOT;  return true;
        case GameObjectType::SpiderPortal:         kind = K_MODE; val = SPIDER; return true;
        case GameObjectType::SwingPortal:          kind = K_MODE; val = SWING;  return true;
        default: return false;
    }
}

void buildIndex(PlayLayer* pl) {
    g_index.clear();
    g_owner = pl;
    if (!pl->m_objects) return;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj || obj->m_isNoTouch) continue;
        int kind; float val;
        if (!classify(obj, kind, val)) continue;
        g_index.push_back({obj, obj->getPositionX(), kind, val});
    }
    std::sort(g_index.begin(), g_index.end(), [](const Entry& a, const Entry& b) { return a.x < b.x; });
    log::info("Auto Play: {} objects indexed", g_index.size());
}

// the real hitboxes of everything from a little behind the player to `range` units ahead
void collectWindow(float px, float range) {
    g_window.clear();
    const float pad = 150.f;
    auto it = std::lower_bound(g_index.begin(), g_index.end(), px - pad, [](const Entry& e, float x) { return e.x < x; });
    for (; it != g_index.end() && it->x <= px + range + pad; ++it) {
        GameObject* o = it->obj;
        if (o->m_isGroupDisabled) continue;         // switched off by a toggle trigger
        CCRect r = o->getObjectRect();
        if (r.size.width <= 0 || r.size.height <= 0) continue;
        g_window.push_back({r.getMinX(), r.getMinY(), r.getMaxX(), r.getMaxY(), it->kind, it->val});
    }
    std::sort(g_window.begin(), g_window.end(), [](const brain::Ob& a, const brain::Ob& b) { return a.x0 < b.x0; });
}

int modeOf(PlayerObject* p) {
    if (p->m_isShip) return brain::SHIP;
    if (p->m_isBird) return brain::UFO;
    if (p->m_isBall) return brain::BALL;
    if (p->m_isDart) return brain::WAVE;
    if (p->m_isRobot) return brain::ROBOT;
    if (p->m_isSpider) return brain::SPIDER;
    if (p->m_isSwing) return brain::SWING;
    return brain::CUBE;
}

float speedOf(PlayerObject* p) {
    float s = p->m_playerSpeed;
    if (s < 0.8f) return 251.16f;
    if (s < 1.0f) return 311.58f;
    if (s < 1.2f) return 387.42f;
    if (s < 1.45f) return 468.f;
    return 576.f;
}

void resetBrain() {
    g_haveLast = false; g_haveTarget = false; g_wasGround = true; g_tapRelease = false;
    g_now = 0; g_releaseAt = 0; g_lastPress = -9; g_lastTap = -9;
    g_lastMode = -1;
    g_learn.reset();
}

} // namespace

class $modify(AutoPlayLayer, PlayLayer) {
    void setButton(bool down) {
        if (g_down == down) return;
        g_down = down;
        if (down) g_usedBot = true;
        this->handleButton(down, 1, true);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_index.clear();
        g_owner = nullptr;
        g_down = false;
        g_usedBot = false;
        g_arc[0] = ARC_DEFAULT[0];
        g_arc[1] = ARC_DEFAULT[1];
        resetBrain();
        loadSettings();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        if (g_down) { g_down = false; this->handleButton(false, 1, true); }
        PlayLayer::resetLevel();
        g_usedBot = false;
        resetBrain();
        loadSettings();
    }

    void levelComplete() {
        // a clear made by the bot is not a real clear: finish it like a start-position run so it is not saved as one
        if (g_usedBot && !m_isPracticeMode) m_isTestMode = true;
        PlayLayer::levelComplete();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        this->think();
    }

    void think() {
        using namespace brain;
        if (++g_settingsAge > 120) loadSettings();
        PlayerObject* p = m_player1;
        if (!g_enabled || !p || p->m_isDead || p->m_isPlatformer || m_hasCompletedLevel || m_levelEndAnimationStarted) {
            if (g_down) this->setButton(false);
            g_haveLast = false;
            return;
        }
        if (g_owner != this || g_index.empty()) {
            if (g_owner != this) buildIndex(this);
            if (g_index.empty()) return;
        }

        State s;
        s.mode = modeOf(p);
        s.x = p->getPositionX();
        s.y = p->getPositionY();
        s.speed = speedOf(p);
        s.g = p->m_isUpsideDown ? -1 : 1;
        s.onGround = p->m_isOnGround;
        s.mini = p->m_vehicleSize < 0.9f;
        s.arc = g_arc[s.mini ? 1 : 0];

        // time is measured by how far the player moved: that is the game's own clock, whatever the fps
        if (!g_haveLast) { g_haveLast = true; g_lastX = s.x; g_lastY = s.y; g_flyKeep = s.y; g_wasGround = s.onGround; return; }
        float frame = (s.x - g_lastX) / s.speed;
        if (frame <= 0.0005f || frame > 0.1f) { g_lastX = s.x; g_lastY = s.y; g_wasGround = s.onGround; return; }
        s.vy = (s.y - g_lastY) / frame;
        g_now += frame;

        if (s.mode != g_lastMode) {
            if (g_lastMode != -1 && g_down) this->setButton(false);
            g_lastMode = s.mode;
            g_haveTarget = false; g_tapRelease = false; g_flyKeep = s.y;
            g_learn.reset();
        }
        if (p->m_isDashing) { g_lastX = s.x; g_lastY = s.y; g_wasGround = s.onGround; return; }

        CCRect pr = p->getObjectRect();
        s.hw = pr.size.width / 2;
        s.hh = pr.size.height / 2;
        if (s.hw < 3 || s.hw > 25 || s.hh < 3 || s.hh > 25) s.hw = s.hh = 15.f * p->m_vehicleSize;
        if (s.mode == WAVE) s.hw = s.hh = s.mini ? 4.f : 6.f;

        // flying modes, ball and spider live between a floor and a ceiling that depend on the portal
        s.floorY = 90; s.ceilY = 1e9f;
        if (s.mode != CUBE && s.mode != ROBOT) {
            float h = s.mode == BALL ? 240.f : (s.mode == SPIDER ? 270.f : 300.f);
            float floor = std::max(90.f, std::ceil((p->m_lastPortalPos.y - h / 2 - 30.f) / 30.f) * 30.f);
            if (s.y > floor - 5 && s.y < floor + h + 5) { s.floorY = floor; s.ceilY = floor + h; }
        }

        bool jumpMode = s.mode == CUBE || s.mode == ROBOT || s.mode == BALL || s.mode == SPIDER;
        collectWindow(s.x, s.speed * (jumpMode ? g_lookAhead : std::max(0.6f, g_flyAhead + 0.35f)));

        if (jumpMode) {
            // the frame average is half a frame old while in the air
            if (!s.onGround) s.vy -= s.g * s.arc.G * (s.mode == BALL ? 0.6f : 1.f) * frame / 2;
            else s.vy = 0;

            if (s.mode == CUBE) {
                bool clean = true;      // no pad or orb close by that could have changed the jump
                for (auto& o : g_window)
                    if (o.kind >= K_ORB && o.kind <= K_PAD_GRAV && o.x1 > s.x - 60 && o.x0 < s.x + 60) { clean = false; break; }
                if (!clean) g_learn.cancel();
                bool started = clean && g_wasGround && !s.onGround && g_now - g_lastPress < 0.12f;
                int m = s.mini ? 1 : 0;
                g_learn.frame(s.y, s.onGround, frame, s.g, started, g_arc[m], ARC_DEFAULT[m]);
                s.arc = g_arc[m];
            }

            if (g_down) {
                if (g_now >= g_releaseAt) this->setButton(false);
            } else {
                JumpPlan plan = planJump(s, g_window, g_lookAhead);
                if (!plan.needed) {
                    g_haveTarget = false;
                } else if (plan.found) {
                    bool press = false;
                    if (plan.first <= 0.01f) {
                        // the window is open: press in its middle, or right now if it is about to close
                        if (!g_haveTarget) { g_targetT = g_now + (plan.last - plan.first) / 2; g_haveTarget = true; }
                        if (g_now >= g_targetT || plan.last <= frame * 2) press = true;
                    } else {
                        g_targetT = g_now + (plan.first + plan.last) / 2;
                        g_haveTarget = true;
                    }
                    if (press) {
                        this->setButton(true);
                        g_releaseAt = g_now + plan.hold;
                        g_lastPress = g_now;
                        g_haveTarget = false;
                    }
                } else if (plan.bestDelay >= 0 && plan.bestDelay <= frame && plan.bestT > plan.baseT + 0.05f) {
                    // nothing survives the whole look-ahead: at least take the press that lives longest
                    this->setButton(true);
                    g_releaseAt = g_now + 0.05f;
                    g_lastPress = g_now;
                }
            }
        } else {
            g_flyKeep = flyTarget(s, g_window, g_flyAhead * (s.mode == WAVE ? 0.6f : 1.f), g_flyKeep);
            if (g_tapRelease) {
                this->setButton(false);
                g_tapRelease = false;
            } else {
                FlyOut out = flyControl(s, g_flyKeep, g_down, g_now - g_lastTap);
                if (s.mode == SHIP) {
                    this->setButton(out.hold);
                } else if (s.mode == WAVE) {
                    bool up = waveDecide(s, g_window, out.hold != (s.g < 0));
                    this->setButton(up != (s.g < 0));
                } else if (out.tap) {
                    if (g_down) this->setButton(false);
                    else { this->setButton(true); g_tapRelease = true; g_lastTap = g_now; }
                }
            }
        }

        g_lastX = s.x; g_lastY = s.y; g_wasGround = s.onGround;
    }
};

class $modify(AutoPlayPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        loadSettings();
        auto win = CCDirector::get()->getWinSize();
        auto spr = ButtonSprite::create(g_enabled ? "Auto Play: ON" : "Auto Play: OFF", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AutoPlayPause::onAutoPlay));
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        btn->setPosition({win.width / 2, win.height - 20});
        menu->addChild(btn);
        this->addChild(menu, 20);
    }

    void onAutoPlay(CCObject* sender) {
        bool on = !Mod::get()->getSettingValue<bool>("enabled");
        Mod::get()->setSettingValue<bool>("enabled", on);
        g_enabled = on;
        if (auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender))
            if (auto spr = typeinfo_cast<ButtonSprite*>(item->getNormalImage()))
                spr->setString(on ? "Auto Play: ON" : "Auto Play: OFF");
    }
};
