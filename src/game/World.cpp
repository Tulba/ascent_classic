/*
 * Ascent MMORPG Server
 * Copyright (C) 2005-2007 Ascent Team <http://www.ascentemu.com/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StdAfx.h"

initialiseSingleton( World );
DayWatcherThread * dw = NULL;

World::World()
{
	m_playerLimit = 0;
	m_allowMovement = true;
	m_gmTicketSystem = true;

	reqGmClient = false;
	GmClientChannel = "";

	m_StartTime = 0;
	eventholder = new EventableObjectHolder(-1);
	m_holder = eventholder;
	m_event_Instanceid = eventholder->GetInstanceID();

	mQueueUpdateInterval = 10000;
	PeakSessionCount = 0;
	mInWorldPlayerCount = 0;
	mAcceptedConnections = 0;
	HordePlayers = 0;
	AlliancePlayers = 0;
	gm_skip_attunement = false;
	show_gm_in_who_list = true;
	map_unload_time=0;
#ifndef CLUSTERING
	SocketSendBufSize = WORLDSOCKET_SENDBUF_SIZE;
	SocketRecvBufSize = WORLDSOCKET_RECVBUF_SIZE;
#endif
	m_levelCap=70;
	m_genLevelCap=70;
}

void CleanupRandomNumberGenerators();
World::~World()
{
	/*sLog.outString("  Saving players to DB...");
	for(SessionMap::iterator i=m_sessions.begin();i!=m_sessions.end();i++)
	{
		(i->second)->LogoutPlayer(true);
	
	}*/

	sLog.outString("  Deleting world packet logger...");
	delete WorldLog::getSingletonPtr();

	sLog.outString("  Deleting Social Manager...");
	delete SocialMgr::getSingletonPtr();

	sLog.outString("  Deleting Object Manager...");
	delete ObjectMgr::getSingletonPtr();
	
	sLog.outString("  Deleting Loot Manager...");		
	delete LootMgr::getSingletonPtr();
	
	sLog.outString("  Deleting LFG Manager...");	
	delete LfgMgr::getSingletonPtr();

	sLog.outString("  Deleting Channel Manager...");
	delete ChannelMgr::getSingletonPtr();

	sLog.outString("  Deleting Quest Manager...");
	delete QuestMgr::getSingletonPtr();
  
	sLog.outString("  Deleting Weather Manager...");
	delete WeatherMgr::getSingletonPtr();

	sLog.outString("  Deleting Taxi Manager...");
	delete TaxiMgr::getSingletonPtr();
	
	sLog.outString("  Deleting Battleground Manager...");	
	delete CBattlegroundManager::getSingletonPtr();

	sLog.outString("Removing all objects and deleting WorldCreator...\n");
	sInstanceMgr.Shutdown();

	//sLog.outString("Deleting Thread Manager..");
	//delete ThreadMgr::getSingletonPtr();
	delete WordFilter::getSingletonPtr();

	sLog.outString("Deleting Random Number Generator...");
	CleanupRandomNumberGenerators();

	for( AreaTriggerMap::iterator i = m_AreaTrigger.begin( ); i != m_AreaTrigger.end( ); ++ i ) 
	{
		delete i->second;
	}

	//eventholder = 0;
	delete eventholder;

	Storage_Cleanup();
	for(list<SpellEntry*>::iterator itr = dummyspells.begin(); itr != dummyspells.end(); ++itr)
		delete *itr;
}


WorldSession* World::FindSession(uint32 id)
{
	m_sessionlock.AcquireReadLock();
	WorldSession * ret = 0;
	SessionMap::const_iterator itr = m_sessions.find(id);

	if(itr != m_sessions.end())
		ret = itr->second;
	
	m_sessionlock.ReleaseReadLock();

	return ret;
}

void World::RemoveSession(uint32 id)
{
	m_sessionlock.AcquireWriteLock();

	SessionMap::iterator itr = m_sessions.find(id);

	if(itr != m_sessions.end())
	{
		delete itr->second;
		m_sessions.erase(itr);
	}

	m_sessionlock.ReleaseWriteLock();
}

void World::AddSession(WorldSession* s)
{
	m_sessionlock.AcquireWriteLock();

	ASSERT(s);
	m_sessions[s->GetAccountId()] = s;

	if(m_sessions.size() >  PeakSessionCount)
		PeakSessionCount = (uint32)m_sessions.size();

	m_sessionlock.ReleaseWriteLock();
}

void World::AddGlobalSession(WorldSession *session)
{
	SessionsMutex.Acquire();
	Sessions.insert(session);
	SessionsMutex.Release();
}

void World::RemoveGlobalSession(WorldSession *session)
{
	SessionsMutex.Acquire();
	Sessions.erase(session);
	SessionsMutex.Release();
}

bool BasicTaskExecutor::run()
{
	/* Set thread priority, this is a bitch for multiplatform :P */
#ifdef WIN32
	switch(priority)
	{
		case BTE_PRIORITY_LOW:
			::SetThreadPriority( ::GetCurrentThread(), THREAD_PRIORITY_LOWEST );
			break;

		case BTW_PRIORITY_HIGH:
			::SetThreadPriority( ::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );
			break;

		default:		// BTW_PRIORITY_MED
			::SetThreadPriority( ::GetCurrentThread(), THREAD_PRIORITY_NORMAL );
			break;
	}
#else
	struct sched_param param;
	switch(priority)
	{
	case BTE_PRIORITY_LOW:
		param.sched_priority = 0;
		break;

	case BTW_PRIORITY_HIGH:
		param.sched_priority = 10;
		break;

	default:		// BTW_PRIORITY_MED
		param.sched_priority = 5;
		break;
	}
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif

	// Execute the task in our new context.
	cb->execute();
#ifdef WIN32
	::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#else
	param.sched_priority = 5;
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif

	return true;
}

void CreateDummySpell(uint32 id)
{
	const char * name = "Dummy Trigger";
	SpellEntry * sp = new SpellEntry;
	memset(sp, 0, sizeof(SpellEntry));
	sp->Id = id;
	sp->Attributes = 384;
	sp->AttributesEx = 268435456;
	sp->Flags3 = 4;
	sp->CastingTimeIndex=1;
	sp->procChance=75;
	sp->rangeIndex=13;
	sp->EquippedItemClass=uint32(-1);
	sp->Effect[0]=3;
	sp->EffectImplicitTargetA[0]=25;
	sp->NameHash=crc32((const unsigned char*)name, (unsigned int)strlen(name));
	sp->dmg_multiplier[0]=1.0f;
	sp->FH=-1;
	dbcSpell.SetRow(id,sp);
	sWorld.dummyspells.push_back(sp);
}

