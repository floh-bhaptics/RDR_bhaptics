#include "game_state.h"

#include "rh.h"

#include <cmath>
#include <random>

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
constexpr const char* kTurret = "RecoilTurret";  // Gatling, Browning: both arms, one shot per play
constexpr const char* kCannon = "RecoilCannon";  // the cannon: one heavy shot
constexpr const char* kImpact = "Impact";        // directional (angleX): a hit by an unknown weapon, fistfights (KO)
constexpr const char* kImpactNoDir = "ImpactNoDir";  // no attacker known, or not a fresh attack (a fall, fire)
// 1.1.0: a fresh hit by a known weapon, directional (angleX) like Impact
constexpr const char* kHitBullet = "Hit_Bullet";
constexpr const char* kHitShotgun = "Hit_Shotgun";
constexpr const char* kHitArrow = "Hit_Arrow";
constexpr const char* kHitKnife = "Hit_Knife";
constexpr const char* kHitFist = "Hit_Fist";
constexpr const char* kHitExplosion = "Hit_Explosion";
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
// 1.1.0
constexpr const char* kDraw[2] = {"Draw_R", "Draw_L"};        // a weapon drawn
constexpr const char* kHolster[2] = {"Holster_R", "Holster_L"};  // put away
constexpr const char* kJump = "Player_Jump";
constexpr const char* kLand = "Player_Land";
constexpr const char* kMount = "HorseMount";
constexpr const char* kDismount = "HorseDismount";
constexpr const char* kWhistle = "Whistle";
constexpr const char* kRainSplash[4] = {"Rain_Splash_1", "Rain_Splash_2", "Rain_Splash_3", "Rain_Splash_4"};
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

