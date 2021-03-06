/**
 * @file
 * @brief Acquirement and Trog/Oka/Sif gifts.
**/

#include "AppHdr.h"

#include "acquire.h"

#include <cstdlib>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <queue>
#include <set>
#include <cmath>

#include "artefact.h"
#include "decks.h"
#include "dungeon.h"
#include "externs.h"
#include "food.h"
#include "invent.h"
#include "itemname.h"
#include "itemprop.h"
#include "items.h"
#include "item_use.h"
#include "makeitem.h"
#include "message.h"
#include "macro.h"
#include "misc.h"
#include "options.h"
#include "player.h"
#include "random.h"
#include "religion.h"
#include "shopping.h"
#include "skills2.h"
#include "spl-book.h"
#include "spl-util.h"
#include "state.h"
#include "unwind.h"

static bool _acquiring_now = false;

static armour_type _random_nonbody_armour_type()
{
    const armour_type at =
        static_cast<armour_type>(
            random_choose(ARM_SHIELD, ARM_CLOAK, ARM_HELMET,
                          ARM_GLOVES, ARM_BOOTS, -1));
    return (at);
}

static const int max_has_value = 100;
typedef FixedVector<int, max_has_value> has_vector;

static armour_type _pick_wearable_armour(const armour_type arm)
{
    armour_type result = arm;

    // Some species specific fitting problems.
    // FIXME: switch to the cleaner logic in can_wear_armour()
    switch (you.species)
    {
    case SP_OGRE:
    case SP_TROLL:
    case SP_RED_DRACONIAN:
    case SP_WHITE_DRACONIAN:
    case SP_GREEN_DRACONIAN:
    case SP_YELLOW_DRACONIAN:
    case SP_GREY_DRACONIAN:
    case SP_BLACK_DRACONIAN:
    case SP_PURPLE_DRACONIAN:
    case SP_MOTTLED_DRACONIAN:
    case SP_PALE_DRACONIAN:
    case SP_BASE_DRACONIAN:
    case SP_SPRIGGAN:
        if (arm == ARM_GLOVES
            || arm == ARM_BOOTS
            || arm == ARM_CENTAUR_BARDING
            || arm == ARM_NAGA_BARDING)
        {
            result = ARM_ROBE;  // no heavy armour
        }
        else if (arm == ARM_SHIELD)
        {
            if (you.species == SP_SPRIGGAN)
                result = ARM_BUCKLER;
            else if (x_chance_in_y(5 + you.skills[SK_SHIELDS], 20))
                result = ARM_LARGE_SHIELD; // prefer big shields for giant races
        }
        else if (arm == NUM_ARMOURS)
        {
            result = ARM_ROBE;  // no heavy armour, see below
        }
        break;

    case SP_NAGA:
        if (arm == ARM_BOOTS || arm == ARM_CENTAUR_BARDING)
            result = ARM_NAGA_BARDING;
        if (arm == ARM_SHIELD && x_chance_in_y(5 + you.skills[SK_SHIELDS], 20))
            result = ARM_LARGE_SHIELD; // nagas have bonuses to big shields
        break;

    case SP_CENTAUR:
        if (arm == ARM_BOOTS || arm == ARM_NAGA_BARDING)
            result = ARM_CENTAUR_BARDING;
        if (arm == ARM_SHIELD && x_chance_in_y(5 + you.skills[SK_SHIELDS], 20))
            result = ARM_LARGE_SHIELD; // so have centaurs
        break;

    default:
        if (arm == ARM_CENTAUR_BARDING || arm == ARM_NAGA_BARDING)
            result = ARM_BOOTS;
        if (arm == ARM_SHIELD)
            if (x_chance_in_y(15 - you.skills[SK_SHIELDS], 20))
                result = ARM_BUCKLER;
            else if (x_chance_in_y(you.skills[SK_SHIELDS] - 10, 15)
                     && you.species != SP_KOBOLD && you.species != SP_HALFLING)
                result = ARM_LARGE_SHIELD;
        break;
    }

    // Mutation specific problems (horns and antennae allow caps, but not
    // Horns 3 or Antennae 3 (checked below)).
    if (result == ARM_BOOTS && !player_has_feet()
        || result == ARM_GLOVES && you.has_claws(false) >= 3)
    {
        result = NUM_ARMOURS;
    }

    // Do this here, before acquirement()'s call to can_wear_armour(),
    // so that caps will be just as common as helmets for those
    // that can't wear helmets.
    // We check for the mutation directly to avoid acquirement fiddles
    // with vampires.
    if (arm == ARM_HELMET
        && (!you_can_wear(EQ_HELMET)
            || you.mutation[MUT_HORNS]
            || you.mutation[MUT_ANTENNAE]))
    {
        // Check for Horns 3 & Antennae 3 - Don't give a cap if those mutation
        // levels have been reached.
        if (you.mutation[MUT_HORNS] <= 2 || you.mutation[MUT_ANTENNAE] <= 2)
          result = coinflip() ? ARM_CAP : ARM_WIZARD_HAT;
        else
          result = NUM_ARMOURS;
    }

    return (result);
}

static armour_type _acquirement_armour_subtype(bool divine)
{
    // Increasing the representation of the non-body armour
    // slots here to make up for the fact that there's only
    // one type of item for most of them. -- bwr
    //
    // NUM_ARMOURS is body armour and handled below
    armour_type result = NUM_ARMOURS;

    if (divine)
    {
        if (coinflip())
            result = _random_nonbody_armour_type();
    }
    else
    {
        static const equipment_type armour_slots[] =
            {  EQ_SHIELD, EQ_CLOAK, EQ_HELMET, EQ_GLOVES, EQ_BOOTS   };

        equipment_type picked = EQ_BODY_ARMOUR;
        const int num_slots = ARRAYSZ(armour_slots);
        // Start count at 1, for body armour (already picked).
        for (int i = 0, count = 1; i < num_slots; ++i)
            if (you_can_wear(armour_slots[i], true) && one_chance_in(++count))
                picked = armour_slots[i];

        switch (picked)
        {
        case EQ_SHIELD:
            result = ARM_SHIELD; break;
        case EQ_CLOAK:
            result = ARM_CLOAK;  break;
        case EQ_HELMET:
            result = ARM_HELMET; break;
        case EQ_GLOVES:
            result = ARM_GLOVES; break;
        case EQ_BOOTS:
            result = ARM_BOOTS;  break;
        default:
        case EQ_BODY_ARMOUR:
            result = NUM_ARMOURS; break;
        }
    }

    result = _pick_wearable_armour(result);

    // Now we'll randomly pick a body armour up to plate mail (light
    // only in the case of robes or animal skins).  Unlike before, now
    // we're only giving out the finished products here, never the
    // hides. - bwr
    if (result == NUM_ARMOURS || result == ARM_ROBE)
    {
        // Start with normal base armour.
        if (result == ARM_ROBE)
        {
            // Animal skins don't get egos, so make them less likely.
            result = (one_chance_in(4) ? ARM_ANIMAL_SKIN : ARM_ROBE);

            // Armour-restricted species get a bonus chance at
            // troll/dragon armour.  (In total, the chance is almost
            // 10%.)
            if (one_chance_in(20))
            {
                result = static_cast<armour_type>(
                    random_choose_weighted(3, ARM_TROLL_LEATHER_ARMOUR,
                                           3, ARM_STEAM_DRAGON_ARMOUR,
                                           1, ARM_SWAMP_DRAGON_ARMOUR,
                                           1, ARM_DRAGON_ARMOUR,
                                           0));
            }

            // Non-god acquirement not only has a much better chance, but
            // can give high-end ones as well.
            if (!divine && one_chance_in(5))
            {
                result = static_cast<armour_type>(random_choose(
                        ARM_DRAGON_ARMOUR,
                        ARM_ICE_DRAGON_ARMOUR,
                        ARM_STEAM_DRAGON_ARMOUR,
                        ARM_MOTTLED_DRAGON_ARMOUR,
                        ARM_STORM_DRAGON_ARMOUR,
                        ARM_GOLD_DRAGON_ARMOUR,
                        ARM_SWAMP_DRAGON_ARMOUR,
                        ARM_PEARL_DRAGON_ARMOUR,
                        -1));
            }
        }
        else
        {
            if (divine)
            {
                const armour_type armours[] = { ARM_ROBE, ARM_LEATHER_ARMOUR,
                                                ARM_RING_MAIL, ARM_SCALE_MAIL,
                                                ARM_CHAIN_MAIL, ARM_SPLINT_MAIL,
                                                ARM_BANDED_MAIL, ARM_PLATE_MAIL };

                result = static_cast<armour_type>(RANDOM_ELEMENT(armours));

                if (one_chance_in(10) && you.skills[SK_ARMOUR] >= 10)
                    result = ARM_CRYSTAL_PLATE_MAIL;

                if (one_chance_in(12))
                    result = ARM_ANIMAL_SKIN;
            }
            else
            {
                const armour_type armours[] =
                    { ARM_ANIMAL_SKIN, ARM_ROBE, ARM_LEATHER_ARMOUR,
                      ARM_RING_MAIL, ARM_SCALE_MAIL, ARM_CHAIN_MAIL,
                      ARM_BANDED_MAIL, ARM_SPLINT_MAIL, ARM_PLATE_MAIL,
                      ARM_CRYSTAL_PLATE_MAIL };

                const int num_arms = ARRAYSZ(armours);

                // Weight sub types relative to (armour skill + 3).
                // Actually, the AC improvement is not linear, and we
                // might also want to take into account Dodging/Stealth
                // and Strength, but this is definitely better than the
                // random chance above.

                // This formula makes sense only for casters.
                const int skill = std::min(27, you.skills[SK_ARMOUR] + 3);
                int total = 0;
                for (int i = 0; i < num_arms; ++i)
                {
                    int weight = std::max(1, 27 - abs(skill - i*3));
                    weight = weight * weight * weight;
                    total += weight;
                    if (x_chance_in_y(weight, total))
                        result = armours[i];
                }
                // ... so we override it for heavy meleers, who get mostly plates.
                // A scale mail is wasted acquirement, even if it's any but most
                // über randart).
                if (random2(you.skills[SK_SPELLCASTING] * 3
                            + you.skills[SK_DODGING])
                    < random2(you.skills[SK_ARMOUR] * 2))
                {
                    result = one_chance_in(4) ? ARM_CRYSTAL_PLATE_MAIL :
                                                ARM_PLATE_MAIL;
                }
            }
        }

        // Everyone can wear things made from hides.
        if (one_chance_in(20))
        {
            result = static_cast<armour_type>(
                random_choose_weighted(20, ARM_TROLL_LEATHER_ARMOUR,
                                       20, ARM_STEAM_DRAGON_ARMOUR,
                                       15, ARM_MOTTLED_DRAGON_ARMOUR,
                                       10, ARM_SWAMP_DRAGON_ARMOUR,
                                       10, ARM_DRAGON_ARMOUR,
                                       10, ARM_ICE_DRAGON_ARMOUR,
                                        5, ARM_STORM_DRAGON_ARMOUR,
                                        5, ARM_GOLD_DRAGON_ARMOUR,
                                        5, ARM_PEARL_DRAGON_ARMOUR,
                                        0));
        }
    }

    return (result);
}