bool World::SetInitialWorldSettings()
{
#ifdef ENABLE_LUA_SCRIPTING
	new LuaEngineMgr;
	LuaEngineMgr::getSingleton().Startup();
#endif
	Log.Line();

	CharacterDatabase.WaitExecute("UPDATE characters SET online = 0 WHERE online = 1");
	//CharacterDatabase.WaitExecute("UPDATE characters SET level = 70 WHERE level > 70");
	CharacterDatabase.WaitExecute("UPDATE characters SET banned=0,banReason='' WHERE banned > 100 AND banned < %u", UNIXTIME);
   
	m_lastTick = UNIXTIME;

	// TODO: clean this
	time_t tiempo;
	char hour[3];
	char minute[3];
	char second[3];
	struct tm *tmPtr;
	tiempo = UNIXTIME;
	tmPtr = localtime(&tiempo);
	strftime( hour, 3, "%H", tmPtr );
	strftime( minute, 3, "%M", tmPtr );
	strftime( second, 3, "%S", tmPtr );
	m_gameTime = (3600*atoi(hour))+(atoi(minute)*60)+(atoi(second)); // server starts at noon

	// TODO: clean this
	// fill in emotes table
	// it appears not every emote has an animation
	mPrices[1] = 10;
	mPrices[4] = 80;
	mPrices[6] = 150;
	mPrices[8] = 200;
	mPrices[10] = 300;
	mPrices[12] = 800;
	mPrices[14] = 900;
	mPrices[16] = 1800;
	mPrices[18] = 2200;
	mPrices[20] = 2300;
	mPrices[22] = 3600;
	mPrices[24] = 4200;
	mPrices[26] = 6700;
	mPrices[28] = 7200;
	mPrices[30] = 8000;
	mPrices[32] = 11000;
	mPrices[34] = 14000;
	mPrices[36] = 16000;
	mPrices[38] = 18000;
	mPrices[40] = 20000;
	mPrices[42] = 27000;
	mPrices[44] = 32000;
	mPrices[46] = 37000;
	mPrices[48] = 42000;
	mPrices[50] = 47000;
	mPrices[52] = 52000;
	mPrices[54] = 57000;
	mPrices[56] = 62000;
	mPrices[58] = 67000;
	mPrices[60] = 72000;

	uint32 start_time = getMSTime();

	Log.Notice("World", "Loading DBC files...");
	if(!LoadDBCs())
		return false;

	/*{
		DBCFile moo;
		moo.open("DBC/FactionTemplate.dbc");
		set<pair<uint32, uint32> > fs;
		set<uint32> known;
		for(uint32 i = 0; i < moo.getRecordCount(); ++i)
		{
			fs.insert(make_pair(moo.getRecord(i).getUInt(0), moo.getRecord(i).getUInt(1)));
			known.insert(moo.getRecord(i).getUInt(0));
		}

		QueryResult * result = sDatabase.Query("SELECT entry, faction FROM creature_proto");
		uint32 e, f;
		do 
		{
			e = result->Fetch()[0].GetUInt32();
			f = result->Fetch()[1].GetUInt32();
			FactionDBC * d;
			if(known.find(f) == known.end())
			{
				printf("Bad faction %u!", f);
				//d = sFactionStore.LookupEntryForced(f);
				d = ((FastIndexedDataStore<FactionDBC>*)FactionStore::getSingletonPtr())->LookupEntryForced(f);
				if(!d)
					printf(".. REALLY bad faction\n");
				else
				{
					// find a faction template that matches.. meh ;p
					set<pair<uint32, uint32> >::iterator itr = fs.begin();
					for(; itr != fs.end(); ++itr)
					{
						if(itr->second == f)
						{
							printf(" replaced with %u (%s)\n", itr->first, sFactionStore.LookupString(d->Name));
							sDatabase.WaitExecute("UPDATE creature_proto SET faction = %u WHERE entry = %u", e, itr->second);
							sDatabase.WaitExecute("UPDATE creature_spawns SET factionid = %u WHERE entry = %u", e, itr->second);
							break;
						}
					}
				}
			}
		} while(result->NextRow());
	}*/

	/* Convert area table ids/flags */
	DBCFile area;
	area.open("DBC/AreaTable.dbc");
	uint32 flag_, area_, zone_;
	for(uint32 i = 0; i < area.getRecordCount(); ++i)
	{
		area_ = area.getRecord(i).getUInt(0);
		flag_ = area.getRecord(i).getUInt(3);
		zone_ = area.getRecord(i).getUInt(2);

		mAreaIDToTable[flag_] = dbcArea.LookupEntry(area_);
		if(mZoneIDToTable.find(zone_) != mZoneIDToTable.end())
		{
			if(mZoneIDToTable[zone_]->AreaFlags != 312 &&
				mAreaIDToTable[flag_]->AreaFlags == 312)
			{
				// over ride.
				mZoneIDToTable[zone_] = mAreaIDToTable[flag_];
			}
		}
		else
		{
			mZoneIDToTable[zone_] = mAreaIDToTable[flag_];
		}
	}

	new ObjectMgr;
	new QuestMgr;
	new LootMgr;
	new LfgMgr;
	new WeatherMgr;
	new TaxiMgr;
	new AddonMgr;
	new SocialMgr;
	new WorldLog;

	// grep: this only has to be done once between version updates
	// to re-fill the table.

	/*sLog.outString("Filling spell replacements table...");
	FillSpellReplacementsTable();
	sLog.outString("");*/

#define MAKE_TASK(sp, ptr) tl.AddTask(new Task(new CallbackP0<sp>(sp::getSingletonPtr(), &sp::ptr)))
	// Fill the task list with jobs to do.
	TaskList tl;
	MAKE_TASK(ObjectMgr, LoadPlayerCreateInfo);
	MAKE_TASK(ObjectMgr, LoadPlayersInfo);
	Storage_FillTaskList(tl);

	// spawn worker threads (2 * number of cpus)
	tl.spawn();

	/* storage stuff has to be loaded first */
	tl.wait();

	Storage_LoadAdditionalTables();

	MAKE_TASK(ObjectMgr, LoadCreatureWaypoints);
	MAKE_TASK(ObjectMgr, LoadTrainers);
	MAKE_TASK(ObjectMgr, LoadTotemSpells);
	MAKE_TASK(ObjectMgr, LoadSpellSkills);
	MAKE_TASK(ObjectMgr, LoadSpellFixes);
	MAKE_TASK(ObjectMgr, LoadSpellOverride);
	MAKE_TASK(ObjectMgr, LoadVendors);
	MAKE_TASK(ObjectMgr, LoadAIThreatToSpellId);
	MAKE_TASK(ObjectMgr, LoadDefaultPetSpells);
	MAKE_TASK(ObjectMgr, LoadPetSpellCooldowns);
	MAKE_TASK(ObjectMgr, LoadGuildCharters);
	MAKE_TASK(ObjectMgr, LoadGMTickets);
	MAKE_TASK(SocialMgr, LoadFromDB);
	MAKE_TASK(AddonMgr, LoadFromDB);
	MAKE_TASK(ObjectMgr, SetHighestGuids);
	MAKE_TASK(ObjectMgr, LoadReputationModifiers);
	MAKE_TASK(ObjectMgr, LoadMonsterSay);
	MAKE_TASK(WeatherMgr, LoadFromDB);
	MAKE_TASK(ObjectMgr,LoadGroups);
	MAKE_TASK(ObjectMgr, LoadCreatureFamilyNames);

	MAKE_TASK(ObjectMgr, LoadExtraCreatureProtoStuff);
	MAKE_TASK(ObjectMgr, LoadExtraItemStuff);
	MAKE_TASK(QuestMgr, LoadExtraQuestStuff);
	MAKE_TASK(ObjectMgr, LoadArenaTeams);

#undef MAKE_TASK

	// wait for all loading to complete.
	tl.wait();
	sLocalizationMgr.Reload(false);

	CommandTableStorage::getSingleton().Load();
	new WordFilter;
	sWordFilter.Load();

#ifdef ENABLE_CHECKPOINT_SYSTEM
	new CheckpointMgr;
	CheckpointMgr::getSingleton().Load();
#endif

	sLog.outString("");
	Log.Notice("World", "Database loaded in %ums.", getMSTime() - start_time);
	sLog.outString("");

#ifdef COLLISION
	Log.Notice("Collision", "Initializing...");
	Collision::InitTileManagers();
#endif

	// calling this puts all maps into our task list.
	sInstanceMgr.Load(&tl);

	// wait for the events to complete.
	tl.wait();

	// wait for them to exit, now.
	tl.kill();
	tl.waitForThreadsToExit();
	sLog.outString("");
	LoadNameGenData();


	//Updating spell.dbc--this is slow like hell due to we cant read string fields
	//dbc method will be changed in future
	DBCFile dbc;

	dbc.open("DBC/Spell.dbc");
	Log.Notice("World", "Processing %u spells...", dbc.getRecordCount());

	QueryResult * result = WorldDatabase.Query("SELECT * FROM spell_proc_data");
	map<uint32, pair<uint32, int32> > procMap;
	if(result)
	{
		do 
		{
			pair<uint32,int32> p;
			p.first = result->Fetch()[1].GetUInt32();
			p.second = result->Fetch()[2].GetInt32();
			procMap.insert(make_pair(result->Fetch()[0].GetUInt32(), p));
		} while(result->NextRow());
	}
	uint32 cnt = (uint32)dbc.getRecordCount();
	uint32 effect;
	uint32 All_Seal_Groups_Combined=0;

	map<uint32, uint32> talentSpells;
	map<uint32,uint32>::iterator talentSpellIterator;
	unsigned int i,j;
	for(i = 0; i < dbcTalent.GetNumRows(); ++i)
	{
		TalentEntry * tal = dbcTalent.LookupRow(i);
		for(j = 0; j < 5; ++j)
			if(tal->RankID[j] != 0)
				talentSpells.insert(make_pair(tal->RankID[j], tal->TalentTree));
	}


	for(uint32 x=0; x < cnt; x++)
	{
		uint32 result = 0;
		// SpellID
		uint32 spellid = dbc.getRecord(x).getUInt(0);
		// Description field
		char* desc = (char*)dbc.getRecord(x).getString(157); 
		const char* ranktext = dbc.getRecord(x).getString(140);
		const char* nametext = dbc.getRecord(x).getString(123);

		uint32 rank = 0;
		uint32 type = 0;
		uint32 namehash = 0;

		// get spellentry
		SpellEntry * sp = dbcSpell.LookupEntry(spellid);

		// hash the name
		//!!!!!!! representing all strings on 32 bits is dangerous. There is a chance to get same hash for a lot of strings ;)
        namehash = crc32((const unsigned char*)nametext, (unsigned int)strlen(nametext));
		sp->NameHash   = namehash; //need these set before we start processing spells

		float radius=max(::GetRadius(dbcSpellRadius.LookupEntry(sp->EffectRadiusIndex[0])),::GetRadius(dbcSpellRadius.LookupEntry(sp->EffectRadiusIndex[1])));
		radius=max(::GetRadius(dbcSpellRadius.LookupEntry(sp->EffectRadiusIndex[2])),radius);
		radius=max(GetMaxRange(dbcSpellRange.LookupEntry(sp->rangeIndex)),radius);
		sp->base_range_or_radius_sqr = radius*radius;

		for(uint32 b=0;b<3;++b)
		{
			if(sp->EffectTriggerSpell[b] != 0 && dbcSpell.LookupEntryForced(sp->EffectTriggerSpell[b]) == NULL)
			{
				/* proc spell referencing non-existant spell. create a dummy spell for use w/ it. */
				CreateDummySpell(sp->EffectTriggerSpell[b]);
			}
		}

		sp->proc_interval = 0;//trigger at each event
		sp->c_is_flags = 0;

		talentSpellIterator = talentSpells.find(sp->Id);
		if(talentSpellIterator == talentSpells.end())
			sp->talent_tree = 0;
		else
			sp->talent_tree = talentSpellIterator->second;

		// parse rank text
		if(!sscanf(ranktext, "Rank %d", (unsigned int*)&rank))
			rank = 0;

		if(namehash == 0x56392512)			/* seal of light */
			sp->procChance=45;	/* this will do */

		if(namehash==0xC5C30B39)		/* seal of command */
			sp->Spell_Dmg_Type=1;

		if(namehash==0x11d6b48c)		/* judgement of command */
			sp->Spell_Dmg_Type=1;

		//these mostly do not mix so we can use else 
        // look for seal, etc in name
        if(strstr(nametext, "Seal"))
		{
            type |= SPELL_TYPE_SEAL;
			All_Seal_Groups_Combined |= sp->SpellGroupType;
		}
        else if(strstr(nametext, "Blessing"))
            type |= SPELL_TYPE_BLESSING;
        else if(strstr(nametext, "Curse"))
            type |= SPELL_TYPE_CURSE;
        else if(strstr(nametext, "Aspect"))
            type |= SPELL_TYPE_ASPECT;
        else if(strstr(nametext, "Sting") || strstr(nametext, "sting"))
            type |= SPELL_TYPE_STING;
        // don't break armor items!
        else if(strcmp(nametext, "Armor") && strstr(nametext, "Armor") || strstr(nametext, "Demon Skin"))
            type |= SPELL_TYPE_ARMOR;
        else if(strstr(nametext, "Aura"))
            type |= SPELL_TYPE_AURA;
		else if(strstr(nametext, "Track")==nametext)
            type |= SPELL_TYPE_TRACK;
//		else if(strstr(nametext, "Gift of the Wild") || strstr(nametext, "Mark of the Wild"))
		else if(namehash==0xF77CAB47 || namehash==0x202F1D74)
            type |= SPELL_TYPE_MARK_GIFT;
//		else if(strstr(nametext, "Immolation Trap") || strstr(nametext, "Freezing Trap") || strstr(nametext, "Frost Trap") || strstr(nametext, "Explosive Trap") || strstr(nametext, "Snake Trap"))
		else if(namehash==0x238FEAF2 || namehash==0x593D7EC7 || namehash==0xF7318BD7 || namehash==0x54B6F3EE || namehash==0xF57361BB)
            type |= SPELL_TYPE_HUNTER_TRAP;
//		else if(strstr(nametext, "Arcane Intellect") || strstr(nametext, "Arcane Brilliance"))
		else if(namehash==0x2F7018BA || namehash==0x9CA89B04)
            type |= SPELL_TYPE_MAGE_INTEL;
//		else if(strstr(nametext, "Amplify Magic") || strstr(nametext, "Dampen Magic"))
		else if(namehash==0x75E2ACFF || namehash==0x3C4E2404)
            type |= SPELL_TYPE_MAGE_MAGI;
//		else if(strstr(nametext, "Fire Ward") || strstr(nametext, "Frost Ward"))
		else if(namehash==0xC83A99CB || namehash==0x80D1ACFF)
            type |= SPELL_TYPE_MAGE_WARDS;
//		else if(strstr(nametext, "Shadow Protection") || strstr(nametext, "Prayer of Shadow Protection"))
		else if(namehash==0xD9948728 || namehash==0xBDA6C77C)
            type |= SPELL_TYPE_PRIEST_SH_PPROT;
//		else if(strstr(nametext, "Water Shield") || strstr(nametext, "Earth Shield") || strstr(nametext, "Lightning Shield"))
		else if(namehash==0xB9DAC27E || namehash==0x0227BA8B || namehash==0x2123CF1E)
            type |= SPELL_TYPE_SHIELD;
//		else if(strstr(nametext, "Power Word: Fortitude") || strstr(nametext, "Prayer of Fortitude"))
		else if(namehash==0x9056D252 || namehash==0x519F97B3)
            type |= SPELL_TYPE_FORTITUDE;
//		else if(strstr(nametext, "Divine Spirit") || strstr(nametext, "Prayer of Spirit"))
		else if(namehash==0xB1792C31 || namehash==0xC8F56DAF)
            type |= SPELL_TYPE_SPIRIT;
//		else if(strstr(nametext, "Curse of Weakness") || strstr(nametext, "Curse of Agony") || strstr(nametext, "Curse of Recklessness") || strstr(nametext, "Curse of Tongues") || strstr(nametext, "Curse of the Elements") || strstr(nametext, "Curse of Idiocy") || strstr(nametext, "Curse of Shadow") || strstr(nametext, "Curse of Doom"))
//		else if(namehash==4129426293 || namehash==885131426 || namehash==626036062 || namehash==3551228837 || namehash==2784647472 || namehash==776142553 || namehash==3407058720 || namehash==202747424)
//		else if(strstr(nametext, "Curse of "))
//            type |= SPELL_TYPE_WARLOCK_CURSES;
		else if(strstr(nametext, "Immolate") || strstr(nametext, "Conflagrate"))
			type |= SPELL_TYPE_WARLOCK_IMMOLATE;
		else if(strstr(nametext, "Amplify Magic") || strstr(nametext, "Dampen Magic"))
			type |= SPELL_TYPE_MAGE_AMPL_DUMP;
        else if(strstr(desc, "Battle Elixir"))
            type |= SPELL_TYPE_ELIXIR_BATTLE;
        else if(strstr(desc, "Guardian Elixir"))
            type |= SPELL_TYPE_ELIXIR_GUARDIAN;
        else if(strstr(desc, "Battle and Guardian elixir"))
            type |= SPELL_TYPE_ELIXIR_FLASK;
		else if(namehash==0xFF89ABD2)		// hunter's mark
			type |= SPELL_TYPE_HUNTER_MARK;
		else if(strstr(desc, "Finishing move")==desc)
			sp->c_is_flags |= SPELL_FLAG_IS_FINISHING_MOVE;
		if(IsDamagingSpell(sp))
			sp->c_is_flags |= SPELL_FLAG_IS_DAMAGING;
		if(IsHealingSpell(sp))
			sp->c_is_flags |= SPELL_FLAG_IS_HEALING;
		if(IsTargetingStealthed(sp))
			sp->c_is_flags |= SPELL_FLAG_IS_TARGETINGSTEALTHED;
		

		//stupid spell ranking problem
		if(sp->spellLevel==0)
		{
			uint32 new_level=0;
			if(strstr(nametext, "Apprentice "))
				new_level = 1;
			else if(strstr(nametext, "Journeyman "))
				new_level = 2;
			else if(strstr(nametext, "Expert "))
				new_level = 3;
			else if(strstr(nametext, "Artisan "))
				new_level = 4;
			else if(strstr(nametext, "Master "))
				new_level = 5;
			if(new_level!=0)
			{
				uint32 teachspell=0;
				if(sp->Effect[0]==SPELL_EFFECT_LEARN_SPELL)
					teachspell = sp->EffectTriggerSpell[0];
				else if(sp->Effect[1]==SPELL_EFFECT_LEARN_SPELL)
					teachspell = sp->EffectTriggerSpell[1];
				else if(sp->Effect[2]==SPELL_EFFECT_LEARN_SPELL)
					teachspell = sp->EffectTriggerSpell[2];
				if(teachspell)
				{
					SpellEntry *spellInfo;
					spellInfo = dbcSpell.LookupEntry(teachspell);
					spellInfo->spellLevel = new_level;
					sp->spellLevel = new_level;
				}
			}
		}

		/*FILE * f = fopen("C:\\spells.txt", "a");
		fprintf(f, "case 0x%08X:		// %s\n", namehash, nametext);
		fclose(f);*/

		// find diminishing status
		sp->DiminishStatus = GetDiminishingGroup(namehash);
		sp->buffIndexType=0;
		switch(namehash)
		{
		case 0xFF89ABD2:		// Hunter's mark
			sp->buffIndexType = SPELL_TYPE_INDEX_MARK;
			break;

		case 0x2266F1F2:		// Polymorph
		case 0xB15D524E:		// Polymorph: Chicken
		case 0xA73086F1:		// Polymorph: Pig
		case 0xBD6B76DC:		// Polymorph: Sheep
		case 0x0128F8E9:		// Polymorph: Turtle
			sp->buffIndexType = SPELL_TYPE_INDEX_POLYMORPH;
			break;

		case 0x328E44DC:		// Fear
			sp->buffIndexType = SPELL_TYPE_INDEX_FEAR;
			break;

		case 0x4219BB33:		// Sap
			sp->buffIndexType = SPELL_TYPE_INDEX_SAP;
			break;

		case 0x94675337:		// Scare Beast
			sp->buffIndexType = SPELL_TYPE_INDEX_SCARE_BEAST;
			break;

		case 0x898B6207:		// Hibernate
			sp->buffIndexType = SPELL_TYPE_INDEX_HIBERNATE;
			break;

		case 0x0227BA8B:		// Earth Shield
			sp->buffIndexType = SPELL_TYPE_INDEX_EARTH_SHIELD;
			break;

		case 0x73812928:		// Cyclone
			sp->buffIndexType = SPELL_TYPE_INDEX_CYCLONE;
			break;

		case 0x3D46465A:		// Banish
			sp->buffIndexType = SPELL_TYPE_INDEX_BANISH;
			break;
		}

		// HACK FIX: Break roots/fear on damage.. this needs to be fixed properly!
		if(!(sp->AuraInterruptFlags & AURA_INTERRUPT_ON_ANY_DAMAGE_TAKEN))
		{
			for(uint32 z = 0; z < 3; ++z) {
				if(sp->EffectApplyAuraName[z] == SPELL_AURA_MOD_FEAR ||
					sp->EffectApplyAuraName[z] == SPELL_AURA_MOD_ROOT)
				{
					sp->AuraInterruptFlags |= AURA_INTERRUPT_ON_UNUSED2;
					break;
				}

				if((sp->Effect[z] == SPELL_EFFECT_SCHOOL_DAMAGE&&sp->Spell_Dmg_Type==2) || sp->Effect[z] == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL || sp->Effect[z] == SPELL_EFFECT_WEAPON_DAMAGE || sp->Effect[z] == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE || sp->Effect[z] == SPELL_EFFECT_DUMMYMELEE)
					sp->is_melee_spell = true;
			}
		}

		// set extra properties
		sp->buffType   = type;
		sp->RankNumber = rank;

		uint32 pr=sp->procFlags;
		for(uint32 y=0;y < 3; y++)
		{
			// get the effect number from the spell
			effect = dbc.getRecord(x).getUInt(64 + y); // spelleffect[0] = 64 // 2.0.1

			//spell group
			/*if(effect==SPELL_EFFECT_SUMMON_TOTEM_SLOT1||effect==SPELL_EFFECT_SUMMON_TOTEM_SLOT2||
				effect==SPELL_EFFECT_SUMMON_TOTEM_SLOT3||effect==SPELL_EFFECT_SUMMON_TOTEM_SLOT4)
			{
			
					const char *p=desc;
					while(p=strstr(p,"$"))
					{
						p++;
						//got $  -> check if spell
						if(*p>='0' && *p <='9')
						{//woot this is spell id
							uint32 tmp=atoi(p);
							SpellEntry*s=sSpellStore.LookupEntry(tmp);
							bool ch=false;
							for(uint32 i=0;i<3;i++)
								if(s->EffectTriggerSpell[i])
								{
									ch=true;
									result=tmp;
									break;
								}
							if(ch)break;
							result=tmp;
							
						}
					
					}
				
			}else*/
			/*if(effect==SPELL_EFFECT_ENCHANT_ITEM)//add inventory type check
			{
				result=0;
				//136--desc field
				//dirty code
				if(strstr(desc,"head"))
					result|=(1<<INVTYPE_HEAD);
				if(strstr(desc,"leg"))
					result|=(1<<INVTYPE_LEGS);
				if(strstr(desc,"neck"))
					result|=(1<<INVTYPE_NECK);
				if(strstr(desc,"shoulder"))
					result|=(1<<INVTYPE_SHOULDERS);
				if(strstr(desc,"body"))
					result|=(1<<INVTYPE_BODY);
				if(strstr(desc,"chest"))
					result|=((1<<INVTYPE_CHEST)|(1<<INVTYPE_ROBE));
				if(strstr(desc,"waist"))
					result|=(1<<INVTYPE_WAIST);
				if(strstr(desc,"foot")||strstr(desc,"feet")||strstr(desc,"boot"))
					result|=(1<<INVTYPE_FEET);
				if(strstr(desc,"wrist")||strstr(desc,"bracer"))
					result|=(1<<INVTYPE_WRISTS);
				if(strstr(desc,"hand")||strstr(desc,"glove"))
					result|=(1<<INVTYPE_HANDS);
				if(strstr(desc,"finger")||strstr(desc,"ring"))
					result|=(1<<INVTYPE_FINGER);
				if(strstr(desc,"trinket"))
					result|=(1<<INVTYPE_TRINKET);
				if(strstr(desc,"shield"))
					result|=(1<<INVTYPE_SHIELD);
				if(strstr(desc,"cloak"))
					result|=(1<<INVTYPE_CLOAK);
				if(strstr(desc,"robe"))
					result|=(1<<INVTYPE_ROBE);
				//if(strstr(desc,"two")||strstr(desc,"Two"))
				//	result|=(1<<INVTYPE_2HWEAPON);<-handled in subclass
			}
			else*/
			if(effect==SPELL_EFFECT_APPLY_AURA)
			{
				uint32 aura = dbc.getRecord(x).getUInt(94+y); // 58+30+3 = 91
				if( aura == SPELL_AURA_PROC_TRIGGER_SPELL ||
					aura == SPELL_AURA_PROC_TRIGGER_DAMAGE
					)//search for spellid in description
				{
					const char *p=desc;
					while((p=strstr(p,"$")))
					{
						p++;
						//got $  -> check if spell
						if(*p>='0' && *p <='9')
						{//woot this is spell id
						
							result=atoi(p);
						}					
					}
					pr=0;

					uint32 len = (uint32)strlen(desc);
					for(i = 0; i < len; ++i)
						desc[i] = tolower(desc[i]);
					//dirty code for procs, if any1 got any better idea-> u are welcome
					//139944 --- some magic number, it will trigger on all hits etc
						//for seems to be smth like custom check
					if(strstr(desc,"your ranged criticals"))
						pr|=PROC_ON_RANGED_CRIT_ATTACK;
					if(strstr(desc,"chance on hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"takes damage"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"attackers when hit"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"character strikes an enemy"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"strike you with a melee attack"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"target casts a spell"))
						pr|=PROC_ON_CAST_SPELL;
                    if(strstr(desc,"your harmful spells land"))
                        pr|=PROC_ON_CAST_SPELL;
                    if(strstr(desc,"on spell critical hit"))
                        pr|=PROC_ON_SPELL_CRIT_HIT;
                    if(strstr(desc,"spell critical strikes"))
                        pr|=PROC_ON_SPELL_CRIT_HIT;
                    if(strstr(desc,"being able to resurrect"))
                        pr|=PROC_ON_DIE;
					if(strstr(desc,"any damage caused"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"the next melee attack against the caster"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"when successfully hit"))
						pr|=PROC_ON_MELEE_ATTACK ;
					if(strstr(desc,"an enemy on hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"when it hits"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"when successfully hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"on a successful hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"damage to attacker on hit"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"on a hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"strikes you with a melee attack"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"when caster takes damage"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"when the caster is using melee attacks"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"when struck in combat") || strstr(desc,"When struck in combat"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"successful melee attack"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"chance per attack"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"chance per hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"that strikes a party member"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"when hit by a melee attack"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"landing a melee critical strike"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(desc,"your critical strikes"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(desc,"whenever you deal ranged damage"))
						pr|=PROC_ON_RANGED_ATTACK;
//					if(strstr(desc,"whenever you deal melee damage"))
					if(strstr(desc,"you deal melee damage"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"your melee attacks"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"damage with your Sword"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"when struck in melee combat"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"any successful spell cast against the priest"))
						pr|=PROC_ON_SPELL_HIT_VICTIM;
					if(strstr(desc,"the next melee attack on the caster"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"striking melee or ranged attackers"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM|PROC_ON_RANGED_ATTACK_VICTIM;
					if(strstr(desc,"when damaging an enemy in melee"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"victim of a critical strike"))
						pr|=PROC_ON_CRIT_HIT_VICTIM;
					if(strstr(desc,"on successful melee or ranged attack"))
						pr|=PROC_ON_MELEE_ATTACK|PROC_ON_RANGED_ATTACK;
					if(strstr(desc,"enemy that strikes you in melee"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"after getting a critical strike"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(desc,"whenever damage is dealt to you"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"when ranged or melee damage is dealt"))
						pr|=PROC_ON_MELEE_ATTACK|PROC_ON_RANGED_ATTACK;
					if(strstr(desc,"damaging melee attacks"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"on melee or ranged attack"))
						pr|=PROC_ON_MELEE_ATTACK|PROC_ON_RANGED_ATTACK;
					if(strstr(desc,"on a melee swing"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"Chance on melee"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"spell criticals against you"))
						pr|=PROC_ON_SPELL_CRIT_HIT_VICTIM;
					if(strstr(desc,"after being struck by a melee or ranged critical hit"))
						pr|=PROC_ON_CRIT_HIT_VICTIM;
//					if(strstr(desc,"on a critical hit"))
					if(strstr(desc,"critical hit"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(desc,"strikes the caster"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"a spell, melee or ranged attack hits the caster"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"after dealing a critical strike"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(desc,"each melee or ranged damage hit against the priest"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM|PROC_ON_RANGED_ATTACK_VICTIM;				
					if(strstr(desc, "a chance to deal additional"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc, "chance to get an extra attack"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc, "melee attacks has"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc, "any damage spell hits a target"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc, "giving each melee attack a chance"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc, "damage when hit"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM; //myabe melee damage ?
					if(strstr(desc, "gives your"))
					{
						if(strstr(desc, "melee"))
							pr|=PROC_ON_MELEE_ATTACK;
						else if(strstr(desc,"sinister strike, backstab, gouge and shiv"))
							pr|=PROC_ON_CAST_SPELL;
						else if(strstr(desc,"chance to daze the target"))
							pr|=PROC_ON_CAST_SPELL;
						else if(strstr(desc,"finishing moves"))
							pr|=PROC_ON_CAST_SPELL;
//						else if(strstr(desc,"shadow bolt, shadowburn, soul fire, incinerate, searing pain and conflagrate"))
//							pr|=PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF;
						//we should find that specific spell (or group) on what we will trigger
						else pr|=PROC_ON_CAST_SPECIFIC_SPELL;
					}
					if(strstr(desc, "chance to add an additional combo") && strstr(desc, "critical") )
						pr|=PROC_ON_CRIT_ATTACK;
					else if(strstr(desc, "chance to add an additional combo"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc, "victim of a melee or ranged critical strike"))
						pr|=PROC_ON_CRIT_HIT_VICTIM;
					if(strstr(desc, "getting a critical effect from"))
						pr|=PROC_ON_SPELL_CRIT_HIT_VICTIM;
					if(strstr(desc, "damaging attack is taken"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc, "struck by a Stun or Immobilize"))
						pr|=PROC_ON_SPELL_HIT_VICTIM;
					if(strstr(desc, "melee critical strike"))
						pr|=PROC_ON_CRIT_ATTACK;
					if(strstr(nametext, "Bloodthirst"))
						pr|=PROC_ON_MELEE_ATTACK | PROC_TAGRGET_SELF;
					if(strstr(desc, "experience or honor"))
						pr|=PROC_ON_GAIN_EXPIERIENCE;
					if(strstr(desc,"your next offensive ability"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"hit by a melee or ranged attack"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM | PROC_ON_RANGED_ATTACK_VICTIM;
					if(strstr(desc,"enemy strikes the caster"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"melee and ranged attacks against you"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM | PROC_ON_RANGED_ATTACK_VICTIM;
					if(strstr(desc,"when a block occurs"))
						pr|=PROC_ON_BLOCK_VICTIM;
					if(strstr(desc,"dealing a critical strike from a weapon swing, spell, or ability"))
						pr|=PROC_ON_CRIT_ATTACK|PROC_ON_SPELL_CRIT_HIT;
					if(strstr(desc,"dealing a critical strike from a weapon swing, spell, or ability"))
						pr|=PROC_ON_CRIT_ATTACK|PROC_ON_SPELL_CRIT_HIT;
					if(strstr(desc,"shadow bolt critical strikes increase shadow damage"))
						pr|=PROC_ON_SPELL_CRIT_HIT;
					if(strstr(desc,"next offensive ability"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"after being hit with a shadow or fire spell"))
						pr|=PROC_ON_SPELL_LAND_VICTIM;
					if(strstr(desc,"giving each melee attack"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"each strike has"))
						pr|=PROC_ON_MELEE_ATTACK;		
					if(strstr(desc,"your Fire damage spell hits"))
						pr|=PROC_ON_CAST_SPELL;		//this happens only on hit ;)
					if(strstr(desc,"corruption, curse of agony, siphon life and seed of corruption spells also cause"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"pain, mind flay and vampiric touch spells also cause"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"shadow damage spells have"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"your spell criticals have"))
						pr|=PROC_ON_SPELL_CRIT_HIT | PROC_ON_SPELL_CRIT_HIT_VICTIM;
					if(strstr(desc,"after dodging their attack"))
					{
						pr|=PROC_ON_DODGE_VICTIM;
						if(strstr(desc,"add a combo point"))
							pr|=PROC_TAGRGET_SELF;
					}
					if(strstr(desc,"fully resisting"))
						pr|=PROC_ON_RESIST_VICTIM;
					if(strstr(desc,"Your Shadow Word: Pain, Mind Flay and Vampiric Touch spells also cause the target"))
						pr|=PROC_ON_CAST_SPELL;
					if(strstr(desc,"your melee and ranged attacks"))
						pr|=PROC_ON_MELEE_ATTACK|PROC_ON_RANGED_ATTACK;
//					if(strstr(desc,"chill effect to your Blizzard"))
//						pr|=PROC_ON_CAST_SPELL;	
					//////////////////////////////////////////////////
					//proc dmg flags
					//////////////////////////////////////////////////
					if(strstr(desc,"each attack blocked"))
						pr|=PROC_ON_BLOCK_VICTIM;
					if(strstr(desc,"into flame, causing an additional"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"victim of a critical melee strike"))
						pr|=PROC_ON_CRIT_HIT_VICTIM;
					if(strstr(desc,"damage to melee attackers"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"target blocks a melee attack"))
						pr|=PROC_ON_BLOCK_VICTIM;
					if(strstr(desc,"ranged and melee attacks to deal"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM | PROC_ON_RANGED_ATTACK_VICTIM;
					if(strstr(desc,"damage on hit"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"chance on hit"))
						pr|=PROC_ON_MELEE_ATTACK;
					if(strstr(desc,"after being hit by any damaging attack"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"striking melee or ranged attackers"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM | PROC_ON_RANGED_ATTACK_VICTIM;
					if(strstr(desc,"damage to attackers when hit"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"striking melee attackers"))
						pr|=PROC_ON_MELEE_ATTACK_VICTIM;
					if(strstr(desc,"whenever the caster takes damage"))
						pr|=PROC_ON_ANY_DAMAGE_VICTIM;
					if(strstr(desc,"damage on every attack"))
						pr|=PROC_ON_MELEE_ATTACK | PROC_ON_RANGED_ATTACK;
					if(strstr(desc,"chance to reflect Fire spells"))
						pr|=PROC_ON_SPELL_HIT_VICTIM;
					if(strstr(desc,"hunter takes on the aspects of a hawk"))
						pr|=PROC_TAGRGET_SELF | PROC_ON_RANGED_ATTACK;
					if(strstr(desc,"successful auto shot attacks"))
						pr|=PROC_ON_AUTO_SHOT_HIT;
					if(strstr(desc,"after getting a critical effect from your"))
						pr=PROC_ON_SPELL_CRIT_HIT;
//					if(strstr(desc,"Your critical strikes from Fire damage"))
//						pr|=PROC_ON_SPELL_CRIT_HIT;
				}//end "if procspellaura"
				//dirty fix to remove auras that should expire on event and they are not
//				else if(sp->procCharges>0)
//				{
					//there are at least 185 spells that should loose charge uppon some event.Be prepared to add more here !
					// ! watch it cause this might conflict with our custom modified spells like : lighning shield !

					//spells like : Presence of Mind,Nature's Swiftness, Inner Focus,Amplify Curse,Coup de Grace
					//SELECT * FROM dbc_spell where proc_charges!=0 and (effect_aura_1=108 or effect_aura_2=108 and effect_aura_3=108) and description!=""
//					if(aura == SPELL_AURA_ADD_PCT_MODIFIER)
//						sp->AuraInterruptFlags |= AURA_INTERRUPT_ON_CAST_SPELL;
					//most of them probably already have these flags...not sure if we should add to all of them without checking
/*					if(strstr(desc, "melee"))
						sp->AuraInterruptFlags |= AURA_INTERRUPT_ON_START_ATTACK;
					if(strstr(desc, "ranged"))
						sp->AuraInterruptFlags |= AURA_INTERRUPT_ON_START_ATTACK;*/
//				}
			}//end "if aura"
		}//end "for each effect"
		sp->procFlags=pr;

		if (strstr(desc, "Must remain seated"))
		{
			sp->RecoveryTime = 1000;
			sp->CategoryRecoveryTime = 1000;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////
		// procintervals
		//////////////////////////////////////////////////////////////////////////////////////////////////////
		//omg lighning shield trigger spell id's are all wrong ?
		//if you are bored you could make thiese by hand but i guess we might find other spells with this problem..and this way it's safe
		if(strstr(nametext, "Lightning Shield") && sp->EffectTriggerSpell[0])
		{
			//check if we can find in the desription
			char *startofid=strstr(desc, "for $");
			if(startofid)
			{
				startofid += strlen("for $");
				sp->EffectTriggerSpell[0]=atoi(startofid); //get new lightning shield trigger id
			}
			sp->proc_interval = 3000; //few seconds
		}
		//mage ignite talent should proc only on some chances
		else if(strstr(nametext, "Ignite") && sp->Id>=11119 && sp->Id<=12848 && sp->EffectApplyAuraName[0]==4)
		{
			//check if we can find in the desription
			char *startofid=strstr(desc, "an additional ");
			if(startofid)
			{
				startofid += strlen("an additional ");
				sp->EffectBasePoints[0]=atoi(startofid); //get new value. This is actually level*8 ;)
			}
			sp->Effect[0] = 6; //aura
			sp->EffectApplyAuraName[0] = 42; //force him to use procspell effect
			sp->EffectTriggerSpell[0] = 12654; //evil , but this is good for us :D
			sp->procFlags = PROC_ON_SPELL_CRIT_HIT; //add procflag here since this was not processed with the others !
		}
		// Winter's Chill handled by frost school
		else if(strstr(nametext, "Winter's Chill"))
		{
			sp->School = 4;
		}
		// Blackout handled by Shadow school
		else if(strstr(nametext, "Blackout"))
		{
			sp->School = 5;
		}
		// Shadow Weaving
		else if(strstr(nametext, "Shadow Weaving"))
		{
			sp->School = 5;
			sp->EffectApplyAuraName[0] = 42;
			sp->procChance = sp->EffectBasePoints[0] + 1;
			sp->procFlags = PROC_ON_CAST_SPECIFIC_SPELL;
		}
		//Improved Aspect of the Hawk
		else if(strstr(nametext, "Improved Aspect of the Hawk"))
			sp->EffectSpellGroupRelation[1] = 0x100000;
		//more triggered spell ids are wrong. I think blizz is trying to outsmart us :S
		else if( strstr(nametext, "Nature's Guardian"))
		{
			sp->EffectTriggerSpell[0]=31616;
			sp->proc_interval = 5000;
		}
		//Chain Heal all ranks %50 heal value (49 + 1)
		else if( strstr(nametext, "Chain Heal"))
		{
			sp->EffectDieSides[0] = 49;
		}
		//this starts to be an issue for trigger spell id : Deep Wounds
		else if(strstr(nametext, "Deep Wounds") && sp->EffectTriggerSpell[0])
		{
			//check if we can find in the desription
			char *startofid=strstr(desc, "over $");
			if(startofid)
			{
				startofid += strlen("over $");
				sp->EffectTriggerSpell[0]=atoi(startofid);
			}
		}
		else if(strstr(nametext, "Holy Shock"))
		{
			//check if we can find in the desription
			char *startofid=strstr(desc, "causing $");
			if(startofid)
			{
				startofid += strlen("causing $");
				sp->EffectTriggerSpell[0]=atoi(startofid);
			}
			//check if we can find in the desription
			startofid=strstr(desc, " or $");
			if(startofid)
			{
				startofid += strlen(" or $");
				sp->EffectTriggerSpell[1]=atoi(startofid);
			}
		}
		else if(strstr(nametext, "Touch of Weakness"))
		{
			//check if we can find in the desription
			char *startofid=strstr(desc, "cause $");
			if(startofid)
			{
				startofid += strlen("cause $");
				sp->EffectTriggerSpell[0]=atoi(startofid);
				sp->EffectTriggerSpell[1]=sp->EffectTriggerSpell[0]; //later versions of this spell changed to eff[1] the aura
				sp->procFlags = uint32(PROC_ON_MELEE_ATTACK_VICTIM);
			}
		}
		else if(strstr(nametext, "Firestone Passive"))
		{
			//Enchants the main hand weapon with fire, granting each attack a chance to deal $17809s1 additional fire damage.
			//check if we can find in the desription
			char * startofid=strstr(desc, "to deal $");
			if(startofid)
			{
				startofid += strlen("to deal $");
				sp->EffectTriggerSpell[0]=atoi(startofid);
				sp->EffectApplyAuraName[0]=42;
				sp->procFlags = PROC_ON_MELEE_ATTACK;
				sp->procChance = 50;
			}
		}
		//some procs trigger at intervals
		else if(strstr(nametext, "Water Shield"))
		{
			sp->proc_interval = 3000; //few seconds
			sp->procFlags |= PROC_TAGRGET_SELF;
		}
		else if(strstr(nametext, "Earth Shield"))
			sp->proc_interval = 3000; //few seconds
		else if(strstr(nametext, "Shadowguard"))
			sp->proc_interval = 3000; //few seconds
		else if(strstr(nametext, "Poison Shield"))
			sp->proc_interval = 3000; //few seconds
		else if(strstr(nametext, "Infused Mushroom"))
			sp->proc_interval = 10000; //10 seconds
		else if(strstr(nametext, "Aviana's Purpose"))
			sp->proc_interval = 10000; //10 seconds
		//don't change to namehash since we are searching only a protion of the name
 		else if(strstr(nametext, "Crippling Poison"))
		{
			sp->SpellGroupType |= 16384; //some of them do have the flags but i's hard to write down those some from 130 spells
			sp->c_is_flags |= SPELL_FLAG_IS_POISON;
		}
		else if(strstr(nametext, "Mind-Numbing Poison"))
		{
			sp->SpellGroupType |= 32768; //some of them do have the flags but i's hard to write down those some from 130 spells
			sp->c_is_flags |= SPELL_FLAG_IS_POISON;
		}
		else if(strstr(nametext, "Instant Poison"))
		{
			sp->SpellGroupType |= 8192; //some of them do have the flags but i's hard to write down those some from 130 spells
			sp->c_is_flags |= SPELL_FLAG_IS_POISON;
		}
		else if(strstr(nametext, "Deadly Poison"))
		{
			sp->SpellGroupType |= 65536; //some of them do have the flags but i's hard to write down those some from 130 spells
			sp->c_is_flags |= SPELL_FLAG_IS_POISON;
		}
		else if(strstr(nametext, "Wound Poison"))
		{
			sp->SpellGroupType |= 268435456; //some of them do have the flags but i's hard to write down those some from 130 spells
			sp->c_is_flags |= SPELL_FLAG_IS_POISON;
		}
		//druid - Swiftmend - required for tree of life
		if(sp->NameHash == 0x176A8339)
			sp->SpellGroupType |= 268435456; //dangerous move !
		//druid - Innervate - required for tree of life
		if(sp->NameHash == 0xC6386A59)
			sp->SpellGroupType |= 268435456; //dangerous move !
		//druid - Nature's Swiftness - required for tree of life
		if(sp->NameHash == 0x4CE6BBE1)
			sp->SpellGroupType |= 268435456; //dangerous move !
		//warlock - Fel armor and demon armor have missing 
		if(sp->NameHash == 0xC6FDD110 || sp->NameHash == 0x915965D6)
			sp->SpellGroupType |= 4096; //some of them do have the flags but i's hard to write down those some from 130 spells
		//warlock - shadow bolt
		if(sp->NameHash == 0x7A7B6753)
			sp->SpellGroupType |= 1; //some of them do have the flags but i's hard to write down those some from 130 spells
		//warlock - Seed of Corruption
		if(sp->NameHash == 0xFD712ED2)
			sp->SpellGroupType |= 65536; 
		//warlock - Curse of Shadow
		if(sp->NameHash == 0xCB139720)
			sp->SpellGroupType |= 131072; 
		//warlock - curse of the elements
		if(sp->NameHash == 0xA5FA5930)
			sp->SpellGroupType |= 2097152; 
/*		else if(strstr(nametext, "Anesthetic Poison"))
			sp->SpellGroupType |= 0; //not yet known ? 
		else if(strstr(nametext, "Blinding Powder"))
			sp->SpellGroupType |= 0; //not yet known ?*/
//		else if(strstr(nametext, "Illumination"))
//			sp->EffectTriggerSpell[0]=20272;
		//sp->dummy=result;
/*		//if there is a proc spell and has 0 as charges then it's probably going to triger infinite times. Better not save these
		if(sp->procCharges==0)
			sp->procCharges=-1;*/

		//Set Silencing spells mech.
		if (sp->EffectApplyAuraName[0] == 27 || 
			sp->EffectApplyAuraName[1] == 27 ||
			sp->EffectApplyAuraName[2] == 27)
			sp->MechanicsType = MECHANIC_SILENCED;
		//Set Stunning spells mech.
		if (sp->EffectApplyAuraName[0] == 12 || 
			sp->EffectApplyAuraName[1] == 12 ||
			sp->EffectApplyAuraName[2] == 12)
			sp->MechanicsType = MECHANIC_STUNNED;
		//Set Fearing spells mech
		if (sp->EffectApplyAuraName[0] == 7 || 
			sp->EffectApplyAuraName[1] == 7 ||
			sp->EffectApplyAuraName[2] == 7)
			sp->MechanicsType = MECHANIC_FLEEING;


		if(sp->proc_interval!=0)
			sp->procFlags |= PROC_REMOVEONUSE;

		/* Seal of Command - Proc Chance */
		if(sp->NameHash == 0xC5C30B39)
		{
			sp->procChance = 25;
			sp->School = SCHOOL_HOLY; //the procspells of the original seal of command have fizical school instead of holy
			sp->Spell_Dmg_Type = SPELL_TYPE_MAGIC; //heh, crazy spell uses melee/ranged/magic dmg type for 1 spell. Now which one is correct ?
		}
		/* Seal of Jusice - Proc Chance */
		else if(sp->NameHash == 0xCC6D4182)
			sp->procChance = 25;
		/* Decapitate */
		else if(sp->NameHash == 0xB6C3243C)
			sp->procChance = 30;
		//shaman - shock, has no spellgroup.very dangerous move !
		else if(sp->NameHash == 0x561A665E)
			sp->SpellGroupType = 4;
		//druid - maul, has missing spellgroup.very dangerous move !
		else if(sp->NameHash == 0x36278137)
			sp->SpellGroupType |= 33554432;
		//druid - swipe, has missing spellgroup.very dangerous move !
		else if(sp->NameHash == 0xDCBA31B0)
			sp->SpellGroupType |= 33554432;
		//mage - fireball. Only some of the spell has the flags 
		else if(sp->NameHash == 0xB39201EC)
			sp->SpellGroupType |= 1;
		else if(sp->NameHash==0x9840A1A6 || sp->NameHash == 0x1513B967 || sp->NameHash==0x204D568D)
			sp->MechanicsType=25;

		if(sp->Id==25771 || sp->Id == 11196 || sp->Id == 6788)
			sp->removable_by_immunity = false;

		/* Backlash */
		if(sp->NameHash == 0x5965939A)
		{
			sp->procFlags |= PROC_ON_MELEE_ATTACK_VICTIM;
			sp->EffectSpellGroupRelation[0] = 1 | 8192;
			sp->AuraInterruptFlags |= AURA_INTERRUPT_ON_CAST_SPELL;
		}

		/* hackfix for this - FIX ME LATER - Burlex */
		if(namehash==3238263755UL)
			sp->procFlags=0;

		map<uint32,pair<uint32,int32> >::iterator itr = procMap.find(namehash);
		if(itr != procMap.end())
		{
			if(itr->second.second > 0)
				sp->procFlags = (uint32)itr->second.second;
			if(itr->first != 0)
				sp->procChance = itr->first;
		}

		if(namehash==0x8D4A2E9F)		// warlock - intensity
			sp->EffectSpellGroupRelation[0] |= 4 | 1 | 64 | 256 | 32 | 128 | 512; //destruction spell
		else if(
			((sp->Attributes & ATTRIBUTES_TRIGGER_COOLDOWN) && (sp->AttributesEx & ATTRIBUTESEX_DELAY_SOME_TRIGGERS)) //rogue cold blood
			|| ((sp->Attributes & ATTRIBUTES_TRIGGER_COOLDOWN) && (!sp->AttributesEx || sp->AttributesEx & ATTRIBUTESEX_REMAIN_OOC))
			)
		{
			sp->c_is_flags |= SPELL_FLAG_IS_REQUIRECOOLDOWNUPDATE;
		}

		if(namehash==0x8401EC6A)
		{
			// shred
			sp->in_front_status = 2;
		}

//junk code to get me has :P 
//if(sp->Id==11267 || sp->Id==11289 || sp->Id==6409)
//	printf("!!!!!!! name %s , id %u , hash %u \n",nametext,sp->Id, namehash);
	}
	//this is so lame : shamanistic rage triggers a new spell which borrows it's stats from parent spell :S
	SpellEntry * parentsp = dbcSpell.LookupEntry(30823);
	SpellEntry * triggersp = dbcSpell.LookupEntry(30824);
	if(parentsp && triggersp) 
		triggersp->EffectBasePoints[0] = parentsp->EffectBasePoints[0];

	SpellEntry * sp = dbcSpell.LookupEntry(16164);
	SpellEntry * sp2;
	if(sp && sp->Id==16164)
		sp->procFlags=PROC_ON_SPELL_CRIT_HIT_VICTIM;

	sp = dbcSpell.LookupEntry(17364);
	//remove stormstrike effect 0
	if(sp && sp->Id==17364)
		sp->Effect[0]=0;

	//Bloodlust targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(2825);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	//Heroism targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(32182);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Drums of war targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(35475);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Symbol of Hope targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(32548);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Drums of Battle targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(35476);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Drums of Panic targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(35474);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Drums of Restoration targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(35478);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
	// Drums of Speed targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(35477);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_ALL_PARTY;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}

	//paladin - Blessing of Light. Changed to scripted because it needs to mod target and should not influence holy nova
	sp = dbcSpell.LookupEntry(19977);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 4;
		sp->EffectApplyAuraName[1] = 4;
	}
	sp = dbcSpell.LookupEntry(19978);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 4;
		sp->EffectApplyAuraName[1] = 4;
	}
	sp = dbcSpell.LookupEntry(19979);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 4;
		sp->EffectApplyAuraName[1] = 4;
	}
	sp = dbcSpell.LookupEntry(27144);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 4;
		sp->EffectApplyAuraName[1] = 4;
	}
	sp = dbcSpell.LookupEntry(32770);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 4;
		sp->EffectApplyAuraName[1] = 4;
	}

	//shaman - Lightning Overload 
	sp = dbcSpell.LookupEntry(30675); 
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc something (we will owerride this)
		sp->EffectTriggerSpell[0]=39805;//proc something (we will owerride this)
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(30678); 
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc something (we will owerride this)
		sp->EffectTriggerSpell[0]=39805;//proc something (we will owerride this)
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(30679); 
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc something (we will owerride this)
		sp->EffectTriggerSpell[0]=39805;//proc something (we will owerride this)
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(30680); 
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc something (we will owerride this)
		sp->EffectTriggerSpell[0]=39805;//proc something (we will owerride this)
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(30681); 
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc something (we will owerride this)
		sp->EffectTriggerSpell[0]=39805;//proc something (we will owerride this)
		sp->procFlags=PROC_ON_CAST_SPELL;
	}

	//shaman - Purge 
	sp = dbcSpell.LookupEntry(370); 
	if(sp)
		sp->DispelType=DISPEL_MAGIC;
	sp = dbcSpell.LookupEntry(8012); 
	if(sp)
		sp->DispelType=DISPEL_MAGIC;
	sp = dbcSpell.LookupEntry(27626); 
	if(sp)
		sp->DispelType=DISPEL_MAGIC;
	sp = dbcSpell.LookupEntry(33625); 
	if(sp)
		sp->DispelType=DISPEL_MAGIC;

	//shaman - Elemental mastery 
	sp = dbcSpell.LookupEntry(16166); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=0xFFFFFFFF;//nature+fire+frost is all that shaman can do
		sp->EffectSpellGroupRelation[1]=0xFFFFFFFF;//nature+fire+frost is all that shaman can do
	}

	//Shaman - Eye of the Storm
	sp = dbcSpell.LookupEntry(29062);
	if(sp)
		sp->procFlags=PROC_ON_CRIT_HIT_VICTIM;
	sp = dbcSpell.LookupEntry(29064);
	if(sp)
		sp->procFlags=PROC_ON_CRIT_HIT_VICTIM;
	sp = dbcSpell.LookupEntry(29065);
	if(sp)
		sp->procFlags=PROC_ON_CRIT_HIT_VICTIM;

	//Shaman - Focused Casting
	sp = dbcSpell.LookupEntry(29063);
	if(sp)
	//        sp->EffectSpellGroupRelation[0]= 1 | 2 | 64 | 128 | 256;
		sp->EffectSpellGroupRelation[0]= 0xFFFFFFFF; // shaman spells. Guess that wraps them all 

	//Shaman - Healing Focus
	sp = dbcSpell.LookupEntry(16181);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16230);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16232);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16233);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16234);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 64 | 128 | 256;

	//shaman - Improved Lightning shield 
	sp = dbcSpell.LookupEntry(324);//shaman - Lightning Shield.
	uint32 ILS_grouprelation;
	if(sp)
		ILS_grouprelation = sp->SpellGroupType;
	else ILS_grouprelation=0;
	//shaman - Improved Lightning shield r1
	sp = dbcSpell.LookupEntry(16261); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=ILS_grouprelation;
	sp = dbcSpell.LookupEntry(16290); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=ILS_grouprelation;
	sp = dbcSpell.LookupEntry(16291); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=ILS_grouprelation;

	//shaman - Tidal focus . Add more heal spells here if i forgot any :P
	sp = dbcSpell.LookupEntry(16179); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16214); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16215); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16216); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=64 | 128 | 256;
	sp = dbcSpell.LookupEntry(16217); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=64 | 128 | 256;

	//shaman - Enhancing Totems r1
	sp = dbcSpell.LookupEntry(16259); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 131072; //Strength of Earth and Grace of Air Totem effects
	sp = dbcSpell.LookupEntry(16295); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 131072;

	//shaman - Elemental Fury - ! Not finished !
	sp = dbcSpell.LookupEntry(16089); 
	if(sp)
//		sp->EffectSpellGroupRelation[0]=1073741824 | 32 | 1048576 | 1 | ... ; //Searing/Magma/Fire Nova Totem effects and Fire,Frost,Nature spells
		sp->EffectSpellGroupRelation[0]=0xFFFFFFFF ; //damn, what other spells do there remain after that list ? Maybe later :P

	//shaman - Restorative Totems r1
	sp = dbcSpell.LookupEntry(16259); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 8192; //Mana Spring and Healing Stream Totem effects
	sp = dbcSpell.LookupEntry(16205); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 8192; //Mana Spring and Healing Stream Totem effects
	sp = dbcSpell.LookupEntry(16206); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 8192; //Mana Spring and Healing Stream Totem effects
	sp = dbcSpell.LookupEntry(16207); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 8192; //Mana Spring and Healing Stream Totem effects
	sp = dbcSpell.LookupEntry(16208); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 8192; //Mana Spring and Healing Stream Totem effects
	
	//shaman - Healing Way
	sp = dbcSpell.LookupEntry(29202); 
	if(sp)
	{
		sp->procFlags = PROC_ON_CAST_SPELL;
		sp->EffectApplyAuraName[0] = 42;
	}
	sp = dbcSpell.LookupEntry(29205); 
	if(sp)
	{
		sp->procFlags = PROC_ON_CAST_SPELL;
		sp->EffectApplyAuraName[0] = 42;
	}
	sp = dbcSpell.LookupEntry(29206); 
	if(sp)
	{
		sp->procFlags = PROC_ON_CAST_SPELL;
		sp->EffectApplyAuraName[0] = 42;
	}

	//shaman - Elemental Devastation
	sp = dbcSpell.LookupEntry(29179); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;
	sp = dbcSpell.LookupEntry(29180); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;
	sp = dbcSpell.LookupEntry(30160); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;

	//shaman - Ancestral healing
	sp = dbcSpell.LookupEntry(16176); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;
	sp = dbcSpell.LookupEntry(16235); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;
	sp = dbcSpell.LookupEntry(16240); 
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;

	//shaman - Mental Quickness. ! these are only a part (18 spells have no grouping)
	sp = dbcSpell.LookupEntry(30812); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=8 | 16 | 512 | 1024 | 524288 | 1048576 | 4194304 | 536870912 | 268435456 | 2147483648;
	sp = dbcSpell.LookupEntry(30813); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=8 | 16 | 512 | 1024 | 524288 | 1048576 | 4194304 | 536870912 | 268435456 | 2147483648;
	sp = dbcSpell.LookupEntry(30814); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=8 | 16 | 512 | 1024 | 524288 | 1048576 | 4194304 | 536870912 | 268435456 | 2147483648;

	//shaman - Totemic focus 
	uint32 All_totems_grouprelation=536870912; //these are only selected flag since some totems contain more then 1 flag
	All_totems_grouprelation |= 8;
	All_totems_grouprelation |= 16;
	All_totems_grouprelation |= 524288;
	All_totems_grouprelation |= 4096;
	//shaman - Totemic focus r1
	sp = dbcSpell.LookupEntry(16173); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=All_totems_grouprelation;
	sp = dbcSpell.LookupEntry(16222); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=All_totems_grouprelation;
	sp = dbcSpell.LookupEntry(16223); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=All_totems_grouprelation;
	sp = dbcSpell.LookupEntry(16224); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=All_totems_grouprelation;
	sp = dbcSpell.LookupEntry(16225); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=All_totems_grouprelation;

	//shaman - Call of Thunder 
	sp = dbcSpell.LookupEntry(403);//shaman - Lightning Bolt.
	uint32 Cthunder_grouprelation;
	if(sp)
		Cthunder_grouprelation = sp->SpellGroupType;
	else Cthunder_grouprelation=0;
	sp = dbcSpell.LookupEntry(421);//shaman - Chain Lightning 
	if(sp)
		Cthunder_grouprelation |= sp->SpellGroupType;
	//shaman - Call of Thunder r1
	sp = dbcSpell.LookupEntry(16041); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Cthunder_grouprelation;
	sp = dbcSpell.LookupEntry(16117); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Cthunder_grouprelation;
	sp = dbcSpell.LookupEntry(16118); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Cthunder_grouprelation;
	sp = dbcSpell.LookupEntry(16119); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Cthunder_grouprelation;
	sp = dbcSpell.LookupEntry(16120); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Cthunder_grouprelation;

	//shaman - COnvection
	sp = dbcSpell.LookupEntry(16039); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 4 | 2;
	sp = dbcSpell.LookupEntry(16109); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 4 | 2;
	sp = dbcSpell.LookupEntry(16110); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 4 | 2;
	sp = dbcSpell.LookupEntry(16111); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 4 | 2;
	sp = dbcSpell.LookupEntry(16112); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 4 | 2;

	//rogue - Elusiveness
	sp = dbcSpell.LookupEntry(2094);//rogue - blind 
	uint32 Elusiveness_grouprelation;
	if(sp)
		Elusiveness_grouprelation = sp->SpellGroupType;
	else Elusiveness_grouprelation=0;
	sp = dbcSpell.LookupEntry(1856);//rogue - vanish 
	if(sp)
		Elusiveness_grouprelation |= sp->SpellGroupType;
	//rogue - Elusiveness r1
	sp = dbcSpell.LookupEntry(13981); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Elusiveness_grouprelation;
	sp = dbcSpell.LookupEntry(14066); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=Elusiveness_grouprelation;

	//rogue - Vile Poisons
	sp = dbcSpell.LookupEntry(14168); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456; //poisons. Duuuh
		sp->EffectSpellGroupRelation[1]=8388608; //maybe this is mixed up with 0 grouprelation ?
		sp->EffectSpellGroupRelation[2]=16384 | 32768 | 8192 | 65536 | 268435456;
	}
	sp = dbcSpell.LookupEntry(16514); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
		sp->EffectSpellGroupRelation[1]=8388608; //maybe this is mixed up with 0 grouprelation ?
		sp->EffectSpellGroupRelation[2]=16384 | 32768 | 8192 | 65536 | 268435456;
	}
	sp = dbcSpell.LookupEntry(16515); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
		sp->EffectSpellGroupRelation[1]=8388608; //maybe this is mixed up with 0 grouprelation ?
		sp->EffectSpellGroupRelation[2]=16384 | 32768 | 8192 | 65536 | 268435456;
	}
	sp = dbcSpell.LookupEntry(16719); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
		sp->EffectSpellGroupRelation[1]=8388608; //maybe this is mixed up with 0 grouprelation ?
		sp->EffectSpellGroupRelation[2]=16384 | 32768 | 8192 | 65536 | 268435456;
	}
	sp = dbcSpell.LookupEntry(16720); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
		sp->EffectSpellGroupRelation[1]=8388608; //maybe this is mixed up with 0 grouprelation ?
		sp->EffectSpellGroupRelation[2]=16384 | 32768 | 8192 | 65536 | 268435456;
	}

	//rogue - Improved Poisons
	sp = dbcSpell.LookupEntry(14113); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(14114); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(14115); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(14116); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(14117); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(21881); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;

	//rogue - Improved Expose Armor
	sp = dbcSpell.LookupEntry(14168); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=524288;
	sp = dbcSpell.LookupEntry(14169); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=524288;

	//rogue - Master Poisoner.
	sp = dbcSpell.LookupEntry(31226); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;
	sp = dbcSpell.LookupEntry(31227); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=16384 | 32768 | 8192 | 65536 | 268435456;

	//rogue - Find Weakness.
	sp = dbcSpell.LookupEntry(31233); 
	if(sp)
		sp->procFlags=PROC_ON_CAST_SPELL;
	sp = dbcSpell.LookupEntry(31239); 
	if(sp)
		sp->procFlags=PROC_ON_CAST_SPELL;
	sp = dbcSpell.LookupEntry(31240); 
	if(sp)
		sp->procFlags=PROC_ON_CAST_SPELL;
	sp = dbcSpell.LookupEntry(31241); 
	if(sp)
		sp->procFlags=PROC_ON_CAST_SPELL;
	sp = dbcSpell.LookupEntry(31242); 
	if(sp)
		sp->procFlags=PROC_ON_CAST_SPELL;
	//rogue - Find Weakness. The effect
	sp = dbcSpell.LookupEntry(31234); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=536870912 | 8388608 | 1048576 | 256 | 8 | 16 | 67108864;//damaging spells
	sp = dbcSpell.LookupEntry(31235); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=536870912 | 8388608 | 1048576 | 256 | 8 | 16 | 67108864;//damaging spells
	sp = dbcSpell.LookupEntry(31236); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=536870912 | 8388608 | 1048576 | 256 | 8 | 16 | 67108864;//damaging spells
	sp = dbcSpell.LookupEntry(31237); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=536870912 | 8388608 | 1048576 | 256 | 8 | 16 | 67108864;//damaging spells
	sp = dbcSpell.LookupEntry(31238); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=536870912 | 8388608 | 1048576 | 256 | 8 | 16 | 67108864;//damaging spells
	
	//rogue - Camouflage.
	sp = dbcSpell.LookupEntry(13975); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = 4194304;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
		sp->EffectApplyAuraName[0] = SPELL_AURA_ADD_PCT_MODIFIER;
		sp->EffectSpellGroupRelation[1] = 4194304;
	}
	sp = dbcSpell.LookupEntry(14062); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = 4194304;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
		sp->EffectApplyAuraName[0] = SPELL_AURA_ADD_PCT_MODIFIER;
		sp->EffectSpellGroupRelation[1] = 4194304;
	}
	sp = dbcSpell.LookupEntry(14063); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = 4194304;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
		sp->EffectApplyAuraName[0] = SPELL_AURA_ADD_PCT_MODIFIER;
		sp->EffectSpellGroupRelation[1] = 4194304;
	}
	sp = dbcSpell.LookupEntry(14064); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = 4194304;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
		sp->EffectApplyAuraName[0] = SPELL_AURA_ADD_PCT_MODIFIER;
		sp->EffectSpellGroupRelation[1] = 4194304;
	}
	sp = dbcSpell.LookupEntry(14065); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = 4194304;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
		sp->EffectApplyAuraName[0] = SPELL_AURA_ADD_PCT_MODIFIER;
		sp->EffectSpellGroupRelation[1] = 4194304;
	}

	//rogue - Mace Specialization.
	sp = dbcSpell.LookupEntry(13709); 
	if(sp)
		sp->procFlags=PROC_ON_MELEE_ATTACK;
	sp = dbcSpell.LookupEntry(13800); 
	if(sp)
		sp->procFlags=PROC_ON_MELEE_ATTACK;
	sp = dbcSpell.LookupEntry(13801); 
	if(sp)
		sp->procFlags=PROC_ON_MELEE_ATTACK;
	sp = dbcSpell.LookupEntry(13802); 
	if(sp)
		sp->procFlags=PROC_ON_MELEE_ATTACK;
	sp = dbcSpell.LookupEntry(13803); 
	if(sp)
		sp->procFlags=PROC_ON_MELEE_ATTACK;

	//rogue - Dirty Tricks 
	sp = dbcSpell.LookupEntry(2094);//rogue - blind 
	uint32 DT_grouprelation;
	if(sp)
		DT_grouprelation = sp->SpellGroupType;
	else DT_grouprelation=0;
	sp = dbcSpell.LookupEntry(2070);//rogue - sap 
	if(sp)
	{
		DT_grouprelation |= sp->SpellGroupType;
		sp2 = dbcSpell.LookupEntry(30980);//rogue - sap - this one is missing the value :S
		if(sp2)
			sp2->SpellGroupType = sp->SpellGroupType;
	}
	//rogue - Dirty Tricks  r1
	sp = dbcSpell.LookupEntry(14076); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=DT_grouprelation;
		sp->EffectSpellGroupRelation[1]=DT_grouprelation;
	}
	sp = dbcSpell.LookupEntry(14094); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=DT_grouprelation;
		sp->EffectSpellGroupRelation[1]=DT_grouprelation;
	}

	//rogue - Dirty Deeds
	sp = dbcSpell.LookupEntry(1833);//rogue - Cheap Shot 
	uint32 DD_grouprelation;
	if(sp)
		DD_grouprelation = sp->SpellGroupType;
	else DD_grouprelation=0;
	sp = dbcSpell.LookupEntry(703);//rogue - Garrote 
	if(sp)
		DD_grouprelation |= sp->SpellGroupType;
	//rogue - Dirty Deeds r1
	sp = dbcSpell.LookupEntry(14082); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=DD_grouprelation;
	sp = dbcSpell.LookupEntry(14083); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=DD_grouprelation;

	//rogue - Shadowstep
	uint32 ss_grouprelation = 512;//rogue - ambush (only a part of the whole group since it would affect other spells too)
	ss_grouprelation |= 4;//rogue - Backstab (only a part of the whole group since it would affect other spells too)
	sp = dbcSpell.LookupEntry(703);//rogue - Garrote 
	if(sp)
		ss_grouprelation |= sp->SpellGroupType;
	//rogue - Shadowstep
	sp = dbcSpell.LookupEntry(36563); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[1] = ss_grouprelation;
		sp->EffectMiscValue[1] = SMT_SPELL_VALUE;
	}

	//rogue - Lethality
	uint32 L_grouprelation = 0;
	L_grouprelation |= 2;//rogue - Sinister Strike (only a part of the whole group since it would affect other spells too)
	L_grouprelation |= 4;//rogue - backstab (only a part of the whole group since it would affect other spells too)
	L_grouprelation |= 8;//rogue - Gouge (only a part of the whole group since it would affect other spells too)
	L_grouprelation |= 33554432;//rogue - Hemorrhage (only a part of the whole group since it would affect other spells too)
	L_grouprelation |= 536870912;//rogue - Shiv (only a part of the whole group since it would affect other spells too)
	L_grouprelation |= 1073741824;//rogue - Ghostly Strike (only a part of the whole group since it would affect other spells too)
	sp = dbcSpell.LookupEntry(14128); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=L_grouprelation;
	sp = dbcSpell.LookupEntry(14132); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=L_grouprelation;
	sp = dbcSpell.LookupEntry(14135); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=L_grouprelation;
	sp = dbcSpell.LookupEntry(14136); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=L_grouprelation;
	sp = dbcSpell.LookupEntry(14137); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=L_grouprelation;

	//rogue - Endurance 
	sp = dbcSpell.LookupEntry(2983);//rogue - Sprint 
	uint32 ED_grouprelation;
	if(sp)
		ED_grouprelation = sp->SpellGroupType;
	else ED_grouprelation=0;
	sp = dbcSpell.LookupEntry(5277);//rogue - Evasion 
	if(sp)
		ED_grouprelation |= sp->SpellGroupType;
	//rogue - Endurance  r1
	sp = dbcSpell.LookupEntry(13742); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=ED_grouprelation;
	sp = dbcSpell.LookupEntry(13872); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=ED_grouprelation;

	//priest - Focused Mind 
	sp = dbcSpell.LookupEntry(8092);//priest - Mind Blast 
	uint32 MF_grouprelation;
	if(sp)
		MF_grouprelation = sp->SpellGroupType;
	else MF_grouprelation=0;
	sp = dbcSpell.LookupEntry(605);//priest - Mind Control 
	if(sp)
		MF_grouprelation |= sp->SpellGroupType;
	sp = dbcSpell.LookupEntry(16568);//priest - Mind Flay 
	if(sp)
		MF_grouprelation |= sp->SpellGroupType;
	//priest - Focused Mind  r1
	sp = dbcSpell.LookupEntry(33213); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=MF_grouprelation;
	sp = dbcSpell.LookupEntry(33214); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=MF_grouprelation;
	sp = dbcSpell.LookupEntry(33215); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=MF_grouprelation;

	//priest - Absolution 
	sp = dbcSpell.LookupEntry(33167); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648;
	sp = dbcSpell.LookupEntry(33171); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648;
	sp = dbcSpell.LookupEntry(33172); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648;
	//priest - Abolish Disease - does not have a group type 
	sp = dbcSpell.LookupEntry(552); 
	if(sp)
		sp->SpellGroupType=2147483648;
	//priest - Mass Dispel - does not have a group type 
	sp = dbcSpell.LookupEntry(32375); 
	if(sp)
		sp->SpellGroupType=2147483648;

	//priest - Mental Agility - all instant spells. I wonder if it conflicts with any other spells 
	sp = dbcSpell.LookupEntry(14520); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648 | 65536 | 67108864 | 4 | 1 | 64 | 32 | 4194304 | 32768 | 8388608 | 8 | 16384 | 2 | 256 | 16777216 | 2097152 | 33554432;
	sp = dbcSpell.LookupEntry(14780); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648 | 65536 | 67108864 | 4 | 1 | 64 | 32 | 4194304 | 32768 | 8388608 | 8 | 16384 | 2 | 256 | 16777216 | 2097152 | 33554432;
	sp = dbcSpell.LookupEntry(14781); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648 | 65536 | 67108864 | 4 | 1 | 64 | 32 | 4194304 | 32768 | 8388608 | 8 | 16384 | 2 | 256 | 16777216 | 2097152 | 33554432;
	sp = dbcSpell.LookupEntry(14782); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648 | 65536 | 67108864 | 4 | 1 | 64 | 32 | 4194304 | 32768 | 8388608 | 8 | 16384 | 2 | 256 | 16777216 | 2097152 | 33554432;
	sp = dbcSpell.LookupEntry(14783); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=2147483648 | 65536 | 67108864 | 4 | 1 | 64 | 32 | 4194304 | 32768 | 8388608 | 8 | 16384 | 2 | 256 | 16777216 | 2097152 | 33554432;

	//priest - Focused Power
	sp = dbcSpell.LookupEntry(33186); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=2147483648;
		sp->EffectSpellGroupRelation[1]=128 | 8192 | 2147483648;
	}
	sp = dbcSpell.LookupEntry(33190); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=2147483648;
		sp->EffectSpellGroupRelation[1]=128 | 8192 | 2147483648;
	}

	//priest - Shadow Reach 
	sp = dbcSpell.LookupEntry(17322); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 4 | 8192 | 16 | 8388608 | 2147483648;
	sp = dbcSpell.LookupEntry(17323); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 4 | 8192 | 16 | 8388608 | 2147483648;

	//priest - Shadow Focus 
	sp = dbcSpell.LookupEntry(15260); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=67108864 | 131072 | 32768 | 16384 | 256 | 65536 | 4 | 8192 | 16 | 8388608 | 2147483648;
	sp = dbcSpell.LookupEntry(15327); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=67108864 | 131072 | 32768 | 16384 | 256 | 65536 | 4 | 8192 | 16 | 8388608 | 2147483648;
	sp = dbcSpell.LookupEntry(15328); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=67108864 | 131072 | 32768 | 16384 | 256 | 65536 | 4 | 8192 | 16 | 8388608 | 2147483648;
	sp = dbcSpell.LookupEntry(15329); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 4 | 8192 | 16 | 8388608 | 2147483648;
	sp = dbcSpell.LookupEntry(15330); 
	if(sp)
		sp->EffectSpellGroupRelation[0]=65536 | 4 | 8192 | 16 | 8388608 | 2147483648;

	//Paladin: Seal of Wisdom
	uint32 procchance = 0;
	sp = dbcSpell.LookupEntry(27116);
	if (sp) procchance = sp->procChance;
	sp = dbcSpell.LookupEntry(20166);if (sp) sp->procChance = procchance;
	sp = dbcSpell.LookupEntry(20356);if (sp) sp->procChance = procchance;
	sp = dbcSpell.LookupEntry(20357);if (sp) sp->procChance = procchance;
	sp = dbcSpell.LookupEntry(27166);if (sp) sp->procChance = procchance;
	//Druid: Frenzied Regeneration
	sp = dbcSpell.LookupEntry(22842);
	if(sp)
	{
		sp->Effect[0] = 6;
		sp->EffectApplyAuraName[0] = 23;
		sp->EffectTriggerSpell[0] = 22845;
	}
	sp = dbcSpell.LookupEntry(22895);
	if(sp)
	{
		sp->Effect[0] = 6;
		sp->EffectApplyAuraName[0] = 23;
		sp->EffectTriggerSpell[0] = 22845;
	}
	sp = dbcSpell.LookupEntry(22896);
	if(sp)
	{
		sp->Effect[0] = 6;
		sp->EffectApplyAuraName[0] = 23;
		sp->EffectTriggerSpell[0] = 22845;
	}
	sp = dbcSpell.LookupEntry(26999);
	if(sp)
	{
		sp->Effect[0] = 6;
		sp->EffectApplyAuraName[0] = 23;
		sp->EffectTriggerSpell[0] = 22845;
	}

	//Druid - Ferocity. Swipe, mangle and possibly the rest have only partial affects since they are missing groups
	sp = dbcSpell.LookupEntry(16934); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=33554432;
		sp->EffectSpellGroupRelation[1]=4096;
	}
	sp = dbcSpell.LookupEntry(16935); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=33554432;
		sp->EffectSpellGroupRelation[1]=4096;
	}
	sp = dbcSpell.LookupEntry(16936); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=33554432;
		sp->EffectSpellGroupRelation[1]=4096;
	}
	sp = dbcSpell.LookupEntry(16937); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=33554432;
		sp->EffectSpellGroupRelation[1]=4096;
	}
	sp = dbcSpell.LookupEntry(16938); 
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=33554432;
		sp->EffectSpellGroupRelation[1]=4096;
	}

	//paladin - seal of blood
	sp = dbcSpell.LookupEntry(31892);
	if(sp)
	{
		sp->procFlags = PROC_ON_MELEE_ATTACK;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 31893;
	}
	sp = dbcSpell.LookupEntry(38008);
	if(sp)
	{
		sp->procFlags = PROC_ON_MELEE_ATTACK;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 31893;
	}
	//paladin - Spiritual Attunement 
	sp = dbcSpell.LookupEntry(31785);
	if(sp)
	{
		sp->procFlags = PROC_ON_SPELL_HIT_VICTIM | PROC_TAGRGET_SELF ;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 31786;
	}
	sp = dbcSpell.LookupEntry(33776);
	if(sp)
	{
		sp->procFlags = PROC_ON_SPELL_HIT_VICTIM | PROC_TAGRGET_SELF;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 31786;
	}
	//Druid: Leader of the Pack
	sp = dbcSpell.LookupEntry(24932);
	if (sp)
	{
		sp->Effect[1] = 0;
		sp->Effect[2] = 0; //removing strange effects.
	}
	//Druid: Improved Leader of the Pack
	sp = dbcSpell.LookupEntry(34299); if (sp) sp->proc_interval = 6000;//6 secs

	//fix for the right Enchant ID for Enchant Cloak - Major Resistance
	sp = dbcSpell.LookupEntry(27962);
	if(sp)
		sp->EffectMiscValue[0] = 2998;
	sp = dbcSpell.LookupEntry(36285);
	if(sp)
		sp->EffectMiscValue[0] = 2998;

	//muhaha, rewriting Retaliation spell as old one :D
	sp = dbcSpell.LookupEntry(20230);
	if(sp)
	{
		sp->Effect[0] = 6; //aura
		sp->EffectApplyAuraName[0] = 42; //force him to use procspell effect
		sp->EffectTriggerSpell[0] = 22858; //evil , but this is good for us :D
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM; //add procflag here since this was not processed with the others !
	}

	//"bloodthirst" new version is ok but old version is wrong from now on :(
	sp = dbcSpell.LookupEntry(23881);
	if(sp)
	{
		sp->Effect[1] = 64; //cast on us, it is good
		sp->EffectTriggerSpell[1] = 23885; //evil , but this is good for us :D
	}
	sp = dbcSpell.LookupEntry(23892);
	if(sp)
	{
		sp->Effect[1] = 64;
		sp->EffectTriggerSpell[1] = 23886; //evil , but this is good for us :D
	}
	sp = dbcSpell.LookupEntry(23893);
	if(sp)
	{
		sp->Effect[1] = 64; //
		sp->EffectTriggerSpell[1] = 23887; //evil , but this is good for us :D
	}
	sp = dbcSpell.LookupEntry(23894);
	if(sp)
	{
		sp->Effect[1] = 64; //
		sp->EffectTriggerSpell[1] = 23888; //evil , but this is good for us :D
	}
	sp = dbcSpell.LookupEntry(25251);
	if(sp)
	{
		sp->Effect[1] = 64; //aura
		sp->EffectTriggerSpell[1] = 25252; //evil , but this is good for us :D
	}
	sp = dbcSpell.LookupEntry(30335);
	if(sp)
	{
		sp->Effect[1] = 64; //aura
		sp->EffectTriggerSpell[1] = 30339; //evil , but this is good for us :D
	}
	// Hunter - Master Tactician
	sp = dbcSpell.LookupEntry(34506);
	if(sp)
		sp->procFlags = PROC_ON_RANGED_ATTACK | PROC_TAGRGET_SELF;
	sp = dbcSpell.LookupEntry(34507);
	if(sp)
		sp->procFlags = PROC_ON_RANGED_ATTACK | PROC_TAGRGET_SELF;
	sp = dbcSpell.LookupEntry(34508);
	if(sp)
		sp->procFlags = PROC_ON_RANGED_ATTACK | PROC_TAGRGET_SELF;
	sp = dbcSpell.LookupEntry(34838);
	if(sp)
		sp->procFlags = PROC_ON_RANGED_ATTACK | PROC_TAGRGET_SELF;
	sp = dbcSpell.LookupEntry(34839);
	if(sp)
		sp->procFlags = PROC_ON_RANGED_ATTACK | PROC_TAGRGET_SELF;

	// Hunter - Hawk Eye
	sp = dbcSpell.LookupEntry(19498);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1;
	sp = dbcSpell.LookupEntry(19499);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1;
	sp = dbcSpell.LookupEntry(19500);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1;

	// general - clearcasting
	sp = dbcSpell.LookupEntry(12536);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 0xFFFFFFFF; //all possible spells we can affect
	sp = dbcSpell.LookupEntry(16246);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 0xFFFFFFFF; //all possible spells we can affect
	sp = dbcSpell.LookupEntry(16870);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 0xFFFFFFFF; //all possible spells we can affect
	sp = dbcSpell.LookupEntry(34754);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 0xFFFFFFFF; //all possible spells we can affect

	// Shaman - Storm Reach
	sp = dbcSpell.LookupEntry(28999);
	if(sp)
	{
		SpellEntry * tsp = dbcSpell.LookupEntry(421); // Chain Lightning
		if(tsp)
			sp->EffectSpellGroupRelation[0] = tsp->SpellGroupType;
		tsp = dbcSpell.LookupEntry(403); // Lightning Bolt
		if(tsp)
			sp->EffectSpellGroupRelation[0] |= tsp->SpellGroupType;
	}
	sp = dbcSpell.LookupEntry(29000);
	if(sp)
	{
		SpellEntry * tsp = dbcSpell.LookupEntry(421); // Chain Lightning
		if(tsp)
			sp->EffectSpellGroupRelation[0] = tsp->SpellGroupType;
		tsp = dbcSpell.LookupEntry(403); // Lightning Bolt
		if(tsp)
			sp->EffectSpellGroupRelation[0] |= tsp->SpellGroupType;
	}
	//Rogue: Seal Fate
	sp = dbcSpell.LookupEntry(14186);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 20;
	}
	sp = dbcSpell.LookupEntry(14190);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 40;
	}
	sp = dbcSpell.LookupEntry(14193);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 60;
	}
	sp = dbcSpell.LookupEntry(14194);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 80;
	}
	sp = dbcSpell.LookupEntry(14195);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 100;
	}
	//Druid: Primal Fury (2 parts)
	//Blood Frenzy
	sp = dbcSpell.LookupEntry(16954);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 100;
	}
	sp = dbcSpell.LookupEntry(16952);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 50;
	}
	//Primal Fury
	sp = dbcSpell.LookupEntry(16961);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 100;
	}
	sp = dbcSpell.LookupEntry(16958);
	if(sp) 
	{
		sp->procFlags = PROC_ON_CRIT_ATTACK;
		sp->procChance = 50;
	}
	//Druid:Intensity
	sp = dbcSpell.LookupEntry(17106);
	if(sp)
	{
	   sp->EffectApplyAuraName[1] = 42;
	   sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(17107);
	if(sp)
	{
		sp->EffectApplyAuraName[1] = 42;
		 sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(17108);
	if(sp)
	{
		sp->EffectApplyAuraName[1] = 42;
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
    //Improved Sprint
	sp = dbcSpell.LookupEntry(13743);
	if(sp)
	{
		sp->procFlags=PROC_ON_CAST_SPELL;
		sp->procChance = 50;
	}
	sp = dbcSpell.LookupEntry(13875);
	if(sp)
	{
		sp->procChance = 100;
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	//warlock - incinerate. Depracated spells are missing the spellgrouptype (maybe they are not depracated at all
	sp = dbcSpell.LookupEntry(29722);
	if(sp)
		sp->SpellGroupType |= 8192; //some of them do have the flags but i's hard to write down those some from 130 spells
	sp = dbcSpell.LookupEntry(32231);
	if(sp)
		sp->SpellGroupType |= 8192; //some of them do have the flags but i's hard to write down those some from 130 spells

	//warlock: Demonic Aegis
	sp = dbcSpell.LookupEntry(30143);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4096;
	sp = dbcSpell.LookupEntry(30144);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4096;
	sp = dbcSpell.LookupEntry(30145);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4096;

	//warlock: Nightfall
	sp = dbcSpell.LookupEntry(18094);
	if (sp)
	{
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 17941;
		sp->procFlags=PROC_ON_CAST_SPELL;
		sp->procChance = 2;
	}
	sp = dbcSpell.LookupEntry(18095);
	if (sp)
	{
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 17941;
		sp->procFlags=PROC_ON_CAST_SPELL;
		sp->procChance = 4;
	}

	//warlock: Contagion
	sp = dbcSpell.LookupEntry(30060);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[1]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[2]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	}
	sp = dbcSpell.LookupEntry(30061);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[1]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[2]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	}
	sp = dbcSpell.LookupEntry(30062);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[1]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[2]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	}
	sp = dbcSpell.LookupEntry(30063);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[1]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[2]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	}
	sp = dbcSpell.LookupEntry(30064);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[1]=1024 | 2 | 65536;
		sp->EffectSpellGroupRelation[2]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	}

	//warlock: Malediction
	sp = dbcSpell.LookupEntry(32477);
	if (sp)
		sp->EffectSpellGroupRelation[0]=131072 | 2097152;
	sp = dbcSpell.LookupEntry(32483);
	if (sp)
		sp->EffectSpellGroupRelation[0]=131072 | 2097152;
	sp = dbcSpell.LookupEntry(32484);
	if (sp)
		sp->EffectSpellGroupRelation[0]=131072 | 2097152;

	//warlock: Improved Searing Pain
	sp = dbcSpell.LookupEntry(17927);
	if (sp)
		sp->EffectSpellGroupRelation[0]=256;
	sp = dbcSpell.LookupEntry(17929);
	if (sp)
		sp->EffectSpellGroupRelation[0]=256;
	sp = dbcSpell.LookupEntry(17930);
	if (sp)
		sp->EffectSpellGroupRelation[0]=256;

	//warlock: Empowered Corruption
	sp = dbcSpell.LookupEntry(32381);
	if (sp)
	{
		sp->EffectBasePoints[0] *= 6;
		sp->EffectSpellGroupRelation[0]=2;
	}
	sp = dbcSpell.LookupEntry(32382);
	if (sp)
	{
		sp->EffectBasePoints[0] *= 6;
		sp->EffectSpellGroupRelation[0]=2;
	}
	sp = dbcSpell.LookupEntry(32383);
	if (sp)
	{
		sp->EffectBasePoints[0] *= 6;
		sp->EffectSpellGroupRelation[0]=2;
	}

	//warlock: Improved Enslave Demon
	sp = dbcSpell.LookupEntry(18821);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=2048;
		sp->EffectMiscValue[0]=SMT_SPELL_VALUE_PCT;
		sp->EffectBasePoints[0] = -(sp->EffectBasePoints[0]+2);
