#include "PlayerbotClassAI.h"
#include "Common.h"
#include "../SpellAuras.h"

PlayerbotClassAI::PlayerbotClassAI(Player* const master, Player* const bot, PlayerbotAI* const ai)
{
    m_master = master;
    m_bot = bot;
    m_ai = ai;

    m_MinHealthPercentTank   = 80;
    m_MinHealthPercentHealer = 60;
    m_MinHealthPercentDPS    = 30;
    m_MinHealthPercentMaster = m_MinHealthPercentDPS;

    ClearWait();
}
PlayerbotClassAI::~PlayerbotClassAI() {}

CombatManeuverReturns PlayerbotClassAI::DoFirstCombatManeuver(Unit *) { return RETURN_NO_ACTION_OK; }
CombatManeuverReturns PlayerbotClassAI::DoNextCombatManeuver(Unit *) { return RETURN_NO_ACTION_OK; }

CombatManeuverReturns PlayerbotClassAI::DoFirstCombatManeuverPVE(Unit *) { return RETURN_NO_ACTION_OK; }
CombatManeuverReturns PlayerbotClassAI::DoNextCombatManeuverPVE(Unit *) { return RETURN_NO_ACTION_OK; }
CombatManeuverReturns PlayerbotClassAI::DoFirstCombatManeuverPVP(Unit *) { return RETURN_NO_ACTION_OK; }
CombatManeuverReturns PlayerbotClassAI::DoNextCombatManeuverPVP(Unit *) { return RETURN_NO_ACTION_OK; }

void PlayerbotClassAI::DoNonCombatActions()
{
    DEBUG_LOG("[PlayerbotAI]: Warning: Using PlayerbotClassAI::DoNonCombatActions() rather than class specific function");
}

bool PlayerbotClassAI::EatDrinkBandage(bool bMana, unsigned char foodPercent, unsigned char drinkPercent, unsigned char bandagePercent)
{
    Item* drinkItem = nullptr;
    Item* foodItem = nullptr;
    if (bMana && m_ai->GetManaPercent() < drinkPercent)
        drinkItem = m_ai->FindDrink();
    if (m_ai->GetHealthPercent() < foodPercent)
        foodItem = m_ai->FindFood();
    if (drinkItem || foodItem)
    {
        // have it wait until drinks are finished and/or combat begins
        //m_ai->SetCombatOrder(PlayerbotAI::ORDERS_TEMP_WAIT_OOC);
        //SetWait(25); // seconds
		//m_ai->SetIgnoreUpdateTime(25);
        if (drinkItem)
        {
            m_ai->TellMaster("I could use a drink.");
            m_ai->UseItem(drinkItem);
        }
        if (foodItem)
        {
            m_ai->TellMaster("I could use some food.");
            m_ai->UseItem(foodItem);
        }
        return true;
    }

    if (m_ai->GetHealthPercent() < bandagePercent && !m_bot->HasAura(RECENTLY_BANDAGED))
    {
        Item* bandageItem = m_ai->FindBandage();
        if (bandageItem)
        {
            m_ai->TellMaster("I could use first aid.");
            m_ai->UseItem(bandageItem);
            return true;
        }
    }

    return false;
}

bool PlayerbotClassAI::CanPull()
{
    DEBUG_LOG("[PlayerbotAI]: Warning: Using PlayerbotClassAI::CanPull() rather than class specific function");
    return false;
}

bool PlayerbotClassAI::CastHoTOnTank()
{
    DEBUG_LOG("[PlayerbotAI]: Warning: Using PlayerbotClassAI::CastHoTOnTank() rather than class specific function");
    return false;
}

CombatManeuverReturns PlayerbotClassAI::HealPlayer(Player* target) {
    if (!m_ai)  return RETURN_NO_ACTION_ERROR;
    if (!m_bot) return RETURN_NO_ACTION_ERROR;

    if (!target) return RETURN_NO_ACTION_INVALIDTARGET;
    if (target->IsInDuel()) return RETURN_NO_ACTION_INVALIDTARGET;

    return RETURN_NO_ACTION_OK;
}