// If armour acquirement turned up a non-ego non-artefact armour item,
// see whether the player has any unfilled equipment slots.  If so,
// hand out a plain (and possibly negatively enchanted) item of that
// type.  Otherwise, keep the original armour.
static bool _try_give_plain_armour(item_def &arm)
{
    static const equipment_type armour_slots[] =
        {  EQ_SHIELD, EQ_CLOAK, EQ_HELMET, EQ_GLOVES, EQ_BOOTS  };

    armour_type picked = NUM_ARMOURS;
    const int num_slots = ARRAYSZ(armour_slots);
    for (int i = 0, count = 0; i < num_slots; ++i)
    {
        if (!you_can_wear(armour_slots[i], true))
            continue;

        // Consider shield uninteresting in some cases.
        if (armour_slots[i] == EQ_SHIELD)
        {
            const item_def* weapon = you.weapon();

            // Unarmed fighters don't need shields.
            if (!weapon && you.skills[SK_UNARMED_COMBAT] > random2(8))
                continue;

            // Two-handed weapons and ranged weapons conflict with shields.
            if (weapon
                && (hands_reqd(*weapon, you.body_size()) == HANDS_TWO
                    || is_range_weapon(*weapon)))
            {
                continue;
            }
        }

        armour_type result;
        switch (armour_slots[i])
        {
        case EQ_SHIELD:
            result = ARM_SHIELD; break;
        case EQ_CLOAK:
            result = ARM_CLOAK;  break;
        case EQ_HELMET:
            result = ARM_HELMET; break;
        case EQ_GLOVES:
            result = ARM_GLOVES; break;
        case EQ_BOOTS:
            result = ARM_BOOTS;  break;
        default:
            continue;
        }
        result = _pick_wearable_armour(result);
        if (result == NUM_ARMOURS || you.seen_armour[result])
            continue;

        if (one_chance_in(++count))
            picked = result;
    }

    // All available secondary slots already filled.
    if (picked == NUM_ARMOURS)
        return (false);

    arm.clear();
    arm.quantity = 1;
    arm.base_type = OBJ_ARMOUR;
    arm.sub_type = picked;
    arm.plus = random2(5) - 2;

    const int max_ench = armour_max_enchant(arm);
    if (arm.plus > max_ench)
        arm.plus = max_ench;
    else if (arm.plus < -max_ench)
        arm.plus = -max_ench;
    item_colour(arm);

    ASSERT(arm.is_valid());
    return (true);
}

//Determine type and quantity for potion acquirement.  Will give either a largish
//stack of somewhat valuable potions, like heal wounds, or a small stack of highly
//valuable potions, like cure mutation.  Currently, the acquirable types are weighted
//equally with no regard given to currently owned potions (as, in this case, even
//duplicates are valuable).
static void _acquirement_determine_potion(int& type_wanted, int& quantity)
{
    //Players with particularly bad mutations will probably want
    //cure mutation.  Not guaranteed, though, because having
    //these things should still be dangerous.
    if(player_mutation_level(MUT_BLURRY_VISION) ||
       player_mutation_level(MUT_BERSERK) ||
       player_mutation_level(MUT_TELEPORT) &&
       one_chance_in(3))
    {
        //Average of 2.5: 1 + 1d2. 
        type_wanted = POT_CURE_MUTATION;
        quantity = 1 + roll_dice(1,2);
        return;
    }

    //Potions of decay make for fantastic Evaporate fodder.
    if(you.has_spell(SPELL_EVAPORATE) && one_chance_in(6))
    {
        //Average of 9: 2 + 2d6.
        type_wanted = POT_DECAY;
        quantity = 2 + roll_dice(2,6);
        return;
    }
    
    //Otherwise, choose the following options with equal probability:
    //Cure Mutation (still nice to have even if the player isn't critically mutated)
    //Experience
    //Gain Dexterity
    //Gain Intelligence
    //Gain Strength
    //Heal Wounds
    //Invisibility
    //Magic OR Berserk Rage (choose based on whether Fighting > Spellcasting)
    //Speed
    //Resistance
    switch(roll_dice(1,10))
    {
    case 1:
    {
        //Average of 2.5: 1 + 1d2.
        type_wanted = POT_CURE_MUTATION;
        quantity = 1 + roll_dice(1,2);
        return;
    }
    case 2:
    {
        //One potion of experience is quite enough.
        type_wanted = POT_EXPERIENCE;
        quantity = 1;
        return;
    }
    case 3:
    {
        //Average of 2: 1d3.
        type_wanted = POT_GAIN_DEXTERITY;
        quantity = roll_dice(1,3);
        return;
    }
    case 4:
    {
        //Average of 2: 1d3.
        type_wanted = POT_GAIN_INTELLIGENCE;
        quantity = roll_dice(1,3);
        return;
    }
    case 5:
    {
        //Average of 2: 1d3.
        type_wanted = POT_GAIN_STRENGTH;
        quantity = roll_dice(1,3);
        return;
    }
    case 6:
    {
        //Average of 10: 1 + 6d2.  Heal Wounds, while useful, is common;
        //it should accordingly be offered in a big stack here.
        type_wanted = POT_HEAL_WOUNDS;
        quantity = 1 + roll_dice(6,2);
        return;
    }
    case 7:
    {
        //Average of 6: 3 + 1d5.
        type_wanted = POT_INVISIBILITY;
        quantity = 3 + roll_dice(1,5);
        return;
    }
    case 8:
    {
        //Magic is useless to pure fighters, Berserk Rage is useless to
        //Trog followers and pure mages.  Therefore, the following scheme is used.
        //If Trog follower: fall through to the next case (Speed).
        //Otherwise, if Fighting > Spellcasting: offer Berserk Rage.
        //Otherwise otherwise, offer Magic.
        if(you.skills[SK_FIGHTING] > you.skills[SK_SPELLCASTING] &&
           you.religion != GOD_TROG)
        {
            //Average of 6: 3 + 1d5.
            type_wanted = POT_BERSERK_RAGE;
            quantity = 3 + roll_dice(1,5);
            return;
        }
        else if(you.religion != GOD_TROG)
        {
            //I find it highly doubtful that a Trog follower *wouldn't* have
            //Fighting > Spellcasting, but I'll check anyway.
            //Average of 6: 3 + 1d5.
            type_wanted = POT_MAGIC;
            quantity = 3 + roll_dice(1,5);
            return;
        }
        
        //Trog followers fall through here.
    }
    case 9:
    {
        //Average of 6: 3 + 1d5.
        type_wanted = POT_SPEED;
        quantity = 3 + roll_dice(1,5);
        return;
    }
    case 10:
    {
        //Average of 6: 3 + 1d5.
        type_wanted = POT_RESISTANCE;
        quantity = 3 + roll_dice(1,5);
        return;
    }
    }

    //It should never get down here, but that won't stop me
    //from adding a case just in, uh, case!
    type_wanted = POT_WATER;
    quantity = 1;
}

