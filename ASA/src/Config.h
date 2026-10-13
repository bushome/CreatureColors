#pragma once

#include <fstream>
#include <string>
#include <unordered_map>

#include "json.hpp"

struct Config
{
    // Mysql
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database = "creaturecolors";
    std::string table = "creaturecolors_templates";

    // General
    std::string permission = "CreatureColors.Use";
    std::string cmd_set_region = "/setcr";
    std::string cmd_save_template = "/savecrt";
    std::string cmd_apply_aoe = "/setcrtaoe";
    float aoe_default_radius = 300.0f;
    float aoe_max_radius = 3000.0f;
    int prompt_timeout_seconds = 30;
    size_t template_name_max_length = 32;

    std::unordered_map<std::string, std::string> messages;

    // Returns the configured message, or a built-in default if the key is missing from config.json.
    const std::string& Msg(const std::string& key) const
    {
        if (auto it = messages.find(key); it != messages.end())
            return it->second;

        static const std::unordered_map<std::string, std::string> defaults = {
            {"NoPermission", "You don't have permission to color creatures."},
            {"NoTarget", "Look at one of your tames first."},
            {"NotYourTame", "That creature isn't owned by you or your tribe."},
            {"InvalidRegion", "Region must be 0-5 and used by this species."},
            {"InvalidColor", "That color ID isn't valid."},
            {"RegionSet", "Region {0} set to color {1}."},
            {"TemplateSaved", "Template '{0}' saved."},
            {"TemplateOverwritten", "Template '{0}' overwritten."},
            {"TemplateExists", "Template '{0}' already exists, do you wish to overwrite y/n?"},
            {"OverwriteCancelled", "Save of template '{0}' cancelled."},
            {"OverwriteExpired", "No answer received, save of template '{0}' cancelled."},
            {"TemplateNotFound", "No template named '{0}'."},
            {"InvalidTemplateName", "Template names can be 1-{0} characters: letters, numbers, - and _."},
            {"AOEResult", "Applied '{0}' to {1} creature(s), skipped {2} of a different species."},
            {"UsageSetCR", "Usage: /setcr <region 0-5> <color id>"},
            {"UsageSaveCRT", "Usage: /savecrt <name>"},
            {"UsageAOE", "Usage: /setcrtaoe <name> [radius up to {0}]"},
            {"DatabaseError", "Couldn't reach the template database. Try again later."},
        };

        if (auto it = defaults.find(key); it != defaults.end())
            return it->second;

        static const std::string empty;
        return empty;
    }
};

inline bool IsSafeSqlIdentifier(const std::string& s)
{
    if (s.empty() || s.size() > 64)
        return false;
    for (char c : s)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

// Loads config.json. Missing keys keep their defaults; returns false with an error on parse failure
// or an unsafe table name (the table name is placed directly into SQL, so it is restricted).
inline bool LoadConfig(const std::string& path, Config& cfg, std::string& error)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        error = "Can't open " + path;
        return false;
    }

    try
    {
        nlohmann::json j;
        file >> j;

        if (j.contains("Mysql"))
        {
            const auto& m = j["Mysql"];
            cfg.host = m.value("Host", cfg.host);
            cfg.port = m.value("Port", cfg.port);
            cfg.user = m.value("User", cfg.user);
            cfg.password = m.value("Password", cfg.password);
            cfg.database = m.value("Database", cfg.database);
            cfg.table = m.value("TableName", cfg.table);
        }

        cfg.permission = j.value("Permission", cfg.permission);

        if (j.contains("Commands"))
        {
            const auto& c = j["Commands"];
            cfg.cmd_set_region = c.value("SetColorRegion", cfg.cmd_set_region);
            cfg.cmd_save_template = c.value("SaveTemplate", cfg.cmd_save_template);
            cfg.cmd_apply_aoe = c.value("ApplyTemplateAOE", cfg.cmd_apply_aoe);
        }

        if (j.contains("AOE"))
        {
            const auto& a = j["AOE"];
            cfg.aoe_default_radius = a.value("DefaultRadius", cfg.aoe_default_radius);
            cfg.aoe_max_radius = a.value("MaxRadius", cfg.aoe_max_radius);
        }

        cfg.prompt_timeout_seconds = j.value("OverwritePromptTimeoutSeconds", cfg.prompt_timeout_seconds);
        cfg.template_name_max_length = j.value("TemplateNameMaxLength", cfg.template_name_max_length);

        if (j.contains("Messages"))
        {
            for (auto it = j["Messages"].begin(); it != j["Messages"].end(); ++it)
            {
                if (it.value().is_string())
                    cfg.messages[it.key()] = it.value().get<std::string>();
            }
        }
    }
    catch (const std::exception& e)
    {
        error = std::string("config.json parse error: ") + e.what();
        return false;
    }

    if (!IsSafeSqlIdentifier(cfg.table))
    {
        error = "Mysql.TableName may only contain letters, numbers and underscores.";
        return false;
    }

    if (cfg.template_name_max_length < 1 || cfg.template_name_max_length > 32)
        cfg.template_name_max_length = 32; // matches VARCHAR(32) in the table

    if (cfg.aoe_default_radius <= 0.0f)
        cfg.aoe_default_radius = 300.0f;
    if (cfg.aoe_max_radius < cfg.aoe_default_radius)
        cfg.aoe_max_radius = cfg.aoe_default_radius;
    if (cfg.prompt_timeout_seconds < 5)
        cfg.prompt_timeout_seconds = 5;

    return true;
}
