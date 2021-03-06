/**
 * @file
 * @brief Pseudo spells triggered by gods and various items.
**/

#include "AppHdr.h"

#include "externs.h"
#include "spl-goditem.h"

#include "artefact.h"
#include "attitude-change.h"
#include "cloud.h"
#include "coord.h"
#include "coordit.h"
#include "describe.h"
#include "env.h"
#include "godconduct.h"
#include "hints.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "map_knowledge.h"
#include "mapmark.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-stuff.h"
#include "mon-util.h"
#include "options.h"
#include "random.h"
#include "religion.h"
#include "spl-util.h"
#include "stuff.h"
#include "terrain.h"
#include "transform.h"
#include "traps.h"
#include "view.h"

int identify(int power, int item_slot, std::string *pre_msg)
{
    //Shouldn't do anything now, really.
    return 0;
}

static bool _mons_hostile(const monster* mon)
{
    // Needs to be done this way because of friendly/neutral enchantments.
    return (!mon->wont_attack() && !mon->neutral());
}

// Check whether this monster might be pacified.
// Returns 0, if monster can be pacified but the attempt failed.
// Returns 1, if monster is pacified.
// Returns -1, if monster can never be pacified.
// Returns -2, if monster can currently not be pacified (asleep).
static int _can_pacify_monster(const monster* mon, const int healed)
{
    if (you.religion != GOD_ELYVILON)
        return (-1);

    if (healed < 1)
        return (0);

    // I was thinking of jellies when I wrote this, but maybe we shouldn't
    // exclude zombies and such... (jpeg)
    if (mons_intel(mon) <= I_PLANT // no self-awareness
        || mons_is_tentacle(mon->type)) // body part
    {
        return (-1);
    }

    const mon_holy_type holiness = mon->holiness();

    if (!mon->is_holy()
        && holiness != MH_UNDEAD
        && holiness != MH_DEMONIC
        && holiness != MH_NATURAL)
    {
        return (-1);
    }

    if (mons_is_stationary(mon)) // not able to leave the level
        return (-1);

    if (mon->asleep()) // not aware of what is happening
        return (-2);

    const int factor = (mons_intel(mon) <= I_ANIMAL)       ? 3 : // animals
                       (is_player_same_species(mon->type)) ? 2   // same species
                                                           : 1;  // other

    int divisor = 3;

    if (mon->is_holy())
        divisor--;
    else if (holiness == MH_UNDEAD)
        divisor++;
    else if (holiness == MH_DEMONIC)
        divisor += 2;

    const int random_factor = random2((you.skill(SK_INVOCATIONS) + 1) *
                                      healed / divisor);

    dprf("pacifying %s? max hp: %d, factor: %d, Inv: %d, healed: %d, rnd: %d",
         mon->name(DESC_PLAIN).c_str(), mon->max_hit_points, factor,
         you.skill(SK_INVOCATIONS), healed, random_factor);

    if (mon->max_hit_points < factor * random_factor)
        return (1);

    return (0);
}

static std::vector<std::string> _desc_mindless(const monster_info& mi)
{
    std::vector<std::string> descs;
    if (mi.intel() <= I_PLANT)
        descs.push_back("mindless");
    return descs;
}

