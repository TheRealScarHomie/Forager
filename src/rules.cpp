#include "rules.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace forager
{
    namespace
    {
        struct Plant
        {
            const wchar_t* object_id;
            const wchar_t* item;
        };

        // map object to config item
        constexpr Plant kPlants[] = {
            {L"pickupitem_redberry", L"berry"},
            {L"pickupitem_yakushimamushroom_03", L"berry"},
            {L"pickupitem_mushroom", L"mushroom"},
            {L"pickupitem_yakushimamushroom_01", L"mushroom"},
            {L"pickupitem_yakushimamushroom_02", L"mushroom"},
            {L"pickupitem_potato", L"potato"},
            {L"pickupitem_cavemushroom", L"cavern mushroom"},
            {L"pickupitem_poppy", L"beautiful flower"},
            {L"pickupitem_affectionfruit", L"kinship peach"},
            {L"pickupitem_lotus_attack_01", L"lotus"},
            {L"pickupitem_lotus_attack_02", L"lotus"},
            {L"pickupitem_lotus_hp_01", L"lotus"},
            {L"pickupitem_lotus_hp_02", L"lotus"},
            {L"pickupitem_lotus_stamina_01", L"lotus"},
            {L"pickupitem_lotus_stamina_02", L"lotus"},
            {L"pickupitem_lotus_weight_01", L"lotus"},
            {L"pickupitem_lotus_weight_02", L"lotus"},
            {L"pickupitem_lotus_workspeed_01", L"lotus"},
            {L"pickupitem_lotus_workspeed_02", L"lotus"},
        };

        std::wstring lower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return text;
        }

        std::wstring singular(std::wstring item)
        {
            if (item.size() > 3 && item.compare(item.size() - 3, 3, L"ies") == 0) return item.substr(0, item.size() - 3) + L"y";
            if (item.size() > 1 && item.back() == L's' && item != L"lotus") item.pop_back();
            return item;
        }
    }

    double flat_distance(const Vec3& a, const Vec3& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    bool inside_base(const Vec3& point, const Vec3& centre, double range)
    {
        return range > 0 && flat_distance(point, centre) <= range && std::fabs(point.z - centre.z) <= range;
    }

    Levels::Levels()
    {
        levels_ = {
            {L"berry", 1}, {L"mushroom", 2}, {L"potato", 4}, {L"cavern mushroom", 3}, {L"beautiful flower", 4}, {L"kinship peach", 5}, {L"lotus", 6},
        };
    }

    bool Levels::set(const std::wstring& item, int level)
    {
        if (level < 0 || level > 10) return false;
        auto found = levels_.find(singular(lower(item)));
        if (found == levels_.end()) return false;
        found->second = level;
        return true;
    }

    int Levels::needed(const std::wstring& object_id) const
    {
        const std::wstring id = lower(object_id);
        for (const Plant& plant : kPlants)
        {
            if (id == plant.object_id) return levels_.at(plant.item);
        }
        return 0;
    }

    double harvest_seconds(double base_seconds, int needed_level, int work_speed)
    {
        const double speed = work_speed > 0 ? work_speed : 70.0;
        const double harder = 1.0 + 0.25 * std::max(0, needed_level - 1);
        return std::clamp(base_seconds * harder * 70.0 / speed, 1.5, 30.0);
    }

    int pick_worker(const std::vector<Worker>& free_workers, int needed_level, const Vec3& target)
    {
        int best = -1;
        double best_distance = 0;
        for (const Worker& worker : free_workers)
        {
            if (worker.level < needed_level) continue;
            const double distance = flat_distance(worker.location, target);
            if (best < 0 || distance < best_distance)
            {
                best = worker.index;
                best_distance = distance;
            }
        }
        return best;
    }
}
