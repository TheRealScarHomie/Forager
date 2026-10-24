#pragma once

#include "rules.hpp"

#include <string>
#include <vector>

namespace forager
{
    struct Config
    {
        Levels levels;

        // work
        double seconds = 6.0;
        int max_workers = 2;
        int scan_ms = 2000;
        double reach = 220.0;

        // advanced
        bool verbose = false;

        void load(const std::wstring& path, std::vector<std::wstring>& warnings);
    };
}