static int _acquirement_weapon_subtype(bool divine)
{
    // Asking for a weapon is biased towards your skills.
    // First pick a skill, weighting towards those you have.
    int count = 0;
    skill_type skill = SK_FIGHTING;
    int best_sk = 0;

    for (int i = SK_SHORT_BLADES; i <= SK_CROSSBOWS; i++)
    {
        skill_type sk = static_cast<skill_type>(i);
        if (is_invalid_skill(sk))
            continue;

        // Adding a small constant allows for the occasional
        // weapon in an untrained skill.

        const int weight = you.skills[sk] + 1;
        count += weight;

        if (you.skills[sk] > best_sk)
            best_sk = you.skills[sk];

        if (x_chance_in_y(weight, count))
            skill = sk;
    }
    if (you.skills[SK_UNARMED_COMBAT] > best_sk)
        best_sk = you.skills[SK_UNARMED_COMBAT];

    // Now choose a subtype which uses that skill.
    int result = OBJ_RANDOM;
    count = 0;
    item_def item_considered;
    item_considered.base_type = OBJ_WEAPONS;
    // Let's guess the percentage of shield use the player did, this is
    // based on empirical data where pure-shield MDs get skills like 17 sh 25 m&f
    // and pure-shield Spriggans 7 sh 18 m&f.
    int shield_sk = you.skills[SK_SHIELDS] * species_apt_factor(SK_SHIELDS);
    int want_shield = std::min(2 * shield_sk, best_sk) + 10;
    int dont_shield = std::max(best_sk - shield_sk, 0) + 10;
    // At XL 10, weapons of the handedness you want get weight *2, those of
    // opposite handedness 1/2, assuming your shields usage is respectively
    // 0% or 100% in the above formula.  At skill 25 that's *3.5 .
    for (int i = 0; i < NUM_WEAPONS; ++i)
    {
        int wskill = range_skill(OBJ_WEAPONS, i);
        if (wskill == SK_THROWING)
            wskill = weapon_skill(OBJ_WEAPONS, i);

        if (wskill != skill)
            continue;
        item_considered.sub_type = i;

        // Can't get blessed blades through acquirement, only from TSO
        if (is_blessed(item_considered))
            continue;

        int acqweight = property(item_considered, PWPN_ACQ_WEIGHT) * 100;

        if (!acqweight)
            continue;

        // HANDS_DOUBLE > HANDS_TWO, but it's currently never returned.
        const bool two_handed = hands_reqd(item_considered, you.body_size()) >= HANDS_TWO;

        // For non-Trog/Okawaru acquirements, give a boost to high-end items.
        if (!divine && !is_range_weapon(item_considered))
        {
            if (acqweight < 500)
                acqweight = 500;
            // Quick blades get unproportionately hit by damage weighting.
            if (i == WPN_QUICK_BLADE)
                acqweight = acqweight * 25 / 9;
            int damage = property(item_considered, PWPN_DAMAGE);
            if (!two_handed)
                damage = damage * 3 / 2;
            damage *= damage * damage;
            acqweight *= damage / property(item_considered, PWPN_SPEED);
        }

        if (two_handed)
            acqweight = acqweight * dont_shield / want_shield;
        else
            acqweight = acqweight * want_shield / dont_shield;

        if (!you.seen_weapon[i])
            acqweight *= 5; // strong emphasis on type variety, brands go only second

        if (x_chance_in_y(acqweight, count += acqweight))
            result = i;
    }
    return (result);
}

static bool _have_item_with_types(object_class_type basetype, int subtype)
{
    for (int i = 0; i < ENDOFPACK; i++)
    {
        const item_def& item = you.inv[i];
        if (item.defined()
            && item.base_type == basetype && item.sub_type == subtype)
        {
            return (true);
        }
    }
    return (false);
}

static missile_type _acquirement_missile_subtype()
{
    int count = 0;
    int skill = SK_THROWING;

    for (int i = SK_SLINGS; i <= SK_THROWING; i++)
    {
        if (you.skills[i])
        {
            count += you.skills[i];
            if (x_chance_in_y(you.skills[i], count))
                skill = i;
        }
    }

    missile_type result = MI_DART;

    switch (skill)
    {
    case SK_SLINGS:    result = MI_SLING_BULLET; break;
    case SK_BOWS:      result = MI_ARROW; break;
    case SK_CROSSBOWS: result = MI_BOLT; break;

    case SK_THROWING:
        // Assuming that blowgun in inventory means that they
        // may want needles for it (but darts might also be
        // wanted).  Maybe expand this... see above comment.
        result =
            (_have_item_with_types(OBJ_WEAPONS, WPN_BLOWGUN) && coinflip())
            ? MI_NEEDLE : MI_DART;
        break;

    default:
        break;
    }
    return (result);
}

static int _acquirement_jewellery_subtype()
{
    int result = 0;

    // Try ten times to give something the player hasn't seen.
    for (int i = 0; i < 10; i++)
    {
        // 1/3 amulets, 2/3 rings.
        result = (one_chance_in(3) ? get_random_amulet_type()
                                   : get_random_ring_type());

        // If we haven't seen this yet, we're done.
        if (get_ident_type(OBJ_JEWELLERY, result) == ID_UNKNOWN_TYPE)
            break;
    }

    return (result);
}

static int _acquirement_staff_subtype(const has_vector& already_has)
{
    int result = random2(STAFF_FIRST_ROD);

    // Elemental preferences -- bwr
    if (result == STAFF_FIRE || result == STAFF_COLD)
    {
        if (you.skills[SK_FIRE_MAGIC] > you.skills[SK_ICE_MAGIC])
            result = STAFF_FIRE;
        if (you.skills[SK_FIRE_MAGIC] < you.skills[SK_ICE_MAGIC])
            result = STAFF_COLD;
    }
    else if (result == STAFF_AIR || result == STAFF_EARTH)
    {
        if (you.skills[SK_AIR_MAGIC] > you.skills[SK_EARTH_MAGIC])
            result = STAFF_AIR;
        if (you.skills[SK_AIR_MAGIC] < you.skills[SK_EARTH_MAGIC])
            result = STAFF_EARTH;
    }

    skill_type best_spell_skill = best_skill(SK_SPELLCASTING, SK_LAST_SKILL);

#define TRY_GIVE(x) { if (!already_has[x]) result = x; }
    // If we're going to give out an enhancer staff,
    // we should at least bias things towards the
    // best spell skill. -- bwr
    switch (best_spell_skill)
    {
    case SK_FIRE_MAGIC:   TRY_GIVE(STAFF_FIRE);        break;
    case SK_ICE_MAGIC:    TRY_GIVE(STAFF_COLD);        break;
    case SK_AIR_MAGIC:    TRY_GIVE(STAFF_AIR);         break;
    case SK_EARTH_MAGIC:  TRY_GIVE(STAFF_EARTH);       break;
    case SK_SORCERY:      TRY_GIVE(STAFF_POISON);      break;
    case SK_NECROMANCY:   TRY_GIVE(STAFF_DEATH);       break;
    case SK_CONJURATIONS: TRY_GIVE(STAFF_CONJURATION); break;
    case SK_ENCHANTMENTS: TRY_GIVE(STAFF_ENCHANTMENT); break;
    case SK_SUMMONINGS:   TRY_GIVE(STAFF_SUMMONING);   break;
#undef TRY_GIVE

    case SK_EVOCATIONS:
        if (!one_chance_in(4))
            result = get_random_rod_type();
        break;

    default: // Invocations and leftover spell schools.
        switch (random2(5))
        {
        case 0: result = STAFF_WIZARDRY;   break;
        case 1: result = STAFF_POWER;      break;
        case 2: result = STAFF_ENERGY;     break;
        case 3: result = STAFF_CHANNELING; break;
        case 4: break;          // keep the original random staff
        }
        break;
    }

    int spell_skills = player_spell_skills();

    // Increased chance of getting a rod for new or
    // non-spellcasters.  -- bwr
    if (one_chance_in(20)
        || (spell_skills <= 1               // short on spells
            && result < STAFF_FIRST_ROD
            && !one_chance_in(4)))
    {
        result = get_random_rod_type();
    }

    return (result);
}

