#include "AllCreatureScript.h"
#include "Chat.h"
#include "Config.h"
#include "CommandScript.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ModuleDatabasePool.h"
#include "MotionMaster.h"
#include "MySQLPreparedStatement.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptedGossip.h"
#include "ServerScript.h"
#include "SpellAuras.h"
#include "TemporarySummon.h"
#include "UnitScript.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <mutex>
#include <numbers>
#include <utility>
#include <unordered_map>

namespace
{
constexpr uint32 HorseEntry = 901087;
constexpr uint32 MountSpell = 458;
constexpr uint32 LeashSpell = 48722;
constexpr uint32 HeartSpell = 20372;
constexpr uint32 HealVisual = 8596;
constexpr std::string_view Prefix = "MonturaCPP\t";
constexpr uint32 PersistMood = 0;
constexpr std::array<uint32, 3> Foods = { 4536, 4540, 117 };
constexpr float InteractionRange = 5.0f;

struct
{
    bool enabled = false;
    uint32 stable = 5300684;
    uint32 respawn = 300;
    uint32 abandon = 30;
    float distance = 2.0f;
} Settings;

class HorseConnection final : public MySQLConnection
{
public:
    explicit HorseConnection(MySQLConnectionInfo& info) : MySQLConnection(info) { }

    void DoPrepareStatements() override
    {
        if (!m_reconnecting)
            m_stmts.resize(PersistMood + 1);

        PrepareStatement(PersistMood, "INSERT INTO immersive_mount_happiness (guid,spell,happiness) "
            "VALUES (?,?,?) ON DUPLICATE KEY UPDATE happiness=VALUES(happiness)", CONNECTION_SYNCH);
    }
};

class HorseDatabase final : public ModuleDatabasePool
{
    MySQLConnection* CreateConnection(MySQLConnectionInfo& info) override { return new HorseConnection(info); }
} Database;
bool DatabaseReady = false;

struct Loan
{
    ObjectGuid horse;
    uint32 map = 0;
    uint32 instance = 0;
    uint32 display = 0;
    uint8 level = 1;
    uint32 health = 1;
    uint32 maxHealth = 1;
    uint32 mood = 20;
    uint32 hunger = 0;
    uint32 remaining = 0;
    uint32 food = 0;
    uint32 feeding = 0;
    Position mealPosition;
    Emote previousEmote = EMOTE_ONESHOT_NONE;
    bool leading = false;
    bool combatWait = false;
    bool hit = false;
    float nativeSpeed = 0.0f;
    float appliedSpeed = 0.0f;
    std::string published;
};

std::recursive_mutex LoanMutex;
std::unordered_map<ObjectGuid, Loan> Loans;

void SaveMood(Player* player, Loan const& loan)
{
    if (!DatabaseReady)
        return;
    auto* stmt = new PreparedStatement<HorseConnection>(PersistMood, Database.GetPreparedStatementParamCount(PersistMood));
    stmt->SetData(0, player->GetGUID().GetCounter());
    stmt->SetData(1, MountSpell);
    stmt->SetData(2, loan.mood);
    Database.Execute(stmt);
}

bool CanPark(Player* player)
{
    return player->IsAlive() && !player->IsInWater() && !player->IsFlying()
        && !player->GetVehicle() && !player->GetTransport();
}

Creature* Resolve(Loan const& loan)
{
    if (!loan.horse)
        return nullptr;
    Map* map = sMapMgr->FindMap(loan.map, loan.instance);
    return map ? map->GetCreature(loan.horse) : nullptr;
}

bool CloseTo(Player* player, Creature* horse)
{
    return horse && horse->IsAlive() && CanPark(player) && !player->IsInCombat()
        && !player->IsMounted() && !player->IsNonMeleeSpellCast(false)
        && player->IsWithinDistInMap(horse, InteractionRange) && player->IsWithinLOSInMap(horse);
}

uint32 Bonus(Loan const& loan)
{
    return loan.mood >= 70 ? 100 : loan.mood >= 35 ? 75 : 50;
}

void Send(Player* player, std::string const& payload)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player->GetGUID(), player->GetGUID(),
        std::string(Prefix) + payload, 0);
    player->GetSession()->SendPacket(&packet);
}