//		sp->EffectSpellGroupRelation[1]=2048; //we do not handle this misc type yet anyway. Removed it just as a reminder
		sp->EffectSpellGroupRelation[2]=2048;
	}
	sp = dbcSpell.LookupEntry(18822);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=2048;
		sp->EffectSpellGroupRelation[1]=2048;
		sp->EffectSpellGroupRelation[2]=2048;
	}

	//warlock: Devastation
	sp = dbcSpell.LookupEntry(18130);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4 | 1 | 64 | 256 | 32 | 128 | 512; //destruction spells
	sp = dbcSpell.LookupEntry(18131);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4 | 1 | 64 | 256 | 32 | 128 | 512;
	sp = dbcSpell.LookupEntry(18132);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4 | 1 | 64 | 256 | 32 | 128 | 512;
	sp = dbcSpell.LookupEntry(18133);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4 | 1 | 64 | 256 | 32 | 128 | 512;
	sp = dbcSpell.LookupEntry(18134);
	if (sp)
		sp->EffectSpellGroupRelation[0]=4 | 1 | 64 | 256 | 32 | 128 | 512;

	//warlock - Shadow Mastery
	sp = dbcSpell.LookupEntry(18271);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
		sp->EffectSpellGroupRelation[1]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
	}
	sp = dbcSpell.LookupEntry(18272);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
		sp->EffectSpellGroupRelation[1]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
	}
	sp = dbcSpell.LookupEntry(18273);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
		sp->EffectSpellGroupRelation[1]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
	}
	sp = dbcSpell.LookupEntry(18274);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
		sp->EffectSpellGroupRelation[1]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
	}
	sp = dbcSpell.LookupEntry(18275);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
		sp->EffectSpellGroupRelation[1]= 2147483648 | 4194304 | 1 | 2 | 16384 | 1024 | 8 | 262144 | 524288 | 2147483648 | 16777216 | 128 | 16 | 32768;
	}

	//warlock - Contagion
	sp = dbcSpell.LookupEntry(30060);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
		sp->EffectSpellGroupRelation[2]= 2147483648 | 4194304 | 1024 | 32768;
	}
	sp = dbcSpell.LookupEntry(30061);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(30062);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(30063);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(30064);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;

	//mage: Arcane Power
	sp = dbcSpell.LookupEntry(12042);
	if (sp)
	{
		sp->EffectSpellGroupRelation[1]=1 | 8192 | 4194304 | 8388608 | 262144 | 131072 | 536870912 | 524352 | 4 | 4096 | 2 | 2048 | 16;
		sp->EffectSpellGroupRelation[2]=1 | 8192 | 4194304 | 8388608 | 262144 | 131072 | 536870912 | 524352 | 4 | 4096 | 2 | 2048 | 16;
	}

	//Mage - Ice Shards
	sp = dbcSpell.LookupEntry(11207);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(12672);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(15047);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(15052);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;
	sp = dbcSpell.LookupEntry(15053);
	if(sp)
		sp->EffectSpellGroupRelation[0]= 524288 | 131072;

	//Mage - Improved Blizzard
	sp = dbcSpell.LookupEntry(11185);
	if(sp)
	{    
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0]=12484;
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(12487);
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0]=12485;
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(12488);
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0]=12486;
		sp->procFlags=PROC_ON_CAST_SPELL;
	}

	//mage: Fire Power
	sp = dbcSpell.LookupEntry(11124);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
		sp->EffectSpellGroupRelation[1]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	}
	sp = dbcSpell.LookupEntry(12398);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
		sp->EffectSpellGroupRelation[1]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	}
	sp = dbcSpell.LookupEntry(12399);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
		sp->EffectSpellGroupRelation[1]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	}
	sp = dbcSpell.LookupEntry(12400);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
		sp->EffectSpellGroupRelation[1]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	}
	sp = dbcSpell.LookupEntry(12378);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
		sp->EffectSpellGroupRelation[1]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	}
	
	//mage - Burning Soul
	sp = dbcSpell.LookupEntry(11083);
	if (sp)
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;
	sp = dbcSpell.LookupEntry(12351);
	if (sp)
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 8 | 262144 | 4194304 | 1;

	//mage - Combustion
	sp = dbcSpell.LookupEntry(28682);
	if (sp)
		sp->EffectSpellGroupRelation[0]=8388608 | 16 | 2 | 4 | 4194304 | 1;

	//mage - Empowered Fireball
	sp = dbcSpell.LookupEntry(31656);
	if (sp)
		sp->EffectSpellGroupRelation[0]=1;
	sp = dbcSpell.LookupEntry(31657);
	if (sp)
		sp->EffectSpellGroupRelation[0]=1;
	sp = dbcSpell.LookupEntry(31658);
	if (sp)
		sp->EffectSpellGroupRelation[0]=1;
	sp = dbcSpell.LookupEntry(31659);
	if (sp)
		sp->EffectSpellGroupRelation[0]=1;
	sp = dbcSpell.LookupEntry(31660);
	if (sp)
		sp->EffectSpellGroupRelation[0]=1;

	//mage - Empowered Frostbolt
	sp = dbcSpell.LookupEntry(31682);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=32;
		sp->EffectSpellGroupRelation[1]=32;
	}
	sp = dbcSpell.LookupEntry(31683);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=32;
		sp->EffectSpellGroupRelation[1]=32;
	}
	sp = dbcSpell.LookupEntry(31684);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=32;
		sp->EffectSpellGroupRelation[1]=32;
	}
	sp = dbcSpell.LookupEntry(31685);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=32;
		sp->EffectSpellGroupRelation[1]=32;
	}
	sp = dbcSpell.LookupEntry(31686);
	if (sp)
	{
		sp->EffectSpellGroupRelation[0]=32;
		sp->EffectSpellGroupRelation[1]=32;
	}

	//mage - Master of Elements
	sp = dbcSpell.LookupEntry(29074);
	if (sp)
	{
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0] = 29077;
		sp->procFlags=uint32(PROC_ON_SPELL_CRIT_HIT|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(29075);
	if (sp)
	{
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0] = 29077;
		sp->procFlags=uint32(PROC_ON_SPELL_CRIT_HIT|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(29076);
	if (sp)
	{
		sp->EffectApplyAuraName[0]=42;
		sp->EffectTriggerSpell[0] = 29077;
		sp->procFlags=uint32(PROC_ON_SPELL_CRIT_HIT|PROC_TAGRGET_SELF);
	}

	//mage: Blazing Speed
	sp = dbcSpell.LookupEntry(31641); 
	if(sp)	sp->EffectTriggerSpell[0]=31643;
	sp = dbcSpell.LookupEntry(31642);
	if(sp)	sp->EffectTriggerSpell[0]=31643;

	//mage talent "frostbyte". we make it to be dummy
	sp = dbcSpell.LookupEntry(11071);
	if(sp)	sp->EffectApplyAuraName[0]=4;
	sp = dbcSpell.LookupEntry(12496);
	if(sp)	sp->EffectApplyAuraName[0]=4;
	sp = dbcSpell.LookupEntry(12497);
	if(sp)	sp->EffectApplyAuraName[0]=4;
	//rogue-shiv -> add 1 combo point
	sp = dbcSpell.LookupEntry(5938);
	if(sp)	sp->Effect[1]=80;

	//warlock - Amplify Curse
	sp = dbcSpell.LookupEntry(18288);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=1024;
		sp->EffectSpellGroupRelation[1]=4194304;
	}
	//warlock - Curse of Doom. Has missign group type
	sp = dbcSpell.LookupEntry(603);
	if(sp)
		sp->SpellGroupType=1024;
	sp = dbcSpell.LookupEntry(30910);
	if(sp)
		sp->SpellGroupType=1024;

	//warlock - Improved Howl of Terror
	sp = dbcSpell.LookupEntry(30054);
	if(sp)
		sp->EffectSpellGroupRelation[0]=4096;
	sp = dbcSpell.LookupEntry(30057);
	if(sp)
		sp->EffectSpellGroupRelation[0]=4096;
	//warlock - Howl of Terror. Has missign group type
	sp = dbcSpell.LookupEntry(5484);
	if(sp)
		sp->SpellGroupType=4096;
	sp = dbcSpell.LookupEntry(17928);
	if(sp)
		sp->SpellGroupType=4096;

	//warlock - Emberstorm
	sp = dbcSpell.LookupEntry(17954);
	if(sp)
		sp->EffectSpellGroupRelation[0]=32 | 64 | 4 | 1048576 | 256 | 512 | 1;
	sp = dbcSpell.LookupEntry(17955);
	if(sp)
		sp->EffectSpellGroupRelation[0]=32 | 64 | 4 | 1048576 | 256 | 512 | 1;
	sp = dbcSpell.LookupEntry(17956);
	if(sp)
		sp->EffectSpellGroupRelation[0]=32 | 64 | 4 | 1048576 | 256 | 512 | 1;
	sp = dbcSpell.LookupEntry(17957);
	if(sp)
		sp->EffectSpellGroupRelation[0]=32 | 64 | 4 | 1048576 | 256 | 512 | 1;
	sp = dbcSpell.LookupEntry(17958);
	if(sp)
		sp->EffectSpellGroupRelation[0]=32 | 64 | 4 | 1048576 | 256 | 512 | 1;

	//warlock - Shadow and Flame
	sp = dbcSpell.LookupEntry(30288);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 8192;
	sp = dbcSpell.LookupEntry(30289);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 8192;
	sp = dbcSpell.LookupEntry(30290);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 8192;
	sp = dbcSpell.LookupEntry(30291);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 8192;
	sp = dbcSpell.LookupEntry(30292);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1 | 8192;

	//warlock - Suppression
	sp = dbcSpell.LookupEntry(18174);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304; //affliction spell
	sp = dbcSpell.LookupEntry(18175);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	sp = dbcSpell.LookupEntry(18176);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	sp = dbcSpell.LookupEntry(18177);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	sp = dbcSpell.LookupEntry(18178);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;

	//warlock - Improved Curse of Agony
	sp = dbcSpell.LookupEntry(18827);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1024;
	sp = dbcSpell.LookupEntry(18829);
	if(sp)
		sp->EffectSpellGroupRelation[0]=1024;

	//warlock - Grim Reach
	sp = dbcSpell.LookupEntry(18218);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;
	sp = dbcSpell.LookupEntry(18219);
	if(sp)
		sp->EffectSpellGroupRelation[0]=2 | 8 | 32768 | 2147483648 | 1024 | 16384 | 262144 | 16 | 524288 | 4194304;

	//warlock - Soul Fire. Has missign group type
	sp = dbcSpell.LookupEntry(6353);
	if(sp)
		sp->SpellGroupType=4;
	sp = dbcSpell.LookupEntry(17924);
	if(sp)
		sp->SpellGroupType=4;
	sp = dbcSpell.LookupEntry(27211);
	if(sp)
		sp->SpellGroupType=4;
	sp = dbcSpell.LookupEntry(30545);
	if(sp)
		sp->SpellGroupType=4;

	//warlock - soul leech
	sp = dbcSpell.LookupEntry(30293);
	if(sp)
	{
		sp->Effect[0] = 6; //aura
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 30294;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(30295);
	if(sp)
	{
		sp->Effect[0] = 6; //aura
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 30294;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(30296);
	if(sp)
	{
		sp->Effect[0] = 6; //aura
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 30294;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}

	//warlock - Pyroclasm
	sp = dbcSpell.LookupEntry(18073);
	if(sp)
	{
		sp->Effect[0] = 0; //delete this owerride effect :P
		sp->EffectTriggerSpell[1] = 18093; //trigger spell was wrong :P
		sp->procFlags=PROC_ON_CAST_SPELL;
		sp->procChance = 13; //god, save us from fixed values !
	}
	sp = dbcSpell.LookupEntry(18096);
	if(sp)
	{
		sp->Effect[0] = 0; //delete this owerride effect :P
		sp->EffectTriggerSpell[1] = 18093; //trigger spell was wrong :P
		sp->procFlags=PROC_ON_CAST_SPELL;
		sp->procChance = 26; //god, save us from fixed values !
	}

	//improved scorch
	sp = dbcSpell.LookupEntry(11095);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42; 
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(12872);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42; 
		sp->procFlags=PROC_ON_CAST_SPELL;
	}
	sp = dbcSpell.LookupEntry(12873);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42; 
		sp->procFlags=PROC_ON_CAST_SPELL;
	}

	//Nature's Grasp
	sp = dbcSpell.LookupEntry(16689);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 339; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(16810);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 1062; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(16811);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 5195; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(16812);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 5196; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(16813);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 9852; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(17329);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 9853; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	sp = dbcSpell.LookupEntry(27009);
	if(sp)
	{
		sp->Effect[0] = 6; 
		sp->EffectApplyAuraName[0] = 42; 
		sp->EffectTriggerSpell[0] = 26989; 
		sp->Effect[1] = 0; 
		sp->procFlags = PROC_ON_MELEE_ATTACK_VICTIM | PROC_REMOVEONUSE;
		sp->AuraInterruptFlags = 0; //we remove it on proc or timeout
	}
	//wrath of air totem targets sorounding creatures instead of us
	sp = dbcSpell.LookupEntry(2895);
	if(sp)
	{
		sp->EffectImplicitTargetA[0]=EFF_TARGET_SELF;
		sp->EffectImplicitTargetA[1]=EFF_TARGET_SELF;
		sp->EffectImplicitTargetA[2]=0;
		sp->EffectImplicitTargetB[0]=0;
		sp->EffectImplicitTargetB[1]=0;
		sp->EffectImplicitTargetB[2]=0;
	}
//-----BLEED effects----- (setting mech to BLEED)
	//garrot
	sp = dbcSpell.LookupEntry(14179);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(8631);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(8632);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(8633);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11289);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11290);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(26839);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(26884);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//pounce
	sp = dbcSpell.LookupEntry(9007);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9824);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9826);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(27007);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//rend
	sp = dbcSpell.LookupEntry(772);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(6546);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(6547);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(6548);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11572);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11573);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11574);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(25208);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//rip
	sp = dbcSpell.LookupEntry(1079);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9492);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9493);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9752);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9894);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9896);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(27008);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//rupture
	sp = dbcSpell.LookupEntry(1943);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(8639);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(8640);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11273);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11274);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(11275);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(26867);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//rake
	sp = dbcSpell.LookupEntry(1822);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(1823);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(1824);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(9904);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	sp = dbcSpell.LookupEntry(27003);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
	//lacerate
	sp = dbcSpell.LookupEntry(33745);if (sp) sp->MechanicsType = MECHANIC_BLEEDING;