static int _acquirement_misc_subtype()
{
    // Note: items listed early are less likely due to chances of being
    // overwritten.
    int result = random_range(MISC_FIRST_DECK, MISC_LAST_DECK);
    if (result == MISC_DECK_OF_PUNISHMENT)
        result = MISC_BOX_OF_BEASTS;
    if (one_chance_in(4))
        result = MISC_BOTTLED_EFREET;
    if (one_chance_in(4) && !you.seen_misc[MISC_DISC_OF_STORMS])
        result = MISC_DISC_OF_STORMS;
    if (x_chance_in_y(you.skills[SK_FIRE_MAGIC], 27)
        && !you.seen_misc[MISC_LAMP_OF_FIRE]
        && you.skills[SK_EVOCATIONS])
    {
        result = MISC_LAMP_OF_FIRE; // useless with no skill
    }
    if (x_chance_in_y(you.skills[SK_AIR_MAGIC], 27)
        && !you.seen_misc[MISC_AIR_ELEMENTAL_FAN]
        && you.skills[SK_EVOCATIONS])
    {
        result = MISC_AIR_ELEMENTAL_FAN; // useless with no skill
    }
    if (one_chance_in(4)
        && !you.seen_misc[MISC_STONE_OF_EARTH_ELEMENTALS]
        && you.skills[SK_EVOCATIONS])
    {   // useful for anyone with >= 1 skill, can't practice otherwise
        result = MISC_STONE_OF_EARTH_ELEMENTALS;
    }
    if (one_chance_in(4) && !you.seen_misc[MISC_LANTERN_OF_SHADOWS])
        result = MISC_LANTERN_OF_SHADOWS;
    if (x_chance_in_y(you.skills[SK_EVOCATIONS], 27)
        && (x_chance_in_y(std::max(you.skills[SK_SPELLCASTING],
                                    you.skills[SK_INVOCATIONS]), 27)
            || player_spirit_shield())
        && !you.seen_misc[MISC_CRYSTAL_BALL_OF_ENERGY])
    {
        result = MISC_CRYSTAL_BALL_OF_ENERGY;
    }
    if (x_chance_in_y(you.skills[SK_EVOCATIONS], 27)
        && !you.seen_misc[MISC_CRYSTAL_BALL_OF_SEEING])
    {
        result = MISC_CRYSTAL_BALL_OF_SEEING;
    }

    return (result);
}

static int _acquirement_wand_subtype()
{
    int picked = NUM_WANDS;

    int total = 0;
    for (int type = 0; type < NUM_WANDS; ++type)
    {
        int w = 0;

        // First, weight according to usefulness.
        switch (type)
        {
        case WAND_HASTING:          // each 17.9%, group unknown each 26.3%
        case WAND_HEALING:
            w = 25; break;
        case WAND_TELEPORTATION:    // each 10.7%, group unknown each 17.6%
        case WAND_INVISIBILITY:
            w = 15; break;
        case WAND_FIRE:             // each 5.7%, group unknown each 9.3%
        case WAND_COLD:
        case WAND_LIGHTNING:
        case WAND_DRAINING:
            w = 8; break;
        case WAND_DIGGING:          // each 3.6%, group unknown each 6.25%
        case WAND_FIREBALL:
        case WAND_DISINTEGRATION:
        case WAND_POLYMORPH_OTHER:
            w = 5; break;
        case WAND_FLAME:            // each 0.7%, group unknown each 1.4%
        case WAND_FROST:
        case WAND_CONFUSION:
        case WAND_PARALYSIS:
        case WAND_SLOWING:
        case WAND_ENSLAVEMENT:
        case WAND_MAGIC_DARTS:
        case WAND_RANDOM_EFFECTS:
        default:
            w = 1; break;
        }

        // Unknown wands get another huge weight bonus.
        if (get_ident_type(OBJ_WANDS, type) == ID_UNKNOWN_TYPE)
            w *= 2;

        total += w;
        if (x_chance_in_y(w, total))
            picked = type;
    }

    return (picked);
}

static int _find_acquirement_subtype(object_class_type class_wanted,
                                     int &quantity, bool divine,
                                     int agent = -1)
{
    ASSERT(class_wanted != OBJ_RANDOM);

    int type_wanted = OBJ_RANDOM;

    // Write down what the player is carrying.
    has_vector already_has;
    already_has.init(0);
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        const item_def& item = you.inv[i];
        if (item.defined() && item.base_type == class_wanted)
        {
            ASSERT(item.sub_type < max_has_value);
            already_has[item.sub_type] += item.quantity;
        }
    }

    bool try_again = (class_wanted == OBJ_JEWELLERY
                      || class_wanted == OBJ_STAVES
                      || class_wanted == OBJ_MISCELLANY);
    int useless_count = 0;

    do
    {
        again:
        switch (class_wanted)
        {
        case OBJ_POTIONS:
            // Need to set both type wanted and quantity
            _acquirement_determine_potion(type_wanted, quantity);
            break;
        
        case OBJ_WEAPONS:    type_wanted = _acquirement_weapon_subtype(divine);  break;
        case OBJ_MISSILES:   type_wanted = _acquirement_missile_subtype(); break;
        case OBJ_ARMOUR:     type_wanted = _acquirement_armour_subtype(divine); break;
        case OBJ_MISCELLANY: type_wanted = _acquirement_misc_subtype(); break;
        case OBJ_WANDS:      type_wanted = _acquirement_wand_subtype(); break;
        case OBJ_STAVES:     type_wanted = _acquirement_staff_subtype(already_has);
            break;
        case OBJ_JEWELLERY:  type_wanted = _acquirement_jewellery_subtype();
            break;
        default: break;         // gold, books
        }

        item_def dummy;
        dummy.base_type = class_wanted;
        dummy.sub_type = type_wanted;
        dummy.plus = 1; // empty wands would be useless
        dummy.flags |= ISFLAG_IDENT_MASK;

        if (is_useless_item(dummy, false) && useless_count++ < 200)
            goto again;

        if (try_again)
        {
            ASSERT(type_wanted < max_has_value);
            if (!already_has[type_wanted])
                try_again = false;
            if (one_chance_in(200))
                try_again = false;
        }
    }
    while (try_again);

    return (type_wanted);
}

// The weight of a spell takes into account its disciplines' skill levels
// and the spell difficulty.
static int _spell_weight(spell_type spell)
{
    ASSERT(spell != SPELL_NO_SPELL);

    int weight = 0;
    unsigned int disciplines = get_spell_disciplines(spell);
    int count = 0;
    for (int i = 0; i <= SPTYP_LAST_EXPONENT; i++)
    {
        int disc = 1 << i;
        if (disciplines & disc)
        {
            int skill = you.skills[spell_type2skill(disc)];

            weight += skill;
            count++;
        }
    }
    ASSERT(count > 0);

    // Particularly difficult spells _reduce_ the overall weight.
    int leveldiff = 5 - spell_difficulty(spell);

    return std::max(0, 2 * weight/count + leveldiff);

}