void Publish(Player* player, Loan& loan, bool force = false)
{
    uint32 food = 0;
    for (uint32 item : Foods)
        if (player->HasItemCount(item, 1))
        {
            food = item;
            break;
        }

    Creature* horse = Resolve(loan);
    bool injured = horse && horse->GetHealth() < horse->GetMaxHealth();
    std::string status = Acore::StringFormat("S {} {} {} {} {} {} {} {}",
        loan.horse.GetCounter(), loan.mood, Bonus(loan), food, loan.leading ? 1 : 0,
        loan.feeding ? 1 : 0, injured ? 1 : 0, player->IsMounted() && player->HasAura(MountSpell) ? 1 : 0);

    if (force || status != loan.published)
    {
        Send(player, status);
        loan.published = status;
    }
}

void StopFeeding(Player* player, Loan& loan)
{
    if (loan.food && player->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_SPELL_CHANNEL_DIRECTED)
        player->SetEmoteState(loan.previousEmote);
    loan.food = 0;
    loan.feeding = 0;
}

void DropLeash(Player* player, Loan& loan, Creature* horse)
{
    if (player->GetUInt32Value(UNIT_CHANNEL_SPELL) == LeashSpell
        && player->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT) == loan.horse)
    {
        player->SetUInt32Value(UNIT_CHANNEL_SPELL, 0);
        player->SetGuidValue(UNIT_FIELD_CHANNEL_OBJECT, ObjectGuid::Empty);
    }

    loan.leading = false;
    loan.remaining = Settings.abandon * 1000;
    if (!horse)
        return;
    horse->RemoveAurasDueToSpell(LeashSpell);
    horse->GetMotionMaster()->Clear(false);
    horse->StopMoving();
    horse->SetHomePosition(horse->GetPosition());
    horse->GetMotionMaster()->MoveIdle();
}

void RemoveHorse(Player* player, Loan& loan)
{
    Creature* horse = Resolve(loan);
    if (loan.leading)
        DropLeash(player, loan, horse);
    StopFeeding(player, loan);
    if (horse)
        horse->DespawnOrUnsummon();
    loan.horse.Clear();
}

void RestoreSpeed(Player* player, Loan const& loan)
{
    if (loan.nativeSpeed > 0 && std::abs(player->GetSpeedRate(MOVE_RUN) - loan.appliedSpeed) < 0.01f)
        player->SetSpeed(MOVE_RUN, loan.nativeSpeed, true);
}

void RefreshStats(Player* player, Loan& loan, Creature* horse)
{
    loan.level = player->GetLevel();
    loan.maxHealth = player->GetMaxHealth();
    loan.health = player->GetHealth();
    if (!horse)
        return;
    horse->SetLevel(loan.level);
    horse->SetMaxHealth(loan.maxHealth);
    horse->SetHealth(loan.health);
}

Creature* Park(Player* player, Loan& loan)
{
    if (!CanPark(player) || !loan.display)
        return nullptr;
    TempSummon* horse = player->SummonCreature(HorseEntry, player->GetPosition());
    if (!horse)
        return nullptr;

    horse->SetLevel(loan.level, false);
    horse->SetDisplayId(loan.display);
    horse->SetOwnerGUID(player->GetGUID());
    horse->SetMaxHealth(loan.maxHealth);
    horse->SetHealth(loan.health);
    horse->SetReactState(REACT_PASSIVE);
    horse->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
    loan.horse = horse->GetGUID();
    loan.map = horse->GetMapId();
    loan.instance = horse->GetInstanceId();
    loan.remaining = Settings.abandon * 1000;
    return horse;
}

