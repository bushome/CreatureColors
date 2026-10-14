// CreatureColors (ASE) - lets permitted players recolor their own or their tribe's tames via chat commands.
//
//   /setcr <region> <color>        set one region on the aimed creature
//   /savecrt <name>                save the aimed creature's regions as a named template (/y or /n prompt on overwrite)
//   /setcrtaoe <name> [radius]     apply a template to matching creatures around the player
//   /y, /n                         answer an overwrite prompt
//   /listcrt [species]             list your templates (only the aimed creature's species when looking at one)
//   /delcrt <name>                 delete one of your templates

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "API/ARK/Ark.h"
#include "Logger/Logger.h"
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

    // Keyed by SteamID. One open overwrite prompt per player.
    std::unordered_map<uint64_t, PendingOverwrite> g_pending;

    const FLinearColor kOk{0.4f, 1.0f, 0.4f, 1.0f};
    const FLinearColor kInfo{1.0f, 0.85f, 0.3f, 1.0f};
    const FLinearColor kError{1.0f, 0.4f, 0.4f, 1.0f};

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

    // Private chat line to one player (never broadcast).
    void SendText(AShooterPlayerController* pc, const FLinearColor& color, const std::string& text)
    {
        if (!pc || text.empty())
            return;
        FString message(ArkApi::Tools::Utf8Decode(text).c_str());
        pc->ClientServerChatDirectMessage(&message, color, false);
    }

    void Send(AShooterPlayerController* pc, const FLinearColor& color, const std::string& key,
              const std::vector<std::string>& args = {})
    {
        if (!pc)
            return;
        SendText(pc, color, Format(g_config.Msg(key), args));
    }

    uint64_t SteamId(AShooterPlayerController* pc)
    {
        return ArkApi::IApiUtils::GetSteamIdFromController(pc);
    }

    AShooterPlayerController* FindPlayer(uint64_t steam_id)
    {
        return ArkApi::GetApiUtils().FindPlayerFromSteamId(steam_id);
    }

    bool HasPermission(AShooterPlayerController* pc)
    {
        const uint64_t steam_id = SteamId(pc);
        if (steam_id == 0)
            return false;

        // Individual perk: only the player's own groups count, never tribe groups. On ASE the Permissions
        // plugin's player checks also include the tribe's groups for online players, so those are removed
        // here. A group assigned to both the player and their tribe is removed too (see config.commented.json).
        const TArray<FString> groups = Permissions::GetPlayerGroups(steam_id);
        const TArray<FString> tribe_groups = Permissions::GetTribeGroups(pc->TargetingTeamField());

        std::vector<std::string> tribe;
        for (const FString& g : tribe_groups)
            tribe.push_back(g.ToString());

        for (const FString& group : groups)
        {
            const std::string name = group.ToString();
            if (std::find(tribe.begin(), tribe.end(), name) != tribe.end() ||
                name.rfind("TribeSize:", 0) == 0 || name.rfind("TribeOnline:", 0) == 0)
                continue;

            if (Permissions::IsGroupHasPermission(group, FString(std::string("*"))))
                return true;
            for (const std::string& permission : g_config.permissions)
            {
                if (Permissions::IsGroupHasPermission(group, FString(permission)))
                    return true;
            }
        }
        return false;
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

    // Template names are case-insensitive in both databases.
    bool SameName(const std::string& a, const std::string& b)
    {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                   return std::tolower(x) == std::tolower(y);
               });
    }

    bool IsValidTemplateName(const std::string& name)
    {
        if (name.empty() || name.size() > g_config.template_name_max_length)
            return false;
        return std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
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

    void CmdSetRegion(AShooterPlayerController* pc, FString* message, EChatSendMode::Type)
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

    void CmdSaveTemplate(AShooterPlayerController* pc, FString* message, EChatSendMode::Type)
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

        const uint64_t steam_id = SteamId(pc);
        g_pending.erase(steam_id); // a new save replaces any open prompt

        g_db.InsertIfAbsent(steam_id, name, tpl, [steam_id, name, tpl](InsertResult result, std::string error) {
            AShooterPlayerController* player = FindPlayer(steam_id);
            switch (result)
            {
            case InsertResult::Inserted:
                Send(player, kOk, "TemplateSaved", {name});
                break;
            case InsertResult::AlreadyExists:
                if (!player)
                    return;
                g_pending[steam_id] = PendingOverwrite{
                    name, tpl, Clock::now() + std::chrono::seconds(g_config.prompt_timeout_seconds)};
                Send(player, kInfo, "TemplateExists", {name, g_config.cmd_confirm_yes, g_config.cmd_confirm_no});
                break;
            case InsertResult::Error:
                Log::GetLog()->error("CreatureColors: save failed: {}", error);
                Send(player, kError, "DatabaseError");
                break;
            }
        });
    }

    // ---------- /y and /n answers to an overwrite prompt ----------
    // Answers are chat commands rather than plain chat so they never reach global chat
    // (or anything relaying it, like a crosschat bot).

    void AnswerOverwrite(AShooterPlayerController* pc, bool yes)
    {
        if (!pc)
            return;

        const uint64_t steam_id = SteamId(pc);
        auto it = g_pending.find(steam_id);
        if (it == g_pending.end())
        {
            Send(pc, kInfo, "NothingToConfirm");
            return;
        }

        PendingOverwrite pending = std::move(it->second);
        g_pending.erase(it);

        if (!yes)
        {
            Send(pc, kInfo, "OverwriteCancelled", {pending.name});
            return;
        }

        g_db.Upsert(steam_id, pending.name, pending.tpl, [steam_id, name = pending.name](bool ok, std::string error) {
            AShooterPlayerController* player = FindPlayer(steam_id);
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
    }

    void CmdConfirmYes(AShooterPlayerController* pc, FString*, EChatSendMode::Type) { AnswerOverwrite(pc, true); }
    void CmdConfirmNo(AShooterPlayerController* pc, FString*, EChatSendMode::Type) { AnswerOverwrite(pc, false); }

    // ---------- /setcrtaoe <name> [radius] ----------

    void CmdApplyAoe(AShooterPlayerController* pc, FString* message, EChatSendMode::Type)
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

        // Radius must be a whole number above 0. Anything else shows usage and doesn't run.
        float radius = g_config.aoe_default_radius;
        int requested = 0;
        bool capped = false;
        if (args.size() == 3)
        {
            if (!ParseInt(args[2], requested) || requested <= 0)
            {
                Send(pc, kInfo, "UsageAOE", {max_str});
                return;
            }
            radius = static_cast<float>(requested);
            if (radius > g_config.aoe_max_radius)
            {
                radius = g_config.aoe_max_radius;
                capped = true;
            }
        }

        const uint64_t steam_id = SteamId(pc);
        g_db.Fetch(steam_id, name, [steam_id, name, radius, requested, capped, max_str](bool ok, std::optional<ColorTemplate> tpl, std::string error) {
            AShooterPlayerController* player = FindPlayer(steam_id);
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

            TArray<AActor*> actors = ArkApi::GetApiUtils().GetAllActorsInRange(
                ArkApi::IApiUtils::GetPosition(player), radius, EServerOctreeGroup::DINOPAWNS_TAMED);

            int applied = 0;
            int skipped = 0;
            for (AActor* actor : actors)
            {
                if (!DinoColors::IsDino(actor))
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

            if (capped)
                Send(player, kInfo, "RadiusCapped", {std::to_string(requested), max_str});
            Send(player, kOk, "AOEResult", {name, std::to_string(applied), std::to_string(skipped)});
        });
    }

    // ---------- /listcrt [species] ----------
    //   aimed at a creature -> only templates for that exact species (variants stay separate)
    //   /listcrt <text>     -> species whose short name contains <text> (case-insensitive)
    //   neither             -> everything, grouped by species
    // Output is packed into chat messages of up to List.LineChars characters, at most List.MaxLines of them.

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // "Blueprint'/Game/PrimalEarth/Dinos/Rex/Rex_Character_BP.Rex_Character_BP'" -> "Rex".
    // Variant suffixes are kept ("..._Character_BP_Aberrant" -> "Rex_Aberrant"). Falls back to the
    // plain class name for anything that doesn't follow the usual naming.
    std::string SpeciesLabel(const std::string& path)
    {
        std::string name = path;
        if (const size_t slash = name.find_last_of('/'); slash != std::string::npos)
            name = name.substr(slash + 1);
        if (const size_t dot = name.find_last_of('.'); dot != std::string::npos)
            name = name.substr(dot + 1);
        while (!name.empty() && (name.back() == '\'' || name.back() == '"'))
            name.pop_back();
        if (name.size() > 2 && name.compare(name.size() - 2, 2, "_C") == 0)
            name.resize(name.size() - 2);

        static const std::string kSuffix = "_character_bp";
        if (const size_t pos = ToLower(name).find(kSuffix); pos != std::string::npos && pos > 0)
            name.erase(pos, kSuffix.size());

        return name.empty() ? path : name;
    }

    // Case-insensitive "natural" order, so name_2 sorts before name_10.
    bool NaturalLess(const std::string& a, const std::string& b)
    {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            const unsigned char ca = a[i], cb = b[j];
            if (std::isdigit(ca) && std::isdigit(cb))
            {
                size_t ie = i, je = j;
                while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
                while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
                // Compare numerically by ignoring leading zeros, then by length, then digit by digit.
                size_t is = i, js = j;
                while (is + 1 < ie && a[is] == '0') ++is;
                while (js + 1 < je && b[js] == '0') ++js;
                if (ie - is != je - js)
                    return ie - is < je - js;
                if (const int c = a.compare(is, ie - is, b, js, je - js); c != 0)
                    return c < 0;
                i = ie;
                j = je;
                continue;
            }
            const int la = std::tolower(ca), lb = std::tolower(cb);
            if (la != lb)
                return la < lb;
            ++i;
            ++j;
        }
        return a.size() - i < b.size() - j;
    }

    bool IsValidListFilter(const std::string& filter)
    {
        if (filter.empty() || filter.size() > 64)
            return false;
        return std::all_of(filter.begin(), filter.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
    }

    struct ListLine
    {
        std::string text;
        size_t names = 0;
    };

    // Greedy packing: keep adding names to the current line until the next one would push it past the
    // budget, then start a new line. A line always takes at least one name, so a long name can't stall it.
    // With show_species, each line restates the species it starts in ("Rex: a, b | Raptor: c").
    std::vector<ListLine> PackList(const std::vector<std::pair<std::string, std::string>>& items, // (label, name)
                                   bool show_species, size_t first_budget, size_t budget)
    {
        std::vector<ListLine> lines(1);
        std::string line_species;

        for (const auto& [label, name] : items)
        {
            ListLine& cur = lines.back();
            const size_t limit = lines.size() == 1 ? first_budget : budget;

            auto piece_for = [&](bool line_empty) {
                if (!show_species)
                    return (line_empty ? std::string() : std::string(", ")) + name;
                if (!line_empty && label == line_species)
                    return ", " + name;
                return (line_empty ? std::string() : std::string(" | ")) + label + ": " + name;
            };

            std::string piece = piece_for(cur.text.empty());
            if (!cur.text.empty() && cur.text.size() + piece.size() > limit)
            {
                lines.push_back({});
                piece = piece_for(true);
            }

            lines.back().text += piece;
            lines.back().names += 1;
            line_species = label;
        }
        return lines;
    }

    void SendList(AShooterPlayerController* player, const std::vector<TemplateEntry>& entries,
                  const std::string& aimed_species, const std::string& filter)
    {
        // Pick the rows to show.
        std::vector<std::pair<std::string, std::string>> items; // (label, name)
        std::vector<std::string> species_of;                     // parallel: full path, for sorting ties
        const std::string filter_lower = ToLower(filter);
        for (const TemplateEntry& e : entries)
        {
            const std::string label = SpeciesLabel(e.species);
            if (!aimed_species.empty() && e.species != aimed_species)
                continue;
            if (!filter.empty() && ToLower(label).find(filter_lower) == std::string::npos)
                continue;
            items.emplace_back(label, e.name);
            species_of.push_back(e.species);
        }

        if (items.empty())
        {
            if (!aimed_species.empty())
                Send(player, kInfo, "NoTemplatesSpecies", {SpeciesLabel(aimed_species)});
            else if (!filter.empty())
                Send(player, kInfo, "NoTemplatesMatch", {filter});
            else
                Send(player, kInfo, "NoTemplates");
            return;
        }

        // Sort by species label, then name (both case-insensitive).
        std::vector<size_t> order(items.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const std::string la = ToLower(items[a].first), lb = ToLower(items[b].first);
            if (la != lb)
                return la < lb;
            if (species_of[a] != species_of[b])
                return species_of[a] < species_of[b];
            return NaturalLess(items[a].second, items[b].second);
        });
        std::vector<std::pair<std::string, std::string>> sorted;
        sorted.reserve(items.size());
        for (size_t i : order)
            sorted.push_back(items[i]);

        // Header for the first message. Its length (with an empty body) comes out of line 1's budget.
        const std::string count = std::to_string(sorted.size());
        std::string header_key;
        std::vector<std::string> header_args;
        if (!aimed_species.empty())
        {
            header_key = "TemplateListSpecies";
            header_args = {SpeciesLabel(aimed_species), count};
        }
        else if (!filter.empty())
        {
            header_key = "TemplateListFiltered";
            header_args = {filter, count};
        }
        else
        {
            header_key = "TemplateList";
            header_args = {count};
        }

        auto with_body = [&](const std::string& body) {
            auto args = header_args;
            args.push_back(body);
            return Format(g_config.Msg(header_key), args);
        };

        const size_t budget = g_config.list_line_chars;
        const size_t header_len = with_body("").size();
        const size_t first_budget = header_len < budget ? budget - header_len : 0;

        // A single species (aimed) doesn't need species labels on every line.
        const bool show_species = aimed_species.empty();
        std::vector<ListLine> lines = PackList(sorted, show_species, first_budget, budget);

        size_t hidden = 0;
        if (lines.size() > g_config.list_max_lines)
        {
            for (size_t i = g_config.list_max_lines; i < lines.size(); ++i)
                hidden += lines[i].names;
            lines.resize(g_config.list_max_lines);
        }

        const std::string total = std::to_string(lines.size());
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::string text = i == 0 ? with_body(lines[i].text) : lines[i].text;
            if (lines.size() > 1)
                text += " " + Format(g_config.Msg("ListPage"), {std::to_string(i + 1), total});
            SendText(player, kInfo, text);
        }

        if (hidden > 0)
            Send(player, kInfo, "TemplateListMore", {std::to_string(hidden)});
    }

    void CmdListTemplates(AShooterPlayerController* pc, FString* message, EChatSendMode::Type)
    {
        if (!pc || !HasPermission(pc))
        {
            Send(pc, kError, "NoPermission");
            return;
        }

        const auto args = SplitArgs(message);
        if (args.size() > 2 || (args.size() == 2 && !IsValidListFilter(args[1])))
        {
            Send(pc, kInfo, "UsageListCRT");
            return;
        }

        // A typed filter wins; otherwise a creature under the crosshair narrows the list to its species.
        // Any creature works here (wild or another tribe's): it only picks which of your own templates to show.
        const std::string filter = args.size() == 2 ? args[1] : std::string();
        std::string aimed_species;
        if (filter.empty())
        {
            if (APrimalDinoCharacter* dino = DinoColors::GetAimedDino(pc))
                aimed_species = DinoColors::GetSpecies(dino);
        }

        const uint64_t steam_id = SteamId(pc);
        g_db.List(steam_id, [steam_id, aimed_species, filter](bool ok, std::vector<TemplateEntry> entries, std::string error) {
            AShooterPlayerController* player = FindPlayer(steam_id);
            if (!player)
                return;
            if (!ok)
            {
                Log::GetLog()->error("CreatureColors: template list failed: {}", error);
                Send(player, kError, "DatabaseError");
                return;
            }
            SendList(player, entries, aimed_species, filter);
        });
    }

    // ---------- /delcrt <name> ----------

    void CmdDeleteTemplate(AShooterPlayerController* pc, FString* message, EChatSendMode::Type)
    {
        if (!pc || !HasPermission(pc))
        {
            Send(pc, kError, "NoPermission");
            return;
        }

        const auto args = SplitArgs(message);
        if (args.size() != 2)
        {
            Send(pc, kInfo, "UsageDelCRT");
            return;
        }

        const std::string name = args[1];
        if (!IsValidTemplateName(name))
        {
            Send(pc, kError, "InvalidTemplateName", {std::to_string(g_config.template_name_max_length)});
            return;
        }

        const uint64_t steam_id = SteamId(pc);

        // Drop an open overwrite prompt for the same template so a later /y can't recreate it.
        if (auto it = g_pending.find(steam_id); it != g_pending.end() && SameName(it->second.name, name))
            g_pending.erase(it);

        g_db.Delete(steam_id, name, [steam_id, name](bool ok, bool deleted, std::string error) {
            AShooterPlayerController* player = FindPlayer(steam_id);
            if (!ok)
            {
                Log::GetLog()->error("CreatureColors: template delete failed: {}", error);
                Send(player, kError, "DatabaseError");
                return;
            }
            if (deleted)
                Send(player, kOk, "TemplateDeleted", {name});
            else
                Send(player, kError, "TemplateNotFound", {name});
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

    std::string PluginDir()
    {
        return ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/CreatureColors";
    }

    // SQLite file path: relative paths are resolved against the plugin folder.
    std::string SqlitePath(const std::string& file)
    {
        const std::filesystem::path p(file);
        return p.is_absolute() ? p.string() : (std::filesystem::path(PluginDir()) / p).string();
    }
}

extern "C" __declspec(dllexport) void Plugin_Init()
{
    Log::Get().Init("CreatureColors");

    std::string error;
    if (!LoadConfig(PluginDir() + "/config.json", g_config, error))
    {
        Log::GetLog()->error("CreatureColors: {} - plugin not started.", error);
        return;
    }

    DbSettings db;
    db.use_mysql = g_config.use_mysql;
    db.host = g_config.host;
    db.port = g_config.port;
    db.user = g_config.user;
    db.password = g_config.password;
    db.database = g_config.database;
    db.sqlite_path = SqlitePath(g_config.sqlite_file);
    db.table = g_config.table;

    g_db.Start(db);
    g_db_started = true;

    const std::string where = g_config.use_mysql
        ? "MariaDB database `" + g_config.database + "` on " + g_config.host
        : "SQLite file " + db.sqlite_path;

    g_db.EnsureTable([where](bool ok, std::string err) {
        if (ok)
            Log::GetLog()->info("CreatureColors: table `{}` ready in {}.", g_config.table, where);
        else
            Log::GetLog()->error("CreatureColors: couldn't create table `{}` in {}: {}", g_config.table, where, err);
    });

    auto& commands = ArkApi::GetCommands();
    commands.AddChatCommand(FString(g_config.cmd_set_region), &CmdSetRegion);
    commands.AddChatCommand(FString(g_config.cmd_save_template), &CmdSaveTemplate);
    commands.AddChatCommand(FString(g_config.cmd_apply_aoe), &CmdApplyAoe);
    commands.AddChatCommand(FString(g_config.cmd_confirm_yes), &CmdConfirmYes);
    commands.AddChatCommand(FString(g_config.cmd_confirm_no), &CmdConfirmNo);
    commands.AddChatCommand(FString(g_config.cmd_list_templates), &CmdListTemplates);
    commands.AddChatCommand(FString(g_config.cmd_delete_template), &CmdDeleteTemplate);
    commands.AddOnTickCallback(kTickCallbackId, &OnTick);

    Log::GetLog()->info("CreatureColors: loaded.");
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    if (!g_db_started)
        return;

    auto& commands = ArkApi::GetCommands();
    commands.RemoveChatCommand(FString(g_config.cmd_set_region));
    commands.RemoveChatCommand(FString(g_config.cmd_save_template));
    commands.RemoveChatCommand(FString(g_config.cmd_apply_aoe));
    commands.RemoveChatCommand(FString(g_config.cmd_confirm_yes));
    commands.RemoveChatCommand(FString(g_config.cmd_confirm_no));
    commands.RemoveChatCommand(FString(g_config.cmd_list_templates));
    commands.RemoveChatCommand(FString(g_config.cmd_delete_template));
    commands.RemoveOnTickCallback(kTickCallbackId);

    g_db.Stop();
    g_pending.clear();
    g_db_started = false;
}