// Returns: 1 -- success, 0 -- failure, -1 -- cancel
static int _healing_spell(int healed, bool divine_ability,
                          const coord_def& where, bool not_self,
                          targ_mode_type mode)
{
    ASSERT(healed >= 1);

    bolt beam;
    dist spd;

    if (where.origin())
    {
        spd.isValid = spell_direction(spd, beam, DIR_TARGET,
                                      mode != TARG_NUM_MODES ? mode :
                                      you.religion == GOD_ELYVILON ?
                                            TARG_ANY : TARG_FRIEND,
                                      LOS_RADIUS, false, true, true, "Heal",
                                      NULL, false, NULL, _desc_mindless);
    }
    else
    {
        spd.target  = where;
        spd.isValid = in_bounds(spd.target);
    }

    if (!spd.isValid)
        return (-1);

    if (spd.target == you.pos())
    {
        if (not_self)
        {
            mpr("You can only heal others!");
            return (-1);
        }

        mpr("You are healed.");
        inc_hp(healed, false);
        return (1);
    }

    monster* mons = monster_at(spd.target);
    if (!mons)
    {
        canned_msg(MSG_NOTHING_THERE);
        // This isn't a cancel, to avoid leaking invisible monster
        // locations.
        return (0);
    }

    const int can_pacify  = _can_pacify_monster(mons, healed);
    const bool is_hostile = _mons_hostile(mons);

    // Don't divinely heal a monster you can't pacify.
    if (divine_ability
        && you.religion == GOD_ELYVILON
        && can_pacify <= 0)
    {
        if (can_pacify == 0)
        {
            canned_msg(MSG_NOTHING_HAPPENS);
            return (0);
        }
        else
        {
            if (can_pacify == -2)
            {
                mprf("You cannot pacify this monster while %s is sleeping!",
                     mons->pronoun(PRONOUN_NOCAP).c_str());
            }
            else
                mpr("You cannot pacify this monster!");
            return (-1);
        }
    }

    bool did_something = false;

    if (you.religion == GOD_ELYVILON
        && can_pacify == 1
        && is_hostile)
    {
        did_something = true;

        const bool is_holy     = mons->is_holy();
        const bool is_summoned = mons->is_summoned();

        int pgain = 0;
        if (!is_holy && !is_summoned && you.piety < MAX_PIETY)
            pgain = 3 * random2(mons->max_hit_points / (2 + you.piety / 20)) / 2;

        // The feedback no longer tells you if you gained any piety this time,
        // it tells you merely the general rate.
        if (random2(1 + pgain))
            simple_god_message(" approves of your offer of peace.");
        else
            mpr("Elyvilon supports your offer of peace.");

        if (is_holy)
            good_god_holy_attitude_change(mons);
        else
        {
            simple_monster_message(mons, " turns neutral.");
            mons_pacify(mons, ATT_NEUTRAL);

            // Give a small piety return.
            gain_piety(pgain, 2);
        }
    }

    if (mons->heal(healed))
    {
        did_something = true;
        mprf("You heal %s.", mons->name(DESC_NOCAP_THE).c_str());

        if (mons->hit_points == mons->max_hit_points)
            simple_monster_message(mons, " is completely healed.");
        else
            print_wounds(mons);

        if (you.religion == GOD_ELYVILON && !is_hostile)
        {
            if (one_chance_in(5))
                simple_god_message(" approves of your healing of a fellow "
                                   "creature.");
            else
                mpr("Elyvilon appreciates your healing of a fellow creature.");

            // Give a small piety return.
            gain_piety(1, 5);
        }
    }

    if (!did_something)
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return (0);
    }

    return (1);
}

// Returns: 1 -- success, 0 -- failure, -1 -- cancel
int cast_healing(int pow, bool divine_ability, const coord_def& where,
                 bool not_self, targ_mode_type mode)
{
    pow = std::min(50, pow);
    return (_healing_spell(pow + roll_dice(2, pow) - 2, divine_ability, where,
                           not_self, mode));
}

bool cast_revivification(int pow)
{
    if (you.hp == you.hp_max)
        canned_msg(MSG_NOTHING_HAPPENS);
    else if (you.hp_max < 21)
        mpr("You lack the resilience to cast this spell.");
    else
    {
        mpr("Your body is healed in an amazingly painful way.");

        int loss = 2;
        for (int i = 0; i < 9; ++i)
            if (x_chance_in_y(8, pow))
                loss++;

        dec_max_hp(loss * you.hp_max / 100);
        set_hp(you.hp_max, false);

        if (you.duration[DUR_DEATHS_DOOR])
        {
            mpr("Your life is in your own hands once again.", MSGCH_DURATION);
            you.paralyse(NULL, 5 + random2(5));
            confuse_player(10 + random2(10));
            you.duration[DUR_DEATHS_DOOR] = 0;
        }
        return (true);
    }

    return (false);
}