bool BeginLoan(Player* player, Creature* stable)
{
    if (!CloseTo(player, stable) || Loans.contains(player->GetGUID()))
        return false;
    player->CastSpell(player, MountSpell, true);
    if (!player->IsMounted() || !player->HasAura(MountSpell))
        return false;
    Loan& loan = Loans[player->GetGUID()];
    loan.display = player->GetMountID();
    RefreshStats(player, loan, nullptr);
    SaveMood(player, loan);
    stable->DespawnOrUnsummon(0ms, Seconds(Settings.respawn));
    Publish(player, loan, true);
    return true;
}

void Remount(Player* player, Loan& loan, Creature* horse)
{
    if (!CloseTo(player, horse) || loan.food || (!loan.leading && !loan.combatWait && !loan.remaining))
        return;
    if (loan.leading)
        DropLeash(player, loan, horse);
    player->CastSpell(player, MountSpell, true);
    if (!player->IsMounted() || !player->HasAura(MountSpell))
        return;
    loan.health = horse->GetHealth();
    loan.nativeSpeed = 0;
    loan.appliedSpeed = 0;
    RemoveHorse(player, loan);
}

void Feed(Player* player, Loan& loan, Creature* horse, uint32 item)
{
    if (!CloseTo(player, horse) || loan.food || loan.leading
        || (loan.mood >= 100 && horse->IsFullHealth())
        || std::find(Foods.begin(), Foods.end(), item) == Foods.end() || !player->HasItemCount(item, 1))
        return;
    loan.food = item;
    loan.feeding = 3000;
    loan.mealPosition = player->GetPosition();
    loan.previousEmote = Emote(player->GetUInt32Value(UNIT_NPC_EMOTESTATE));
    player->SetEmoteState(EMOTE_STATE_SPELL_CHANNEL_DIRECTED);
    horse->CastSpell(horse, HealVisual, true);
    if (Aura* heart = horse->AddAura(HeartSpell, horse))
        heart->SetDuration(3000);
}

void UpdateMeal(Player* player, Loan& loan, Creature* horse, uint32 diff)
{
    if (!loan.food)
        return;
    if (!CloseTo(player, horse) || player->GetExactDist2d(&loan.mealPosition) > 0.6f)
    {
        StopFeeding(player, loan);
        if (horse)
            horse->RemoveAurasDueToSpell(HeartSpell);
        player->GetSession()->SendAreaTriggerMessage("Alimentación interrumpida.");
        return;
    }
    if (diff < loan.feeding)
    {
        loan.feeding -= diff;
        return;
    }
    uint32 item = loan.food;
    StopFeeding(player, loan);
    if (!player->HasItemCount(item, 1) || (loan.mood >= 100 && horse->IsFullHealth()))
        return;
    player->DestroyItemCount(item, 1, true);
    uint32 healed = std::max(1u, uint32(std::lround(horse->GetMaxHealth() * 0.10)));
    horse->SetHealth(horse->GetHealth() + std::min(healed, horse->GetMaxHealth() - horse->GetHealth()));
    loan.health = horse->GetHealth();
    loan.mood = std::min(100u, loan.mood + 10);
    SaveMood(player, loan);
    horse->CastSpell(horse, HealVisual, true);
    horse->HandleEmoteCommand(EMOTE_ONESHOT_EAT_NO_SHEATHE);
    player->GetSession()->SendAreaTriggerMessage("Has alimentado a tu montura.");
}

void Lead(Player* player, Loan& loan, Creature* horse, bool requested)
{
    if (loan.leading == requested)
        return;
    if (!requested)
    {
        DropLeash(player, loan, horse);
        player->GetSession()->SendAreaTriggerMessage("Tu caballo se queda aquí.");
        return;
    }
    if (!CloseTo(player, horse) || loan.food || player->GetUInt32Value(UNIT_CHANNEL_SPELL))
        return;
    Aura* leash = player->AddAura(LeashSpell, horse);
    if (!leash)
        return;
    leash->SetMaxDuration(-1);
    leash->SetDuration(-1);
    player->SetGuidValue(UNIT_FIELD_CHANNEL_OBJECT, loan.horse);
    player->SetUInt32Value(UNIT_CHANNEL_SPELL, LeashSpell);
    horse->GetMotionMaster()->MoveFollow(player, Settings.distance, std::numbers::pi_v<float>);
    loan.leading = true;
}