// Please note that job_type JOB_MANAONLY is a cumulative restriction. JOB_TANK | JOB_HEAL means both; JOB_TANK | JOB_MANAONLY means tanks with powertype MANA (paladins, druids)
CombatManeuverReturns PlayerbotClassAI::Buff(bool (*BuffHelper)(PlayerbotAI*, uint32, Unit*), uint32 spellId, uint32 type, bool bMustBeOOC)
{
    if (!m_ai)  return RETURN_NO_ACTION_ERROR;
    if (!m_bot) return RETURN_NO_ACTION_ERROR;
    if (!m_bot->isAlive() || m_bot->IsInDuel()) return RETURN_NO_ACTION_ERROR;
    if (bMustBeOOC && m_bot->isInCombat()) return RETURN_NO_ACTION_ERROR;

    if (spellId == 0) return RETURN_NO_ACTION_OK;

    // First, fill the list of targets
    if (m_bot->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = m_bot->GetGroup()->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *groupMember = sObjectMgr.GetPlayer(itr->guid);
            if (!groupMember || !groupMember->isAlive() || groupMember->IsInDuel())
                continue;
            JOB_TYPE job = GetTargetJob(groupMember);
            if (job & type && (!(job & JOB_MANAONLY) || groupMember->getClass() == CLASS_DRUID || groupMember->GetPowerType() == POWER_MANA))
            {
                if (BuffHelper(m_ai, spellId, groupMember))
                    return RETURN_CONTINUE;
            }
        }
    }
    else
    {
        if (m_master && !m_master->IsInDuel()
            && (!(GetTargetJob(m_master) & JOB_MANAONLY) || m_master->getClass() == CLASS_DRUID || m_master->GetPowerType() == POWER_MANA))
            if (BuffHelper(m_ai, spellId, m_master))
                return RETURN_CONTINUE;
        // Do not check job or power type - any buff you have is always useful to self
        if (BuffHelper(m_ai, spellId, m_bot))
            return RETURN_CONTINUE;
    }

    return RETURN_NO_ACTION_OK;
}

/**
 * GetHealTarget()
 * return Unit* Returns unit to be healed. First checks 'critical' Healer(s), next Tank(s), next Master (if different from:), next DPS.
 * If none of the healths are low enough (or multiple valid targets) against these checks, the lowest health is healed. Having a target
 * returned does not guarantee it's worth healing, merely that the target does not have 100% health.
 *
 * return NULL If NULL is returned, no healing is required. At all.
 *
 * Will need extensive re-write for co-operation amongst multiple healers. As it stands, multiple healers would all pick the same 'ideal'
 * healing target.
 */