// The pattern for a hit by weapon `w` (HEALTH::GET_LAST_HIT_WEAPON), or nullptr when it says nothing useful.
// Fists have no weapon enum: an unknown weapon from an attacker within reach counts as a fist (`close`).
const char* hit_event(WeaponModel w, bool close) {
    switch (w) {
        case WEAPON_SHOTGUN_SawedOff: case WEAPON_SHOTGUN_DoubleBarrel: case WEAPON_SHOTGUN_PumpAction:
        case WEAPON_SHOTGUN_SemiAuto: case WEAPON_SHOTGUN_Blunderbuss:
            return ev::kHitShotgun;
        case WEAPON_BOW_ShortBow:
            return ev::kHitArrow;
        case WEAPON_MELEE_Knife: case WEAPON_THROWN_ThrowingKnife: case WEAPON_THROWN_Tomahawk:
            return ev::kHitKnife;
        case WEAPON_MELEE_TORCH: case WEAPON_LASSO_Lasso:
            return ev::kHitFist;
        case WEAPON_THROWN_Dynamite: case WEAPON_THROWN_FireBottle: case WEAPON_DEFAULT_Cannon:
        case WEAPON_RIFLE_Antitank: case WEAPON_THROWN_ZombieBoomBait:
            return ev::kHitExplosion;
        default:
            break;
    }
    switch (classify(w)) {
        case WeaponClass::Pistol: case WeaponClass::Rifle: case WeaponClass::Turret:
            return ev::kHitBullet;
        default:
            return close ? ev::kHitFist : nullptr;
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
// 1.1.0
int g_ko = 0;               // KO points (fistfights)
int g_hit_time = 0;         // HEALTH::GET_LAST_HIT_TIME: changes with each new attack on the player
bool g_drawn = false;       // a weapon out (INVENTORY::IS_WEAPON_DRAWN)
bool g_on_turret = false;
bool g_jumping = false;
bool g_on_ground = true;
uint64_t g_airborne_since = 0;  // 0: on the ground
bool g_riding = false;
bool g_whistling = false;

Repeater g_heartbeat, g_deadeye_pulse, g_horse, g_fire, g_turret, g_rain;
std::mt19937 g_rng{std::random_device{}()};

int rand_int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
float rand_float(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }

int arm() { return g_cfg.left_handed ? 1 : 0; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The attacker's direction around the player, 0 = in front, degrees counter-clockwise seen from above (90 = left,
// 270 = right), as bHaptics' angleX takes it. The attacker's position, not a shot direction.
//
// The game's world is y-up, the ground plane x / z. Its heading convention (RDRVR's measurement, gameplay/main.cpp:
// "heading h faces (-sin h, -cos h) in x/z, positive turning left"): heading 0 faces -z, not +z, and grows
// counter-clockwise. With `world` measured from +z toward +x (atan2(dx, dz)), the player faces world angle h + 180,
// and since both angles grow counter-clockwise, the counter-clockwise angle to the attacker is world - (h + 180).
// (1.0.0: was world - h, 180 degrees off: front and back swapped.)
// [Impact] AngleOffset / AngleInvert are still there for fine calibration.
bool hit_angle(Actor attacker, float* out_deg, float* out_dist = nullptr) {
    if (attacker == 0 || attacker == g_player || !ENTITY::IS_ACTOR_VALID(attacker)) return false;
    const Vector3 me = ACTOR::GET_POSITION(g_player);
    const Vector3 them = ACTOR::GET_POSITION(attacker);
    const float dx = them.x - me.x, dz = them.z - me.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.05f || dist > 300.0f) return false;  // on top of us, or a stale attacker from long ago
    const float world = std::atan2(dx, dz) * Math<float>::RadToDeg;
    const float heading = ACTOR::GET_HEADING(g_player);
    float rel = world - (heading + 180.0f);
    if (g_cfg.hit_angle_invert) rel = -rel;
    rel += g_cfg.hit_angle_offset;
    rel = std::fmod(std::fmod(rel, 360.0f) + 360.0f, 360.0f);
    log::debug("hit from (%.1f %.1f %.1f), me (%.1f %.1f %.1f) heading %.1f: world %.1f -> angle %.1f", them.x, them.y,
               them.z, me.x, me.y, me.z, heading, world, rel);
    *out_deg = rel;
    if (out_dist) *out_dist = dist;
    return true;
}

void reset_baseline() {
    g_baseline = false;
    g_heartbeat.reset();
    g_deadeye_pulse.reset();
    g_horse.reset();
    g_fire.reset();
    g_turret.reset();
    g_rain.reset();
    g_airborne_since = 0;
}

// ---- the parts of a tick

void health(uint64_t now) {
    const float hp = HEALTH::GET_ACTOR_HEALTH(g_player);
    const float max_hp = HEALTH::GET_ACTOR_MAX_HEALTH(g_player);
    const bool dead = HEALTH::IS_ACTOR_DEAD(g_player);
    const int ko = HEALTH::GET_ACTOR_KO_POINTS(g_player);
    const int hit_time = HEALTH::GET_LAST_HIT_TIME(g_player);

    if (g_baseline && max_hp > 0.0f) {
        if (dead && !g_dead) {
            haptics::stop_all();
            haptics::play(ev::kDeath, g_cfg.impact);
            log::info("death");
        } else if (!dead) {
            const float drop = g_health - hp;
            const int max_ko = HEALTH::GET_ACTOR_MAX_KO_POINTS(g_player);
            const int ko_drop = g_ko - ko;
            if (drop >= g_cfg.min_damage) {
                // a light graze still noticeable, a third of the bar a full hit
                const float k = clampf(0.35f + 2.0f * drop / max_hp, 0.35f, 1.0f) * g_cfg.impact;
                // A fresh attack (the hit time moved): its attacker and weapon belong to this drop. Otherwise (a fall
                // or fire after a fight) they are left over from the last attack and say nothing about this one.
                const bool fresh = !g_cfg.check_hit_time || hit_time != g_hit_time;
                float angle = 0.0f, dist = 0.0f;
                const Actor attacker = fresh ? HEALTH::GET_LAST_ATTACKER(g_player) : 0;
                const WeaponModel hw = fresh ? HEALTH::GET_LAST_HIT_WEAPON(g_player) : WEAPON_INVALID;
                const bool directional = hit_angle(attacker, &angle, &dist);
                const char* event = fresh ? hit_event(hw, directional && dist < 3.0f) : nullptr;
                if (event && directional) {
                    haptics::play(event, k, 1.0f, angle, 0.0f);
                } else if (event) {
                    haptics::play(event, k);  // e.g. dynamite whose thrower is out of reach of the natives
                } else if (directional) {
                    haptics::play(ev::kImpact, k, 1.0f, angle, 0.0f);
                } else {
                    haptics::play(ev::kImpactNoDir, k);  // falls, fire, explosions without a known source
                }
                log::debug("hit: %.1f -> %.1f of %.1f (last damage %d), weapon %d, fresh %d (hit time %d -> %d), %s, "
                           "intensity %.2f", g_health, hp, max_hp, HEALTH::GET_LAST_DAMAGE(g_player), static_cast<int>(hw),
                           fresh, g_hit_time, hit_time, event ? event : (directional ? "Impact" : "ImpactNoDir"), k);
            } else if (ko_drop > 0 && max_ko > 0) {
                // fistfights: the brawl's punches take KO points, not health
                const float k = clampf(0.4f + 3.0f * static_cast<float>(ko_drop) / max_ko, 0.4f, 1.0f) * g_cfg.impact;
                float angle = 0.0f;
                if (hit_angle(HEALTH::GET_LAST_ATTACKER(g_player), &angle)) {
                    haptics::play(ev::kImpact, k, 1.0f, angle, 0.0f);
                } else {
                    haptics::play(ev::kImpactNoDir, k);
                }
                log::debug("punch: KO %d -> %d of %d, intensity %.2f", g_ko, ko, max_ko, k);
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
    g_ko = ko;
    g_hit_time = hit_time;
}

void weapon(uint64_t now) {
    const WeaponModel w = INVENTORY::GET_WEAPON_IN_HAND(g_player);
    const WeaponClass cls = classify(w);
    const bool reloading = ENTITY::IS_ACTOR_RELOADING(g_player);
    const bool throwing = ENTITY::IS_ACTOR_THROWING(g_player);
    const bool shooting = ENTITY::IS_ACTOR_SHOOTING(g_player);
    const float clip = w != WEAPON_INVALID ? INVENTORY::ACTOR_GET_WEAPON_AMMO(g_player, w) : -1.0f;
    const bool drawn = INVENTORY::IS_WEAPON_DRAWN(g_player) != 0;
    // On a mounted gun: the turret native, or a turret weapon in hand (whichever the game reports)
    const bool on_turret = TURRET::IS_USING_TURRET(g_player) || cls == WeaponClass::Turret;
    const int a = arm();

    // Drawing and putting away, on the gun arm. Not on a mounted gun (getting on one "draws" it).
    if (g_baseline && !on_turret && !g_on_turret && drawn != g_drawn) {
        haptics::play(drawn ? ev::kDraw[a] : ev::kHolster[a], g_cfg.movement);
        log::debug("%s (weapon %d)", drawn ? "draw" : "holster", static_cast<int>(drawn ? w : g_weapon));
    }

    if (g_baseline && w == g_weapon && !on_turret) {
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
    // The cannon is told apart by the weapon in hand; a mounted gun the game reports no weapon for is a Gatling.
    if (on_turret && shooting) {
        if (w == WEAPON_DEFAULT_Cannon) {
            if (g_turret.due(now, 1500)) haptics::play(ev::kCannon, g_cfg.recoil);
        } else {
            if (g_turret.due(now, 110)) haptics::play(ev::kTurret, g_cfg.recoil);
        }
    }

    g_weapon = w;
    g_clip = clip;
    g_reloading = reloading;
    g_throwing = throwing;
    g_drawn = drawn;
    g_on_turret = on_turret;
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
    const bool riding = RIDING::IS_ACTOR_RIDING(g_player);
    if (g_baseline && riding != g_riding) haptics::play(riding ? ev::kMount : ev::kDismount, g_cfg.movement);
    g_riding = riding;

    const bool whistling = ENTITY::IS_ACTOR_WHISTLING(g_player) != 0;
    if (g_baseline && whistling && !g_whistling) haptics::play(ev::kWhistle, g_cfg.movement);
    g_whistling = whistling;

    if (!riding) {
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

// Jumping and landing, on foot only (a horse's jumps are the horse's).
void movement(uint64_t now) {
    if (g_riding || g_in_water || VEHICLES::IS_ACTOR_INSIDE_VEHICLE(g_player)) {
        g_airborne_since = 0;
        g_on_ground = true;
        g_jumping = false;
        return;
    }
    const bool jumping = ENTITY::IS_ACTOR_JUMPING(g_player) != 0;
    const bool on_ground = ENTITY::IS_ACTOR_ON_GROUND(g_player) != 0;

    if (g_baseline && jumping && !g_jumping) haptics::play(ev::kJump, g_cfg.movement);

    if (!on_ground && g_on_ground) g_airborne_since = now;  // left the ground (a jump, a drop, a ledge)
    if (on_ground && !g_on_ground && g_airborne_since) {
        // A landing after a real time in the air: stairs and bumps lose contact for a frame or two. Harder the longer
        // the fall: 0.5 at the threshold, full at a second.
        const uint64_t air = now - g_airborne_since;
        if (g_baseline && air >= g_cfg.min_air_ms) {
            const float k = clampf(0.5f + 0.5f * static_cast<float>(air - g_cfg.min_air_ms) / 1000.0f, 0.5f, 1.0f);
            haptics::play(ev::kLand, k * g_cfg.movement);
            log::debug("land after %llu ms, intensity %.2f", static_cast<unsigned long long>(air), k);
        }
        g_airborne_since = 0;
    }
    g_jumping = jumping;
    g_on_ground = on_ground;
}

// Rain: while it rains (or storms) and the player is outdoors, a random one of the tiny splash patterns, at a random
// angle around the body, after a random pause.
void rain(uint64_t now) {
    if (g_cfg.rain <= 0.0f) return;
    const Weather w = WEATHER::GET_WEATHER();
    const bool wet = w == WEATHER_RAINY || w == WEATHER_STORMY;
    const bool outdoors = !g_cfg.rain_outdoors_only || ENTITY::IS_ACTOR_OUTDOORS(g_player) != 0;
    if (!wet || !outdoors || g_in_water) {
        g_rain.reset();
        return;
    }
    if (!g_rain.due(now, static_cast<uint64_t>(rand_int(g_cfg.rain_min_ms, g_cfg.rain_max_ms)))) return;
    const float k = (w == WEATHER_STORMY ? 1.0f : 0.7f) * g_cfg.rain;
    haptics::play(ev::kRainSplash[rand_int(0, 3)], k, 1.0f, rand_float(0.0f, 360.0f), 0.0f);
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
        movement(now);
        rain(now);
    } else {
        g_weapon = WEAPON_INVALID;  // the respawn's loadout is a fresh baseline, not a shot
        g_drawn = false;            // nor a draw / holster or a dismount
        g_riding = false;
    }
    g_baseline = true;
}

}  // namespace rdrbh::game
