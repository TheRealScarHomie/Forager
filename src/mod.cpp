// Forager
// by TheRealScarHomie

#include "config.hpp"
#include "engine.hpp"
#include "rules.hpp"
#include "ue4ss_abi.hpp"

#include <windows.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace RC;
using namespace RC::Unreal;

namespace forager
{
    using namespace ue;

    constexpr const wchar_t* kVersion = L"1.0.0";
    constexpr uint8_t kHoldPriority = 1;

    static Config cfg;
    static std::wstring g_mod_dir;

    static void log(const std::wstring& message)
    {
        Output::send(L"[Forager] " + message + L"\n");
    }

    static void vlog(const std::wstring& message)
    {
        if (cfg.verbose) log(message);
    }

    static double now()
    {
        static LARGE_INTEGER frequency = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
        }();
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    }

    struct Natives
    {
        UObject* pal_utility = nullptr;
        UObject* kismet = nullptr;
        UObject* statics = nullptr;
        UObject* ai_action = nullptr;

        UFunction* get_base_manager = nullptr;
        UFunction* get_actors = nullptr;
        UFunction* is_server = nullptr;
        UFunction* create_action = nullptr;
        UFunction* soft_path = nullptr;
        UFunction* soft_ref = nullptr;
        UFunction* load_blocking = nullptr;
        UFunction* get_tables = nullptr;

        uint8_t harvest = 33;
        uint8_t collection = 6;
        uint8_t task = 18;
        uint8_t ui_text = 1;
        uint8_t busy_actions[8] = {};
        bool ready = false;
    };

    // bp class lookup
    struct Asset
    {
        const wchar_t* path;
        Weak cls;
        double retry_at = 0;
    };

    struct Guid
    {
        uint32_t a, b, c, d;
    };

    struct Base
    {
        Vec3 centre;
        double range = 0;
        std::vector<UObject*> handles;
    };

    enum class Stage
    {
        Walking,
        Harvesting,
    };

    struct Job
    {
        Weak handle;
        Weak target;
        UObject* target_key = nullptr;
        Vec3 spot;
        int level = 0;
        Stage stage = Stage::Walking;
        double give_up_at = 0;
        double next_move = 0;
        double done_at = 0;
        int move_tries = 0;
        bool seen_held = false;
    };

    struct Leaving
    {
        Weak handle;
        double check_at = 0;
        double forget_at = 0;
        int tries = 0;
    };

    struct Rest
    {
        double until = 0;
        int fails = 0;
    };

    static Natives n;
    static Asset g_plant_class{L"/Game/Pal/Blueprint/MapObject/Object/PickupItemOnLevel/BP_MapObject_PickupItem_Base.BP_MapObject_PickupItem_Base_C"};
    static Asset g_hold_class{L"/Game/Pal/Blueprint/Controller/AIAction/BP_AIActionDummy.BP_AIActionDummy_C"};
    static UObject* g_instance = nullptr;
    static double g_instance_at = 0;
    static UObject* g_manager = nullptr;
    static bool g_server = false;
    static std::vector<Base> bases;
    static std::vector<Job> jobs;
    static std::vector<Leaving> leaving;
    static std::unordered_map<UObject*, Rest> resting;
    static std::unordered_set<UObject*> ignored;
    static bool g_task_named = false;
    static int g_wanted_plants = 0;
    static int g_seen_plants = 0;
    static double g_scan_ms = 0;
    static double g_worst_ms = 0;
    static int g_foraged = 0;

    static UFunction* function_on(UObject* object, const wchar_t* name)
    {
        return object ? find_function(type_of(object), name) : nullptr;
    }

    // ref calls

    static UObject* call_object(UObject* target, const wchar_t* function)
    {
        Call call(function_on(target, function));
        if (!call.ok()) return nullptr;
        call.invoke(target);
        return call.get<UObject*>(L"ReturnValue");
    }

    static uint8_t call_byte(UObject* target, const wchar_t* function)
    {
        Call call(function_on(target, function));
        if (!call.ok()) return 0;
        call.invoke(target);
        return call.get<uint8_t>(L"ReturnValue");
    }

    static bool call_bool(UObject* target, const wchar_t* function)
    {
        return call_byte(target, function) != 0;
    }

    static int call_level(UObject* target, const wchar_t* function, const wchar_t* param, uint8_t suitability)
    {
        Call call(function_on(target, function));
        if (!call.set<uint8_t>(param, suitability)) return 0;
        call.invoke(target);
        return call.get<int32_t>(L"ReturnValue");
    }

    static Vec3 read_vector(FProperty* property, const uint8_t* memory)
    {
        if (!property || !memory) return Vec3{};
        if (size_of(property) >= 24)
        {
            double v[3];
            std::memcpy(v, memory, sizeof(v));
            return Vec3{v[0], v[1], v[2]};
        }
        float v[3];
        std::memcpy(v, memory, sizeof(v));
        return Vec3{v[0], v[1], v[2]};
    }

    static void put_vector(Call& call, const wchar_t* name, const Vec3& value)
    {
        FProperty* property = call.param(name);
        uint8_t* memory = call.ptr(name);
        if (!property || !memory) return;
        if (size_of(property) >= 24)
        {
            const double v[3] = {value.x, value.y, value.z};
            std::memcpy(memory, v, sizeof(v));
        }
        else
        {
            const float v[3] = {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
            std::memcpy(memory, v, sizeof(v));
        }
    }

    static Vec3 location_of(UObject* actor)
    {
        Call call(function_on(actor, L"K2_GetActorLocation"));
        if (!call.ok()) return Vec3{};
        call.invoke(actor);
        return read_vector(call.param(L"ReturnValue"), call.ptr(L"ReturnValue"));
    }

    // objects

    static bool resolve_natives()
    {
        if (n.ready) return true;
        n.pal_utility = find_object(L"/Script/Pal.Default__PalUtility");
        n.kismet = find_object(L"/Script/Engine.Default__KismetSystemLibrary");
        n.statics = find_object(L"/Script/Engine.Default__GameplayStatics");
        n.ai_action = find_object(L"/Script/Pal.Default__PalAIActionBase");
        n.get_base_manager = function_on(n.pal_utility, L"GetBaseCampManager");
        n.get_actors = function_on(n.statics, L"GetAllActorsOfClass");
        n.is_server = function_on(n.kismet, L"IsServer");
        n.create_action = function_on(n.ai_action, L"CreateActionInstanceFixName");
        n.soft_path = function_on(n.kismet, L"MakeSoftObjectPath");
        n.soft_ref = function_on(n.kismet, L"Conv_SoftObjPathToSoftObjRef");
        n.load_blocking = function_on(n.kismet, L"LoadAsset_Blocking");
        n.get_tables = function_on(n.pal_utility, L"GetMasterDataTables");
        n.ready = n.get_base_manager && n.get_actors && n.is_server && n.create_action && n.soft_path && n.soft_ref && n.load_blocking;
        if (!n.ready) return false;
        n.harvest = static_cast<uint8_t>(enum_value(L"/Script/Pal.EPalActionType", L"EPalActionType::Harvest", 33));
        n.collection = static_cast<uint8_t>(enum_value(L"/Script/Pal.EPalWorkSuitability", L"EPalWorkSuitability::Collection", 6));
        n.task = static_cast<uint8_t>(enum_value(L"/Script/Pal.EPalWorkType", L"EPalWorkType::CollectResourcePickable", 18));
        n.ui_text = static_cast<uint8_t>(enum_value(L"/Script/Pal.EPalLocalizeTextCategory", L"EPalLocalizeTextCategory::UICommon", 1));
        const wchar_t* busy[] = {L"EPalActionType::Sleep",  L"EPalActionType::Death",        L"EPalActionType::Eat",
                                 L"EPalActionType::Stan",   L"EPalActionType::CommonWork",   L"EPalActionType::TransportItem",
                                 L"EPalActionType::Liftup", L"EPalActionType::BeLiftup"};
        for (int i = 0; i < 8; ++i) n.busy_actions[i] = static_cast<uint8_t>(enum_value(L"/Script/Pal.EPalActionType", busy[i], 0));
        log(L"game classes found");
        return true;
    }

    static UObject* instance()
    {
        if (g_instance) return g_instance;
        const double t = now();
        if (t < g_instance_at) return nullptr;
        g_instance_at = t + 2.0;
        // per sesh
        for (UObject* candidate : find_all(L"PalGameInstance"))
        {
            if (candidate && name_of(candidate).rfind(L"Default__", 0) != 0)
            {
                g_instance = candidate;
                break;
            }
        }
        return g_instance;
    }

    static UObject* load(const wchar_t* path)
    {
        Call make(n.soft_path);
        if (!make.set_string(L"PathString", path)) return nullptr;
        make.invoke(n.kismet);
        Call convert(n.soft_ref);
        if (!convert.copy_in(L"SoftObjectPath", make.param(L"ReturnValue"), make.ptr(L"ReturnValue"))) return nullptr;
        convert.invoke(n.kismet);
        Call load_call(n.load_blocking);
        if (!load_call.copy_in(L"Asset", convert.param(L"ReturnValue"), convert.ptr(L"ReturnValue"))) return nullptr;
        load_call.invoke(n.kismet);
        return load_call.get<UObject*>(L"ReturnValue");
    }

    static UObject* class_of_asset(Asset& asset)
    {
        if (UObject* live = resolve(asset.cls)) return live;
        const double t = now();
        if (t < asset.retry_at) return nullptr;
        asset.retry_at = t + 5.0;
        UObject* loaded = load(asset.path);
        asset.cls = weak_of(loaded);
        return loaded;
    }

    static UObject* base_manager()
    {
        UObject* context = instance();
        if (!context) return nullptr;
        Call call(n.get_base_manager);
        if (!call.set<UObject*>(L"WorldContextObject", context)) return nullptr;
        call.invoke(n.pal_utility);
        return call.get<UObject*>(L"ReturnValue");
    }

    static bool is_server()
    {
        UObject* context = instance();
        if (!context) return false;
        Call call(n.is_server);
        call.set<UObject*>(L"WorldContextObject", context);
        call.invoke(n.kismet);
        return call.get<uint8_t>(L"ReturnValue") != 0;
    }

    static void read_bases()
    {
        bases.clear();
        UFunction* get_ids = function_on(g_manager, L"GetBaseCampIds");
        UFunction* try_get = function_on(g_manager, L"TryGetModel");
        if (!get_ids || !try_get) return;
        Call ids(get_ids);
        ids.invoke(g_manager);
        auto* list = reinterpret_cast<RawArray*>(ids.ptr(L"OutIds"));
        if (!list || !list->data || list->num <= 0 || list->num > 4096) return;

        for (int32_t i = 0; i < list->num; ++i)
        {
            Call get(try_get);
            get.set<Guid>(L"BaseCampId", static_cast<Guid*>(list->data)[i]);
            get.invoke(g_manager);
            UObject* model = get.get<UObject*>(L"OutModel");
            if (!model) continue;

            Base base;
            FProperty* transform = find_property(type_of(model), L"Transform");
            FProperty* translation = find_property(struct_of(transform), L"Translation");
            if (!transform || !translation) continue;
            base.centre = read_vector(translation, &at<uint8_t>(model, offset_of(transform) + offset_of(translation)));
            if (float* range = field<float>(model, L"AreaRange")) base.range = *range;

            UObject* container = object_field(object_field(model, L"WorkerDirector"), L"CharacterContainer");
            if (auto* slots = field<RawArray>(container, L"SlotArray"); slots && slots->data && slots->num > 0 && slots->num < 10000)
            {
                for (int32_t s = 0; s < slots->num; ++s)
                {
                    if (UObject* handle = object_field(static_cast<UObject**>(slots->data)[s], L"Handle")) base.handles.push_back(handle);
                }
            }
            bases.push_back(std::move(base));
        }
    }

    static std::vector<UObject*> wild_plants()
    {
        std::vector<UObject*> out;
        UObject* cls = class_of_asset(g_plant_class);
        UObject* context = instance();
        if (!cls || !context) return out;
        Call call(n.get_actors);
        call.set<UObject*>(L"WorldContextObject", context);
        call.set<UObject*>(L"ActorClass", cls);
        call.invoke(n.statics);
        auto* list = reinterpret_cast<RawArray*>(call.ptr(L"OutActors"));
        if (!list || !list->data || list->num <= 0 || list->num > 100000) return out;
        out.assign(static_cast<UObject**>(list->data), static_cast<UObject**>(list->data) + list->num);
        return out;
    }

    static std::wstring plant_id(UObject* plant)
    {
        UObject* model = call_object(plant, L"GetModel");
        return read_fname(field<uint8_t>(model, L"MapObjectMasterDataId"));
    }

    // pals

    static UObject* actor_of(UObject* handle)
    {
        return call_object(handle, L"TryGetIndividualActor");
    }

    static UObject* ai_of(UObject* actor)
    {
        return call_object(call_object(actor, L"GetController"), L"GetAIActionComponent");
    }

    static bool gathering_switched_off(UObject* parameter)
    {
        FProperty* save = find_property(type_of(parameter), L"SaveParameter");
        FProperty* option = find_property(struct_of(save), L"WorkSuitabilityOptionInfo");
        FProperty* off_list = find_property(struct_of(option), L"OffWorkSuitabilityList");
        if (!save || !option || !off_list) return false;
        const RawArray& list = at<RawArray>(parameter, offset_of(save) + offset_of(option) + offset_of(off_list));
        if (!list.data || list.num <= 0 || list.num > 64) return false;
        const auto* begin = static_cast<const uint8_t*>(list.data);
        return std::find(begin, begin + list.num, n.collection) != begin + list.num;
    }

    static bool busy_action(uint8_t action)
    {
        for (uint8_t blocked : n.busy_actions)
        {
            if (blocked != 0 && action == blocked) return true;
        }
        return false;
    }

    static bool pal_is_needed_elsewhere(UObject* actor, UObject* parameter)
    {
        if (call_bool(parameter, L"IsDead") || call_bool(parameter, L"IsSleeping")) return true;
        if (call_bool(actor, L"GetBattleMode")) return true;
        return call_bool(object_field(actor, L"CharacterParameterComponent"), L"IsAssignedToAnyWork");
    }

    enum class Skip : int
    {
        Free,
        NoRank,
        Working,
        Sick,
        Upset,
        Starving,
        TurnedOff,
        Acting,
        Count,
    };

    static int skipped[static_cast<int>(Skip::Count)];

    static Skip pal_state(UObject* actor, UObject* parameter)
    {
        if (pal_is_needed_elsewhere(actor, parameter)) return Skip::Working;
        if (gathering_switched_off(parameter)) return Skip::TurnedOff;
        if (call_byte(parameter, L"GetWorkerSick") != 0) return Skip::Sick;
        if (call_byte(parameter, L"GetBaseCampWorkerEventType") != 0) return Skip::Upset;
        if (call_byte(parameter, L"GetHungerType") > 1) return Skip::Starving;

        UObject* actions = object_field(actor, L"ActionComponent");
        Call current(function_on(actions, L"GetCurrentActionType"));
        if (!current.ok()) return Skip::Free;
        current.set<bool>(L"bIsCheckQueue", true);
        current.invoke(actions);
        return busy_action(current.get<uint8_t>(L"ReturnValue")) ? Skip::Acting : Skip::Free;
    }

    // current task shown on pal
    static void show_task(UObject* actor, bool foraging)
    {
        uint8_t* shown = field<uint8_t>(object_field(actor, L"CharacterParameterComponent"), L"WorkType");
        if (!shown) return;
        if (foraging) *shown = n.task;
        else if (*shown == n.task) *shown = 0;
    }

    static bool is_held(UObject* ai, UObject* hold_class)
    {
        Call has(function_on(ai, L"HasAction"));
        if (!has.set<UObject*>(L"checkClass", hold_class)) return false;
        has.set<uint8_t>(L"checkPriority", kHoldPriority);
        has.invoke(ai);
        return has.get<uint8_t>(L"ReturnValue") != 0;
    }

    // holds a pal for foraging
    static bool hold_pal(UObject* actor)
    {
        UObject* cls = class_of_asset(g_hold_class);
        UObject* ai = ai_of(actor);
        UFunction* set_action = function_on(ai, L"SetAction");
        if (!cls || !ai || !set_action) return false;

        Call create(n.create_action);
        create.set<UObject*>(L"WorldContextObject", actor);
        create.set<UObject*>(L"actionClass", cls);
        create.invoke(n.ai_action);
        UObject* action = create.get<UObject*>(L"ReturnValue");
        if (!action) return false;

        Call set(set_action);
        set.set<UObject*>(L"NewAction", action);
        set.set<uint8_t>(L"Priority", kHoldPriority);
        set.set<UObject*>(L"Instigator", actor);
        set.invoke(ai);
        show_task(actor, true);
        return true;
    }

    static void end_hold(UObject* actor)
    {
        UObject* ai = ai_of(actor);
        Call terminate(function_on(ai, L"TerminateCurrentActionByClass"));
        if (terminate.set<UObject*>(L"actionClass", resolve(g_hold_class.cls))) terminate.invoke(ai);
    }

    // logic action clear
    static void force_release(UObject* actor)
    {
        UObject* ai = ai_of(actor);
        Call cancel(function_on(ai, L"AllCancelAction_Logic_HardScript_Reaction"));
        if (cancel.set<UObject*>(L"Instigator", actor)) cancel.invoke(ai);
    }

    static void release_pal(const Weak& handle, UObject* actor, double t)
    {
        UObject* actions = object_field(actor, L"ActionComponent");
        Call cancel(function_on(actions, L"CancelActionByType"));
        if (cancel.set<uint8_t>(L"Type", n.harvest)) cancel.invoke(actions);
        end_hold(actor);
        show_task(actor, false);
        leaving.push_back(Leaving{handle, t + 1.0, t + 60.0});
    }

    // pal post-release check
    static void check_leaving(double t)
    {
        for (auto it = leaving.begin(); it != leaving.end();)
        {
            if (t < it->check_at)
            {
                ++it;
                continue;
            }
            UObject* actor = actor_of(resolve(it->handle));
            UObject* hold_class = resolve(g_hold_class.cls);
            if (!actor || !hold_class || !is_held(ai_of(actor), hold_class))
            {
                it = leaving.erase(it);
                continue;
            }
            if (t >= it->forget_at)
            {
                log(L"a pal could not be released from foraging");
                it = leaving.erase(it);
                continue;
            }
            end_hold(actor);
            if (++it->tries >= 2) force_release(actor);
            it->check_at = t + 1.0;
            ++it;
        }
    }

    enum class Walk : uint8_t
    {
        Failed = 0,
        Arrived = 1,
        Started = 2,
    };

    static bool is_walking(UObject* actor)
    {
        return call_byte(call_object(actor, L"GetController"), L"GetMoveStatus") == 3;
    }

    static Walk walk_to(UObject* actor, const Vec3& spot)
    {
        UObject* controller = call_object(actor, L"GetController");
        Call move(function_on(controller, L"PalMoveToLocation"));
        if (!move.ok()) return Walk::Failed;
        put_vector(move, L"Dest", spot);
        move.set<float>(L"AcceptanceRadius", 60.0f);
        move.set<bool>(L"bStopOnOverlap", true);
        move.set<bool>(L"bUsePathfinding", true);
        move.set<bool>(L"bProjectDestinationToNavigation", true);
        move.set<bool>(L"bCanStrafe", false);
        move.set<bool>(L"bAllowPartialPaths", true);
        move.invoke(controller);
        return static_cast<Walk>(std::min<uint8_t>(move.get<uint8_t>(L"ReturnValue"), 2));
    }

    static void start_harvest(UObject* actor, UObject* plant)
    {
        UObject* controller = call_object(actor, L"GetController");
        Call stop(function_on(controller, L"StopMovement"));
        stop.invoke(controller);

        UObject* actions = object_field(actor, L"ActionComponent");
        Call play(function_on(actions, L"PlayActionByType"));
        play.set<UObject*>(L"ActionTarget", plant);
        if (play.set<uint8_t>(L"Type", n.harvest)) play.invoke(actions);
    }

    // drops items and removes plant object
    static bool finish_harvest(UObject* plant)
    {
        UObject* concrete = object_field(call_object(plant, L"GetModel"), L"ConcreteModel");
        UObject* module = call_object(concrete, L"GetItemContainerModule");
        FProperty* spill = module ? find_property(type_of(module), L"bDropItemAtDisposed") : nullptr;
        Call dispose(function_on(plant, L"DisposeSelf_ServerInternal"));
        if (!spill || !dispose.ok()) return false;
        write_bool(spill, module, true);
        dispose.invoke(plant);
        ++g_foraged;
        return true;
    }

    // jobs

    static void rest(UObject* plant, double t, bool failed)
    {
        Rest& entry = resting[plant];
        if (!failed)
        {
            entry.until = t + 5.0;
            return;
        }
        entry.until = t + std::min(120.0, 20.0 * (1 << std::min(entry.fails, 3)));
        ++entry.fails;
    }

    static void update_jobs(double t)
    {
        check_leaving(t);
        for (auto it = jobs.begin(); it != jobs.end();)
        {
            Job& job = *it;
            bool done = false;
            UObject* handle = resolve(job.handle);
            UObject* actor = actor_of(handle);
            UObject* parameter = actor ? call_object(handle, L"TryGetIndividualParameter") : nullptr;
            UObject* plant = resolve(job.target);
            const bool held = actor && is_held(ai_of(actor), resolve(g_hold_class.cls));
            const bool displaced = job.seen_held && !held;
            job.seen_held = job.seen_held || held;

            if (!actor)
            {
                rest(job.target_key, t, false);
                done = true;
            }
            else if (!parameter || displaced || pal_is_needed_elsewhere(actor, parameter))
            {
                release_pal(job.handle, actor, t);
                rest(job.target_key, t, false);
                done = true;
            }
            else if (!plant || t >= job.give_up_at)
            {
                release_pal(job.handle, actor, t);
                if (plant) rest(job.target_key, t, true);
                done = true;
            }
            else if (job.stage == Stage::Walking)
            {
                const double distance = flat_distance(location_of(actor), job.spot);
                bool arrived = distance <= cfg.reach || (job.move_tries >= 2 && distance <= cfg.reach * 2.0 && !is_walking(actor));
                if (!arrived && t >= job.next_move && !is_walking(actor))
                {
                    job.next_move = t + 1.0;
                    const Walk walk = ++job.move_tries > 8 ? Walk::Failed : walk_to(actor, job.spot);
                    arrived = walk == Walk::Arrived;
                    if (walk == Walk::Failed)
                    {
                        release_pal(job.handle, actor, t);
                        rest(job.target_key, t, true);
                        done = true;
                    }
                }
                if (arrived)
                {
                    start_harvest(actor, plant);
                    const int speed = call_level(parameter, L"GetCraftSpeed_withBuff_WorkSuitability", L"Suitability", n.collection);
                    job.stage = Stage::Harvesting;
                    job.done_at = t + harvest_seconds(cfg.seconds, job.level, speed);
                    job.give_up_at = job.done_at + 10.0;
                }
            }
            else if (t >= job.done_at)
            {
                if (finish_harvest(plant)) vlog(L"a plant was foraged");
                else
                {
                    rest(job.target_key, t, true);
                    vlog(L"a plant could not be foraged");
                }
                release_pal(job.handle, actor, t);
                done = true;
            }

            it = done ? jobs.erase(it) : it + 1;
        }
    }

    struct Candidate
    {
        UObject* handle;
        UObject* actor;
        int level;
        Vec3 location;
        bool used = false;
    };

    static std::vector<Candidate> free_gatherers(const Base& base, const std::unordered_set<UObject*>& busy)
    {
        std::vector<Candidate> out;
        for (UObject* handle : base.handles)
        {
            if (busy.count(handle)) continue;
            UObject* actor = actor_of(handle);
            UObject* parameter = actor ? call_object(handle, L"TryGetIndividualParameter") : nullptr;
            if (!parameter) continue;
            const int level = call_level(parameter, L"GetWorkSuitabilityRankWithCharacterRank", L"WorkSuitability", n.collection);
            if (level <= 0)
            {
                ++skipped[static_cast<int>(Skip::NoRank)];
                continue;
            }
            const Skip state = pal_state(actor, parameter);
            ++skipped[static_cast<int>(state)];
            if (state != Skip::Free) continue;
            out.push_back(Candidate{handle, actor, level, location_of(actor)});
        }
        return out;
    }

    static void housekeeping(double t)
    {
        static double next = 0;
        if (t < next) return;
        next = t + 60.0;
        ignored.clear();
        for (auto it = resting.begin(); it != resting.end();) it = t > it->second.until + 900.0 ? resting.erase(it) : std::next(it);
    }

    static void hand_out_jobs(double t)
    {
        housekeeping(t);
        read_bases();
        g_wanted_plants = 0;
        for (int& count : skipped) count = 0;
        if (bases.empty()) return;

        std::unordered_set<UObject*> busy;
        std::unordered_set<UObject*> targeted;
        for (const Job& job : jobs)
        {
            busy.insert(resolve(job.handle));
            targeted.insert(job.target_key);
        }
        for (const Leaving& entry : leaving) busy.insert(resolve(entry.handle));

        struct Wanted
        {
            UObject* plant;
            Vec3 spot;
            int level;
        };
        std::vector<std::vector<Wanted>> wanted(bases.size());
        const std::vector<UObject*> plants = wild_plants();
        g_seen_plants = static_cast<int>(plants.size());
        for (UObject* plant : plants)
        {
            if (ignored.count(plant)) continue;
            const Vec3 spot = location_of(plant);
            size_t home = bases.size();
            for (size_t b = 0; b < bases.size() && home == bases.size(); ++b)
            {
                if (inside_base(spot, bases[b].centre, bases[b].range)) home = b;
            }
            const int level = home < bases.size() ? cfg.levels.needed(plant_id(plant)) : 0;
            if (level <= 0)
            {
                ignored.insert(plant);
                continue;
            }
            ++g_wanted_plants;
            const auto resting_entry = resting.find(plant);
            if (targeted.count(plant) || (resting_entry != resting.end() && t < resting_entry->second.until)) continue;
            wanted[home].push_back(Wanted{plant, spot, level});
        }

        for (size_t b = 0; b < bases.size(); ++b)
        {
            if (wanted[b].empty()) continue;
            int active = 0;
            for (UObject* handle : bases[b].handles) active += busy.count(handle) ? 1 : 0;
            if (active >= cfg.max_workers) continue;

            std::vector<Candidate> gatherers = free_gatherers(bases[b], busy);
            for (const Wanted& want : wanted[b])
            {
                if (active >= cfg.max_workers) break;
                std::vector<Worker> workers;
                for (size_t i = 0; i < gatherers.size(); ++i)
                {
                    if (!gatherers[i].used) workers.push_back(Worker{static_cast<int>(i), gatherers[i].level, gatherers[i].location});
                }
                const int chosen = pick_worker(workers, want.level, want.spot);
                if (chosen < 0) continue;
                Candidate& pal = gatherers[chosen];
                pal.used = true;
                if (!hold_pal(pal.actor)) continue;

                Job job;
                job.handle = weak_of(pal.handle);
                job.target = weak_of(want.plant);
                job.target_key = want.plant;
                job.spot = want.spot;
                job.level = want.level;
                job.give_up_at = t + 40.0;
                job.next_move = t;
                jobs.push_back(job);
                ++active;
            }
        }
    }

    static bool rename_in(UObject* table)
    {
        FProperty* row_struct = find_property(type_of(table), L"RowStruct");
        if (!row_struct) return false;
        UStruct* row_type = at<UStruct*>(table, offset_of(row_struct));
        FProperty* text = row_type ? find_property(row_type, L"TextData") : nullptr;
        if (!text) return false;

        // rowmap > rowstruct
        bool found = false;
        for_each_pair(&at<uint8_t>(table, offset_of(row_struct) + 8), 8, 24, [&](uint8_t* key, uint8_t* value) {
            if (_wcsicmp(read_fname(key).c_str(), L"COMMON_WORK_TYPE_CollectResourcePickable") != 0) return false;
            found = true;
            uint8_t* row = at<uint8_t*>(value, 0);
            if (row && read_text(text, row + offset_of(text)) == L"Gathering") write_text(text, row + offset_of(text), L"Foraging");
            return true;
        });
        return found;
    }

    // gath text rename
    static bool name_the_task()
    {
        // walker
        int renamed = 0;
        for (const wchar_t* cls : {L"DataTable", L"CompositeDataTable"})
        {
            for (UObject* table : find_all(cls))
            {
                if (table && rename_in(table)) ++renamed;
            }
        }
        vlog(L"task text found in " + std::to_wstring(renamed) + L" tables");
        return renamed > 0;
    }

    static void forget_world()
    {
        jobs.clear();
        leaving.clear();
        bases.clear();
        resting.clear();
        ignored.clear();
        g_plant_class.cls = Weak{};
        g_hold_class.cls = Weak{};
        g_plant_class.retry_at = g_hold_class.retry_at = 0;
        g_task_named = false;
        forget_lookups();
    }

    static void tick()
    {
        static double next_resolve = 0, next_world_check = 0, next_scan = 0, next_job_update = 0, next_task_name = 0;
        const double t = now();
        if (!n.ready)
        {
            if (t < next_resolve) return;
            next_resolve = t + 2.0;
            if (!resolve_natives()) return;
        }

        if (t >= next_world_check)
        {
            next_world_check = t + 0.5;
            UObject* manager = base_manager();
            if (manager != g_manager)
            {
                g_manager = manager;
                forget_world();
            }
            g_server = manager && is_server();
        }
        if (!g_manager || !g_server) return;

        if (!g_task_named && t >= next_task_name)
        {
            next_task_name = t + 5.0;
            g_task_named = name_the_task();
        }
        if (t >= next_job_update)
        {
            next_job_update = t + 0.25;
            update_jobs(t);
        }
        if (t >= next_scan)
        {
            next_scan = t + cfg.scan_ms / 1000.0;
            hand_out_jobs(t);
            // cost watcher
            g_scan_ms = (now() - t) * 1000.0;
            g_worst_ms = std::max(g_worst_ms, g_scan_ms);
        }
    }

    static bool on_console(const wchar_t* command)
    {
        const std::wstring text = command ? command : L"";
        if (text != L"forager" && text.rfind(L"forager ", 0) != 0) return false;
        std::wstring arg = text.size() > 7 ? text.substr(7) : L"";
        while (!arg.empty() && arg.front() == L' ') arg.erase(0, 1);
        if (arg == L"debug on" || arg == L"debug off")
        {
            cfg.verbose = arg == L"debug on";
            log(std::wstring(L"verbose logging ") + (cfg.verbose ? L"on" : L"off"));
            return true;
        }
        const auto yes_no = [](bool value) { return value ? L"yes" : L"no"; };
        log(std::wstring(L"v") + kVersion + L"  host " + yes_no(g_server) + L"  plant class " + yes_no(resolve(g_plant_class.cls)) + L"  hold class " +
            yes_no(resolve(g_hold_class.cls)) + L"  task text " + yes_no(g_task_named));
        log(L"bases " + std::to_wstring(bases.size()) + L"  plants in world " + std::to_wstring(g_seen_plants) + L"  plants wanted " +
            std::to_wstring(g_wanted_plants) + L"  pals foraging " + std::to_wstring(jobs.size()) + L"  foraged so far " + std::to_wstring(g_foraged));
        log(L"ready " + std::to_wstring(skipped[0]) + L"  no gathering " + std::to_wstring(skipped[1]) + L"  already working " + std::to_wstring(skipped[2]) +
            L"  sick " + std::to_wstring(skipped[3]) + L"  upset " + std::to_wstring(skipped[4]) + L"  starving " + std::to_wstring(skipped[5]) +
            L"  gathering off " + std::to_wstring(skipped[6]) + L"  mid-action " + std::to_wstring(skipped[7]));
        if (!bases.empty()) log(L"base range " + std::to_wstring(static_cast<int>(bases[0].range)) + L"  pals at base " + std::to_wstring(bases[0].handles.size()));
        int walking = 0;
        for (const Job& job : jobs) walking += job.stage == Stage::Walking ? 1 : 0;
        log(L"walking " + std::to_wstring(walking) + L"  harvesting/gathering " + std::to_wstring(jobs.size() - walking) + L"  releasing " + std::to_wstring(leaving.size()) +
            L"  plants resting " + std::to_wstring(resting.size()));
        log(L"last scan " + std::to_wstring(static_cast<int>(g_scan_ms)) + L"ms  worst " + std::to_wstring(static_cast<int>(g_worst_ms)) + L"ms  every " +
            std::to_wstring(cfg.scan_ms) + L"ms");
        return true;
    }

    class ForagerMod : public CppUserModBase
    {
      public:
        ForagerMod()
        {
            ModName = L"Forager";
            ModVersion = kVersion;
            ModDescription = L"Lets Pals with the Gathering ability forage things for pickup from the base area like berry bushes, mushrooms and more!";
            ModAuthors = L"TheRealScarHomie";
        }

        ~ForagerMod() override {}

        void on_unreal_init() override
        {
            // dll pin
            static bool started = false;
            if (started) return;
            started = true;
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&now), &self);

            wchar_t buffer[MAX_PATH * 2];
            const DWORD length = GetModuleFileNameW(self, buffer, static_cast<DWORD>(std::size(buffer)));
            g_mod_dir.assign(buffer, length);
            for (int i = 0; i < 2; ++i)
            {
                const size_t cut = g_mod_dir.find_last_of(L"\\/");
                if (cut != std::wstring::npos) g_mod_dir.resize(cut);
            }

            std::vector<std::wstring> warnings;
            cfg.load(g_mod_dir + L"\\config.ini", warnings);
            for (const auto& warning : warnings) log(warning);

            Hook::RegisterEngineTickPreCallback([](UEngine*, float) {
                try
                {
                    tick();
                }
                catch (...)
                {
                    static double last = -100;
                    if (now() - last > 30)
                    {
                        last = now();
                        log(L"error during update please leave a bug report");
                    }
                }
            });
            Hook::RegisterProcessConsoleExecCallback([](UObject*, const wchar_t* command, FOutputDevice&, UObject*) {
                try
                {
                    return on_console(command);
                }
                catch (...)
                {
                    return false;
                }
            });
            log(std::wstring(L"v") + kVersion + L" loaded from " + g_mod_dir);
        }
    };
}

#define FORAGER_API __declspec(dllexport)
extern "C"
{
    FORAGER_API RC::CppUserModBase* start_mod()
    {
        return new forager::ForagerMod();
    }

    FORAGER_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