// Antimagic is sort of an anti-extension... it sets a lot of magical
// durations to 1 so it's very nasty at times (and potentially lethal,
// that's why we reduce levitation to 2, so that the player has a chance
// to stop insta-death... sure the others could lead to death, but that's
// not as direct as falling into deep water) -- bwr
void antimagic()
{
    duration_type dur_list[] = {
        DUR_INVIS, DUR_CONF, DUR_PARALYSIS, DUR_HASTE, DUR_MIGHT, DUR_AGILITY,
        DUR_BRILLIANCE, DUR_CONFUSING_TOUCH, DUR_SURE_BLADE, DUR_CORONA,
        DUR_FIRE_SHIELD, DUR_ICY_ARMOUR, DUR_REPEL_MISSILES,
        DUR_REGENERATION, DUR_SWIFTNESS, DUR_TELEPORT, DUR_CONTROL_TELEPORT,
        DUR_TRANSFORMATION, DUR_DEATH_CHANNEL, DUR_DEFLECT_MISSILES,
        DUR_PHASE_SHIFT, DUR_SEE_INVISIBLE, DUR_WEAPON_BRAND, DUR_SILENCE,
        DUR_CONDENSATION_SHIELD, DUR_STONESKIN,
        DUR_INSULATION, DUR_RESIST_POISON, DUR_RESIST_FIRE, DUR_RESIST_COLD,
        DUR_SLAYING, DUR_STEALTH, DUR_MAGIC_SHIELD, DUR_SAGE, DUR_PETRIFIED,
        DUR_LIQUEFYING, DUR_DARKNESS
    };

    if (!you.permanent_levitation() && !you.permanent_flight()
        && you.duration[DUR_LEVITATION] > 2)
    {
        you.duration[DUR_LEVITATION] = 2;
    }

    if (!you.permanent_flight() && you.duration[DUR_CONTROLLED_FLIGHT] > 1)
        you.duration[DUR_CONTROLLED_FLIGHT] = 1;

    // Post-berserk slowing isn't magic, so don't remove that.
    if (you.duration[DUR_SLOW] > you.duration[DUR_EXHAUSTED])
        you.duration[DUR_SLOW] = std::max(you.duration[DUR_EXHAUSTED], 1);

    for (unsigned int i = 0; i < ARRAYSZ(dur_list); ++i)
        if (you.duration[dur_list[i]] > 1)
            you.duration[dur_list[i]] = 1;

    contaminate_player(-1 * (1 + random2(5)));
}

// The description idea was okay, but this spell just isn't that exciting.
// So I'm converting it to the more practical expose secret doors. -- bwr
void cast_detect_secret_doors(int pow)
{
    int found = 0;

    for (radius_iterator ri(you.get_los()); ri; ++ri)
        if (grd(*ri) == DNGN_SECRET_DOOR && random2(pow) > random2(15))
        {
            reveal_secret_door(*ri);
            found++;
        }

    if (found)
        redraw_screen();

    mprf("You detect %s", (found > 0) ? "secret doors!" : "nothing.");
}

int detect_traps(int pow)
{
    pow = std::min(50, pow);

    // Trap detection moved to traps.cc.  -am

    const int range = 8 + random2(8) + pow;
    return reveal_traps(range);
}

// pow -1 for passive
int detect_items(int pow)
{
    int items_found = 0;
    int map_radius;
    if (pow >= 0)
        map_radius = 8 + random2(8) + pow;
    else
    {
        //ASSERT(you.religion == GOD_ASHENZARI);
        map_radius = std::min(you.piety / 20, LOS_RADIUS);
        if (map_radius <= 0)
            return 0;
    }

    for (radius_iterator ri(you.pos(), map_radius, C_SQUARE); ri; ++ri)
    {
        // Don't you love the 0,5 shop hack?
        if (!in_bounds(*ri))
            continue;

        // Don't expose new dug out areas:
        // Note: assumptions are being made here about how
        // terrain can change (eg it used to be solid, and
        // thus item free).
        if (pow != -1 && env.map_knowledge(*ri).changed())
            continue;

        if (igrd(*ri) != NON_ITEM
            && !env.map_knowledge(*ri).item())
        {
            items_found++;
            env.map_knowledge(*ri).set_detected_item();
        }
    }

    return (items_found);
}