Player* PlayerbotClassAI::GetHealTarget(JOB_TYPE type)
{
    if (!m_ai)  return nullptr;
    if (!m_bot) return nullptr;
    if (!m_bot->isAlive() || m_bot->IsInDuel()) return nullptr;

    // define seperately for sorting purposes - DO NOT CHANGE ORDER!
    std::vector<heal_priority> targets;

    // First, fill the list of targets
    if (m_bot->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = m_bot->GetGroup()->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *groupMember = sObjectMgr.GetPlayer(itr->guid);
            if (!groupMember || !groupMember->isAlive() || groupMember->IsInDuel())
                continue;
            JOB_TYPE job = GetTargetJob(groupMember);
            if (job & type)
                targets.push_back( heal_priority(groupMember, (groupMember->GetHealth() * 100 / groupMember->GetMaxHealth()), job) );
        }
    }
    else
    {
        targets.push_back( heal_priority(m_bot, m_bot->GetHealthPercent(), GetTargetJob(m_bot)) );
        if (m_master && !m_master->IsInDuel())
            targets.push_back( heal_priority(m_master, (m_master->GetHealth() * 100 / m_master->GetMaxHealth()), GetTargetJob(m_master)) );
    }

    // Sorts according to type: Healers first, tanks next, then master followed by DPS, thanks to the order of the TYPE enum
    std::sort(targets.begin(), targets.end());

    uint8 uCount = 0,i = 0;
    // x is used as 'target found' variable; i is used as the targets iterator throughout all 4 types.
    int16 x = -1;

    // Try to find a healer in need of healing (if multiple, the lowest health one)
    while (true)
    {
        // This works because we sorted it above
        if ( (uCount + i) >= targets.size() || !(targets.at(uCount).type & JOB_HEAL)) break;
        uCount++;
    }

    // We have uCount healers in the targets, check if any qualify for priority healing
    for (; uCount > 0; uCount--, i++)
    {
        if (targets.at(i).hp <= m_MinHealthPercentHealer)
            if (x == -1 || targets.at(x).hp > targets.at(i).hp)
                x = i;
    }
    if (x > -1) return targets.at(x).p;

    // Try to find a tank in need of healing (if multiple, the lowest health one)
    while (true)
    {
        if ( (uCount + i) >= targets.size() || !(targets.at(uCount).type & JOB_TANK)) break;
        uCount++;
    }

    for (; uCount > 0; uCount--, i++)
    {
        if (targets.at(i).hp <= m_MinHealthPercentTank)
            if (x == -1 || targets.at(x).hp > targets.at(i).hp)
                x = i;
    }
    if (x > -1) return targets.at(x).p;

    // Try to find master in need of healing (lowest health one first)
    if (m_MinHealthPercentMaster != m_MinHealthPercentDPS)
    {
        while (true)
        {
            if ( (uCount + i) >= targets.size() || !(targets.at(uCount).type & JOB_MASTER)) break;
            uCount++;
        }

        for (; uCount > 0; uCount--, i++)
        {
            if (targets.at(i).hp <= m_MinHealthPercentMaster)
                if (x == -1 || targets.at(x).hp > targets.at(i).hp)
                    x = i;
        }
        if (x > -1) return targets.at(x).p;
    }

    // Try to find anyone else in need of healing (lowest health one first)
    while (true)
    {
        if ( (uCount + i) >= targets.size() ) break;
        uCount++;
    }

    for (; uCount > 0; uCount--, i++)
    {
        if (targets.at(i).hp <= m_MinHealthPercentDPS)
            if (x == -1 || targets.at(x).hp > targets.at(i).hp)
                x = i;
    }
    if (x > -1) return targets.at(x).p;

    // Nobody is critical, find anyone hurt at all, return lowest (let the healer sort out if it's worth healing or not)
    for (i = 0, uCount = targets.size(); uCount > 0; uCount--, i++)
    {
        if (targets.at(i).hp < 100)
            if (x == -1 || targets.at(x).hp > targets.at(i).hp)
                x = i;
    }
    if (x > -1) return targets.at(x).p;

    return nullptr;
}

Player* PlayerbotClassAI::GetResurrectionTarget(JOB_TYPE type, bool bMustBeOOC)
{
    if (!m_ai)  return nullptr;
    if (!m_bot) return nullptr;
    if (!m_bot->isAlive() || m_bot->IsInDuel()) return nullptr;
    if (bMustBeOOC && m_bot->isInCombat()) return nullptr;

    // First, fill the list of targets
    if (m_bot->GetGroup())
    {
        // define seperately for sorting purposes - DO NOT CHANGE ORDER!
        std::vector<heal_priority> targets;

        Group::MemberSlotList const& groupSlot = m_bot->GetGroup()->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *groupMember = sObjectMgr.GetPlayer(itr->guid);
			if (!groupMember || groupMember->isAlive())
                continue;
            JOB_TYPE job = GetTargetJob(groupMember);
            if (job & type)
                targets.push_back( heal_priority(groupMember, 0, job) );
        }

        // Sorts according to type: Healers first, tanks next, then master followed by DPS, thanks to the order of the TYPE enum
        std::sort(targets.begin(), targets.end());

        if (targets.size())
            return targets.at(0).p;
    }
    else if (!m_master->isAlive())
        return m_master;

    return nullptr;
}
//get target by job
Player* PlayerbotClassAI::GetTarget(JOB_TYPE type)
{
	if (!m_ai)  return nullptr;
	if (!m_bot) return nullptr;
	//if (!m_bot->isAlive() || m_bot->IsInDuel()) return nullptr;
	//if (bMustBeOOC && m_bot->isInCombat()) return nullptr;

	// First, fill the list of targets
	if (m_bot->GetGroup())
	{
		// define seperately for sorting purposes - DO NOT CHANGE ORDER!
		std::vector<heal_priority> targets;

		Group::MemberSlotList const& groupSlot = m_bot->GetGroup()->GetMemberSlots();
		for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
		{
			Player *groupMember = sObjectMgr.GetPlayer(itr->guid);
			if (!groupMember /*|| groupMember->isAlive()*/)
				continue;
			JOB_TYPE job = GetTargetJob(groupMember);
			if (job & type)
				targets.push_back(heal_priority(groupMember, 0, job));
		}

		// Sorts according to type: Healers first, tanks next, then master followed by DPS, thanks to the order of the TYPE enum
		std::sort(targets.begin(), targets.end());

		if (targets.size())
			return targets.at(0).p;
	}
	else if (!m_master->isAlive())
		return m_master;

	return nullptr;
}

