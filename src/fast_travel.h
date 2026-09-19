#pragma once
#include <cstdint>
#include <string>

namespace sentinel::fast_travel {
class EntryPolicy {
public:
    void selected(const char* namespace_id,const char* map,uint64_t generation,
                  bool completion_known,bool completed) {
        active_namespace_.clear(); active_map_.clear(); active_generation_=0;
        selected_namespace_=namespace_id ? namespace_id : "";
        selected_map_=map ? map : "";
        selected_generation_=generation;
        selected_known_=completion_known;
        selected_completed_=completed;
    }
    bool bind(const char* namespace_id,const char* map,uint64_t generation_before,bool admitted) {
        active_namespace_.clear(); active_map_.clear(); active_generation_=0;
        const bool eligible=admitted && selected_known_ && selected_completed_ && generation_before &&
            selected_generation_==generation_before && namespace_id && map &&
            selected_namespace_==namespace_id && selected_map_==map;
        if (eligible) {
            active_namespace_=selected_namespace_; active_map_=selected_map_;
            active_generation_=generation_before+1;
        }
        selected_namespace_.clear(); selected_map_.clear(); selected_generation_=0;
        selected_known_=false; selected_completed_=false;
        return eligible;
    }
    bool authorized(const char* namespace_id,const char* map,uint64_t generation) const {
        return generation && generation==active_generation_ && namespace_id && map &&
            active_namespace_==namespace_id && active_map_==map;
    }
    void reset() { *this={}; }
private:
    std::string selected_namespace_,selected_map_,active_namespace_,active_map_;
    uint64_t selected_generation_=0,active_generation_=0;
    bool selected_known_=false,selected_completed_=false;
};

enum class DispatchDecision { wait, invoke, confirmed };
class DispatchPolicy {
public:
    DispatchDecision observe(bool authorized,uint64_t generation,uintptr_t target,
                             bool target_unlocked,bool world_unlocked) {
        if (!authorized || !generation) { reset(); return DispatchDecision::wait; }
        if (generation_!=generation) { reset(); generation_=generation; }
        if (!target) {
            if (queued_ || confirmed_) absent_after_attempt_=true;
            return DispatchDecision::wait;
        }
        if (target_unlocked && world_unlocked) {
            confirmed_=true; confirmed_target_=target; absent_after_attempt_=false;
            return DispatchDecision::confirmed;
        }
        if (confirmed_) {
            if (target==confirmed_target_ && !absent_after_attempt_) return DispatchDecision::wait;
            confirmed_=false; queued_=false;
        }
        if (queued_) {
            if (target==attempted_target_ && !absent_after_attempt_) return DispatchDecision::wait;
            queued_=false;
        }
        absent_after_attempt_=false;
        return DispatchDecision::invoke;
    }
    void invoked(uintptr_t target,bool postcondition) {
        attempted_target_=target; queued_=true; absent_after_attempt_=false;
        if (postcondition) { confirmed_=true; confirmed_target_=target; }
    }
    void reset() { *this={}; }
    bool queued() const { return queued_; }
    bool confirmed() const { return confirmed_; }
private:
    uint64_t generation_=0;
    uintptr_t attempted_target_=0,confirmed_target_=0;
    bool queued_=false,confirmed_=false,absent_after_attempt_=false;
};

inline EntryPolicy& entry_policy() { static EntryPolicy value; return value; }
inline DispatchPolicy& dispatch_policy() { static DispatchPolicy value; return value; }
}