bool UpdateLoan(Player* player, Loan& loan, uint32 diff)
{
    if (!player->IsAlive())
        return false;
    Creature* horse = Resolve(loan);
    if (loan.horse && (!horse || !horse->IsAlive()))
        return false;
    if (loan.level != player->GetLevel())
        RefreshStats(player, loan, horse);
    bool mounted = player->IsMounted() && player->HasAura(MountSpell);
    bool attacked = loan.hit || !player->getAttackers().empty();
    loan.hit = false;

    if (player->IsMounted() && !mounted && !horse)
        return false;
    if (mounted && player->GetMountID())
        loan.display = player->GetMountID();
    if (mounted && attacked && !horse)
    {
        horse = Park(player, loan);
        if (horse)
        {
            RestoreSpeed(player, loan);
            player->RemoveAurasDueToSpell(MountSpell);
            if (player->IsMounted())
                player->Dismount();
            mounted = false;
            loan.combatWait = true;
            player->GetSession()->SendAreaTriggerMessage("Te han atacado. Tu caballo se ha quedado aquí.");
        }
    }
    if (!mounted && !horse && !player->IsMounted())
    {
        horse = Park(player, loan);
        if (horse)
            loan.combatWait = player->IsInCombat();
    }
    if (horse && loan.leading)
    {
        if (attacked || player->IsMounted() || !CanPark(player)
            || !player->IsWithinDistInMap(horse, 60.0f) || !horse->HasAura(LeashSpell)
            || player->GetUInt32Value(UNIT_CHANNEL_SPELL) != LeashSpell)
        {
            DropLeash(player, loan, horse);
            loan.combatWait = attacked;
        }
    }
    if (horse && loan.combatWait && !player->IsInCombat() && !attacked)
    {
        loan.combatWait = false;
        loan.remaining = Settings.abandon * 1000;
    }
    if (horse && !loan.leading && !loan.combatWait)
    {
        if (diff >= loan.remaining)
        {
            player->GetSession()->SendAreaTriggerMessage("Tu caballo ya no está disponible.");
            return false;
        }
        loan.remaining -= diff;
    }

    UpdateMeal(player, loan, horse, diff);
    loan.hunger += diff;
    if (loan.hunger >= 120000)
    {
        loan.hunger -= 120000;
        loan.mood = loan.mood > 5 ? loan.mood - 5 : 0;
        SaveMood(player, loan);
        if (loan.mood == 5)
            player->GetSession()->SendAreaTriggerMessage("Tu montura tiene mucha hambre.");
    }
    if (!loan.mood)
    {
        if (!horse && mounted)
            horse = Park(player, loan);
        if (mounted)
        {
            RestoreSpeed(player, loan);
            player->RemoveAurasDueToSpell(MountSpell);
            if (player->IsMounted())
                player->Dismount();
        }
        if (horse)
        {
            if (loan.leading)
                DropLeash(player, loan, horse);
            Unit::Kill(horse, horse, false);
            horse->DespawnOrUnsummon(8s);
            loan.horse.Clear();
        }
        player->GetSession()->SendAreaTriggerMessage("Tu montura ha muerto de hambre.");
        return false;
    }
    if (mounted)
    {
        float current = player->GetSpeedRate(MOVE_RUN);
        if (!loan.nativeSpeed || std::abs(current - loan.appliedSpeed) >= 0.01f)
            loan.nativeSpeed = current;
        loan.appliedSpeed = loan.nativeSpeed * (100.0f + Bonus(loan)) / 160.0f;
        if (std::abs(current - loan.appliedSpeed) >= 0.01f)
            player->SetSpeed(MOVE_RUN, loan.appliedSpeed, true);
    }
    Publish(player, loan);
    return true;
}
class ImmersiveHorseCreatures final : public AllCreatureScript
{
public:
    ImmersiveHorseCreatures() : AllCreatureScript("ImmersiveHorseCreatures") { }