//-------------------------------
	//Druid: Pounce Bleed
	sp = dbcSpell.LookupEntry(9007);if (sp) sp->DurationIndex = 18000;
	sp = dbcSpell.LookupEntry(9824);if (sp) sp->DurationIndex = 18000;
	sp = dbcSpell.LookupEntry(9826);if (sp) sp->DurationIndex = 18000;
	sp = dbcSpell.LookupEntry(27007);if (sp) sp->DurationIndex = 18000;
	//Druid: Natural Shapeshifter
	sp = dbcSpell.LookupEntry(16833);if (sp) sp->DurationIndex = 0;
	sp = dbcSpell.LookupEntry(16834);if (sp) sp->DurationIndex = 0;
	sp = dbcSpell.LookupEntry(16835);if (sp) sp->DurationIndex = 0;
	//Priest: Shadow Power
	sp = dbcSpell.LookupEntry(15310);
	if(sp)
	{
		uint32 group = sp->EffectSpellGroupRelation[0];
		sp = dbcSpell.LookupEntry(33221);
		if(sp)
			sp->EffectSpellGroupRelation[0]=group;
		sp = dbcSpell.LookupEntry(33222);
		if(sp)
			sp->EffectSpellGroupRelation[0]=group;
		sp = dbcSpell.LookupEntry(33223);
		if(sp)
			sp->EffectSpellGroupRelation[0]=group;
		sp = dbcSpell.LookupEntry(33224);
		if(sp)
			sp->EffectSpellGroupRelation[0]=group;
		sp = dbcSpell.LookupEntry(33225);
		if(sp)
			sp->EffectSpellGroupRelation[0]=group;
	}
	//Relentless Strikes
	sp = dbcSpell.LookupEntry(14179);
	if(sp)
	{
		sp->EffectApplyAuraName[0]=42;//proc spell
		sp->procFlags = PROC_ON_CAST_SPELL;
		sp->EffectBasePoints[1] = 20; //client showes 20% chance but whe do not have it ? :O
	}
	//priest - surge of light
	sp = dbcSpell.LookupEntry(33150);
	if(sp)
		sp->procFlags = uint32(PROC_ON_SPELL_CRIT_HIT_VICTIM | PROC_TAGRGET_SELF);
	sp = dbcSpell.LookupEntry(33154);
	if(sp)
		sp->procFlags = uint32(PROC_ON_SPELL_CRIT_HIT_VICTIM | PROC_TAGRGET_SELF);
	sp = dbcSpell.LookupEntry(33151);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0]=128;
		sp->EffectSpellGroupRelation[1]=128;
		sp->EffectSpellGroupRelation[2]=128;
		sp->AuraInterruptFlags = AURA_INTERRUPT_ON_CAST_SPELL;
	}
	//Seal of Justice -lowered proc chance (experimental values !)
	sp = dbcSpell.LookupEntry(20164);
	if(sp)
		sp->procChance = 20;
	sp = dbcSpell.LookupEntry(31895);
	if(sp)
		sp->procChance = 20;
	//make Berserking a simple spell 
	sp = dbcSpell.LookupEntry(20554);
	if(sp)
	{
		sp->Effect[0] = SPELL_EFFECT_TRIGGER_SPELL;
		sp->EffectTriggerSpell[0] = 26635;
	}
	sp = dbcSpell.LookupEntry(26296);
	if(sp)
	{
		sp->Effect[0] = SPELL_EFFECT_TRIGGER_SPELL;
		sp->EffectTriggerSpell[0] = 26635;
	}
	sp = dbcSpell.LookupEntry(26297);
	if(sp)
	{
		sp->Effect[0] = SPELL_EFFECT_TRIGGER_SPELL;
		sp->EffectTriggerSpell[0] = 26635;
	}
	//rogue - intiative
	sp = dbcSpell.LookupEntry(13976);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(13979);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}
	sp = dbcSpell.LookupEntry(13980);
	if(sp)
	{
		sp->EffectApplyAuraName[0] = 42;
		sp->procFlags=uint32(PROC_ON_CAST_SPELL|PROC_TAGRGET_SELF);
	}
	//this an on equip item spell(2824) :  ice arrow(29501)
	sp = dbcSpell.LookupEntry(29501);
	if(sp)
	{
		sp->procChance = 30;//some say it is triggered every now and then
		sp->procFlags = PROC_ON_RANGED_ATTACK;
	}
	//warrior - berserker rage is missing 1 effect = regenerate rage
	sp = dbcSpell.LookupEntry(18499);
	if(sp)
	{
		sp->Effect[2] = 6;
		sp->EffectApplyAuraName[2] = 42;
		sp->EffectTriggerSpell[2] = 9174; //not sure if this is the one. In my time this adds 30 rage
		sp->procFlags = PROC_ON_ANY_DAMAGE_VICTIM | PROC_TAGRGET_SELF;
	}
	//warrior - improved berserker rage
	sp = dbcSpell.LookupEntry(20500);
	if(sp)
		sp->procFlags = PROC_ON_ANY_DAMAGE_VICTIM | PROC_TAGRGET_SELF;
	sp = dbcSpell.LookupEntry(20501);
	if(sp)
		sp->procFlags = PROC_ON_ANY_DAMAGE_VICTIM | PROC_TAGRGET_SELF;
	//warrior - Rampage
	sp = dbcSpell.LookupEntry(29801);
	if(sp)
	{
		sp->procFlags = PROC_ON_MELEE_ATTACK | PROC_TAGRGET_SELF;
		sp->EffectTriggerSpell[0] = sp->EffectTriggerSpell[1];
	}
	sp = dbcSpell.LookupEntry(30030);
	if(sp)
	{
		sp->procFlags = PROC_ON_MELEE_ATTACK | PROC_TAGRGET_SELF;
		sp->EffectTriggerSpell[0] = sp->EffectTriggerSpell[1];
	}
	sp = dbcSpell.LookupEntry(30033);
	if(sp)
	{
		sp->procFlags = PROC_ON_MELEE_ATTACK | PROC_TAGRGET_SELF;
		sp->EffectTriggerSpell[0] = sp->EffectTriggerSpell[1];
	}
	// priest - Reflective Shield
	sp = dbcSpell.LookupEntry(33201);
	if(sp)
	{
		sp->procFlags = PROC_ON_ABSORB;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 33619; //!! WRONG spell, we will make direct dmg here
	}
	sp = dbcSpell.LookupEntry(33202);
	if(sp)
	{
		sp->procFlags = PROC_ON_ABSORB;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 33619; //!! WRONG spell, we will make direct dmg here
	}
	sp = dbcSpell.LookupEntry(33203);
	if(sp)
	{
		sp->procFlags = PROC_ON_ABSORB;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 33619; //!! WRONG spell, we will make direct dmg here
	}
	sp = dbcSpell.LookupEntry(33204);
	if(sp)
	{
		sp->procFlags = PROC_ON_ABSORB;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 33619; //!! WRONG spell, we will make direct dmg here
	}
	sp = dbcSpell.LookupEntry(33205);
	if(sp)
	{
		sp->procFlags = PROC_ON_ABSORB;
		sp->EffectApplyAuraName[0] = 42;
		sp->EffectTriggerSpell[0] = 33619; //!! WRONG spell, we will make direct dmg here
	}
	// mage - Frost Warding
	sp = dbcSpell.LookupEntry(11189);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 33554432;
	sp = dbcSpell.LookupEntry(28332);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 33554432;
	//Gnomish Poultryizer trinket - Poultryizer
	sp = dbcSpell.LookupEntry(30507);
	if(sp)
	{
		sp->Effect[0] = 6;
		sp->EffectApplyAuraName[0] = 56;
	}
	// paladin - benediction
	uint32 judgement_group=0;
	SpellEntry * tsp = dbcSpell.LookupEntry(20271); //judgement
	if(tsp)
		judgement_group = tsp->SpellGroupType;
	sp = dbcSpell.LookupEntry(20101);
	if(sp)
		sp->EffectSpellGroupRelation[0] = All_Seal_Groups_Combined | judgement_group;
	sp = dbcSpell.LookupEntry(20102);
	if(sp)
		sp->EffectSpellGroupRelation[0] = All_Seal_Groups_Combined | judgement_group;
	sp = dbcSpell.LookupEntry(20103);
	if(sp)
		sp->EffectSpellGroupRelation[0] = All_Seal_Groups_Combined | judgement_group;
	sp = dbcSpell.LookupEntry(20104);
	if(sp)
		sp->EffectSpellGroupRelation[0] = All_Seal_Groups_Combined | judgement_group;
	sp = dbcSpell.LookupEntry(20105);
	if(sp)
		sp->EffectSpellGroupRelation[0] = All_Seal_Groups_Combined | judgement_group;