// When randomly picking a book for acquirement, use the sum of the
// weights of all unknown spells in the book.
static int _book_weight(book_type book)
{
    ASSERT(book >= 0 && book <= MAX_FIXED_BOOK);

    int total_weight = 0;
    for (int i = 0; i < SPELLBOOK_SIZE; i++)
    {
        spell_type stype = which_spell_in_book(book, i);
        if (stype == SPELL_NO_SPELL)
            continue;

        // Skip over spells already seen.
        if (you.seen_spell[stype])
            continue;

        total_weight += _spell_weight(stype);
    }

    return (total_weight);
}

static bool _is_magic_skill(int skill)
{
    return (skill >= SK_SPELLCASTING && skill < SK_INVOCATIONS);
}

static bool _skill_useless_with_god(int skill)
{
    switch (you.religion)
    {
    case GOD_TROG:
        return (_is_magic_skill(skill) || skill == SK_INVOCATIONS);
    case GOD_ZIN:
    case GOD_SHINING_ONE:
    case GOD_ELYVILON:
    case GOD_FEDHAS:
        return (skill == SK_NECROMANCY);
    case GOD_XOM:
    case GOD_NEMELEX_XOBEH:
    case GOD_KIKUBAAQUDGHA:
    case GOD_VEHUMET:
    case GOD_NO_GOD:
        return (skill == SK_INVOCATIONS);
    default:
        return (false);
    }
}

static bool _do_book_acquirement(item_def &book, int agent)
{
    // items() shouldn't make book a randart for acquirement items.
    ASSERT(!is_random_artefact(book));

    int          level       = (you.skills[SK_SPELLCASTING] + 2) / 3;
    unsigned int seen_levels = you.attribute[ATTR_RND_LVL_BOOKS];

    level = std::max(1, level);

    if (agent == GOD_XOM)
        level = random_range(1, 9);
    else if (seen_levels & (1 << level))
    {
        // Give a book of a level not seen before, preferably one with
        // spells of a low enough level for the player to cast, or the
        // lowest aviable level if all levels which the player can cast
        // have already been given.
        int max_level = std::min(9, you.get_experience_level());

        std::vector<int> vec;
        for (int i = 1; i <= 9 && (vec.empty() || i <= max_level); i++)
            if (!(seen_levels & (1 << i)))
                vec.push_back(i);

        if (!vec.empty())
            level = vec[random2(vec.size())];
        else
            level = -1;
    }

    int choice = NUM_BOOKS;

    bool knows_magic = false;
    // Manuals are too useful for Xom, and useless when gifted from Sif Muna.
    if (agent != GOD_XOM && agent != GOD_SIF_MUNA)
    {
        int magic_weights = 0;
        int other_weights = 0;

        for (int i = SK_FIRST_SKILL; i < NUM_SKILLS; i++)
        {
            skill_type sk = static_cast<skill_type>(i);
            if (is_invalid_skill(sk))
                continue;

            int weight = you.skills[sk];

            // Anyone can get Spellcasting 1. Doesn't prove anything.
            if (sk == SK_SPELLCASTING && weight >= 1)
                weight--;

            if (_is_magic_skill(sk))
                magic_weights += weight;
            else
                other_weights += weight;
        }

        // If someone has 25% or more magic skills, never give manuals.
        // Otherwise, count magic skills double to bias against manuals
        // for magic users.
        if (magic_weights * 3 < other_weights
            && x_chance_in_y(other_weights, 2*magic_weights + other_weights))
        {
            choice = BOOK_MANUAL;
            if (magic_weights > 0)
                knows_magic = true;
        }
    }

    if (choice == NUM_BOOKS)
    {
        choice = random_choose_weighted(
                                        30, BOOK_RANDART_THEME,
           agent == GOD_SIF_MUNA ? 10 : 40, NUM_BOOKS, // normal books
                     level == -1 ?  0 :  1, BOOK_RANDART_LEVEL, 0);
    }

    // Acquired randart books have a chance of being named after the player.
    std::string owner = "";
    if (agent == AQ_SCROLL && one_chance_in(12)
        || agent == AQ_CARD_GENIE && one_chance_in(6))
    {
        owner = you.your_name;
    }

    switch (choice)
    {
    default:
    case NUM_BOOKS:
    {
        int total_weights = 0;

        // Pick a random spellbook according to unknown spells contained.
        int weights[MAX_FIXED_BOOK+1];
        for (int bk = 0; bk <= MAX_FIXED_BOOK; bk++)
        {
            if (bk > MAX_NORMAL_BOOK && agent == GOD_SIF_MUNA)
            {
                weights[bk] = 0;
                continue;
            }
#if TAG_MAJOR_VERSION == 32
            if (bk == BOOK_MINOR_MAGIC_II || bk == BOOK_MINOR_MAGIC_III)
            {
                weights[bk] = 0;
                continue;
            }
#endif
            weights[bk]    = _book_weight(static_cast<book_type>(bk));
            total_weights += weights[bk];
        }

        if (total_weights > 0)
        {
            book.sub_type = choose_random_weighted(weights,
                                                   weights + ARRAYSZ(weights));
            break;
        }
        // else intentional fall-through
    }
    case BOOK_RANDART_THEME:
        book.sub_type = BOOK_RANDART_THEME;
        if (!make_book_theme_randart(book, 0, 0, 5 + coinflip(), 20,
                                     SPELL_NO_SPELL, owner))
        {
            return (false);
        }
        break;

    case BOOK_RANDART_LEVEL:
    {
        book.sub_type  = BOOK_RANDART_LEVEL;
        int max_spells = 5 + level/3;
        if (!make_book_level_randart(book, level, max_spells, owner))
            return (false);
        break;
    }

    case BOOK_MANUAL:
    {
        // The Tome of Destruction is rare enough we won't change this.
        if (book.sub_type == BOOK_DESTRUCTION)
            return (true);

        int weights[NUM_SKILLS];
        int total_weights = 0;

        for (int i = SK_FIRST_SKILL; i < NUM_SKILLS; i++)
        {
            skill_type sk = static_cast<skill_type>(i);
            if (is_invalid_skill(sk))
            {
                weights[sk] = 0;
                continue;
            }

            int skl = you.skills[sk];

            if (skl == 27 || is_useless_skill(sk))
            {
                weights[sk] = 0;
                continue;
            }

            int w = (skl < 12) ? skl + 3 : std::max(0, 25 - skl);

            // Give a bonus for some highly sought after skills.
            if (sk == SK_FIGHTING || sk == SK_ARMOUR || sk == SK_SPELLCASTING
                || sk == SK_INVOCATIONS || sk == SK_EVOCATIONS)
            {
                w += 5;
            }

            // Greatly reduce the chances of getting a manual for a skill
            // you couldn't use unless you switched your religion.
            if (_skill_useless_with_god(sk))
                w /= 2;

            // If we don't have any magic skills, make non-magic skills
            // more likely.
            if (!knows_magic && !_is_magic_skill(sk))
                w *= 2;

            weights[sk] = w;
            total_weights += w;
        }

        // Are we too skilled to get any manuals?
        if (total_weights == 0)
            return _do_book_acquirement(book, agent);

        book.sub_type = BOOK_MANUAL;
        book.plus     = choose_random_weighted(weights, weights + NUM_SKILLS);
        // Set number of reads possible before it "crumbles to dust".
        book.plus2    = 3 + random2(15);
        break;
    } // manuals
    } // switch book choice
    return (true);
}

static int _failed_acquirement(bool quiet)
{
    if (!quiet)
        mpr("The demon of the infinite void smiles upon you.");
    return (NON_ITEM);
}

static int _weapon_brand_quality(int brand, bool range)
{
    switch(brand)
    {
    case SPWPN_SPEED:
        return range ? 3 : 5;
    case SPWPN_PENETRATION:
        return 4;
    case SPWPN_ELECTROCUTION:
    case SPWPN_DISTORTION:
    case SPWPN_HOLY_WRATH:
    case SPWPN_REAPING:
        return 3;
    case SPWPN_CHAOS:
        return 2;
    default:
        return 1;
    case SPWPN_NORMAL:
        return 0;
    case SPWPN_PAIN:
        return you.skills[SK_NECROMANCY] / 2;
    case SPWPN_VORPAL:
        return range ? 5 : 1;
    }
}

static bool _armour_slot_seen(armour_type arm)
{
    item_def item;
    item.base_type = OBJ_ARMOUR;
    item.quantity = 1;

    for (int i = 0; i < NUM_ARMOURS; i++)
    {
        if (get_armour_slot(arm) != get_armour_slot((armour_type)i))
            continue;
        item.sub_type = i;

        // having seen a helmet means nothing about your decision to go
        // bare-headed if you have horns
        if (!can_wear_armour(item, false, true))
            continue;

        if (you.seen_armour[i])
            return true;
    }
    return false;
}

