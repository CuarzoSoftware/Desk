#include <Core/Settings.h>
#include <Core/Log.h>
#include <CZ/XDG/XDGINI.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <charconv>
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

fs::path Settings::DefaultPicture() noexcept
{
    const fs::path path { fs::path(CZ_DESK_ASSETS_DIR) / "Wallpaper.jpg" };
    std::error_code ec;

    if (fs::is_regular_file(path, ec))
        return path;

    Log(CZWarning, CZLN, "No default picture at {} (is Desk installed?)", path.string());
    return {};
}

void Settings::load() noexcept
{
    *this = Settings();

    const fs::path path { FilePath() };
    std::error_code ec;

    // Until one is chosen (e.g. the first launch): the picture installed with Desk
    picture = DefaultPicture();

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

    if (auto v { value("Windows", "View") })
        listView = *v == "List";

    // Comma separated (empty: every section expanded)
    if (auto v { value("Info", "Collapsed") })
    {
        infoCollapsed.clear();

        for (size_t start { 0 }; start <= v->size();)
        {
            const size_t end { std::min(v->find(',', start), v->size()) };

            if (end > start)
                infoCollapsed.insert(v->substr(start, end - start));

            start = end + 1;
        }
    }

    if (auto v { value("Windows", "NameOffset") })
        if (Int32 offset { 0 }; std::from_chars(v->data(), v->data() + v->size(), offset).ec == std::errc())
            listNameOffset = std::clamp(offset, -100000, 100000);

    for (const auto &[key, width] : { std::pair { "DateWidth", &listColumns.date },
                                      std::pair { "SizeWidth", &listColumns.size },
                                      std::pair { "KindWidth", &listColumns.kind } })
        if (auto v { value("Windows", key) })
            if (const int w { std::atoi(v->c_str()) }; w > 0)
                *width = std::clamp(w, DESK_LIST_COLUMN_MIN, 4000);

    // Numbered in order (the keys are sorted as text: "10" before "2")
    if (auto g { ini->find("Favorites") }; g != ini->end())
    {
        std::vector<std::pair<int, fs::path>> numbered;

        for (const auto &[key, dir] : g->second)
            if (int n { 0 }; std::from_chars(key.data(), key.data() + key.size(), n).ec == std::errc() && !dir.empty())
                numbered.emplace_back(n, dir);

        std::sort(numbered.begin(), numbered.end(), [](const auto &a, const auto &b){ return a.first < b.first; });
        favorites.emplace();

        for (auto &[n, dir] : numbered)
            favorites->emplace_back(std::move(dir));
    }

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
        out << "[Windows]\nView=" << (listView ? "List" : "Grid") << '\n';
        if (listNameOffset)
            out << "NameOffset=" << *listNameOffset << '\n';
        out << "DateWidth=" << listColumns.date << '\n';
        out << "SizeWidth=" << listColumns.size << '\n';
        out << "KindWidth=" << listColumns.kind << "\n\n";
        out << "[Info]\nCollapsed=";

        for (auto it { infoCollapsed.begin() }; it != infoCollapsed.end(); it++)
            out << (it == infoCollapsed.begin() ? "" : ",") << *it;

        out << "\n\n";
        if (favorites)
        {
            out << "[Favorites]\n";

            for (size_t i = 0; i < favorites->size(); i++)
                out << i << '=' << (*favorites)[i].string() << '\n';

            out << '\n';
        }

        out << "[Positions]\n";

        for (const auto &[key, cell] : positions)
            out << EncodeKey(key) << '=' << cell.x() << ',' << cell.y() << '\n';
    }

    fs::rename(tmp, path, ec);

    if (ec)
        Log(CZError, CZLN, "Failed to save the settings file {}: {}", path.string(), ec.message());
}

static constexpr const char *StoredPictureStem { "wallpaper" };

bool Settings::IsStoredPicture(const fs::path &path) noexcept
{
    return !path.empty() && path.parent_path() == FilePath().parent_path() && path.stem() == StoredPictureStem;
}

fs::path Settings::StorePicture(const fs::path &source) noexcept
{
    if (source.empty() || IsStoredPicture(source))
        return source;

    const fs::path dir { FilePath().parent_path() };
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Its extension kept: it tells the decoders (and SVG) what it is
    std::string ext { source.extension().string() };
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    const fs::path target { dir / (std::string(StoredPictureStem) + ext) };
    const fs::path tmp { dir / (std::string(StoredPictureStem) + ".tmp" + ext) };

    // Aside first, then renamed over the previous one: never half written
    if (!fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec))
    {
        Log(CZError, CZLN, "Can't copy the desktop picture {}: {}", source.string(), ec.message());
        fs::remove(tmp, ec);
        return source;
    }

    fs::rename(tmp, target, ec);

    if (ec)
    {
        Log(CZError, CZLN, "Can't store the desktop picture at {}: {}", target.string(), ec.message());
        fs::remove(tmp, ec);
        return source;
    }

    // The previous one (another extension)
    for (const auto &entry : fs::directory_iterator(dir, ec))
        if (entry.path() != target && entry.path().stem() == StoredPictureStem)
            fs::remove(entry.path(), ec);

    Log(CZInfo, CZLN, "Desktop picture {} stored at {}", source.string(), target.string());
    return target;
}
