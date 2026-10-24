#include "config.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include <windows.h>

namespace forager
{
    static std::wstring trim(const std::wstring& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::iswspace(s[a])) ++a;
        while (b > a && std::iswspace(s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static std::wstring lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    }

    static bool parse_bool(const std::wstring& v, bool& out)
    {
        const std::wstring l = lower(v);
        if (l == L"true" || l == L"1" || l == L"yes" || l == L"on") { out = true; return true; }
        if (l == L"false" || l == L"0" || l == L"no" || l == L"off") { out = false; return true; }
        return false;
    }

    static bool parse_int(const std::wstring& v, int lo, int hi, int& out)
    {
        try
        {
            size_t used = 0;
            const int n = std::stoi(v, &used);
            if (used != v.size() || n < lo || n > hi) return false;
            out = n;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool parse_double(const std::wstring& v, double lo, double hi, double& out)
    {
        try
        {
            size_t used = 0;
            const double n = std::stod(v, &used);
            if (used != v.size() || n < lo || n > hi) return false;
            out = n;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static std::wstring utf8_to_wide(const std::string& s)
    {
        if (s.empty()) return L"";
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
        return out;
    }

    void Config::load(const std::wstring& path, std::vector<std::wstring>& warnings)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return;
        std::stringstream raw;
        raw << file.rdbuf();
        std::string bytes = raw.str();
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF) bytes.erase(0, 3);
        std::wistringstream lines(utf8_to_wide(bytes));

        std::wstring section;
        std::wstring line;
        int number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            line = trim(line);
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line.front() == L'[' && line.back() == L']')
            {
                section = lower(trim(line.substr(1, line.size() - 2)));
                continue;
            }
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            const std::wstring name = trim(line.substr(0, eq));
            const std::wstring setting = section + L"." + lower(name);
            const std::wstring value = trim(line.substr(eq + 1));
            bool ok = true;
            bool known = true;

            if (section.empty() || section == L"levels")
            {
                // column titling
                if (lower(name) == L"item") continue;
                int level = 0;
                ok = parse_int(value, 0, 10, level);
                if (ok) known = levels.set(name, level);
            }
            else if (setting == L"work.seconds") ok = parse_double(value, 1.0, 60.0, seconds);
            else if (setting == L"work.maxworkers") ok = parse_int(value, 1, 20, max_workers);
            else if (setting == L"work.scanms") ok = parse_int(value, 500, 60000, scan_ms);
            else if (setting == L"work.reach") ok = parse_double(value, 50.0, 1000.0, reach);
            else if (setting == L"advanced.verboselog") ok = parse_bool(value, verbose);
            else known = false;

            if (!known) warnings.push_back(L"config line " + std::to_wstring(number) + L": unknown setting '" + name + L"'");
            else if (!ok) warnings.push_back(L"config line " + std::to_wstring(number) + L": bad value '" + value + L"', default reverted");
        }
    }
}