static bool _is_armour_plain(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOUR);
    if (is_artefact(item))
        return false;

    if (item.sub_type != ARM_ANIMAL_SKIN
        && item.sub_type >= ARM_MIN_UNBRANDED
        && item.sub_type <= ARM_MAX_UNBRANDED)
    {
        // These are always interesting, even with no brand.
        // May still be redundant, but that has another check.
        return false;
    }

    return (get_armour_ego_type(item) == SPARM_NORMAL);
}

int acquirement_create_item_general(object_class_type class_wanted,
                                    int agent, bool quiet,
                                    const coord_def &pos, bool debug, bool move)
{
    ASSERT(class_wanted != OBJ_RANDOM);

    const bool divine = (agent == GOD_OKAWARU || agent == GOD_XOM
                         || agent == GOD_TROG);
    int thing_created = NON_ITEM;
    int quant = 1;
#define ITEM_LEVEL (divine ? MAKE_GIFT_ITEM : MAKE_GOOD_ITEM)
#define MAX_ACQ_TRIES 40
    for (int item_tries = 0; item_tries < MAX_ACQ_TRIES; item_tries++)
    {
        int type_wanted = _find_acquirement_subtype(class_wanted, quant,
                                                    divine, agent);

        // Don't generate randart books in items(), we do that
        // ourselves.
        int want_arts = (class_wanted == OBJ_BOOKS ? 0 : 1);

        thing_created = items(want_arts, class_wanted, type_wanted, true,
                               ITEM_LEVEL, MAKE_ITEM_RANDOM_RACE,
                               0, 0, agent);

        if (thing_created == NON_ITEM)
            continue;

        item_def &doodad(mitm[thing_created]);

        // Not a god, prefer better brands.
        if (!divine && !is_artefact(doodad) && doodad.base_type == OBJ_WEAPONS)
        {
            while (_weapon_brand_quality(get_weapon_brand(doodad),
                                        is_range_weapon(doodad)) < random2(6))
            {
                reroll_brand(doodad, ITEM_LEVEL);
            }
        }

        // Try to not generate brands that were already seen, although unlike
        // jewelry and books, this is not absolute.
        while (!is_artefact(doodad)
               && (doodad.base_type == OBJ_WEAPONS
                     && you.seen_weapon[doodad.sub_type]
                        & (1<<get_weapon_brand(doodad))
                   || doodad.base_type == OBJ_ARMOUR
                     && you.seen_armour[doodad.sub_type]
                        & (1<<get_armour_ego_type(doodad)))
               && !one_chance_in(5))
        {
            reroll_brand(doodad, ITEM_LEVEL);
        }

        // For plain armour, try to change the subtype to something
        // matching a currently unfilled equipment slot.
        if (doodad.base_type == OBJ_ARMOUR && !is_artefact(doodad))
        {
            const special_armour_type sparm = get_armour_ego_type(doodad);

            if (agent != GOD_XOM
                  && you.seen_armour[doodad.sub_type] & (1 << sparm)
                  && x_chance_in_y(MAX_ACQ_TRIES - item_tries, MAX_ACQ_TRIES + 5)
                || !divine
                   && you.seen_armour[doodad.sub_type]
                   && !one_chance_in(3)
                   && item_tries < 20)
            {
                // We have seen the exact item already, it's very unlikely
                // extras will do any good.
                // For scroll acquirement, prefer base items not seen before
                // as well, even if you didn't see the exact brand yet.
                destroy_item(thing_created, true);
                thing_created = NON_ITEM;
                continue;
            }

            // Try to fill empty slots.
            if ((_is_armour_plain(doodad)
                 || get_armour_slot(doodad) == EQ_BODY_ARMOUR)
                && _armour_slot_seen((armour_type)doodad.sub_type))
            {
                if (_try_give_plain_armour(doodad))
                {
                    // Only Xom gives negatively enchanted items (75% if not 0).
                    if (doodad.plus < 0 && agent != GOD_XOM)
                        doodad.plus = 0;
                    else if (doodad.plus > 0 && coinflip())
                        doodad.plus *= -1;
                }
                else if (agent != GOD_XOM && one_chance_in(3))
                {
                    // If the item is plain and there aren't any
                    // unfilled slots, we might want to roll again.
                    destroy_item(thing_created, true);
                    thing_created = NON_ITEM;
                    continue;
                }
            }
        }

        if (doodad.base_type == OBJ_WEAPONS
               && !can_wield(&doodad, false, true)
            || doodad.base_type == OBJ_ARMOUR
               && !can_wear_armour(doodad, false, true))
        {
            destroy_item(thing_created, true);
            thing_created = NON_ITEM;
            continue;
        }

        // Trog does not gift the Wrath of Trog, nor weapons of pain
        // (which work together with Necromantic magic).
        if (agent == GOD_TROG)
        {
            // ... but he loves the antimagic brand specially.
            if (one_chance_in(10) && doodad.base_type == OBJ_WEAPONS
                && !is_range_weapon(doodad) && !is_unrandom_artefact(doodad))
            {
                set_item_ego_type(doodad, OBJ_WEAPONS, SPWPN_ANTIMAGIC);
            }

            int brand = get_weapon_brand(doodad);
            if (brand == SPWPN_PAIN
                || is_unrandom_artefact(doodad)
                   && (doodad.special == UNRAND_TROG
                       || doodad.special == UNRAND_WUCAD_MU))
            {
                destroy_item(thing_created, true);
                thing_created = NON_ITEM;
                continue;
            }
        }

        // MT - Check: god-gifted weapons and armour shouldn't kill you.
        // Except Xom.
        if ((agent == GOD_TROG || agent == GOD_OKAWARU)
            && is_artefact(doodad))
        {
            artefact_properties_t  proprt;
            artefact_wpn_properties(doodad, proprt);

            // Check vs. stats. positive stats will automatically fall
            // through.  As will negative stats that won't kill you.
            if (-proprt[ARTP_STRENGTH] >= you.strength()
                || -proprt[ARTP_INTELLIGENCE] >= you.intel()
                || -proprt[ARTP_DEXTERITY] >= you.dex())
            {
                // Try again.
                destroy_item(thing_created);
                thing_created = NON_ITEM;
                continue;
            }
        }

        // Sif Muna shouldn't gift Vehumet or Kiku's special books.
        // (The spells therein are still fair game for randart books.)
        if (agent == GOD_SIF_MUNA
            && doodad.sub_type >= MIN_GOD_ONLY_BOOK
            && doodad.sub_type <= MAX_GOD_ONLY_BOOK)
        {
            ASSERT(doodad.base_type == OBJ_BOOKS);

            // Try again.
            destroy_item(thing_created);
            thing_created = NON_ITEM;
            continue;
        }
        break;
    }

    if (thing_created == NON_ITEM)
        return _failed_acquirement(quiet);

    // Easier to read this way.
    item_def& thing(mitm[thing_created]);
    ASSERT(thing.is_valid());

    if (class_wanted == OBJ_WANDS)
        thing.plus = std::max(static_cast<int>(thing.plus), 3 + random2(3));
    else if (class_wanted == OBJ_GOLD)
    {
        // New gold acquirement formula from dpeg.
        // Min=220, Max=5520, Mean=1218, Std=911
        thing.quantity = 10 * (20
                                + roll_dice(1, 20)
                                + (roll_dice(1, 8)
                                   * roll_dice(1, 8)
                                   * roll_dice(1, 8)));
    }
    else if (quant > 1)
        thing.quantity = quant;

    if (thing.base_type == OBJ_BOOKS)
    {
        if (!_do_book_acquirement(thing, agent))
        {
            destroy_item(thing, true);
            return _failed_acquirement(quiet);
        }
    }
    else if (thing.base_type == OBJ_JEWELLERY)
    {
        switch (thing.sub_type)
        {
        case RING_SLAYING:
            // Make sure plus to damage is >= 1.
            thing.plus2 = std::max(abs(thing.plus2), 1);
            // fall through...

        case RING_PROTECTION:
        case RING_STRENGTH:
        case RING_INTELLIGENCE:
        case RING_DEXTERITY:
        case RING_EVASION:
            // Make sure plus is >= 1.
            thing.plus = std::max(abs(thing.plus), 1);
            break;

        case AMU_INACCURACY:
            // This is the only truly bad piece of jewellery.
            if (!one_chance_in(9))
                make_item_randart(thing);
            break;

        default:
            break;
        }
    }
    else if (thing.base_type == OBJ_WEAPONS
             && !is_unrandom_artefact(thing))
    {
        // HACK: Make unwieldable weapons wieldable.
        // Note: messing with fixed artefacts is probably very bad.
        switch (you.species)
        {
        case SP_DEMONSPAWN:
        case SP_MUMMY:
        case SP_GHOUL:
        case SP_VAMPIRE:
        {
            int brand = get_weapon_brand(thing);
            if (brand == SPWPN_HOLY_WRATH)
            {
                if (is_random_artefact(thing))
                {
                    // Keep resetting seed until it's good.
                    for (; brand == SPWPN_HOLY_WRATH;
                         brand = get_weapon_brand(thing))
                    {
                        make_item_randart(thing);
                    }
                }
                else
                {
                    set_item_ego_type(thing, OBJ_WEAPONS, SPWPN_VORPAL);
                }
            }
            break;
        }

        case SP_HALFLING:
        case SP_KOBOLD:
        case SP_SPRIGGAN:
            switch (thing.sub_type)
            {
            case WPN_LONGBOW:
                thing.sub_type = WPN_BOW;
                break;

            case WPN_GREAT_SWORD:
            case WPN_TRIPLE_SWORD:
                thing.sub_type = (coinflip() ? WPN_FALCHION : WPN_LONG_SWORD);
                break;

            case WPN_GREAT_MACE:
            case WPN_DIRE_FLAIL:
                thing.sub_type = (coinflip() ? WPN_MACE : WPN_FLAIL);
                break;

            case WPN_BATTLEAXE:
            case WPN_EXECUTIONERS_AXE:
                thing.sub_type = (coinflip() ? WPN_HAND_AXE : WPN_WAR_AXE);
                break;

            case WPN_HALBERD:
            case WPN_GLAIVE:
            case WPN_SCYTHE:
            case WPN_BARDICHE:
                thing.sub_type = (coinflip() ? WPN_SPEAR : WPN_TRIDENT);
                break;
            }
            break;

        default:
            break;
        }

        // These can never get egos, and mundane versions are quite common, so
        // guarantee artifact status.  Rarity is a bit low to compensate.
        if (thing.sub_type == WPN_GIANT_CLUB
            || thing.sub_type == WPN_GIANT_SPIKED_CLUB)
        {
            if (!one_chance_in(25))
                make_item_randart(thing, true);
        }

        int plusmod = random2(4);
        if (agent == GOD_TROG)
        {
            // More damage, less accuracy.
            thing.plus  -= plusmod;
            thing.plus2 += plusmod;
            if (!is_artefact(thing))
                thing.plus = std::max(static_cast<int>(thing.plus), 0);
        }
        else if (agent == GOD_OKAWARU)
        {
            // More accuracy, less damage.
            thing.plus  += plusmod;
            thing.plus2 -= plusmod;
            if (!is_artefact(thing))
                thing.plus2 = std::max(static_cast<int>(thing.plus2), 0);
        }
    }

    if (agent > GOD_NO_GOD && agent < NUM_GODS && agent == you.religion)
    {
        if(strcmp(thing.inscription.c_str(), ""))
            thing.inscription = "god gift; " + thing.inscription;
        else
            thing.inscription = "god gift";
    }

    // Moving this above the move since it might not exist after falling.
    if (thing_created != NON_ITEM && !quiet)
        canned_msg(MSG_SOMETHING_APPEARS);

    // If a god wants to give you something but the floor doesn't want it,
    // it counts as a failed acquirement - no piety, etc cost.
    if (feat_destroys_item(grd(pos), thing) && (agent > GOD_NO_GOD) &&
        (agent < NUM_GODS))
    {
        if (agent == GOD_XOM)
            simple_god_message(" snickers.", GOD_XOM);
        else
            return _failed_acquirement(quiet);
    }

    if(move)
    {
        move_item_to_grid(&thing_created, pos);
    }

    if (thing_created != NON_ITEM)
    {
        ASSERT(mitm[thing_created].is_valid());
    }
    return (thing_created);
}