static void _fuzz_detect_creatures(int pow, int *fuzz_radius, int *fuzz_chance)
{
    dprf("dc_fuzz: Power is %d", pow);

    pow = std::max(1, pow);

    *fuzz_radius = pow >= 50 ? 1 : 2;

    // Fuzz chance starts off at 100% and declines to a low of 10% for
    // obscenely powerful castings (pow caps around the 60 mark).
    *fuzz_chance = 100 - 90 * (pow - 1) / 59;
    if (*fuzz_chance < 10)
        *fuzz_chance = 10;
}

static bool _mark_detected_creature(coord_def where, monster* mon,
                                    int fuzz_chance, int fuzz_radius)
{
    bool found_good = false;

    if (fuzz_radius && x_chance_in_y(fuzz_chance, 100))
    {
        const int fuzz_diam = 2 * fuzz_radius + 1;

        coord_def place;
        // Try five times to find a valid placement, else we attempt to place
        // the monster where it really is (and may fail).
        for (int itry = 0; itry < 5; ++itry)
        {
            place.set(where.x + random2(fuzz_diam) - fuzz_radius,
                      where.y + random2(fuzz_diam) - fuzz_radius);

            // If the player would be able to see a monster at this location
            // don't place it there.
            if (you.see_cell(place))
                continue;

            // Try not to overwrite another detected monster.
            if (env.map_knowledge(place).detected_monster())
                continue;

            // Don't print monsters on terrain they cannot pass through,
            // not even if said terrain has since changed.
            if (map_bounds(place) && !env.map_knowledge(place).changed()
                && mon->can_pass_through_feat(grd(place)))
            {
                found_good = true;
                break;
            }
        }

        if (found_good)
            where = place;
    }

    // Mimics are too obvious by now, even out of LOS.
    if (mons_is_unknown_mimic(mon))
        discover_mimic(mon);

    env.map_knowledge(where).set_detected_monster(mons_detected_base(mon->type));

    return (found_good);
}

int detect_creatures(int pow, bool telepathic)
{
    int fuzz_radius = 0, fuzz_chance = 0;
    if (!telepathic)
        _fuzz_detect_creatures(pow, &fuzz_radius, &fuzz_chance);

    int creatures_found = 0;
    const int map_radius = 10 + random2(8) + pow;

    // Clear the map so detect creatures is more useful and the detection
    // fuzz is harder to analyse by averaging.
    if (!telepathic)
        clear_map(false);

    for (radius_iterator ri(you.pos(), map_radius, C_SQUARE); ri; ++ri)
    {
        if (monster* mon = monster_at(*ri))
        {
            // If you can see the monster, don't "detect" it elsewhere.
            if (!mons_near(mon) || !mon->visible_to(&you))
            {
                creatures_found++;
                _mark_detected_creature(*ri, mon, fuzz_chance, fuzz_radius);
            }

            // Assuming that highly intelligent spellcasters can
            // detect scrying. -- bwr
            if (mons_intel(mon) == I_HIGH
                && mons_class_flag(mon->type, M_SPELLCASTER))
            {
                behaviour_event(mon, ME_DISTURB, MHITYOU, you.pos());
            }
        }
    }

    return (creatures_found);
}

bool remove_curse(bool alreadyknown, std::string *pre_msg)
{
    bool success = false;

    // Only cursed *weapons* in hand count as cursed. - bwr
    if (you.weapon()
        && (you.weapon()->base_type == OBJ_WEAPONS
            || you.weapon()->base_type == OBJ_STAVES)
        && you.weapon()->cursed())
    {
        // Also sets wield_change.
        do_uncurse_item(*you.weapon());
        success = true;
    }

    // Everything else uses the same paradigm - are we certain?
    // What of artefact rings and amulets? {dlb}:
    for (int i = EQ_WEAPON + 1; i < NUM_EQUIP; i++)
    {
        // Melded equipment can also get uncursed this way.
        if (you.equip[i] != -1 && you.inv[you.equip[i]].cursed())
        {
            do_uncurse_item(you.inv[you.equip[i]]);
            success = true;
        }
    }

    if (success)
    {
        if (pre_msg)
            mpr(*pre_msg);
        mpr("You feel as if something is helping you.");
        learned_something_new(HINT_REMOVED_CURSE);
    }
    else if (alreadyknown)
        mpr("None of your equipped items are cursed.", MSGCH_PROMPT);
    else
    {
        if (pre_msg)
            mpr(*pre_msg);
        canned_msg(MSG_NOTHING_HAPPENS);
    }

    return (success);
}