    void OnBeforeCreatureSelectLevel(CreatureTemplate const*, Creature* creature, uint8& level) override
    {
        if (creature->GetEntry() != HorseEntry)
            return;
        if (TempSummon* horse = creature->ToTempSummon())
            if (Unit* rider = horse->GetSummonerUnit(); rider && rider->IsPlayer())
                level = rider->GetLevel();
    }

    bool CanCreatureGossipHello(Player* player, Creature* horse) override
    {
        if (!Settings.enabled || horse->GetEntry() != HorseEntry)
            return false;
        std::lock_guard lock(LoanMutex);
        CloseGossipMenuFor(player);
        if (horse->GetSpawnId() == Settings.stable)
        {
            if (!BeginLoan(player, horse))
                player->GetSession()->SendAreaTriggerMessage(
                    "Acércate al caballo fuera de combate. Solo puedes tener un préstamo.");
        }
        else if (auto it = Loans.find(player->GetGUID()); it != Loans.end() && it->second.horse == horse->GetGUID())
        {
            Remount(player, it->second, horse);
            Publish(player, it->second);
        }
        return true;
    }
};

class ImmersiveHorsePlayers final : public PlayerScript
{
public:
    ImmersiveHorsePlayers() : PlayerScript("ImmersiveHorsePlayers",
        { PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard lock(LoanMutex);
        auto it = Loans.find(player->GetGUID());
        if (it == Loans.end())
            return;
        RestoreSpeed(player, it->second);
        RemoveHorse(player, it->second);
        player->RemoveAurasDueToSpell(MountSpell);
        Loans.erase(it);
    }
};

class ImmersiveHorseCombat final : public UnitScript
{
public:
    ImmersiveHorseCombat() : UnitScript("ImmersiveHorseCombat", true, { UNITHOOK_ON_DAMAGE }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!Settings.enabled || !damage || !attacker || attacker == victim || !victim || !victim->IsPlayer())
            return;
        std::lock_guard lock(LoanMutex);
        if (auto it = Loans.find(victim->GetGUID()); it != Loans.end())
            it->second.hit = true;
    }
};

void HandleAddon(Player* player, std::string_view message)
{
    if (message.empty())
        return;
    auto argument = message.substr(1);
    uint32 value = 0;
    if (!argument.empty())
    {
        auto result = std::from_chars(argument.data(), argument.data() + argument.size(), value);
        if (result.ec != std::errc() || result.ptr != argument.data() + argument.size())
            return;
    }
    std::lock_guard lock(LoanMutex);
    auto it = Loans.find(player->GetGUID());
    if (message.front() == 'C')
    {
        std::list<Creature*> nearby;
        player->GetCreatureListWithEntryInGrid(nearby, HorseEntry, InteractionRange + 3.0f);
        for (Creature* horse : nearby)
        {
            if (horse->GetGUID().GetCounter() != value || !CloseTo(player, horse))
                continue;
            bool allowed = it == Loans.end() ? horse->GetSpawnId() == Settings.stable
                : it->second.horse == horse->GetGUID() && !it->second.food;
            Send(player, Acore::StringFormat("C {} {}", value, allowed ? 1 : 0));
            return;
        }
        Send(player, Acore::StringFormat("C {} 0", value));
        return;
    }
    if (it == Loans.end())
        return;
    Loan& loan = it->second;
    Creature* horse = Resolve(loan);
    switch (message.front())
    {
        case 'F':
            Feed(player, loan, horse, value);
            break;
        case 'L':
            if (horse)
                Lead(player, loan, horse, value == 1);
            break;
        case 'S':
            Publish(player, loan, true);
            return;
        default:
            return;
    }
    Publish(player, loan);
}