//dtsund: Wrapper, since I needed to modify the original function to pass
//an assert.
int acquirement_create_item(object_class_type class_wanted,
                            int agent, bool quiet,
                            const coord_def &pos, bool debug)
{
return acquirement_create_item_general(class_wanted,
                                       agent, quiet,
                                       pos, debug, true);
}



//dtsund: More shopping.cc copy-pasta.
//This function generates each of the lines saying what the items are.
static std::string _acquirement_print_stock(const std::vector<int>& stock,
                                      const std::vector<bool>& selected)
{
    //ShopInfo &si  = StashTrack.get_shop(shop.pos);
    //const bool id = true;
    std::string purchasable;
    for (unsigned int i = 0; i < stock.size(); ++i)
    {
        const item_def& item = mitm[stock[i]];

        cgotoxy(1, i+1, GOTO_CRT);
        const char c = i + 'a';
        purchasable += c;

        // Colour stock as follows:
        //  * lightcyan, if on the shopping list.
        //  * lightred, if you can't buy all you selected.
        //  * lightgreen, if this item is purchasable along with your selections
        //  * red, if this item is not purchasable even by itself.
        //  * yellow, if this item would be purchasable if you deselected
        //            something else.

        // Is this too complicated? (jpeg)

        if (selected[i])
            textcolor(LIGHTCYAN);
        else
            textcolor(LIGHTGREEN);
        
        
        if (selected[i])
            cprintf("%c + ", c);
        else
            cprintf("%c - ", c);

        if (Options.menu_colour_shops)
        {
            // Colour stock according to menu colours.
            const std::string colprf = menu_colour_item_prefix(item);
            const int col = menu_colour(item.name(DESC_NOCAP_A),
                                        colprf, "shop");
            textcolor(col != -1 ? col : LIGHTGREY);
        }
        else
            textcolor(i % 2 ? LIGHTGREY : WHITE);

        cprintf("%s", chop_string(item.name(DESC_NOCAP_A, false, true), 56).c_str());
    }
    textcolor(LIGHTGREY);

    return (purchasable);
}


//Copied from shopping.cc
//This function assists _acquirement_keys, which in turn helps
//_list_acquirement_keys.
static std::string _hyphenated_suffix(char prev, char last)
{
    std::string s;
    if (prev > last + 2)
        s += "</w>-<w>";
    else if (prev == last + 2)
        s += (char) (last + 1);

    if (prev != last)
        s += prev;
    return (s);
}


//Copied from shopping.cc
//This function does utility work for _list_acquirement_keys.
static std::string _acquirement_keys(const std::string &s)
{
    if (s.empty())
        return "";

    std::string list = "<w>" + s.substr(0, 1);
    char last = s[0];
    for (unsigned int i = 1; i < s.length(); ++i)
    {
        if (s[i] == s[i - 1] + 1)
            continue;

        char prev = s[i - 1];
        list += _hyphenated_suffix(prev, last);
        list += (last = s[i]);
    }

    list += _hyphenated_suffix(s[s.length() - 1], last);
    list += "</w>";
    return (list);
}



//Even more borrowing from shopping.cc
//This function makes the part of the display that tells the player
//what he/she can do.
static void _list_acquirement_keys(const std::string &purchasable, bool viewing,
                            int total_stock, int num_selected)
{
    ASSERT(total_stock > 0);

    const int numlines = get_number_of_lines();
    formatted_string fs;

    cgotoxy(1, numlines - 1, GOTO_CRT);

    std::string pkeys = "";
    if (viewing)
    {
        pkeys = "<w>a</w>";
        if (total_stock > 1)
        {
            pkeys += "-<w>";
            pkeys += 'a' + total_stock - 1;
            pkeys += "</w>";
        }
    }
    else
        pkeys = _acquirement_keys(purchasable);

    if (!pkeys.empty())
    {
        pkeys = "[" + pkeys + "] Select Item to "
                + (viewing ? "Examine" : "Acquire");
    }
    fs = formatted_string::parse_string(make_stringf(
            "[<w>x</w>/<w>Esc</w>"
#ifdef USE_TILE
            "/<w>R-Click</w>"
#endif
            "] exit            [<w>!</w>] %s   %s",
            (viewing ? "to acquire items" : "to examine items"),
            pkeys.c_str()));

    fs.cprintf("%*s", get_number_of_cols() - fs.length() - 1, "");
    fs.display();
    cgotoxy(1, numlines, GOTO_CRT);

    fs = formatted_string::parse_string(
            "[<w>Enter</w>"
#ifdef USE_TILE
            "/<w>L-Click</w>"
#endif
            "] acquire item    [<w>\\</w>] list known items   "
            "[<w>?</w>/<w>*</w>] inventory");

    fs.cprintf("%*s", get_number_of_cols() - fs.length() - 1, "");
    fs.display();
}