/*	// paladin - Improved Hammer of Justice
	uint32 Hammer_of_Justice_group=0;
	tsp = dbcSpell.LookupEntry(853); //Hammer of Justice
	if(tsp)
		Hammer_of_Justice_group = tsp->SpellGroupType;
	sp = dbcSpell.LookupEntry(20487);
	if(sp)
		sp->EffectSpellGroupRelation[0] = Hammer_of_Justice_group;
	sp = dbcSpell.LookupEntry(20488);
	if(sp)
		sp->EffectSpellGroupRelation[0] = Hammer_of_Justice_group;
	sp = dbcSpell.LookupEntry(20489);
	if(sp)
		sp->EffectSpellGroupRelation[0] = Hammer_of_Justice_group;
	sp = dbcSpell.LookupEntry(24188);
	if(sp)
		sp->EffectSpellGroupRelation[0] = Hammer_of_Justice_group; 
	// paladin - Improved Judgement
	sp = dbcSpell.LookupEntry(25956);
	if(sp)
		sp->EffectSpellGroupRelation[0] = judgement_group;
	sp = dbcSpell.LookupEntry(25957);
	if(sp)
		sp->EffectSpellGroupRelation[0] = judgement_group; */
	// paladin - Improved Sanctity Aura
	uint32 Sanctity_group=0;
	tsp = dbcSpell.LookupEntry(20218); //Sanctity Aura
	if(tsp)
		Sanctity_group = tsp->SpellGroupType;
	sp = dbcSpell.LookupEntry(31869);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = Sanctity_group;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
	}
	sp = dbcSpell.LookupEntry(31870);
	if(sp)
	{
		sp->EffectSpellGroupRelation[0] = Sanctity_group;
		sp->EffectMiscValue[0] = SMT_SPELL_VALUE;
	}
    sp = dbcSpell.LookupEntry(20608); //Reincarnation
    if(sp)
    {
        for(uint32 i=0;i<8;i++)
        {
            if(sp->Reagent[i])
            {
                sp->Reagent[i] = 0;
                sp->ReagentCount[i] = 0;
            }
        }
    }

	// druid - Tree of Life
	sp = dbcSpell.LookupEntry(5420);
	if(sp)
		sp->EffectSpellGroupRelation[2] = 268435456 | 32 | 64 | 16; //for the mana cost tweak

	// druid - Nature's Grace
	sp = dbcSpell.LookupEntry(16880);
	if(sp)
		sp->procFlags = PROC_ON_SPELL_CRIT_HIT;
	sp = dbcSpell.LookupEntry(16886);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 0xFFFFFFFF; //all spells, too bad not all spells have grouping flags :S

	// druid - Starlight Wrath
	sp = dbcSpell.LookupEntry(16814);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1 | 4;
	sp = dbcSpell.LookupEntry(16815);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1 | 4;
	sp = dbcSpell.LookupEntry(16816);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1 | 4;
	sp = dbcSpell.LookupEntry(16817);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1 | 4;
	sp = dbcSpell.LookupEntry(16818);
	if(sp)
		sp->EffectSpellGroupRelation[0] = 1 | 4;
	// Druid: Omen of Clarity
	sp = dbcSpell.LookupEntry(16864);
	if (sp)
	{
		sp->procChance=100;
		sp->procFlags = PROC_ON_MELEE_ATTACK | PROC_ON_CRIT_ATTACK;
	}


