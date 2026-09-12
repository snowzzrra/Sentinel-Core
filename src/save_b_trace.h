// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <windows.h>

namespace sentinel::save {
enum class BStage : size_t { session, profile_read, profile_output, profile_choice, profile_capture,
    profile_prepare, profile_publish, catalog, creation, difficulty, transition, checkpoint_factory,
    provider, sdk_prepare, sdk_submit, sdk_callback, sdk_result, readback_create, readback_prepare,
    readback_verify, continuity, resume, parser, startup_gate, count };
inline constexpr const char* b_stage_names[]{"session", "profile_read", "profile_output", "profile_choice", "profile_capture",
    "profile_prepare", "profile_publish", "catalog", "creation", "difficulty", "transition", "checkpoint_factory",
    "provider", "sdk_prepare", "sdk_submit", "sdk_callback", "sdk_result", "readback_create", "readback_prepare",
    "readback_verify", "continuity", "resume", "parser", "startup_gate"};
static_assert(std::size(b_stage_names)==static_cast<size_t>(BStage::count));
enum class BStatus : uint32_t { entered=1, pending=2, succeeded=3, refused=4, blocked=5 };
struct BFact {
    const char* key=nullptr; int64_t value=0;
    BFact()=default;
    template<class T> BFact(const char* k,T v):key(k),value(static_cast<int64_t>(v)) {}
};
struct BEvent {
    uint64_t sequence=0,at_ms=0,operation=0; uintptr_t source=0;
    uint32_t thread=0; BStage stage=BStage::session; BStatus status=BStatus::entered;
    const char* predicate="not_observed"; std::array<BFact,16> facts{};
};
struct BSnapshot {
    uint64_t sequence=0;
    std::array<BEvent,static_cast<size_t>(BStage::count)> stages{};
    BEvent first_failure{};
};
// Fixed storage; hooks record literal predicates and numeric facts only. No
// allocation, disk I/O, native pointer dereference or gameplay policy here.
// Polls with identical facts are coalesced. First causal failure never rolls over.
class BTrace final {
public:
    void record(BStage stage,BStatus status,const char* predicate,uint64_t operation=0,
            std::initializer_list<BFact> facts={},uintptr_t source=0) {
        record_fields(stage,status,predicate,operation,facts.begin(),facts.size(),source);
    }
    void record_fields(BStage stage,BStatus status,const char* predicate,uint64_t operation,
            const BFact* facts,size_t fact_count,uintptr_t source=0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& previous=value_.stages[static_cast<size_t>(stage)];
        BEvent next{}; next.stage=stage; next.status=status; next.predicate=predicate;
        next.operation=operation; next.source=source; size_t count=0;
        while(count<fact_count && count<next.facts.size()) { next.facts[count]=facts[count]; ++count; }
        bool same=previous.sequence && previous.status==status && previous.predicate==predicate &&
            previous.operation==operation && previous.source==source;
        for(size_t i=0;i<next.facts.size() && same;++i)
            same=previous.facts[i].key==next.facts[i].key && previous.facts[i].value==next.facts[i].value;
        if(same) return;
        next.sequence=++value_.sequence; next.at_ms=GetTickCount64(); next.thread=GetCurrentThreadId(); previous=next;
        if(status==BStatus::refused && !value_.first_failure.sequence) value_.first_failure=next;
    }
    BSnapshot snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return value_; }
private:
    mutable std::mutex mutex_; BSnapshot value_{};
};
}
