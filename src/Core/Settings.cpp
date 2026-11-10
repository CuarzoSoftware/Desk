#include <Core/Settings.h>
#include <Core/Log.h>
#include <CZ/XDG/XDGINI.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <unistd.h>

namespace fs = std::filesystem;

fs::path Settings::FilePath() noexcept
{
    fs::path dir;

    if (const char *config { getenv("XDG_CONFIG_HOME") }; config && config[0] == '/')
        dir = config;
    else if (const char *home { getenv("HOME") }; home && home[0] == '/')
        dir = fs::path(home) / ".config";
    else if (const passwd *pw { getpwuid(getuid()) }; pw && pw->pw_dir)
        dir = fs::path(pw->pw_dir) / ".config";

    return dir / "cz-desk" / "desk.conf";
}

// Keys are file names: keep only characters that can't confuse the INI parser.
static std::string EncodeKey(const std::string &key) noexcept
{
    static constexpr char hex[] { "0123456789ABCDEF" };
    std::string out;

    for (const unsigned char c : key)
    {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_')
            out += char(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }

    return out;
}

static std::string DecodeKey(const std::string &key) noexcept
{
    std::string out;

    for (size_t i = 0; i < key.size(); i++)
    {
        if (key[i] == '%' && i + 2 < key.size() && std::isxdigit((unsigned char)key[i + 1]) && std::isxdigit((unsigned char)key[i + 2]))
        {
            out += char(std::strtol(key.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
            out += key[i];
    }

    return out;
}

static bool ParseColor(const std::string &value, SkColor &out) noexcept
{
    std::string hex { value };

    if (!hex.empty() && hex.front() == '#')
        hex.erase(0, 1);

    if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](unsigned char c){ return std::isxdigit(c); }))
        return false;

    out = 0xFF000000 | SkColor(std::strtoul(hex.c_str(), nullptr, 16));
    return true;
}

void Settings::load() noexcept
{
    *this = Settings();

    const fs::path path { FilePath() };
    std::error_code ec;

    if (!fs::is_regular_file(path, ec))
    {
        Log(CZInfo, CZLN, "No settings file at {}, using defaults", path.string());
        return;
    }

    auto ini { XDGINI::LoadFile(path) };

    if (!ini)
        return;

    const auto value = [&ini](const char *group, const char *key) -> const std::string * {
        auto g { ini->find(group) };
        if (g == ini->end()) return nullptr;
        auto it { g->second.find(key) };
        return it == g->second.end() ? nullptr : &it->second;
    };

    if (auto v { value("Background", "Picture") })
        picture = *v;

    if (auto v { value("Background", "UsePicture") })
        usePicture = !(*v == "false" || *v == "0");

    if (auto v { value("Background", "Fit") })
        fit = *v == "Contain" ? Fit::Contain : *v == "Fill" ? Fit::Fill : Fit::Cover;

    if (auto v { value("Background", "Color") }; v && !ParseColor(*v, color))
        Log(CZWarning, CZLN, "Invalid background color '{}' (expected #RRGGBB)", *v);

    if (auto v { value("Icons", "Size") })
        iconSize = std::clamp(std::atoi(v->c_str()), DESK_ICON_SIZE_MIN, DESK_ICON_SIZE_MAX);

    if (auto v { value("Icons", "SortBy") })
        sortBy = *v == "Name" ? SortBy::Name : *v == "Kind" ? SortBy::Kind : *v == "Size" ? SortBy::Size : SortBy::Custom;

    if (auto g { ini->find("Positions") }; g != ini->end())
    {
        for (const auto &[key, cell] : g->second)
        {
            int col, row;

            if (std::sscanf(cell.c_str(), "%d,%d", &col, &row) == 2 && col >= 0 && row >= 0)
                positions[DecodeKey(key)] = SkIPoint::Make(col, row);
        }
    }
}

void Settings::save() noexcept
{
    const fs::path path { FilePath() };
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Write to a temporary file first so a crash can't leave a truncated settings file.
    const fs::path tmp { path.string() + ".tmp" };
    {
        std::ofstream out { tmp, std::ios::trunc };

        if (!out)
        {
            Log(CZError, CZLN, "Failed to write the settings file {}", tmp.string());
            return;
        }

        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%06X", unsigned(color & 0xFFFFFF));

        out << "[Background]\n";
        out << "Picture=" << picture.string() << '\n';
        out << "UsePicture=" << (usePicture ? "true" : "false") << '\n';
        out << "Fit=" << (fit == Fit::Contain ? "Contain" : fit == Fit::Fill ? "Fill" : "Cover") << '\n';
        out << "Color=" << hex << "\n\n";
        out << "[Icons]\nSize=" << iconSize << '\n';
        out << "SortBy=" << (sortBy == SortBy::Name ? "Name" : sortBy == SortBy::Kind ? "Kind" : sortBy == SortBy::Size ? "Size" : "Custom") << "\n\n";
        out << "[Positions]\n";

        for (const auto &[key, cell] : positions)
            out << EncodeKey(key) << '=' << cell.x() << ',' << cell.y() << '\n';
    }

    fs::rename(tmp, path, ec);

    if (ec)
        Log(CZError, CZLN, "Failed to save the settings file {}: {}", path.string(), ec.message());
}