/*	// druid - Improved Mark of the Wild - already working ?
	uint32 imarkofthv_group=0;
	tsp = dbcSpell.LookupEntry(1126); //Mark of the wild
	if(tsp)
		imarkofthv_group = tsp->SpellGroupType;
	tsp = dbcSpell.LookupEntry(21849); //Gift of the Wild
	if(tsp)
		imarkofthv_group |= tsp->SpellGroupType;
	sp = dbcSpell.LookupEntry(17050);
	if(sp)
		sp->EffectSpellGroupRelation[0] = imarkofthv_group;
	sp = dbcSpell.LookupEntry(17051);
	if(sp)
		sp->EffectSpellGroupRelation[0] = imarkofthv_group;
	sp = dbcSpell.LookupEntry(17053);
	if(sp)
		sp->EffectSpellGroupRelation[0] = imarkofthv_group;
	sp = dbcSpell.LookupEntry(17054);
	if(sp)
		sp->EffectSpellGroupRelation[0] = imarkofthv_group;
	sp = dbcSpell.LookupEntry(17055);
	if(sp)
		sp->EffectSpellGroupRelation[0] = imarkofthv_group;*/
//---------ITEMS-----------------
	sp = dbcSpell.LookupEntry(33758);
	if(sp)
		sp->proc_interval = 10000; //10 seconds.
	sp = dbcSpell.LookupEntry(33759);
	if(sp)
		sp->proc_interval = 10000; //10 seconds.
	// Band of the Eternal Sage
	sp = dbcSpell.LookupEntry(35083);
	if(sp)
		sp->procFlags = PROC_ON_CAST_SPELL;
	// Band of the Eternal Restorer 
	sp = dbcSpell.LookupEntry(35086);
	if(sp)
		sp->procFlags = PROC_ON_CAST_SPELL;

	// Everbloom Idol
	sp = dbcSpell.LookupEntry(33693);
	if (sp)
	{
		SpellEntry* sh = dbcSpell.LookupEntry(16968);
		if (sh)
			sp->EffectSpellGroupRelation[0] = sh->EffectSpellGroupRelation[0];
	}
	//Nordrassil Harness setbonus
	sp = dbcSpell.LookupEntry(37333);
	if (sp)
	{
		SpellEntry* sh = dbcSpell.LookupEntry(16968);
		if (sh)
			sp->EffectSpellGroupRelation[0] = sh->EffectSpellGroupRelation[0];
	}
	//all Drums 
	sp = dbcSpell.LookupEntry(35474);if (sp) sp->RequiredShapeShift=0;
	sp = dbcSpell.LookupEntry(35475);if (sp) sp->RequiredShapeShift=0;
	sp = dbcSpell.LookupEntry(35476);if (sp) sp->RequiredShapeShift=0;
	sp = dbcSpell.LookupEntry(35477);if (sp) sp->RequiredShapeShift=0;
	sp = dbcSpell.LookupEntry(35478);if (sp) sp->RequiredShapeShift=0;
	
	

		

	//windfury weapon changes. Start to hate these day by day
	EnchantEntry * Enchantment = dbcEnchant.LookupEntry(283);
	if(Enchantment)
	{
		Enchantment->spell[0] = 33757; //this is actually good
		sp = dbcSpell.LookupEntry(33757);
		if(sp)
		{
			sp->EffectApplyAuraName[0] = 42; //who needs dummys anyway ?
			sp->procFlags = PROC_ON_MELEE_ATTACK; //we do not need proc on spell ;)
			sp->EffectTriggerSpell[0] = 8232; //for the logs and rest
		}
	}
	Enchantment = dbcEnchant.LookupEntry(284);
	if(Enchantment)
	{
		Enchantment->spell[0] = 33756; 
		sp = dbcSpell.LookupEntry(33756);
		if(sp)
		{
			sp->EffectApplyAuraName[0] = 42; //who needs dummys anyway ?
			sp->procFlags = PROC_ON_MELEE_ATTACK; //we do not need proc on spell ;)
			sp->EffectTriggerSpell[0] = 8235; //for the logs and rest
		}
	}
	Enchantment = dbcEnchant.LookupEntry(525);
	if(Enchantment)
	{
		Enchantment->spell[0] = 33755; 
		sp = dbcSpell.LookupEntry(33755);
		if(sp)
		{
			sp->EffectApplyAuraName[0] = 42; //who needs dummys anyway ?
			sp->procFlags = PROC_ON_MELEE_ATTACK; //we do not need proc on spell ;)
			sp->EffectTriggerSpell[0] = 10486; //for the logs and rest
		}
	}
	Enchantment = dbcEnchant.LookupEntry(1669);
	if(Enchantment)
	{
		Enchantment->spell[0] = 33754; 
		sp = dbcSpell.LookupEntry(33754);
		if(sp)
		{
			sp->EffectApplyAuraName[0] = 42; //who needs dummys anyway ?
			sp->procFlags = PROC_ON_MELEE_ATTACK; //we do not need proc on spell ;)
			sp->EffectTriggerSpell[0] = 16362; //for the logs and rest
		}
	}
	Enchantment = dbcEnchant.LookupEntry(2636);
	if(Enchantment)
	{
		Enchantment->spell[0] = 33727; 
		sp = dbcSpell.LookupEntry(33727);
		if(sp)
		{
			sp->EffectApplyAuraName[0] = 42; //who needs dummys anyway ?
			sp->procFlags = PROC_ON_MELEE_ATTACK; //we do not need proc on spell ;)
			sp->EffectTriggerSpell[0] = 25505; //for the logs and rest
		}
	}
	//for test only
	sp = dbcSpell.LookupEntry(32796);
	if(sp)
	{
//		printf("!!!!!!hash %u \n",sp->NameHash);
//		sp->procChance=100;
//		SpellDuration *sd=sSpellDuration.LookupEntry(sp->DurationIndex);
//printf("iterruptflag %u, duration %u",sp->AuraInterruptFlags,GetDuration(sd));
	}
	//improoved berserker stance should be triggered on berserker stance use
//	sp = sSpellStore.LookupEntry(12704);
//	if(sp)	sp->procFlags=PROC_ON_CAST_SPECIFIC_SPELL;

