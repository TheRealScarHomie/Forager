#pragma once

#include <map>
#include <string>
#include <vector>

namespace forager
{
    struct Vec3
    {
        double x = 0, y = 0, z = 0;
    };

    double flat_distance(const Vec3& a, const Vec3& b);
    bool inside_base(const Vec3& point, const Vec3& centre, double range);

    // gathering level needed per plant
    class Levels
    {
      public:
        Levels();
        bool set(const std::wstring& item, int level);
        int needed(const std::wstring& object_id) const;

      private:
        std::map<std::wstring, int> levels_;
    };

    double harvest_seconds(double base_seconds, int needed_level, int work_speed);

    struct Worker
    {
        int index = 0;
        int level = 0;
        Vec3 location;
    };

    int pick_worker(const std::vector<Worker>& free_workers, int needed_level, const Vec3& target);
}
