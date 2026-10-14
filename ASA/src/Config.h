#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.hpp"

struct Config
{
    // Storage: MariaDB/MySQL when true, local SQLite file when false
    bool use_mysql = true;
    std::string sqlite_file = "database/creaturecolors.db"; // relative to the plugin folder

    // Mysql
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database = "creaturecolors";
    std::string table = "creaturecolors_templates";

    // General
    // Any one of these grants access. Config accepts a single string or an array of strings.
    std::vector<std::string> permissions{"CreatureColors.Use"};
    std::string cmd_set_region = "/setcr";
    std::string cmd_save_template = "/savecrt";
    std::string cmd_apply_aoe = "/setcrtaoe";
    std::string cmd_confirm_yes = "/y";
    std::string cmd_confirm_no = "/n";
    std::string cmd_list_templates = "/listcrt";
    std::string cmd_delete_template = "/delcrt";
    float aoe_default_radius = 300.0f;
    float aoe_max_radius = 3000.0f;
    int prompt_timeout_seconds = 30;
    size_t template_name_max_length = 32;
    size_t list_line_chars = 180; // /listcrt: characters per chat message before starting the next one
    size_t list_max_lines = 5;    // /listcrt: most messages one command sends

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
            {"TemplateExists", "Template '{0}' already exists, do you wish to overwrite? {1} or {2}"},
            {"NothingToConfirm", "There's no template overwrite waiting for an answer."},
            {"OverwriteCancelled", "Save of template '{0}' cancelled."},
            {"OverwriteExpired", "No answer received, save of template '{0}' cancelled."},
            {"TemplateNotFound", "No template named '{0}'."},
            {"InvalidTemplateName", "Template names can be 1-{0} characters: letters, numbers, - and _."},
            {"RadiusCapped", "Radius {0} is above the limit, using {1} instead."},
            {"AOEResult", "Applied '{0}' to {1} creature(s), skipped {2} of a different species."},
            {"UsageSetCR", "Usage: /setcr <region 0-5> <color id>"},
            {"UsageSaveCRT", "Usage: /savecrt <name>"},
            {"UsageAOE", "Usage: /setcrtaoe <name> [radius up to {0}]"},
            {"UsageDelCRT", "Usage: /delcrt <name>"},
            {"UsageListCRT", "Usage: /listcrt [species] (or look at a creature to list only its templates)"},
            {"TemplateList", "Your templates ({0}): {1}"},
            {"TemplateListSpecies", "Your {0} templates ({1}): {2}"},
            {"TemplateListFiltered", "Templates matching '{0}' ({1}): {2}"},
            {"TemplateListMore", "...and {0} more. Add a species filter or look at a creature to narrow the list."},
            {"ListPage", "({0}/{1})"},
            {"NoTemplates", "You don't have any saved templates."},
            {"NoTemplatesSpecies", "You don't have any {0} templates."},
            {"NoTemplatesMatch", "No templates match '{0}'."},
            {"TemplateDeleted", "Template '{0}' deleted."},
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
        // config.json may contain // and /* */ comments.
        const nlohmann::json j = nlohmann::json::parse(file, nullptr, true, true);

        cfg.use_mysql = j.value("UseMysql", cfg.use_mysql);

        if (j.contains("SQLite"))
            cfg.sqlite_file = j["SQLite"].value("File", cfg.sqlite_file);

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

        if (j.contains("Permission"))
        {
            const auto& p = j["Permission"];
            std::vector<std::string> list;
            if (p.is_string())
                list.push_back(p.get<std::string>());
            else if (p.is_array())
                for (const auto& item : p)
                    if (item.is_string() && !item.get<std::string>().empty())
                        list.push_back(item.get<std::string>());
            if (!list.empty())
                cfg.permissions = std::move(list);
        }

        if (j.contains("Commands"))
        {
            const auto& c = j["Commands"];
            cfg.cmd_set_region = c.value("SetColorRegion", cfg.cmd_set_region);
            cfg.cmd_save_template = c.value("SaveTemplate", cfg.cmd_save_template);
            cfg.cmd_apply_aoe = c.value("ApplyTemplateAOE", cfg.cmd_apply_aoe);
            cfg.cmd_confirm_yes = c.value("ConfirmYes", cfg.cmd_confirm_yes);
            cfg.cmd_confirm_no = c.value("ConfirmNo", cfg.cmd_confirm_no);
            cfg.cmd_list_templates = c.value("ListTemplates", cfg.cmd_list_templates);
            cfg.cmd_delete_template = c.value("DeleteTemplate", cfg.cmd_delete_template);
        }

        if (j.contains("AOE"))
        {
            const auto& a = j["AOE"];
            cfg.aoe_default_radius = a.value("DefaultRadius", cfg.aoe_default_radius);
            cfg.aoe_max_radius = a.value("MaxRadius", cfg.aoe_max_radius);
        }

        cfg.prompt_timeout_seconds = j.value("OverwritePromptTimeoutSeconds", cfg.prompt_timeout_seconds);
        cfg.template_name_max_length = j.value("TemplateNameMaxLength", cfg.template_name_max_length);

        if (j.contains("List"))
        {
            const auto& l = j["List"];
            cfg.list_line_chars = l.value("LineChars", cfg.list_line_chars);
            cfg.list_max_lines = l.value("MaxLines", cfg.list_max_lines);
        }

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

    if (cfg.sqlite_file.empty())
        cfg.sqlite_file = "database/creaturecolors.db";

    if (cfg.template_name_max_length < 1 || cfg.template_name_max_length > 32)
        cfg.template_name_max_length = 32; // matches VARCHAR(32) in the table

    // A line must fit the header plus one 32-character name with its species label.
    cfg.list_line_chars = (std::clamp)(cfg.list_line_chars, size_t{100}, size_t{500});
    cfg.list_max_lines = (std::clamp)(cfg.list_max_lines, size_t{1}, size_t{20});

    if (cfg.aoe_default_radius <= 0.0f)
        cfg.aoe_default_radius = 300.0f;
    if (cfg.aoe_max_radius < cfg.aoe_default_radius)
        cfg.aoe_max_radius = cfg.aoe_default_radius;
    if (cfg.prompt_timeout_seconds < 5)
        cfg.prompt_timeout_seconds = 5;

    return true;
}