//	sp = sSpellStore.LookupEntry(16280);
//	if(sp)	printf("!!Interrupt flags %u interval %u charges %u\n",sp->AuraInterruptFlags,sp->proc_interval,sp->procCharges);
//	sp = sSpellStore.LookupEntry(16284);
//	if(sp)	printf("Interrupt flags %u\n",sp->AuraInterruptFlags);

	//fix for Predatory Strikes
	uint32 mm=(1<<(FORM_BEAR-1))|(1<<(FORM_DIREBEAR-1))|(1<<(FORM_MOONKIN-1))|(1<<(FORM_CAT-1));
	sp = dbcSpell.LookupEntry(16972); if(sp)sp->RequiredShapeShift = mm;
	sp = dbcSpell.LookupEntry(16974); if(sp)sp->RequiredShapeShift = mm;
	sp = dbcSpell.LookupEntry(16975); if(sp)sp->RequiredShapeShift = mm;
	sp = dbcSpell.LookupEntry(20134); if(sp)sp->procChance = 50;

	/* aspect of the pack - change to AA */
	sp = dbcSpell.LookupEntry(13159); if(sp)sp->Effect[0] = SPELL_EFFECT_APPLY_AREA_AURA;
	sp = dbcSpell.LookupEntry(13159); if(sp)sp->Effect[1] = SPELL_EFFECT_APPLY_AREA_AURA;
	
	/* shadowstep - change proc flags */
	sp = dbcSpell.LookupEntry(36563); if(sp)sp->procFlags = 0;

	/* thrown - add a 1.6 second cooldown */
	const static uint32 thrown_spells[] = {SPELL_RANGED_GENERAL,SPELL_RANGED_THROW,SPELL_RANGED_WAND, 26679, 27084, 29436, 37074, 41182, 41346, 0};
	for(i = 0; thrown_spells[i] != 0; ++i)
	{
		sp = dbcSpell.LookupEntry(thrown_spells[i]);
		if(sp->RecoveryTime==0 && sp->StartRecoveryTime == 0)
			sp->RecoveryTime = 1600;
	}

	Log.Notice("World","Starting Transport System...");
	objmgr.LoadTransporters();

	// start mail system
	MailSystem::getSingleton().StartMailSystem();

	Log.Notice("World", "Starting Auction System...");
	new AuctionMgr;
	sAuctionMgr.LoadAuctionHouses();

	m_queueUpdateTimer = mQueueUpdateInterval;
	if(Config.MainConfig.GetBoolDefault("Startup", "BackgroundLootLoading", true))
	{
		Log.Notice("World", "Backgrounding loot loading...");

		// loot background loading in a lower priority thread.
		ThreadPool.ExecuteTask(new BasicTaskExecutor(new CallbackP0<LootMgr>(LootMgr::getSingletonPtr(), &LootMgr::LoadLoot), 
			BTE_PRIORITY_LOW));
	}
	else
	{
		Log.Notice("World", "Loading loot in foreground...");
		lootmgr.LoadLoot();
	}

	Log.Notice("BattlegroundManager", "Starting...");
	new CBattlegroundManager;

	dw = new DayWatcherThread();
	ThreadPool.ExecuteTask(dw);
	ThreadPool.ExecuteTask(new CharacterLoaderThread());

	sEventMgr.AddEvent(this, &World::CheckForExpiredInstances, EVENT_WORLD_UPDATEAUCTIONS, 120000, 0, 0);
	return true;
}


void World::Update(time_t diff)
{
	eventholder->Update((uint32)diff);
	sAuctionMgr.Update();
	_UpdateGameTime();

	/* since time() is an expensive system call, we only update it once per server loop */
	UNIXTIME = time(NULL);
}


void World::SendGlobalMessage(WorldPacket *packet, WorldSession *self)
{
	m_sessionlock.AcquireReadLock();

	SessionMap::iterator itr;
	for (itr = m_sessions.begin(); itr != m_sessions.end(); itr++)
	{
		if (itr->second->GetPlayer() &&
			itr->second->GetPlayer()->IsInWorld()
			&& itr->second != self)  // dont send to self!
		{
			itr->second->SendPacket(packet);
		}
	}

	m_sessionlock.ReleaseReadLock();
}
void World::SendFactionMessage(WorldPacket *packet, uint8 teamId)
{
	m_sessionlock.AcquireReadLock();
	SessionMap::iterator itr;
	Player * plr;
	for(itr = m_sessions.begin(); itr != m_sessions.end(); itr++)
	{
		plr = itr->second->GetPlayer();
		if(!plr || !plr->IsInWorld())
			continue;

		if(plr->GetTeam() == teamId)
			itr->second->SendPacket(packet);
	}
	m_sessionlock.ReleaseReadLock();
}

void World::SendZoneMessage(WorldPacket *packet, uint32 zoneid, WorldSession *self)
{
	m_sessionlock.AcquireReadLock();

	SessionMap::iterator itr;
	for (itr = m_sessions.begin(); itr != m_sessions.end(); itr++)
	{
		if (itr->second->GetPlayer() &&
			itr->second->GetPlayer()->IsInWorld()
			&& itr->second != self)  // dont send to self!
		{
			if (itr->second->GetPlayer()->GetZoneId() == zoneid)
				itr->second->SendPacket(packet);
		}
	}

	m_sessionlock.ReleaseReadLock();
}

void World::SendWorldText(const char* text, WorldSession *self)
{
    uint32 textLen = (uint32)strlen((char*)text) + 1;

    WorldPacket data(textLen + 40);

	data.Initialize(SMSG_MESSAGECHAT);
	data << uint8(CHAT_MSG_SYSTEM);
	data << uint32(LANG_UNIVERSAL);
	
	data << (uint64)0; // Who cares about guid when there's no nickname displayed heh ?
	data << (uint32)0;
	data << (uint64)0;

	data << textLen;
	data << text;
	data << uint8(0);

	SendGlobalMessage(&data, self);

	sLog.outString("> %s", text);
}

void World::SendWorldWideScreenText(const char *text, WorldSession *self)
{
	WorldPacket data(256);
	data.Initialize(SMSG_AREA_TRIGGER_MESSAGE);
	data << (uint32)0 << text << (uint8)0x00;
	SendGlobalMessage(&data, self);
}

void World::UpdateSessions(uint32 diff)
{
	SessionSet::iterator itr, it2;
	WorldSession *session;
	int result;
	for(itr = Sessions.begin(); itr != Sessions.end();)
	{
		session = (*itr);
		it2 = itr;
		++itr;
		if(!session || session->GetInstance() != 0)
		{
			Sessions.erase(it2);
			continue;
		}

		if((result = session->Update(0)))
		{
			if(result == 1)
			{
				// complete deletion
				DeleteSession(session);
			}
			Sessions.erase(it2);
		}
	}
}

std::string World::GenerateName(uint32 type)
{
	if(_namegendata[type].size() == 0)
		return "ERR";

	uint32 ent = RandomUInt((uint32)_namegendata[type].size()-1);
	return _namegendata[type].at(ent).name;
}

void World::DeleteSession(WorldSession *session)
{
	m_sessionlock.AcquireWriteLock();
	// remove from big map
	m_sessions.erase(session->GetAccountId());

	m_sessionlock.ReleaseWriteLock();

	// delete us
	delete session;
}

uint32 World::GetNonGmSessionCount()
{
	m_sessionlock.AcquireReadLock();

	uint32 total = (uint32)m_sessions.size();

	SessionMap::const_iterator itr = m_sessions.begin();
	for( ; itr != m_sessions.end(); itr++ )
	{
		if( (itr->second)->HasGMPermissions() )
			total--;
	}

	m_sessionlock.ReleaseReadLock();

	return total;
}

uint32 World::AddQueuedSocket(WorldSocket* Socket)
{
	// Since we have multiple socket threads, better guard for this one,
	// we don't want heap corruption ;)
	queueMutex.Acquire();

	// Add socket to list
	mQueuedSessions.push_back(Socket);
	queueMutex.Release();
	// Return queue position
	return (uint32)mQueuedSessions.size();
}

void World::RemoveQueuedSocket(WorldSocket* Socket)
{
	// Since we have multiple socket threads, better guard for this one,
	// we don't want heap corruption ;)
	queueMutex.Acquire();

	// Find socket in list
	QueueSet::iterator iter = mQueuedSessions.begin();
	for(; iter != mQueuedSessions.end(); ++iter)
	{
		if((*iter) == Socket)
		{
			// Remove from the queue and abort.
			// This will be slow (Removing from middle of a vector!) but it won't
			// get called very much, so it's not really a big deal.

			mQueuedSessions.erase(iter);
			queueMutex.Release();
			return;
		}
	}
	queueMutex.Release();
}

uint32 World::GetQueuePos(WorldSocket* Socket)
{
	// Since we have multiple socket threads, better guard for this one,
	// we don't want heap corruption ;)
	queueMutex.Acquire();

	// Find socket in list
	QueueSet::iterator iter = mQueuedSessions.begin();
	uint32 QueuePos = 1;
	for(; iter != mQueuedSessions.end(); ++iter, ++QueuePos)
	{
		if((*iter) == Socket)
		{
			queueMutex.Release();
			// Return our queue position.
			return QueuePos;
		}
	}
	queueMutex.Release();
	// We shouldn't get here..
	return 1;
}

void World::UpdateQueuedSessions(uint32 diff)
{
#ifndef CLUSTERING
	if(diff >= m_queueUpdateTimer) 
	{
		m_queueUpdateTimer = mQueueUpdateInterval;
		queueMutex.Acquire();

		if(mQueuedSessions.size() == 0)
		{
			queueMutex.Release();
			return;
		}
		
		if(m_sessions.size() < m_playerLimit)
		{
			// Yay. We can let another player in now.
			// Grab the first fucker from the queue, but guard of course, since
			// this is in a different thread again.

			QueueSet::iterator iter = mQueuedSessions.begin();
			WorldSocket * QueuedSocket = *iter;
			mQueuedSessions.erase(iter);

			// Welcome, sucker.
			if(QueuedSocket->GetSession())
			{
				QueuedSocket->GetSession()->deleteMutex.Acquire();
				QueuedSocket->Authenticate();
			}
		}

		if(mQueuedSessions.size() == 0)
		{
			queueMutex.Release();
			return;
		}

		// Update the remaining queue members.
		QueueSet::iterator iter = mQueuedSessions.begin();
		uint32 Position = 1;
		for(; iter != mQueuedSessions.end(); ++iter, ++Position)
		{
			(*iter)->UpdateQueuePosition(Position);
		}
		queueMutex.Release();
	} 
	else 
	{
		m_queueUpdateTimer -= diff;
	}
#endif
}

void World::SaveAllPlayers()
{
	if(!(ObjectMgr::getSingletonPtr()))
		return;

	sLog.outString("Saving all players to database...");
	uint32 count = 0;
	PlayerStorageMap::const_iterator itr;
		// Servers started and obviously runing. lets save all players.
	uint32 mt;
	objmgr._playerslock.AcquireReadLock();   
	for (itr = objmgr._players.begin(); itr != objmgr._players.end(); itr++)
		{
			if(itr->second->GetSession())
			{
				mt = getMSTime();
				itr->second->SaveToDB(false);
				sLog.outString("Saved player `%s` (level %u) in %ums.", itr->second->GetName(), itr->second->GetUInt32Value(UNIT_FIELD_LEVEL), getMSTime() - mt);
				++count;
			}
		}
	objmgr._playerslock.ReleaseReadLock();
	sLog.outString("Saved %u players.", count);
}


WorldSession* World::FindSessionByName(const char * Name)//case insensetive
{
	m_sessionlock.AcquireReadLock();

	// loop sessions, see if we can find him
	SessionMap::iterator itr = m_sessions.begin();
	for(; itr != m_sessions.end(); ++itr)
	{
	  if(!stricmp(itr->second->GetAccountName().c_str(),Name))
	  {
		  m_sessionlock.ReleaseReadLock();
			return itr->second;
	  }
	}
	m_sessionlock.ReleaseReadLock();
	return 0;
}

void World::BroadcastExtendedMessage(WorldSession * self, const char* str, ...)
{
	va_list ap;
	va_start(ap, str);
	char msg[1024];
	vsnprintf(msg, 1024, str, ap);
	va_end(ap);
	SessionSet::iterator itr = mExtendedSessions.begin();
	WorldSession * s;
	for(; itr != mExtendedSessions.end(); )
	{
		s = *itr;
		++itr;

		if(s->GetPlayer() /*&& s != this*/)
			s->GetPlayer()->BroadcastMessage(msg);
	}
}

void World::ShutdownClasses()
{
	sLog.outString("Deleting Addon Manager...");
	sAddonMgr.SaveToDB();
	delete AddonMgr::getSingletonPtr();

	sLog.outString("\nDeleting Auction Manager...");
	delete AuctionMgr::getSingletonPtr();
	sLog.outString("Deleting Loot Manager...");
	delete LootMgr::getSingletonPtr();

	delete MailSystem::getSingletonPtr();
}

void World::EventDeleteBattleground(Battleground * BG)
{
	// remove the instance
	//sWorldCreator.DestroyBattlegroundInstance(BG);
	//sBattlegroundMgr.RemoveBattleground(BG->GetID());
}

void World::GetStats(uint32 * GMCount, float * AverageLatency)
{
	int gm = 0;
	int count = 0;
	int avg = 0;
	PlayerStorageMap::const_iterator itr;
	objmgr._playerslock.AcquireReadLock();
	for (itr = objmgr._players.begin(); itr != objmgr._players.end(); itr++)
	{
		if(itr->second->GetSession())
		{
			count++;
			avg += itr->second->GetSession()->GetLatency();
			if(itr->second->GetSession()->GetPermissionCount())
				gm++;
		}			
	}
	objmgr._playerslock.ReleaseReadLock();

	*AverageLatency = count ? (float)((float)avg / (float)count) : 0;
	*GMCount = gm;
}

void TaskList::AddTask(Task * task)
{
	queueLock.Acquire();
	tasks.insert(task);
	queueLock.Release();
}

Task * TaskList::GetTask()
{
	queueLock.Acquire();

	Task* t = 0;
	for(set<Task*>::iterator itr = tasks.begin(); itr != tasks.end(); ++itr)
	{
		if(!(*itr)->in_progress)
		{
			t = (*itr);
			t->in_progress = true;
			break;
		}
	}
	queueLock.Release();
	return t;
}

void TaskList::spawn()
{
	running = true;
	thread_count = 0;

	uint32 threadcount;
	if(Config.MainConfig.GetBoolDefault("Startup", "EnableMultithreadedLoading", true))
	{
		// get processor count
#ifndef WIN32
#if UNIX_FLAVOUR == UNIX_FLAVOUR_LINUX
#ifdef X64
		threadcount = 2;
#else
		long affmask;
		sched_getaffinity(0, 4, (cpu_set_t*)&affmask);
		threadcount = (BitCount8(affmask)) * 2;
		if(threadcount > 8) threadcount = 8;
		else if(threadcount <= 0) threadcount = 1;
#endif
#else
		threadcount = 2;
#endif
#else
		SYSTEM_INFO s;
		GetSystemInfo(&s);
		threadcount = s.dwNumberOfProcessors * 2;
		if(threadcount>8)
			threadcount=8;
#endif
	}
	else
		threadcount = 1;

	Log.Line();
	Log.Notice("World", "Beginning %s server startup with %u threads.", (threadcount == 1) ? "progressive" : "parallel", threadcount);
	Log.Line();

	for(uint32 x = 0; x < threadcount; ++x)
		ThreadPool.ExecuteTask(new TaskExecutor(this));
}

void TaskList::wait()
{
	bool has_tasks = true;
	while(has_tasks)
	{
		queueLock.Acquire();
		has_tasks = false;
		for(set<Task*>::iterator itr = tasks.begin(); itr != tasks.end(); ++itr)
		{
			if(!(*itr)->completed)
			{
				has_tasks = true;
				break;
			}
		}
		queueLock.Release();
		Sleep(20);
	}
}

void TaskList::kill()
{
	running = false;
}

void Task::execute()
{
	_cb->execute();
}

bool TaskExecutor::run()
{
	Task * t;
	THREAD_TRY_EXECUTION
	{
		while(starter->running)
		{
			t = starter->GetTask();
			if(t)
			{
				t->execute();
				t->completed = true;
				starter->RemoveTask(t);
				delete t;
			}
			else
				Sleep(20);
		}
	}
	THREAD_HANDLE_CRASH
	return true;
}

void TaskList::waitForThreadsToExit()
{
	while(thread_count)
	{
		Sleep(20);
	}
}

void World::DeleteObject(Object * obj)
{
	delete obj;
}

void World::Rehash(bool load)
{
	if(load)
	{
		#ifdef WIN32
		Config.MainConfig.SetSource("ascent.conf", true);
		#else
		Config.MainConfig.SetSource((char*)CONFDIR "/ascent.conf", true);
		#endif
	}

	if(!ChannelMgr::getSingletonPtr())
		new ChannelMgr;

	if(!MailSystem::getSingletonPtr())
		new MailSystem;

	channelmgr.seperatechannels = Config.MainConfig.GetBoolDefault("Server", "SeperateChatChannels", false);
	sendRevisionOnJoin = Config.MainConfig.GetBoolDefault("Server", "SendBuildOnJoin", false);
	MapPath = Config.MainConfig.GetStringDefault("Terrain", "MapPath", "maps");
	UnloadMapFiles = Config.MainConfig.GetBoolDefault("Terrain", "UnloadMapFiles", true);
	BreathingEnabled = Config.MainConfig.GetBoolDefault("Server", "EnableBreathing", true);
	SendStatsOnJoin = Config.MainConfig.GetBoolDefault("Server", "SendStatsOnJoin", true);
	compression_threshold = Config.MainConfig.GetIntDefault("Server", "CompressionThreshold", 1000);

	// load regeneration rates.
	setRate(RATE_HEALTH,Config.MainConfig.GetFloatDefault("Rates", "Health",1));
	setRate(RATE_POWER1,Config.MainConfig.GetFloatDefault("Rates", "Power1",1));
	setRate(RATE_POWER2,Config.MainConfig.GetFloatDefault("Rates", "Power2",1));
	setRate(RATE_POWER3,Config.MainConfig.GetFloatDefault("Rates", "Power4",1));
	setRate(RATE_DROP0,Config.MainConfig.GetFloatDefault("Rates", "DropGrey",1));
  setRate(RATE_DROP1,Config.MainConfig.GetFloatDefault("Rates", "DropWhite",1));
  setRate(RATE_DROP2,Config.MainConfig.GetFloatDefault("Rates", "DropGreen",1));
  setRate(RATE_DROP3,Config.MainConfig.GetFloatDefault("Rates", "DropBlue",1));
  setRate(RATE_DROP4,Config.MainConfig.GetFloatDefault("Rates", "DropPurple",1));
  setRate(RATE_DROP5,Config.MainConfig.GetFloatDefault("Rates", "DropOrange",1));
  setRate(RATE_DROP6,Config.MainConfig.GetFloatDefault("Rates", "DropArtifact",1));
	setRate(RATE_XP,Config.MainConfig.GetFloatDefault("Rates", "XP",1));
	setRate(RATE_RESTXP,Config.MainConfig.GetFloatDefault("Rates", "RestXP", 1));
	setRate(RATE_QUESTXP,Config.MainConfig.GetFloatDefault("Rates", "QuestXP", 1));
	setIntRate(INTRATE_SAVE, Config.MainConfig.GetIntDefault("Rates", "Save", 1));
	setRate(RATE_MONEY, Config.MainConfig.GetFloatDefault("Rates", "DropMoney", 1.0f));
	setRate(RATE_QUESTREPUTATION, Config.MainConfig.GetFloatDefault("Rates", "QuestReputation", 1.0f));
	setRate(RATE_KILLREPUTATION, Config.MainConfig.GetFloatDefault("Rates", "KillReputation", 1.0f));
	setRate(RATE_HONOR, Config.MainConfig.GetFloatDefault("Rates", "Honor", 1.0f));
	setRate(RATE_SKILLCHANCE, Config.MainConfig.GetFloatDefault("Rates", "SkillChance", 1.0f));
	setRate(RATE_SKILLRATE, Config.MainConfig.GetFloatDefault("Rates", "SkillRate", 1.0f));
	setIntRate(INTRATE_COMPRESSION, Config.MainConfig.GetIntDefault("Rates", "Compression", 1));
	setIntRate(INTRATE_PVPTIMER, Config.MainConfig.GetIntDefault("Rates", "PvPTimer", 300000));
	setRate(RATE_ARENAPOINTMULTIPLIER2X, Config.MainConfig.GetFloatDefault("Rates", "ArenaMultiplier2x", 1.0f));
	setRate(RATE_ARENAPOINTMULTIPLIER3X, Config.MainConfig.GetFloatDefault("Rates", "ArenaMultiplier3x", 1.0f));
	setRate(RATE_ARENAPOINTMULTIPLIER5X, Config.MainConfig.GetFloatDefault("Rates", "ArenaMultiplier5x", 1.0f));
	SetPlayerLimit(Config.MainConfig.GetIntDefault("Server", "PlayerLimit", 1000));
	SetMotd(Config.MainConfig.GetStringDefault("Server", "Motd", "Ascent Default MOTD").c_str());
	mQueueUpdateInterval = Config.MainConfig.GetIntDefault("Server", "QueueUpdateInterval", 5000);
	SetKickAFKPlayerTime(Config.MainConfig.GetIntDefault("Server", "KickAFKPlayers", 0));
	sLog.SetScreenLoggingLevel(Config.MainConfig.GetIntDefault("LogLevel", "Screen", 1));
	sLog.SetFileLoggingLevel(Config.MainConfig.GetIntDefault("LogLevel", "File", -1));
	Log.log_level = Config.MainConfig.GetIntDefault("LogLevel", "Screen", 1);
	gm_skip_attunement = Config.MainConfig.GetBoolDefault("Server", "SkipAttunementsForGM", true);
#ifndef CLUSTERING
	SocketRecvBufSize = Config.MainConfig.GetIntDefault("WorldSocket", "RecvBufSize", WORLDSOCKET_RECVBUF_SIZE);
	SocketSendBufSize = Config.MainConfig.GetIntDefault("WorldSocket", "SendBufSize", WORLDSOCKET_SENDBUF_SIZE);
#endif

	bool log_enabled = Config.MainConfig.GetBoolDefault("Log", "Cheaters", false);
	if(Anticheat_Log->IsOpen())
	{
		if(!log_enabled)
			Anticheat_Log->Close();
	}
	else
		if(log_enabled)
			Anticheat_Log->Open();

	log_enabled = Config.MainConfig.GetBoolDefault("Log", "GMCommands", false);
	if(GMCommand_Log->IsOpen())
	{
		if(!log_enabled)
			GMCommand_Log->Close();
	}
	else
		if(log_enabled)
			GMCommand_Log->Open();

#ifdef WIN32
	DWORD current_priority_class = GetPriorityClass(GetCurrentProcess());
	bool high = Config.MainConfig.GetBoolDefault("Server", "AdjustPriority", false);

	if(current_priority_class == HIGH_PRIORITY_CLASS && !high)
		SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	else if(current_priority_class != HIGH_PRIORITY_CLASS && high)
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

	reqGmClient = Config.MainConfig.GetBoolDefault("GMClient", "ReqGmClient", false);
	if(!Config.MainConfig.GetString("GMClient", "GmClientChannel", &GmClientChannel))
	{
		GmClientChannel = "";
	}

	realmtype = Config.MainConfig.GetBoolDefault("Server", "RealmType", false);
	TimeOut= uint32(1000* Config.MainConfig.GetIntDefault("Server", "ConnectionTimeout", 180) );

	uint32 config_flags = 0;
	if(Config.MainConfig.GetBoolDefault("Mail", "DisablePostageCostsForGM", true))
		config_flags |= MAIL_FLAG_NO_COST_FOR_GM;

	if(Config.MainConfig.GetBoolDefault("Mail", "DisablePostageCosts", false))
		config_flags |= MAIL_FLAG_DISABLE_POSTAGE_COSTS;

	if(Config.MainConfig.GetBoolDefault("Mail", "DisablePostageDelayItems", true))
		config_flags |= MAIL_FLAG_DISABLE_HOUR_DELAY_FOR_ITEMS;

	if(Config.MainConfig.GetBoolDefault("Mail", "DisableMessageExpiry", false))
		config_flags |= MAIL_FLAG_NO_EXPIRY;

	if(Config.MainConfig.GetBoolDefault("Mail", "EnableInterfactionMail", true))
		config_flags |= MAIL_FLAG_CAN_SEND_TO_OPPOSITE_FACTION;

	if(Config.MainConfig.GetBoolDefault("Mail", "EnableInterfactionForGM", true))
		config_flags |= MAIL_FLAG_CAN_SEND_TO_OPPOSITE_FACTION_GM;

	sMailSystem.config_flags = config_flags;
	flood_lines = Config.MainConfig.GetIntDefault("FloodProtection", "Lines", 0);
	flood_seconds = Config.MainConfig.GetIntDefault("FloodProtection", "Seconds", 0);
	flood_message = Config.MainConfig.GetBoolDefault("FloodProtection", "SendMessage", false);
	show_gm_in_who_list = Config.MainConfig.GetBoolDefault("Server", "ShowGMInWhoList", true);
	if(!flood_lines || !flood_seconds)
		flood_lines = flood_seconds = 0;

	map_unload_time=Config.MainConfig.GetIntDefault("Server", "MapUnloadTime", 0);

	antihack_teleport = Config.MainConfig.GetBoolDefault("AntiHack", "Teleport", true);
	antihack_speed = Config.MainConfig.GetBoolDefault("AntiHack", "Speed", true);
	antihack_falldmg = Config.MainConfig.GetBoolDefault("AntiHack", "FallDamage", true);
	antihack_flight = Config.MainConfig.GetBoolDefault("AntiHack", "Flight", true);
	no_antihack_on_gm = Config.MainConfig.GetBoolDefault("AntiHack", "DisableOnGM", false);
	SpeedhackProtection = antihack_speed;
	m_levelCap = Config.MainConfig.GetIntDefault("Server", "LevelCap", 70);
	m_genLevelCap = Config.MainConfig.GetIntDefault("Server", "GenLevelCap", 70);
	Channel::LoadConfSettings();
}