bool curse_item(bool armour, bool alreadyknown, std::string *pre_msg)
{
    // make sure there's something to curse first
    int count = 0;
    int affected = EQ_WEAPON;
    int min_type, max_type;
    if (armour)
        min_type = EQ_MIN_ARMOUR, max_type = EQ_MAX_ARMOUR;
    else
        min_type = EQ_LEFT_RING, max_type = EQ_AMULET;
    for (int i = min_type; i <= max_type; i++)
    {
        if (you.equip[i] != -1 && !you.inv[you.equip[i]].cursed())
        {
            count++;
            if (one_chance_in(count))
                affected = i;
        }
    }

    if (affected == EQ_WEAPON)
    {

        if (pre_msg)
            mpr(*pre_msg);
        canned_msg(MSG_NOTHING_HAPPENS);

        return false;
    }

    if (pre_msg)
        mpr(*pre_msg);
    // Make the name before we curse it.
    do_curse_item(you.inv[you.equip[affected]], false);
    learned_something_new(HINT_YOU_CURSED);
    return true;
}

bool detect_curse(int scroll, bool suppress_msg)
{
    int item_count = 0;
    int found_item = NON_ITEM;

    for (int i = 0; i < ENDOFPACK; i++)
    {
        item_def& item = you.inv[i];

        if (!item.defined())
            continue;

        if (item_count <= 1)
        {
            item_count += item.quantity;
            if (i == scroll)
                item_count--;
            if (item_count > 0)
                found_item = i;
        }
    }

    // Not carrying any items -> don't id the scroll.
    if (!item_count)
        return (false);

    ASSERT(found_item != NON_ITEM);

    if (!suppress_msg)
    {
        if (item_count == 1)
        {
            // If you're carrying just one item, mention it explicitly.
            item_def item = you.inv[found_item];

            // If the carried item is just the stack of scrolls,
            // decrease quantity by one to make up for the scroll just read.
            if (found_item == scroll)
                item.quantity--;

            mprf("%s softly glows as it is inspected for curses.",
                 item.name(DESC_CAP_YOUR).c_str());
        }
        else
            mpr("Your items softly glow as they are inspected for curses.");
    }

    return (true);
}