Player* PlayerbotClassAI::GetDispalTarget(JOB_TYPE type, bool bMustBeOOC)
{
	if (!m_ai)  return NULL;
	if (!m_bot) return NULL;
	if (!m_bot->isAlive()) return NULL;
	//if (bMustBeOOC && m_bot->isInCombat()) return NULL;

	// First, fill the list of targets
	if (m_bot->GetGroup())
	{
		// define seperately for sorting purposes - DO NOT CHANGE ORDER!
		std::vector<heal_priority> targets;
		std::vector<heal_priority> targets1;
		std::vector<heal_priority> targets2;
		std::vector<heal_priority> targets3;

		Group::MemberSlotList const& groupSlot = m_bot->GetGroup()->GetMemberSlots();
		for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
		{
			Player *groupMember = sObjectMgr.GetPlayer(itr->guid);
			if (!groupMember)
				continue;
			uint32 dispelMask = GetDispellMask(DISPEL_CURSE);
			uint32 dispelMask1 = GetDispellMask(DISPEL_DISEASE);
			uint32 dispelMask2 = GetDispellMask(DISPEL_POISON);
			uint32 dispelMask3 = GetDispellMask(DISPEL_MAGIC);

			Unit::SpellAuraHolderMap const& auras = groupMember->GetSpellAuraHolderMap();

			for (Unit::SpellAuraHolderMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
			{
				SpellAuraHolder *holder = itr->second;


				if ((1 << holder->GetSpellProto()->Dispel) & dispelMask)
				{
					if (holder->GetSpellProto()->Dispel == DISPEL_CURSE)
					{

						JOB_TYPE job = GetTargetJob(groupMember);
						if (job & type)
							targets.push_back(heal_priority(groupMember, 0, job));
					}

				}
				else if ((1 << holder->GetSpellProto()->Dispel) & dispelMask1)
				{
					if (holder->GetSpellProto()->Dispel == DISPEL_DISEASE)
					{
						JOB_TYPE job = GetTargetJob(groupMember);
						if (job & type)
							targets1.push_back(heal_priority(groupMember, 0, job));
					}
				}
				else if ((1 << holder->GetSpellProto()->Dispel) & dispelMask2)
				{
					if (holder->GetSpellProto()->Dispel == DISPEL_POISON)
					{
						JOB_TYPE job = GetTargetJob(groupMember);
						if (job & type)
							targets2.push_back(heal_priority(groupMember, 0, job));
					}
				}
				else if ((1 << holder->GetSpellProto()->Dispel) & dispelMask3)
				{
					if (holder->GetSpellProto()->Dispel == DISPEL_MAGIC && (holder->GetSpellProto()->Id != 6136 && holder->GetSpellProto()->Id != 7321 && holder->GetSpellProto()->Id != 12484 && holder->GetSpellProto()->Id != 12485 && holder->GetSpellProto()->Id != 12486 && holder->GetSpellProto()->Id != 15850 && holder->GetSpellProto()->Id != 16927 && holder->GetSpellProto()->Id != 18101 && holder->GetSpellProto()->Id != 20005))
					{
						bool positive = true;
						if (!holder->IsPositive())
							positive = false;
						else
							positive = (holder->GetSpellProto()->AttributesEx & SPELL_ATTR_EX_NEGATIVE) == 0;

						// do not remove positive auras if friendly target
						//               negative auras if non-friendly target
						if (positive == groupMember->IsFriendlyTo(m_bot))
							continue;
						JOB_TYPE job = GetTargetJob(groupMember);
						if (job & type)
							targets3.push_back(heal_priority(groupMember, 0, job));
					}
				}
				else continue;
			}
			std::sort(targets.begin(), targets.end());
			std::sort(targets1.begin(), targets1.end());
			std::sort(targets2.begin(), targets2.end());
			std::sort(targets3.begin(), targets3.end());

			uint8 pClass = m_bot->getClass();

			if (pClass == CLASS_MAGE || pClass == CLASS_DRUID)
			{
				if (targets.size())
					return targets.at(0).p;
			}
			if (pClass == CLASS_PRIEST || pClass == CLASS_PALADIN || pClass == CLASS_SHAMAN)
			{
				if (targets1.size())
					return targets1.at(0).p;
			}
			if (pClass == CLASS_DRUID || pClass == CLASS_PALADIN || pClass == CLASS_SHAMAN)
			{
				if (targets2.size())
					return targets2.at(0).p;
			}
			if (pClass == CLASS_PRIEST || pClass == CLASS_PALADIN)
			{
				if (targets3.size())
					return targets3.at(0).p;
			}
		}






	}

		// Sorts according to type: Healers first, tanks next, then master followed by DPS, thanks to the order of the TYPE enum
		
	
	else if (!m_master->isAlive())
		return m_master;

	return NULL;
}

JOB_TYPE PlayerbotClassAI::GetTargetJob(Player* target)
{
    // is a bot
    if (target->GetPlayerbotAI())
    {
        if (target->GetPlayerbotAI()->IsHealer())
            return JOB_HEAL;
        if (target->GetPlayerbotAI()->IsTank())
            return JOB_TANK;
        return JOB_DPS;
    }

    // figure out what to do with human players - i.e. figure out if they're tank, DPS or healer
    uint32 uSpec = target->GetSpec();
    switch (target->getClass())
    {
        case CLASS_PALADIN:
            if (uSpec == PALADIN_SPEC_HOLY)
                return JOB_HEAL;
            if (uSpec == PALADIN_SPEC_PROTECTION)
                return JOB_TANK;
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
        case CLASS_DRUID:
            if (uSpec == DRUID_SPEC_RESTORATION)
                return JOB_HEAL;
            // Feral can be used for both Tank or DPS... play it safe and assume tank. If not... he best be good at threat management or he'll ravage the healer's mana
            else if (uSpec == DRUID_SPEC_FERAL)
                return JOB_TANK;
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
        case CLASS_PRIEST:
            // Since Discipline can be used for both healer or DPS assume DPS
            if (uSpec == PRIEST_SPEC_HOLY)
                return JOB_HEAL;
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
        case CLASS_SHAMAN:
            if (uSpec == SHAMAN_SPEC_RESTORATION)
                return JOB_HEAL;
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
        case CLASS_WARRIOR:
            if (uSpec == WARRIOR_SPEC_PROTECTION)
                return JOB_TANK;
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_ROGUE:
        case CLASS_HUNTER:
        default:
            return (m_master == target) ? JOB_MASTER : JOB_DPS;
    }
}

CombatManeuverReturns PlayerbotClassAI::CastSpellNoRanged(uint32 nextAction, Unit *pTarget)
{
    if (!m_ai)  return RETURN_NO_ACTION_ERROR;
    if (!m_bot) return RETURN_NO_ACTION_ERROR;

    if (nextAction == 0)
        return RETURN_NO_ACTION_OK; // Asked to do nothing so... yeh... Dooone.

    if (pTarget != nullptr)
        return (m_ai->CastSpell(nextAction, *pTarget) ? RETURN_CONTINUE : RETURN_NO_ACTION_ERROR);
    else
        return (m_ai->CastSpell(nextAction) ? RETURN_CONTINUE : RETURN_NO_ACTION_ERROR);
}

CombatManeuverReturns PlayerbotClassAI::CastSpellWand(uint32 nextAction, Unit *pTarget, uint32 SHOOT)
{
    if (!m_ai)  return RETURN_NO_ACTION_ERROR;
    if (!m_bot) return RETURN_NO_ACTION_ERROR;

    if (SHOOT > 0 && m_bot->FindCurrentSpellBySpellId(SHOOT) && m_bot->GetWeaponForAttack(RANGED_ATTACK, true, true))
    {
        if (nextAction == SHOOT)
            // At this point we're already shooting and are asked to shoot. Don't cause a global cooldown by stopping to shoot! Leave it be.
            return RETURN_CONTINUE; // ... We're asked to shoot and are already shooting so... Task accomplished?

        // We are shooting but wish to cast a spell. Stop 'casting' shoot.
        m_bot->InterruptNonMeleeSpells(true, SHOOT);
        // ai->TellMaster("Interrupting auto shot.");
    }

    // We've stopped ranged (if applicable), if no nextAction just return
    if (nextAction == 0)
        return RETURN_CONTINUE; // Asked to do nothing so... yeh... Dooone.

    if (nextAction == SHOOT)
    {
        if (SHOOT > 0 && m_ai->GetCombatStyle() == PlayerbotAI::COMBAT_RANGED && !m_bot->FindCurrentSpellBySpellId(SHOOT) && m_bot->GetWeaponForAttack(RANGED_ATTACK, true, true))
            return (m_ai->CastSpell(SHOOT, *pTarget) ? RETURN_CONTINUE : RETURN_NO_ACTION_ERROR);
        else
            // Do Melee attack
            return RETURN_NO_ACTION_UNKNOWN; // We're asked to shoot and aren't.
    }

    if (pTarget != nullptr)
        return (m_ai->CastSpell(nextAction, *pTarget) ? RETURN_CONTINUE : RETURN_NO_ACTION_ERROR);
    else
        return (m_ai->CastSpell(nextAction) ? RETURN_CONTINUE : RETURN_NO_ACTION_ERROR);
}

void PlayerbotClassAI::usetrinkets()
{
	Item *Trinkets1, *Trinkets2;
	Trinkets1 = m_bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_TRINKET1);
	Trinkets2 = m_bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_TRINKET2);

	

	if (Trinkets1)
	{
		const ItemPrototype* const pItemProto1 = Trinkets1->GetProto();

		for (int32 Index1 = 0; Index1 < MAX_ITEM_PROTO_SPELLS; ++Index1)
		{


			if (pItemProto1->Spells[Index1].SpellTrigger != 0)
				continue;
			if (pItemProto1->Spells[Index1].SpellTrigger == 0 && (!m_bot->HasSpellCooldown(pItemProto1->Spells[Index1].SpellId)))
			{
				if (pItemProto1->ItemId == 11832 && m_ai->GetManaPercent() < 90)
				{
					m_ai->UseItem(Trinkets1);
					
				}
				else if (pItemProto1->ItemId == 11819 && m_ai->GetManaPercent() < 80)
				{
					m_ai->UseItem(Trinkets1);
				}
				else if (pItemProto1->ItemId == 833 && m_ai->GetHealthPercent() < 60)
				{
					m_ai->UseItem(Trinkets1);
				}
				else
				{
					m_ai->UseItem(Trinkets1);
				}
			}
		}
	}
	if (Trinkets2)
	{
		const ItemPrototype* const pItemProto2 = Trinkets2->GetProto();
		for (int32 Index2 = 0; Index2 < MAX_ITEM_PROTO_SPELLS; ++Index2)
		{

			if (pItemProto2->Spells[Index2].SpellTrigger != 0)
				continue;
			if (pItemProto2->Spells[Index2].SpellTrigger == 0 && (!m_bot->HasSpellCooldown(pItemProto2->Spells[Index2].SpellId)))
			{
				if (pItemProto2->ItemId == 11832 && m_ai->GetManaPercent() < 90)
				{
					m_ai->UseItem(Trinkets2);
					
				}
				else if (pItemProto2->ItemId == 11819 && m_ai->GetManaPercent() < 80)
				{
					m_ai->UseItem(Trinkets2);
				}
				else if (pItemProto2->ItemId == 833 && m_ai->GetHealthPercent() < 60)
				{
					m_ai->UseItem(Trinkets2);
				}
				else
				{
					m_ai->UseItem(Trinkets2);
				}
			}
		}
	}
	
}