#include "game_state.h"

#include "rh.h"

#include <cmath>

#include "haptics.h"
#include "log.h"

namespace rdrbh::game {
namespace {

// ---- the event names in the bHaptics workspace (developer portal). One event may drive several devices
// (vest + sleeves + gloves), so there is one name per gameplay moment, with _L / _R for the arm.
namespace ev {
constexpr const char* kRecoilPistol[2] = {"RecoilPistol_R", "RecoilPistol_L"};    // pistols, revolvers
constexpr const char* kRecoilRifle[2] = {"RecoilRifle_R", "RecoilRifle_L"};       // repeaters, rifles, sniper rifles
constexpr const char* kRecoilShotgun[2] = {"RecoilShotgun_R", "RecoilShotgun_L"};
// long guns held with both hands (RDRVR's foregrip hold; [Hands] TwoHanded): _R / _L is still the gun (trigger) hand
constexpr const char* kRecoilRifleTwoHanded[2] = {"RecoilRifle_TwoHanded_R", "RecoilRifle_TwoHanded_L"};
constexpr const char* kRecoilShotgunTwoHanded[2] = {"RecoilShotgun_TwoHanded_R", "RecoilShotgun_TwoHanded_L"};
constexpr const char* kBow[2] = {"BowRelease_R", "BowRelease_L"};
constexpr const char* kThrow[2] = {"Throw_R", "Throw_L"};
constexpr const char* kReload[2] = {"Reload_R", "Reload_L"};
constexpr const char* kTurret = "RecoilTurret";  // Gatling, Browning, cannon: both arms
constexpr const char* kImpact = "Impact";        // directional (angleX)
constexpr const char* kImpactNoDir = "ImpactNoDir";
constexpr const char* kDeath = "Death";
constexpr const char* kHeal = "Heal";
constexpr const char* kHeartbeat = "Heartbeat";
constexpr const char* kDeadeyeStart = "DeadeyeStart";
constexpr const char* kDeadeyePulse = "DeadeyePulse";
constexpr const char* kDeadeyeEnd = "DeadeyeEnd";
constexpr const char* kHorseTrot = "HorseTrot";
constexpr const char* kHorseGallop = "HorseGallop";
constexpr const char* kOnFire = "OnFire";
constexpr const char* kWaterEnter = "WaterEnter";
}  // namespace ev

enum class WeaponClass { None, Pistol, Rifle, Shotgun, Bow, Thrown, Turret, Melee };

WeaponClass classify(WeaponModel w) {
    switch (w) {
        case WEAPON_PISTOL_Volcanic: case WEAPON_PISTOL_SemiAuto: case WEAPON_PISTOL_HighPower: case WEAPON_PISTOL_Mauser:
        case WEAPON_REVOLVER_Cattleman: case WEAPON_REVOLVER_Schofield: case WEAPON_REVOLVER_DoubleAction:
        case WEAPON_REVOLVER_Lemat:
            return WeaponClass::Pistol;
        case WEAPON_REPEATER_Carbine: case WEAPON_REPEATER_Winchester: case WEAPON_REPEATER_Henry:
        case WEAPON_REPEATER_Evans: case WEAPON_RIFLE_Springfield: case WEAPON_RIFLE_BoltAction:
        case WEAPON_RIFLE_Buffalo: case WEAPON_SNIPERRIFLE_RollingBlock: case WEAPON_SNIPERRIFLE_Carcano:
        case WEAPON_RIFLE_Antitank:
            return WeaponClass::Rifle;
        case WEAPON_SHOTGUN_SawedOff: case WEAPON_SHOTGUN_DoubleBarrel: case WEAPON_SHOTGUN_PumpAction:
        case WEAPON_SHOTGUN_SemiAuto: case WEAPON_SHOTGUN_Blunderbuss:
            return WeaponClass::Shotgun;
        case WEAPON_BOW_ShortBow:
            return WeaponClass::Bow;
        case WEAPON_THROWN_FireBottle: case WEAPON_THROWN_Dynamite: case WEAPON_THROWN_ThrowingKnife:
        case WEAPON_THROWN_Tomahawk: case WEAPON_THROWN_HolyWater: case WEAPON_THROWN_ZombieBait:
        case WEAPON_THROWN_ZombieBoomBait:
            return WeaponClass::Thrown;
        case WEAPON_TURRET_Gatling: case WEAPON_TURRET_Browning: case WEAPON_DEFAULT_Cannon:
            return WeaponClass::Turret;
        case WEAPON_MELEE_Knife: case WEAPON_MELEE_TORCH: case WEAPON_LASSO_Lasso:
            return WeaponClass::Melee;
        default:
            return WeaponClass::None;
    }
}

// A repeating effect: plays at most every `interval` ms while it is wanted.
struct Repeater {
    uint64_t next_ms = 0;
    bool due(uint64_t now, uint64_t interval) {
        if (now < next_ms) return false;
        next_ms = now + interval;
        return true;
    }
    void reset() { next_ms = 0; }
};

// ---- state carried from tick to tick
Config g_cfg;

Actor g_player = 0;
bool g_baseline = false;  // the fields below hold a real previous tick (false after a player change / pause)

float g_health = 0.0f;
bool g_dead = false;
WeaponModel g_weapon = WEAPON_INVALID;
float g_clip = -1.0f;
bool g_reloading = false;
bool g_throwing = false;
bool g_deadeye = false;
bool g_in_water = false;
bool g_suppressed = false;  // paused / cutscene last tick

Repeater g_heartbeat, g_deadeye_pulse, g_horse, g_fire, g_turret;

int arm() { return g_cfg.left_handed ? 1 : 0; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The attacker's direction around the player, 0 = in front, degrees as bHaptics' angleX takes it.
// The world is y-up (RDRVR's research: "y is up (the game's world)"), so the ground plane is x / z.
// TODO(calibrate in game): the heading's zero and sense. [Impact] AngleOffset / AngleInvert cover it once known;
// with DebugLog=1 each hit logs the raw numbers.
bool hit_angle(Actor attacker, float* out_deg) {
    if (attacker == 0 || attacker == g_player || !ENTITY::IS_ACTOR_VALID(attacker)) return false;
    const Vector3 me = ACTOR::GET_POSITION(g_player);
    const Vector3 them = ACTOR::GET_POSITION(attacker);
    const float dx = them.x - me.x, dz = them.z - me.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.05f || dist > 300.0f) return false;  // on top of us, or a stale attacker from long ago
    const float world = std::atan2(dx, dz) * Math<float>::RadToDeg;
    const float heading = ACTOR::GET_HEADING(g_player);
    float rel = world - heading;
    if (g_cfg.hit_angle_invert) rel = -rel;
    rel += g_cfg.hit_angle_offset;
    rel = std::fmod(std::fmod(rel, 360.0f) + 360.0f, 360.0f);
    log::debug("hit from (%.1f %.1f %.1f), me (%.1f %.1f %.1f) heading %.1f: world %.1f -> angle %.1f", them.x, them.y,
               them.z, me.x, me.y, me.z, heading, world, rel);
    *out_deg = rel;
    return true;
}

void reset_baseline() {
    g_baseline = false;
    g_heartbeat.reset();
    g_deadeye_pulse.reset();
    g_horse.reset();
    g_fire.reset();
    g_turret.reset();
}

// ---- the parts of a tick

void health(uint64_t now) {
    const float hp = HEALTH::GET_ACTOR_HEALTH(g_player);
    const float max_hp = HEALTH::GET_ACTOR_MAX_HEALTH(g_player);
    const bool dead = HEALTH::IS_ACTOR_DEAD(g_player);

    if (g_baseline && max_hp > 0.0f) {
        if (dead && !g_dead) {
            haptics::stop_all();
            haptics::play(ev::kDeath, g_cfg.impact);
            log::info("death");
        } else if (!dead) {
            const float drop = g_health - hp;
            if (drop >= g_cfg.min_damage) {
                // a light graze still noticeable, a third of the bar a full hit
                const float k = clampf(0.35f + 2.0f * drop / max_hp, 0.35f, 1.0f) * g_cfg.impact;
                float angle = 0.0f;
                const Actor attacker = HEALTH::GET_LAST_ATTACKER(g_player);
                if (hit_angle(attacker, &angle)) {
                    haptics::play(ev::kImpact, k, 1.0f, angle, 0.0f);
                } else {
                    haptics::play(ev::kImpactNoDir, k);  // falls, fire, explosions without a known source
                }
                log::debug("hit: %.1f -> %.1f of %.1f (last damage %d), intensity %.2f", g_health, hp, max_hp,
                           HEALTH::GET_LAST_DAMAGE(g_player), k);
            } else if (-drop >= g_cfg.heal_ratio * max_hp) {
                haptics::play(ev::kHeal, g_cfg.environment);  // medicine; the slow regeneration stays below the ratio
                log::debug("heal: %.1f -> %.1f", g_health, hp);
            }
        }
    }

    if (!dead && max_hp > 0.0f && hp / max_hp < g_cfg.low_health_ratio) {
        // faster the lower it goes: 1.1 s at the threshold, 0.6 s near zero
        const float t = clampf(hp / (max_hp * g_cfg.low_health_ratio), 0.0f, 1.0f);
        if (g_heartbeat.due(now, static_cast<uint64_t>(600.0f + 500.0f * t))) haptics::play(ev::kHeartbeat, g_cfg.heartbeat);
    }

    g_health = hp;
    g_dead = dead;
}

void weapon(uint64_t now) {
    const WeaponModel w = INVENTORY::GET_WEAPON_IN_HAND(g_player);
    const WeaponClass cls = classify(w);
    const bool reloading = ENTITY::IS_ACTOR_RELOADING(g_player);
    const bool throwing = ENTITY::IS_ACTOR_THROWING(g_player);
    const bool shooting = ENTITY::IS_ACTOR_SHOOTING(g_player);
    const float clip = w != WEAPON_INVALID ? INVENTORY::ACTOR_GET_WEAPON_AMMO(g_player, w) : -1.0f;
    const int a = arm();

    if (g_baseline && w == g_weapon) {
        // A shot: the loaded rounds went down while the weapon stayed the same and no reload ran.
        // TODO(verify in game): RDRVR's DualWield fires a second gun through the first's trigger; whether its rounds
        // come off this count, and which hand it is, a native poll cannot see.
        const float fired = g_clip - clip;
        if (fired >= 1.0f && !reloading) {
            const float k = clampf(0.8f + 0.2f * (fired - 1.0f), 0.8f, 1.0f) * g_cfg.recoil;  // both barrels: stronger
            switch (cls) {
                case WeaponClass::Pistol: haptics::play(ev::kRecoilPistol[a], k); break;
                case WeaponClass::Rifle:
                    haptics::play(g_cfg.two_handed ? ev::kRecoilRifleTwoHanded[a] : ev::kRecoilRifle[a], k);
                    break;
                case WeaponClass::Shotgun:  // the Sawed-off too: RDRVR gives it the Double-barrel's foregrip
                    haptics::play(g_cfg.two_handed ? ev::kRecoilShotgunTwoHanded[a] : ev::kRecoilShotgun[a], k);
                    break;
                case WeaponClass::Bow: haptics::play(ev::kBow[a], k); break;
                default: break;
            }
            log::debug("shot: weapon %d class %d, clip %.0f -> %.0f", static_cast<int>(w), static_cast<int>(cls), g_clip, clip);
        }
        if (reloading && !g_reloading) haptics::play(ev::kReload[a], g_cfg.recoil);
        if (cls == WeaponClass::Thrown && throwing && !g_throwing) haptics::play(ev::kThrow[a], g_cfg.recoil);
    }

    // Mounted guns: their rounds may not count down like a clip, so IS_ACTOR_SHOOTING drives a steady rattle.
    // TODO(verify): whether GET_WEAPON_IN_HAND reports the turret while on it; RDRVR has its own mounted-gun probe.
    if (cls == WeaponClass::Turret && shooting) {
        if (g_turret.due(now, w == WEAPON_DEFAULT_Cannon ? 1500 : 110)) haptics::play(ev::kTurret, g_cfg.recoil);
    }

    g_weapon = w;
    g_clip = clip;
    g_reloading = reloading;
    g_throwing = throwing;
}

void deadeye(uint64_t now) {
    const bool on = ACTOR::IS_PLAYER_DEADEYE(g_player);
    if (g_baseline) {
        if (on && !g_deadeye) {
            haptics::play(ev::kDeadeyeStart, g_cfg.deadeye);
            g_deadeye_pulse.due(now, 700);  // the start effect first, the pulse after it
        } else if (!on && g_deadeye) {
            haptics::stop(ev::kDeadeyePulse);
            haptics::play(ev::kDeadeyeEnd, g_cfg.deadeye);
        }
    }
    if (on && g_deadeye_pulse.due(now, 1000)) haptics::play(ev::kDeadeyePulse, g_cfg.deadeye);
    g_deadeye = on;
}

void horse(uint64_t now) {
    if (!RIDING::IS_ACTOR_RIDING(g_player)) {
        g_horse.reset();
        return;
    }
    const Actor mount = RIDING::GET_MOUNT(g_player);
    if (mount == 0 || !ENTITY::IS_ACTOR_VALID(mount)) return;
    Vector3 v;
    if (!ENTITY::GET_ACTOR_VELOCITY(mount, &v)) return;
    const float speed = std::sqrt(v.x * v.x + v.z * v.z);  // ground speed (y up)
    if (speed >= g_cfg.gallop_speed) {
        // one beat per stride; a faster horse beats faster (about 2.5 strides a second at a full gallop)
        const uint64_t interval = static_cast<uint64_t>(clampf(3000.0f / speed, 250.0f, 450.0f));
        if (g_horse.due(now, interval)) haptics::play(ev::kHorseGallop, g_cfg.horse);
    } else if (speed >= g_cfg.trot_speed) {
        if (g_horse.due(now, 380)) haptics::play(ev::kHorseTrot, g_cfg.horse * 0.7f);
    }
}

void environment(uint64_t now) {
    if (WORLD::FIRE_IS_ACTOR_ON_FIRE(g_player)) {
        if (g_fire.due(now, 400)) haptics::play(ev::kOnFire, g_cfg.environment);
    } else {
        g_fire.reset();
    }
    const bool water = ENTITY::IS_ACTOR_IN_WATER(g_player);
    if (g_baseline && water && !g_in_water) haptics::play(ev::kWaterEnter, g_cfg.environment);
    g_in_water = water;
}

}  // namespace

void init(const Config& cfg) {
    g_cfg = cfg;
    reset_baseline();
}

void tick(uint64_t now) {
    const Actor player = ACTOR::GET_PLAYER_ACTOR(-1);
    if (player == 0 || !ENTITY::IS_ACTOR_VALID(player)) {
        g_player = 0;
        reset_baseline();
        return;
    }
    if (player != g_player) {  // a load, Undead Nightmare, John -> Jack: start over
        log::info("player actor 0x%x", player);
        g_player = player;
        reset_baseline();
    }

    // Menus, pause and cutscenes: no effects, and nothing that changed meanwhile plays when they end.
    const bool suppressed = CORE::IS_GAME_PAUSED() || CUTSCENE::CUTSCENE_MANAGER_IS_CUTSCENE_PLAYING();
    if (suppressed) {
        if (!g_suppressed) haptics::stop_all();
        g_suppressed = true;
        reset_baseline();
        return;
    }
    g_suppressed = false;

    health(now);
    if (!g_dead) {
        weapon(now);
        deadeye(now);
        horse(now);
        environment(now);
    } else {
        g_weapon = WEAPON_INVALID;  // the respawn's loadout is a fresh baseline, not a shot
    }
    g_baseline = true;
}

}  // namespace rdrbh::game