static bool _do_imprison(int pow, const coord_def& where, bool zin)
{
    // power guidelines:
    // powc is roughly 50 at Evoc 10 with no godly assistance, ranging
    // up to 300 or so with godly assistance or end-level, and 1200
    // as more or less the theoretical maximum.
    int number_built = 0;

    const dungeon_feature_type safe_tiles[] = {
        DNGN_SHALLOW_WATER, DNGN_FLOOR, DNGN_OPEN_DOOR
    };

    bool proceed;
    monster *mon;
    std::string targname;

    if (zin)
    {
        // We need to get this now because we won't be able to see
        // the monster once the walls go up!
        mon = monster_at(where);
        targname = mon->name(DESC_NOCAP_THE);
        bool success = true;
        bool none_vis = true;

        for (adjacent_iterator ai(where); ai; ++ai)
        {
            // The tile is occupied.
            if (actor *fatass = actor_at(*ai))
            {
                success = false;
                if (you.can_see(fatass))
                    none_vis = false;
                break;
            }

            // Make sure we have a legitimate tile.
            proceed = false;
            for (unsigned int i = 0; i < ARRAYSZ(safe_tiles) && !proceed; ++i)
                if (grd(*ai) == safe_tiles[i] || feat_is_trap(grd(*ai), true))
                    proceed = true;

            if (!proceed && grd(*ai) > DNGN_MAX_NONREACH)
            {
                success = false;
                none_vis = false;
                break;
            }
        }

        if (!success)
        {
            mprf(none_vis ? "You briefly glimpse something next to %s."
                          : "You need more space to imprison %s.",
                 targname.c_str());
            return (false);
        }

    }

    for (adjacent_iterator ai(where); ai; ++ai)
    {
        // This is where power comes in.
        if (!zin && one_chance_in(pow / 5))
            continue;

        // The tile is occupied.
        if (actor_at(*ai))
            continue;

        // Make sure we have a legitimate tile.
        proceed = false;
        for (unsigned int i = 0; i < ARRAYSZ(safe_tiles) && !proceed; ++i)
            if (grd(*ai) == safe_tiles[i] || feat_is_trap(grd(*ai), true))
                proceed = true;

        if (proceed)
        {
            // All items are moved inside.
            if (igrd(*ai) != NON_ITEM)
                move_items(*ai, where);

            // All clouds are destroyed.
            if (env.cgrid(*ai) != EMPTY_CLOUD)
                delete_cloud(env.cgrid(*ai));

            // All traps are destroyed.
            if (trap_def *ptrap = find_trap(*ai))
                ptrap->destroy();

            // Actually place the wall.

            if (zin)
            {
                // Make the walls silver.
                grd(*ai) = DNGN_METAL_WALL;
                env.grid_colours(*ai) = LIGHTGREY;

                map_wiz_props_marker *marker = new map_wiz_props_marker(*ai);
                marker->set_property("feature_description", "A gleaming silver wall");
                marker->set_property("prison", "Zin");
                env.markers.add(marker);
            }
            else
                grd(*ai) = DNGN_ROCK_WALL;

            set_terrain_changed(*ai);
            number_built++;
        }
    }

    if (number_built > 0)
    {
        if (zin)
        {
            mprf("Zin imprisons %s with walls of pure silver!",
                 targname.c_str());
        }
        else
            mpr("Walls emerge from the floor!");

        you.update_beholders();
        you.update_fearmongers();
        env.markers.clear_need_activate();
    }
    else
        canned_msg(MSG_NOTHING_HAPPENS);

    return (number_built > 0);
}

bool entomb(int pow)
{
    // Zotdef - turned off
    if (crawl_state.game_is_zotdef())
    {
        mpr("The dungeon rumbles ominously, and rocks fall from the ceiling!");
        return (false);
    }

    return (_do_imprison(pow, you.pos(), false));
}

bool cast_imprison(int pow, monster* mons, int source)
{
    if (_do_imprison(pow, mons->pos(), true))
    {
        const int tomb_duration = BASELINE_DELAY * pow;
        env.markers.add(new map_tomb_marker(mons->pos(),
                                            tomb_duration,
                                            source,
                                            mons->mindex()));
        env.markers.clear_need_activate(); // doesn't need activation
        return (true);
    }

    return (false);
}

bool cast_smiting(int pow, monster* mons)
{
    if (mons == NULL || mons->submerged())
    {
        canned_msg(MSG_NOTHING_THERE);
        // Counts as a real cast, due to victory-dancing and
        // invisible/submerged monsters.
        return (true);
    }

    god_conduct_trigger conducts[3];
    disable_attack_conducts(conducts);

    const bool success = !stop_attack_prompt(mons, false, you.pos());

    if (success)
    {
        set_attack_conducts(conducts, mons);

        mprf("You smite %s!", mons->name(DESC_NOCAP_THE).c_str());

        behaviour_event(mons, ME_ANNOY, MHITYOU);
        if (mons_is_mimic(mons->type))
            mimic_alert(mons);
    }

    enable_attack_conducts(conducts);

    if (success)
    {
        // Maxes out at around 40 damage at 27 Invocations, which is
        // plenty in my book (the old max damage was around 70, which
        // seems excessive).
        mons->hurt(&you, 7 + (random2(pow) * 33 / 191));
        if (mons->alive())
            print_wounds(mons);
    }

    return (success);
}