void World::LoadNameGenData()
{
	DBCFile dbc;
	dbc.open("DBC/NameGen.dbc");
	for(uint32 i = 0; i < dbc.getRecordCount(); ++i)
	{
		NameGenData d;
		if(dbc.getRecord(i).getString(1)==NULL)
			continue;

		d.name = string(dbc.getRecord(i).getString(1));
		d.type = dbc.getRecord(i).getUInt(3);
		_namegendata[d.type].push_back(d);
	}
}

void World::CharacterEnumProc(QueryResultVector& results, uint32 AccountId)
{
	WorldSession * s = FindSession(AccountId);
	if(s == NULL)
		return;

	s->CharacterEnumProc(results[0].result);
}

void World::LoadAccountDataProc(QueryResultVector& results, uint32 AccountId)
{
	WorldSession * s = FindSession(AccountId);
	if(s == NULL)
		return;

	s->LoadAccountDataProc(results[0].result);
}

void World::CleanupCheaters()
{
	/*uint32 guid;
	string name;
	uint32 cl;
	uint32 level;
	uint32 talentpts;
	char * start, *end;
	Field * f;
	uint32 should_talents;
	uint32 used_talents;
	SpellEntry * sp;

	QueryResult * result = CharacterDatabase.Query("SELECT guid, name, class, level, available_talent_points, spells FROM characters");
	if(result == NULL)
		return;

	do 
	{
		f = result->Fetch();
		guid = f[0].GetUInt32();
		name = string(f[1].GetString());
		cl = f[2].GetUInt32();
		level = f[3].GetUInt32();
		talentpts = f[4].GetUInt32();
		start = f[5].GetString();
		should_talents = (level<10 ? 0 : level - 9);
		used_talents -= 
        		

		start = (char*)get_next_field.GetString();//buff;
		while(true) 
		{
			end = strchr(start,',');
			if(!end)break;
			*end=0;
			sp = dbcSpell.LookupEntry(atol(start));
			start = end +1;

			if(sp->talent_tree)

		}

	} while(result->NextRow());*/

}

void World::CheckForExpiredInstances()
{
	sInstanceMgr.CheckForExpiredInstances();
}

struct insert_playeritem
{
	uint32 ownerguid;
	uint32 entry;
	uint32 wrapped_item_id;
	uint32 wrapped_creator;
	uint32 creator;
	uint32 count;
	uint32 charges;
	uint32 flags;
	uint32 randomprop;
	uint32 randomsuffix;
	uint32 itemtext;
	uint32 durability;
	int32 containerslot;
	int32 slot;
	string enchantments;
};

#define LOAD_THREAD_SLEEP 180

void CharacterLoaderThread::OnShutdown()
{
#ifdef WIN32
	SetEvent(hEvent);
	running=false;
#endif
}

bool CharacterLoaderThread::run()
{
#ifdef WIN32
	hEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
#else
	struct timeval tv;
	tv.tv_usec=0;
	tv.tv_sec = LOAD_THREAD_SLEEP;
#endif

	for(;;)
	{
		sWorld.PollCharacterInsertQueue();
		/* While this looks weird, it ensures the system doesn't waste time switching to these contexts.
		   WaitForSingleObject will suspend the thread,
		   and on unix, select will as well. - Burlex
			*/
#ifdef WIN32
		WaitForSingleObject(hEvent,LOAD_THREAD_SLEEP*1000);
#else
		select(0,NULL,NULL,NULL,&tv);
#endif
		if(!running)
			break;
	}

	return true;
}

void World::PollCharacterInsertQueue()
{
	// Our local stuff..
	bool has_results = false;
	map<uint32, vector<insert_playeritem> > itemMap;
	map<uint32,vector<insert_playeritem> >::iterator itr;
	Field * f;
	insert_playeritem ipi;                          
	static const char * characterTableFormat = "uSuuuuuussuuuuuuuuuuuuuuffffuususuufffuuuuusuuuUssuuuuuuffffuuuuufffssssssuuuuuuuu";

	// Get a single connection to maintain for the whole process.
	MysqlCon * con = CharacterDatabase.GetFreeConnection();
	
	// Lock the table to prevent any more inserts
	CharacterDatabase.FWaitExecute("LOCK TABLES `playeritems_insert_queue` WRITE", con);

	// Cache all items in memory. This will save us doing additional queries and slowing down the db.
	QueryResult * result = CharacterDatabase.FQuery("SELECT * FROM playeritems_insert_queue", con);
	if(result)
	{
		do 
		{
			f = result->Fetch();
			
			ipi.ownerguid = f[0].GetUInt32();
			ipi.entry = f[1].GetUInt32();
			ipi.wrapped_item_id = f[2].GetUInt32();
			ipi.wrapped_creator = f[3].GetUInt32();
			ipi.creator = f[4].GetUInt32();
			ipi.count = f[5].GetUInt32();
			ipi.charges = f[6].GetUInt32();
			ipi.flags = f[7].GetUInt32();
			ipi.randomprop = f[8].GetUInt32();
			ipi.randomsuffix = f[9].GetUInt32();
			ipi.itemtext = f[10].GetUInt32();
			ipi.durability = f[11].GetUInt32();
			ipi.containerslot = f[12].GetInt32();
			ipi.slot = f[13].GetInt32();
			ipi.enchantments = string(f[14].GetString());

			itr = itemMap.find(ipi.ownerguid);
			if(itr == itemMap.end())
			{
				vector<insert_playeritem> to_insert;
				to_insert.push_back(ipi);
				itemMap.insert(make_pair(ipi.ownerguid,to_insert));
			}
			else
			{
				itr->second.push_back(ipi);
			}
		
		} while(result->NextRow());
		delete result;
	}

	// Unlock the item table
	CharacterDatabase.FWaitExecute("UNLOCK TABLES", con);

	// Lock the character table
	CharacterDatabase.FWaitExecute("LOCK TABLES `characters_insert_queue` WRITE", con);

	// Load the characters, and assign them their new guids, and insert them into the main db.
	result = CharacterDatabase.FQuery("SELECT * FROM characters_insert_queue", con);

	// Can be unlocked now.
	CharacterDatabase.FWaitExecute("UNLOCK TABLES", con);

	if(result)
	{
		uint32 guid;
		std::stringstream ss;
		do 
		{
			f = result->Fetch();
			char * p = (char*)characterTableFormat;
			uint32 i = 1;
			guid = f[0].GetUInt32();
			uint32 new_guid = objmgr.GenerateLowGuid(HIGHGUID_PLAYER);
			uint32 new_item_guid;
			ss << "INSERT INTO characters VALUES(" << new_guid;

			// create his playerinfo in the server
			PlayerInfo * inf = new PlayerInfo();
			inf->acct = f[1].GetUInt32();

			while(*p != 0)
			{
				switch(*p)
				{
				case 's':
					ss << ",'" << CharacterDatabase.EscapeString(f[i].GetString(), con) << "'";
					break;

				case 'f':
					ss << ",'" << f[i].GetFloat() << "'";
					break;

				case 'S':
					{
						// this is the character name, append a hex version of the guid to it to prevent name clashes.
						char newname[100];
						snprintf(newname,20,"%5s%X",f[i].GetString(),new_guid);
						ss << ",'" << CharacterDatabase.EscapeString(newname,con) << "'";
						inf->name = strdup(newname);
					}break;

				case 'U':
					{
						// this is our forced rename field. force it to one.
						ss << ",1";
					}break;

				default:
					ss << "," << f[i].GetUInt32();
					break;
				}

				++i;
				++p;
			}

			ss << ")";
			CharacterDatabase.FWaitExecute(ss.str().c_str(),con);

			inf->cl = f[4].GetUInt32();
			inf->gender = f[5].GetUInt32();
			inf->guid = new_guid;
			inf->lastLevel = f[7].GetUInt32();
			inf->lastOnline = UNIXTIME;
			inf->lastZone = 0;
			inf->m_Group=NULL;
			inf->m_loggedInPlayer=NULL;
			inf->officerNote=NULL;
			inf->publicNote=NULL;
			inf->race=f[3].GetUInt32();
			inf->Rank=0;
			inf->subGroup=0;
			switch(inf->race)
			{
			case RACE_HUMAN:
			case RACE_GNOME:
			case RACE_DWARF:
			case RACE_NIGHTELF:
			case RACE_DRAENEI:
				inf->team=0;
				break;

			default:
				inf->team=1;
				break;
			}
			
			// add playerinfo to objectmgr
			objmgr.AddPlayerInfo(inf);

			// grab all his items, assign them their new guids and insert them
			itr = itemMap.find(guid);
			if(itr != itemMap.end())
			{
				for(vector<insert_playeritem>::iterator vtr = itr->second.begin(); vtr != itr->second.end(); ++vtr)
				{
					ss.rdbuf()->str("");
					ss << "INSERT INTO playeritems VALUES(";
					new_item_guid = objmgr.GenerateLowGuid(HIGHGUID_ITEM);
					ss << new_guid << ","
						<< new_item_guid << ","
						<< (*vtr).entry << ","
						<< (*vtr).wrapped_item_id << ","
						<< (*vtr).wrapped_creator << ","
						<< (*vtr).creator << ","
						<< (*vtr).count << ","
						<< (*vtr).charges << ","
						<< (*vtr).flags << ","
						<< (*vtr).randomprop << ","
						<< (*vtr).randomsuffix << ","
						<< (*vtr).itemtext << ","
						<< (*vtr).durability << ","
						<< (*vtr).containerslot << ","
						<< (*vtr).slot << ",'"
						<< (*vtr).enchantments << "')";
					CharacterDatabase.FWaitExecute(ss.str().c_str(),con);
				}
			}
			ss.rdbuf()->str("");
		} while(result->NextRow());
		has_results = true;
		delete result;
	}

	// Clear all the data in the tables.
	if(has_results)
	{
		CharacterDatabase.FWaitExecute("DELETE FROM characters_insert_queue", con);
		CharacterDatabase.FWaitExecute("DELETE FROM playeritems_insert_queue", con);
	}

	// Release the database connection
	con->busy.Release();
}

#ifdef ENABLE_CHECKPOINT_SYSTEM
initialiseSingleton(CheckpointMgr);
CheckpointMgr::~CheckpointMgr()
{

}

void CheckpointMgr::Load()
{
	StorageContainerIterator<MapCheckPoint> * itr = CheckpointStorage.MakeIterator();
	while(!itr->AtEnd())
	{
		if(itr->Get()->required_checkpoint_id==0)
			itr->Get()->pPrevCp=NULL;
		else
			itr->Get()->pPrevCp=CheckpointStorage.LookupEntry(itr->Get()->required_checkpoint_id);

		CheckpointMap::iterator it2 = m_checkpoints.find(itr->Get()->creature_id);
		if(it2==m_checkpoints.end())
		{
			vector<uint32> a;
			a.push_back(itr->Get()->checkpoint_id);
			m_checkpoints.insert(make_pair(itr->Get()->creature_id,a));
		}
		else
			it2->second.push_back(itr->Get()->checkpoint_id);

		if(!itr->Inc())
			break;
	}

	QueryResult * result = CharacterDatabase.Query("SELECT * FROM guild_checkpoints");
	if(!result)
		return;

	do 
	{
		Field * f = result->Fetch();
		uint32 guildid = f[0].GetUInt32();
		uint32 cid = f[1].GetUInt32();

		CheckpointCMap::iterator it3 = m_cCheckpoints.find(guildid);
		if(it3==m_cCheckpoints.end())
		{
			set<uint32> s;
			s.insert(cid);
			m_cCheckpoints.insert(make_pair(guildid,s));
		}
		else
			it3->second.insert(cid);

	} while(result->NextRow());
	delete result;
}

void CheckpointMgr::GuildCompletedCheckpoint(uint32 GuildId, uint32 Cid)
{
	CheckpointCMap::iterator itr = m_cCheckpoints.find(GuildId);
	if(itr==m_cCheckpoints.end())
	{
		set<uint32> s;
		s.insert(Cid);
		m_cCheckpoints.insert(make_pair(GuildId,s));
	}
	else
		itr->second.insert(Cid);

	CharacterDatabase.WaitExecute("INSERT INTO guild_checkpoints VALUES(%u,%u)",GuildId,Cid);

	Guild * pGuild = objmgr.GetGuild(GuildId);
	MapCheckPoint * pCheck = CheckpointStorage.LookupEntry(Cid);
	if(pCheck&&pGuild)
	{
		char msg[300];
		snprintf(msg,300,"Your guild has now passed the `%s` checkpoint.", pCheck->name);
		WorldPacket * data = sChatHandler.FillSystemMessageData(msg);
		list<PlayerInfo*>::iterator itr = pGuild->Begin();
		for(; itr != pGuild->End(); ++itr)
			if((*itr)->m_loggedInPlayer)
				(*itr)->m_loggedInPlayer->GetSession()->SendPacket(data);
		delete data;
	}
}

bool CheckpointMgr::HasCompletedCheckpointAndPrequsites(uint32 GuildId, MapCheckPoint * pCheckpoint)
{
	MapCheckPoint * pcp2 = pCheckpoint->pPrevCp;
	CheckpointCMap::iterator itr = m_cCheckpoints.find(GuildId);
	if(itr==m_cCheckpoints.end())
		return false;

	if(itr->second.find(pCheckpoint->checkpoint_id)==itr->second.end())
		return false;

	while(pcp2)
	{
		if(itr->second.find(pcp2->checkpoint_id)==itr->second.end())
			return false;
		
		pcp2=pcp2->pPrevCp;
	}
	return true;
}

void CheckpointMgr::KilledCreature(uint32 GuildId, uint32 CreatureId)
{
	CheckpointMap::iterator itr = m_checkpoints.find(CreatureId);
	if(itr==m_checkpoints.end())
		return;

	for(vector<uint32>::iterator i = itr->second.begin(); i != itr->second.end(); ++i)
	{
		MapCheckPoint * pcp = CheckpointStorage.LookupEntry((*i));
		if(pcp)
		{
			// check prerequsites
			if(pcp->pPrevCp && !HasCompletedCheckpointAndPrequsites(GuildId, pcp->pPrevCp))
				return;

			GuildCompletedCheckpoint(GuildId,pcp->checkpoint_id);
		}
	}
}

#endif

void World::DisconnectUsersWithAccount(const char * account, WorldSession * m_session)
{
	SessionMap::iterator itr;
	WorldSession * session;
	m_sessionlock.AcquireReadLock();
	for(itr = m_sessions.begin(); itr != m_sessions.end();)
	{
		session = itr->second;
		++itr;

		if(!stricmp(account, session->GetAccountNameS()))
		{
			m_session->SystemMessage("Disconnecting user with account `%s` IP `%s` Player `%s`.", session->GetAccountNameS(), 
				session->GetSocket() ? session->GetSocket()->GetRemoteIP().c_str() : "noip", session->GetPlayer() ? session->GetPlayer()->GetName() : "noplayer");

			session->Disconnect();
		}
	}
	m_sessionlock.ReleaseReadLock();
}

void World::DisconnectUsersWithIP(const char * ip, WorldSession * m_session)
{
	SessionMap::iterator itr;
	WorldSession * session;
	m_sessionlock.AcquireReadLock();
	for(itr = m_sessions.begin(); itr != m_sessions.end();)
	{
		session = itr->second;
		++itr;

		if(!session->GetSocket())
			continue;

		string ip2 = session->GetSocket()->GetRemoteIP().c_str();
		if(!stricmp(ip, ip2.c_str()))
		{
			m_session->SystemMessage("Disconnecting user with account `%s` IP `%s` Player `%s`.", session->GetAccountNameS(), 
				ip2.c_str(), session->GetPlayer() ? session->GetPlayer()->GetName() : "noplayer");

			session->Disconnect();
		}
	}
	m_sessionlock.ReleaseReadLock();
}

void World::DisconnectUsersWithPlayerName(const char * plr, WorldSession * m_session)
{
	SessionMap::iterator itr;
	WorldSession * session;
	m_sessionlock.AcquireReadLock();
	for(itr = m_sessions.begin(); itr != m_sessions.end();)
	{
		session = itr->second;
		++itr;

		if(!session->GetPlayer())
			continue;

		if(!stricmp(plr, session->GetPlayer()->GetName()))
		{
			m_session->SystemMessage("Disconnecting user with account `%s` IP `%s` Player `%s`.", session->GetAccountNameS(), 
				session->GetSocket() ? session->GetSocket()->GetRemoteIP().c_str() : "noip", session->GetPlayer() ? session->GetPlayer()->GetName() : "noplayer");

			session->Disconnect();
		}
	}
	m_sessionlock.ReleaseReadLock();
}