class ImmersiveHorsePackets final : public ServerScript
{
public:
    ImmersiveHorsePackets() : ServerScript("ImmersiveHorsePackets") { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!Settings.enabled || !session || !session->GetPlayer()
            || packet.GetOpcode() != CMSG_MESSAGECHAT || packet.size() < 10)
            return true;
        WorldPacket copy(packet);
        uint32 type = 0;
        uint32 language = 0;
        copy >> type >> language;
        if (type != CHAT_MSG_WHISPER || language != static_cast<uint32>(LANG_ADDON))
            return true;
        try
        {
            std::string recipient = copy.ReadCString();
            std::string message = copy.ReadCString(false);
            if (!message.starts_with(Prefix))
                return true;
            Player* player = session->GetPlayer();
            if (recipient == player->GetName() && message.size() <= Prefix.size() + 11)
                HandleAddon(player, std::string_view(message).substr(Prefix.size()));
            return false;
        }
        catch (ByteBufferException const&)
        {
            return true;
        }
    }
};

class ImmersiveHorseCommands final : public CommandScript
{
public:
    ImmersiveHorseCommands() : CommandScript("ImmersiveHorseCommands") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable options = { { "hambre", Hunger, SEC_GAMEMASTER, Console::No } };
        static ChatCommandTable commands = { { "montura", options } };
        return commands;
    }

    static bool Hunger(ChatHandler* handler)
    {
        if (!Settings.enabled)
            return false;
        Player* player = handler->GetPlayer();
        std::lock_guard lock(LoanMutex);
        auto it = Loans.find(player->GetGUID());
        if (it == Loans.end())
        {
            handler->SendSysMessage("Primero pide un caballo prestado en Villadorada.");
            return true;
        }
        it->second.mood = 5;
        it->second.hunger = 115000;
        SaveMood(player, it->second);
        Publish(player, it->second);
        handler->SendSysMessage("Prueba de hambre: tu caballo morirá en 5 segundos si no come.");
        return true;
    }
};

class ImmersiveHorseWorld final : public WorldScript
{
public:
    ImmersiveHorseWorld() : WorldScript("ImmersiveHorseWorld") { }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            return;
        Settings.enabled = sConfigMgr->GetOption<bool>("MonturasInmersivas.Enable", false);
        Settings.stable = sConfigMgr->GetOption<uint32>("MonturasInmersivas.StableSpawnId", 5300684);
        Settings.respawn = std::clamp(
            sConfigMgr->GetOption<uint32>("MonturasInmersivas.RespawnSeconds", 300), 1u, 86400u);
        Settings.abandon = std::clamp(
            sConfigMgr->GetOption<uint32>("MonturasInmersivas.AbandonedSeconds", 30), 1u, 86400u);
        Settings.distance = std::clamp(
            sConfigMgr->GetOption<float>("MonturasInmersivas.FollowDistance", 2.0f), 0.5f, 10.0f);
    }

    void OnStartup() override
    {
        if (!Settings.enabled)
            return;
        Database.SetConnectionInfo(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""), 1);
        DatabaseReady = Database.Open() == 0 && Database.PrepareStatements();
        if (!DatabaseReady)
            LOG_ERROR("module.monturas", "Unable to prepare immersive mount storage in the characters database.");
    }

    void OnUpdate(uint32 diff) override
    {
        if (!Settings.enabled)
            return;
        _elapsed += diff;
        if (_elapsed < 500)
            return;

        uint32 elapsed = std::exchange(_elapsed, 0);
        std::lock_guard lock(LoanMutex);
        for (auto it = Loans.begin(); it != Loans.end();)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(it->first);
            if (!player)
            {
                if (Creature* horse = Resolve(it->second))
                    horse->DespawnOrUnsummon();
                it = Loans.erase(it);
                continue;
            }
            if (!player->IsInWorld() || UpdateLoan(player, it->second, elapsed))
            {
                ++it;
                continue;
            }
            RemoveHorse(player, it->second);
            it = Loans.erase(it);
            Send(player, "S 0 0 0 0 0 0 0 0");
        }
    }

    void OnShutdown() override
    {
        if (Settings.enabled)
            Database.Close();
    }

private:
    uint32 _elapsed = 0;
};
}

void Addmod_monturas_inmersivasScripts()
{
    new ImmersiveHorseWorld();
    new ImmersiveHorseCreatures();
    new ImmersiveHorsePlayers();
    new ImmersiveHorseCombat();
    new ImmersiveHorsePackets();
    new ImmersiveHorseCommands();
}
