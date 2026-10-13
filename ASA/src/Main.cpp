// CreatureColors (ASA) - lets permitted players recolor their own or their tribe's tames via chat commands.
//
//   /setcr <region> <color>        set one region on the aimed creature
//   /savecrt <name>                save the aimed creature's regions as a named template (y/n prompt on overwrite)
//   /setcrtaoe <name> [radius]     apply a template to matching creatures around the player

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "API/ARK/Ark.h"
#include "Tools.h"

#include "Config.h"
#include "Database.h"
#include "DinoColors.h"
#include "PermissionsApi.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    Config g_config;
    Database g_db;
    bool g_db_started = false;

    struct PendingOverwrite
    {
        std::string name;
        ColorTemplate tpl;
        Clock::time_point expires;
    };

    // Keyed by EOS ID. One open overwrite prompt per player.
    std::unordered_map<std::string, PendingOverwrite> g_pending;

    const FLinearColor kOk{0.4f, 1.0f, 0.4f, 1.0f};
    const FLinearColor kInfo{1.0f, 0.85f, 0.3f, 1.0f};
    const FLinearColor kError{1.0f, 0.4f, 0.4f, 1.0f};

    const FString kChatCallbackId("CreatureColors.Chat");
    const FString kTickCallbackId("CreatureColors.Tick");

    // ---------- helpers ----------

    std::string Format(std::string text, const std::vector<std::string>& args)
    {
        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string token = "{" + std::to_string(i) + "}";
            for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + args[i].size()))
                text.replace(pos, token.size(), args[i]);
        }
        return text;
    }

    void Send(AShooterPlayerController* pc, const FLinearColor& color, const std::string& key,
              const std::vector<std::string>& args = {})
    {
        if (!pc)
            return;
        const std::string text = Format(g_config.Msg(key), args);
        if (!text.empty())
            AsaApi::GetApiUtils().SendServerMessage(pc, color, L"{}", AsaApi::Tools::Utf8Decode(text));
    }

    std::string EosId(AShooterPlayerController* pc)
    {
        return AsaApi::IApiUtils::GetEOSIDFromController(pc).ToString();
    }

    AShooterPlayerController* FindPlayer(const std::string& eos_id)
    {
        return AsaApi::GetApiUtils().FindPlayerFromEOSID(FString(eos_id));
    }

    bool HasPermission(AShooterPlayerController* pc)
    {
        const FString eos = AsaApi::IApiUtils::GetEOSIDFromController(pc);
        if (eos.IsEmpty())
            return false;

        const FString permission(g_config.permission);
        if (Permissions::IsPlayerHasPermission(eos, permission))
            return true;

        // Also honor the permission when it's granted to a tribe group the player's tribe is in.
        const int team = pc->TargetingTeamField();
        return team >= DinoColors::kFirstPlayerTeam && Permissions::IsTribeHasPermission(team, permission);
    }

    std::vector<std::string> SplitArgs(const FString* message)
    {
        std::vector<std::string> parts;
        std::istringstream stream(message->ToString());
        std::string word;
        while (stream >> word)
            parts.push_back(word);
        return parts; // parts[0] is the command itself
    }

    bool ParseInt(const std::string& s, int& out)
    {
        const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
        return res.ec == std::errc() && res.ptr == s.data() + s.size();
    }

    bool ParseFloat(const std::string& s, float& out)
    {
        const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
        return res.ec == std::errc() && res.ptr == s.data() + s.size();
    }

    bool IsValidTemplateName(const std::string& name)
    {
        if (name.empty() || name.size() > g_config.template_name_max_length)
            return false;
        return std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string Trim(const std::string& s)
    {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    // Shared checks for commands that act on the aimed creature. Returns the creature or nullptr
    // after telling the player why.
    APrimalDinoCharacter* GetOwnedTarget(AShooterPlayerController* pc)
    {
        APrimalDinoCharacter* dino = DinoColors::GetAimedDino(pc);
        if (!dino || dino->IsDead())
        {
            Send(pc, kError, "NoTarget");
            return nullptr;
        }
        if (!DinoColors::IsOwnedBy(dino, pc))
        {
            Send(pc, kError, "NotYourTame");
            return nullptr;
        }
        return dino;
    }

    // ---------- /setcr <region> <color> ----------

    void CmdSetRegion(AShooterPlayerController* pc, FString* message, int, int)
    {
        if (!pc || !HasPermission(pc))
        {
            Send(pc, kError, "NoPermission");
            return;
        }

        const auto args = SplitArgs(message);
        int region = -1;
        int color = -1;
        if (args.size() != 3 || !ParseInt(args[1], region) || !ParseInt(args[2], color))
        {
            Send(pc, kInfo, "UsageSetCR");
            return;
        }

        APrimalDinoCharacter* dino = GetOwnedTarget(pc);
        if (!dino)
            return;

        if (region < 0 || region >= DinoColors::kRegionCount || !DinoColors::IsRegionUsed(dino, region))
        {
            Send(pc, kError, "InvalidRegion");
            return;
        }
        if (!DinoColors::IsValidColorId(color))
        {
            Send(pc, kError, "InvalidColor");
            return;
        }

        DinoColors::SetRegion(dino, region, static_cast<uint8_t>(color));
        Send(pc, kOk, "RegionSet", {std::to_string(region), std::to_string(color)});
    }

    // ---------- /savecrt <name> ----------

    void CmdSaveTemplate(AShooterPlayerController* pc, FString* message, int, int)
    {
        if (!pc || !HasPermission(pc))
        {
            Send(pc, kError, "NoPermission");
            return;
        }

        const auto args = SplitArgs(message);
        if (args.size() != 2)
        {
            Send(pc, kInfo, "UsageSaveCRT");
            return;
        }

        const std::string name = args[1];
        if (!IsValidTemplateName(name))
        {
            Send(pc, kError, "InvalidTemplateName", {std::to_string(g_config.template_name_max_length)});
            return;
        }

        APrimalDinoCharacter* dino = GetOwnedTarget(pc);
        if (!dino)
            return;

        // Capture now, on the game thread; the creature pointer is not used after this point.
        ColorTemplate tpl = DinoColors::Capture(dino);
        if (tpl.species.empty() || tpl.species.size() > 255)
        {
            Send(pc, kError, "NoTarget");
            return;
        }

        const std::string eos = EosId(pc);
        g_pending.erase(eos); // a new save replaces any open prompt

        g_db.InsertIfAbsent(eos, name, tpl, [eos, name, tpl](InsertResult result, std::string error) {
            AShooterPlayerController* player = FindPlayer(eos);
            switch (result)
            {
            case InsertResult::Inserted:
                Send(player, kOk, "TemplateSaved", {name});
                break;
            case InsertResult::AlreadyExists:
                if (!player)
                    return;
                g_pending[eos] = PendingOverwrite{
                    name, tpl, Clock::now() + std::chrono::seconds(g_config.prompt_timeout_seconds)};
                Send(player, kInfo, "TemplateExists", {name});
                break;
            case InsertResult::Error:
                Log::GetLog()->error("CreatureColors: save failed: {}", error);
                Send(player, kError, "DatabaseError");
                break;
            }
        });
    }

    // ---------- y/n answer to an overwrite prompt ----------

    bool OnChatMessage(AShooterPlayerController* pc, FString* message, int, int, bool, bool command_executed)
    {
        if (command_executed || !pc || !message || g_pending.empty())
            return false;

        const std::string eos = EosId(pc);
        auto it = g_pending.find(eos);
        if (it == g_pending.end())
            return false;

        const std::string answer = Lower(Trim(message->ToString()));
        const bool yes = answer == "y" || answer == "yes";
        const bool no = answer == "n" || answer == "no";
        if (!yes && !no)
            return false; // ordinary chat; prompt stays open

        PendingOverwrite pending = std::move(it->second);
        g_pending.erase(it);

        if (no)
        {
            Send(pc, kInfo, "OverwriteCancelled", {pending.name});
            return true; // keep the answer out of public chat
        }

        g_db.Upsert(eos, pending.name, pending.tpl, [eos, name = pending.name](bool ok, std::string error) {
            AShooterPlayerController* player = FindPlayer(eos);
            if (ok)
            {
                Send(player, kOk, "TemplateOverwritten", {name});
            }
            else
            {
                Log::GetLog()->error("CreatureColors: overwrite failed: {}", error);
                Send(player, kError, "DatabaseError");
            }
        });
        return true;
    }

    // ---------- /setcrtaoe <name> [radius] ----------

    void CmdApplyAoe(AShooterPlayerController* pc, FString* message, int, int)
    {
        if (!pc || !HasPermission(pc))
        {
            Send(pc, kError, "NoPermission");
            return;
        }

        const auto args = SplitArgs(message);
        const std::string max_str = std::to_string(static_cast<int>(g_config.aoe_max_radius));
        if (args.size() < 2 || args.size() > 3)
        {
            Send(pc, kInfo, "UsageAOE", {max_str});
            return;
        }

        const std::string name = args[1];
        if (!IsValidTemplateName(name))
        {
            Send(pc, kError, "InvalidTemplateName", {std::to_string(g_config.template_name_max_length)});
            return;
        }

        float radius = g_config.aoe_default_radius;
        if (args.size() == 3 && (!ParseFloat(args[2], radius) || radius <= 0.0f))
        {
            Send(pc, kInfo, "UsageAOE", {max_str});
            return;
        }
        radius = std::min(radius, g_config.aoe_max_radius);

        const std::string eos = EosId(pc);
        g_db.Fetch(eos, name, [eos, name, radius](bool ok, std::optional<ColorTemplate> tpl, std::string error) {
            AShooterPlayerController* player = FindPlayer(eos);
            if (!player)
                return;

            if (!ok)
            {
                Log::GetLog()->error("CreatureColors: template lookup failed: {}", error);
                Send(player, kError, "DatabaseError");
                return;
            }
            if (!tpl)
            {
                Send(player, kError, "TemplateNotFound", {name});
                return;
            }

            AShooterCharacter* character = player->GetPlayerCharacter();
            if (!character || character->IsDead())
                return;

            TArray<AActor*> actors = AsaApi::GetApiUtils().GetAllActorsInRange(
                character->GetLocation(), radius, EServerOctreeGroup::DINOPAWNS_TAMED);

            int applied = 0;
            int skipped = 0;
            for (AActor* actor : actors)
            {
                if (!actor || !actor->IsPrimalDino())
                    continue;

                auto* dino = static_cast<APrimalDinoCharacter*>(actor);
                if (dino->IsDead() || !DinoColors::IsOwnedBy(dino, player))
                    continue; // other tribes' creatures are ignored and not counted

                if (DinoColors::GetSpecies(dino) != tpl->species)
                {
                    ++skipped;
                    continue;
                }

                DinoColors::Apply(dino, *tpl);
                ++applied;
            }

            Send(player, kOk, "AOEResult", {name, std::to_string(applied), std::to_string(skipped)});
        });
    }

    // ---------- tick: deliver DB results, expire prompts ----------

    void OnTick(float)
    {
        g_db.Pump();

        if (g_pending.empty())
            return;

        const auto now = Clock::now();
        for (auto it = g_pending.begin(); it != g_pending.end();)
        {
            if (now >= it->second.expires)
            {
                Send(FindPlayer(it->first), kInfo, "OverwriteExpired", {it->second.name});
                it = g_pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::string ConfigPath()
    {
        return AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/CreatureColors/config.json";
    }
}

extern "C" __declspec(dllexport) void Plugin_Init()
{
    Log::Get().Init("CreatureColors");

    std::string error;
    if (!LoadConfig(ConfigPath(), g_config, error))
    {
        Log::GetLog()->error("CreatureColors: {} - plugin not started.", error);
        return;
    }

    g_db.Start(DbSettings{g_config.host, g_config.port, g_config.user, g_config.password,
                          g_config.database, g_config.table});
    g_db_started = true;

    g_db.EnsureTable([](bool ok, std::string err) {
        if (ok)
            Log::GetLog()->info("CreatureColors: table `{}` ready.", g_config.table);
        else
            Log::GetLog()->error("CreatureColors: couldn't create table `{}`: {}", g_config.table, err);
    });

    auto& commands = AsaApi::GetCommands();
    commands.AddChatCommand(FString(g_config.cmd_set_region), &CmdSetRegion);
    commands.AddChatCommand(FString(g_config.cmd_save_template), &CmdSaveTemplate);
    commands.AddChatCommand(FString(g_config.cmd_apply_aoe), &CmdApplyAoe);
    commands.AddOnChatMessageCallback(kChatCallbackId, &OnChatMessage);
    commands.AddOnTickCallback(kTickCallbackId, &OnTick);

    Log::GetLog()->info("CreatureColors: loaded.");
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    if (!g_db_started)
        return;

    auto& commands = AsaApi::GetCommands();
    commands.RemoveChatCommand(FString(g_config.cmd_set_region));
    commands.RemoveChatCommand(FString(g_config.cmd_save_template));
    commands.RemoveChatCommand(FString(g_config.cmd_apply_aoe));
    commands.RemoveOnChatMessageCallback(kChatCallbackId);
    commands.RemoveOnTickCallback(kTickCallbackId);

    g_db.Stop();
    g_pending.clear();
    g_db_started = false;
}