static void _acquire_print(const char *shoppy, int line)
{
    cgotoxy(1, line + 19, GOTO_CRT);
    cprintf("%s", shoppy);
    clear_to_end_of_line();
}


static void _acquire_more()
{
    cgotoxy(65, 20, GOTO_CRT);
    cprintf("-more-");
    get_ch();
}




static bool _acquire_yesno(const char* prompt, int safeanswer)
{
    if (_acquiring_now)
    {
        textcolor(channel_to_colour(MSGCH_PROMPT));
        _acquire_print(prompt, 1);

        return yesno(NULL, true, safeanswer, false, false, true);
    }
    else
        return yesno(prompt, true, safeanswer, false, false, false);
}



//dtsund: Acquirement was changed rather drastically.  Now lets you choose from
//specific items, rather than classes of item.
bool acquirement(object_class_type class_wanted, int agent,
                 bool quiet, int* item_index, bool debug)
{
    ASSERT(!crawl_state.game_is_arena());

    int thing_created = NON_ITEM;

    if (item_index == NULL)
        item_index = &thing_created;

    *item_index = NON_ITEM;

    //Experimental new scroll of acquirement code below.
    //Largely ripped shamelessly from shopping.cc.
    if(class_wanted == OBJ_RANDOM)
    {
        unwind_bool in_acquirement(_acquiring_now, true);
        cursor_control coff(false);
        bool tempQuiet = true;

        clrscr();

        const std::string hello = "What would you like to acquire?";
        
        std::vector<int> stock(8);
        
        if(you.species == SP_CAT)
        {
            stock.resize(5, false);
        }

        //dtsund: I don't really know what half of this does.
        coord_def stock_loc = coord_def(0, MAX_SHOPS+1);
        
        int index = 0;

        if(you.species != SP_CAT)
        {
            stock[index] = acquirement_create_item_general(OBJ_WEAPONS, agent, tempQuiet,
                                                   stock_loc, debug, false);
            index++;
            stock[index] = acquirement_create_item_general(OBJ_ARMOUR, agent, tempQuiet,
                                                   stock_loc, debug, false);
            index++;
        }
        stock[index] = acquirement_create_item_general(OBJ_JEWELLERY, agent, tempQuiet,
                                                   stock_loc, debug, false);
        index++;
        stock[index] = acquirement_create_item_general(OBJ_BOOKS, agent, tempQuiet,
                                                   stock_loc, debug, false);
        index++;
        if(you.species != SP_CAT)
        {
            stock[index] = acquirement_create_item_general(OBJ_STAVES, agent, tempQuiet,
                                                   stock_loc, debug, false);
            index++;
            stock[index] = acquirement_create_item_general(OBJ_WANDS, agent, tempQuiet,
                                                   stock_loc, debug, false);
            index++;
        }
        stock[index] = acquirement_create_item_general(OBJ_POTIONS, agent, tempQuiet,
                                                   stock_loc, debug, false);
        index++;
        stock[index] = acquirement_create_item_general(OBJ_GOLD, agent, tempQuiet,
                                                   stock_loc, debug, false);
        index++;

        // Autoinscribe randarts in the menu.
        for (unsigned int i = 0; i < stock.size(); i++)
        {
            item_def& item = mitm[stock[i]];
            if(is_artefact(item))
            {   
                //Need to explicitly ID artifacts to prevent name mangling
                //but don't want to hand out ID info like candy
                set_ident_type(item, ID_KNOWN_TYPE);
                set_ident_flags(item, ISFLAG_IDENT_MASK);
            }
            if (Options.autoinscribe_artefacts && is_artefact(item))
            {
                item.inscription = artefact_auto_inscription(item);
            }
        }

        std::vector<bool> selected;
        selected.resize(stock.size(), false);

        const bool id_stock         = true;
              bool viewing          = false;
    
        while (true)
        {
            int num_selected = 0;
            for (unsigned int i = 0; i < stock.size(); i++)
            {
                if (selected[i])
                    num_selected++;
            }
            clrscr();
            if (stock.empty())
            {
                //Shouldn't ever get here, maybe put an ASSERT here.
            }

            const std::string purchasable = _acquirement_print_stock(stock, selected);
            _list_acquirement_keys(purchasable, viewing, stock.size(), num_selected);

            //Money stuff, I'll show it just to help decide about gold acquirement.
            snprintf(info, INFO_SIZE, "You have %d gold piece%s.", you.gold,
                     you.gold != 1 ? "s" : "");

            textcolor(YELLOW);

            _acquire_print(info, 0);
            
            if(viewing)
            {
                snprintf(info, INFO_SIZE, "What would you like to examine? ");
            }
            else
            {
                snprintf(info, INFO_SIZE, "What would you like to acquire? ");
            }

            textcolor(CYAN);
            _acquire_print(info, 1);

            textcolor(LIGHTGREY);

            mouse_control mc(MOUSE_MODE_MORE);
            int key = getchm();

            if (key == '\\')
                check_item_knowledge();
            else if (key == 'x' || key_is_escape(key) || key == CK_MOUSE_CMD)
            {
                snprintf(info, INFO_SIZE, "Really cancel your acquirement? (y/n)");
                if (_acquire_yesno(info, 'n'))
                {
                    break;
                }   
            }
            else if (key == '\r' || key == CK_MOUSE_CLICK)
            {
                if (num_selected == 0) // Nothing selected.
                {
                    _acquire_print("You haven't selected anything!", 1);
                    continue;
                }
                else
                {
                    snprintf(info, INFO_SIZE, "Acquire this item? (y/n)");

                    if (_acquire_yesno(info, 'n'))
                    {
                        //int num_items = 0, outside_items = 0, quant;
                        for (unsigned int i =0; i < selected.size(); i++)
                        {
                            item_def& item = mitm[stock[i]];
                            if (selected[i])
                            {
                                //May as well ID it before giving it to the player.
                                set_ident_type(item, ID_KNOWN_TYPE);
                                set_ident_flags(item, ISFLAG_IDENT_MASK);
                                move_item_to_grid(&stock[i], you.pos());
                            }
                            else
                            {
                                destroy_item(item);
                            }
                        }
                        canned_msg(MSG_SOMETHING_APPEARS);
                        break;
                    }
                }
                continue;
            }
            else if (key == '!')
            {
                // Toggle between browsing and shopping.
                viewing = !viewing;
            }
            else if (key == '?' || key == '*')
                browse_inventory(false);

            else if (!isaalpha(key))
            {
                _acquire_print("Huh?", 1);
                _acquire_more();
            }
            else
            {
                key = tolower(key) - 'a';
                if (key >= static_cast<int>(stock.size()))
                {
                    _acquire_print("No such item.", 1);
                    _acquire_more();
                    continue;
                }

                item_def& item = mitm[stock[key]];
                if (viewing)
                {
                    // A hack to make the description more useful.
                    // In theory, the user could kill the process at this
                    // point and end up with valid ID for the item.
                    // That's not very useful, though, because it doesn't set
                    // type-ID and once you can access the item (by buying it)
                    // you have its full ID anyway. Worst case, it won't get
                    // noted when you buy it.
                    const uint64_t old_flags = item.flags;
                    if (id_stock)
                    {
                        item.flags |= (ISFLAG_IDENT_MASK | ISFLAG_NOTED_ID
                                       | ISFLAG_NOTED_GET);
                    }
                    describe_item(item, false, true);
                    if (id_stock)
                        item.flags = old_flags;
                }
                else
                {                
                    //This for loop serves the purpose of only allowing the player to
                    //acquire one item.
                    for(unsigned int i = 0; i < selected.size(); i++)
                    {
                        selected[i] = false;
                    }

                    selected[key] = !selected[key];
                }
            }
        }

    redraw_screen();
    return(true);

    }

    *item_index = acquirement_create_item(class_wanted, agent, quiet,
                                          you.pos(), debug);

    return (true);
}
