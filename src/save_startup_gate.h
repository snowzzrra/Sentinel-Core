// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_b_trace.h"
#include "sentinel_engine.h"
#include <atomic>

namespace sentinel::save {
// One process-lifetime gate. Arm before publishing the RootInit detour; never
// reset/close its event while a retained native entry can still reach it.
class StartupGate final {
public:
    enum class State : uint32_t { unarmed, pending, ready, failed, cancelled, wait_failed };
    struct Result { State state; uint32_t error=0; };
    struct Arm {
        uint64_t at_ms=0;
        uint32_t manager_read_reason=SC_REASON_NOT_SAMPLED,root_read_reason=SC_REASON_NOT_SAMPLED,
            vtable_read_reason=SC_REASON_NOT_SAMPLED,init_read_reason=SC_REASON_NOT_SAMPLED;
        bool manager_present=false,root_equals_binding=false,init_equals_target=false;
    };
    StartupGate()=default;
    StartupGate(const StartupGate&)=delete;
    StartupGate& operator=(const StartupGate&)=delete;
    ~StartupGate() { if (event_) CloseHandle(event_); }
    bool arm() {
        event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if (!event_) return false;
        begin_ms_=GetTickCount64(); state_.store(State::pending,std::memory_order_release); return true;
    }
    Result wait() {
        const auto state=state_.load(std::memory_order_acquire);
        if (state!=State::pending) return {state};
        uint64_t missing=0; first_wait_ms_.compare_exchange_strong(missing,GetTickCount64());
        waiting_.fetch_add(1,std::memory_order_acq_rel);
        // No Session, diagnostic, installer or native lock is held here.
        const auto result=WaitForSingleObject(event_,INFINITE);
        const auto error=result==WAIT_OBJECT_0?0u:GetLastError();
        waiting_.fetch_sub(1,std::memory_order_acq_rel);
        return {result==WAIT_OBJECT_0?state_.load(std::memory_order_acquire):State::wait_failed,error};
    }
    uint64_t begin_ms() const { return begin_ms_; }
    uint32_t waiting() const { return waiting_.load(std::memory_order_acquire); }
    void observe_arm(const Arm& value) { std::lock_guard<std::mutex> guard(observation_mutex_); arm_=value; }
    void entered(bool root_equal,bool caller_equal,uint64_t caller_rva) {
        std::lock_guard<std::mutex> guard(observation_mutex_);
        if (!entry_ms_) { entry_ms_=GetTickCount64(); entry_thread_=GetCurrentThreadId();
            root_equal_=root_equal; caller_equal_=caller_equal; caller_rva_=caller_rva; }
    }
    void record(BTrace& trace,BStatus status,const char* predicate,State state,uint32_t error=0) const {
        std::lock_guard<std::mutex> guard(observation_mutex_);
        const uint32_t reasons[]{arm_.manager_read_reason,arm_.root_read_reason,arm_.vtable_read_reason,arm_.init_read_reason};
        uint32_t read_step=0,read_reason=SC_REASON_NONE;
        for (uint32_t i=0;i<std::size(reasons);++i) if (reasons[i]) { read_step=i+1; read_reason=reasons[i]; break; }
        trace.record(BStage::startup_gate,status,predicate,0,
            {{"gate_state",state},{"install_begin_ms",begin_ms_},{"arm_ms",arm_.at_ms},
             {"install_complete_ms",complete_ms_},{"first_wait_ms",first_wait_ms_.load(std::memory_order_acquire)},
             {"startup_enter_ms",entry_ms_},{"startup_thread",entry_thread_},{"wait_error",error},
             {"arm_read_step",read_step},{"arm_read_reason",read_reason},
             {"manager_present",arm_.manager_present},{"root_equals_binding",arm_.root_equals_binding},
             {"init_slot_equals_target0",arm_.init_equals_target},{"root_equal",root_equal_},
             {"caller_equal",caller_equal_},{"caller_rva",caller_rva_}});
    }
    // Scope completion covers every return and C++ exception from installation.
    class Completion final {
    public:
        explicit Completion(StartupGate& gate,BTrace* trace=nullptr):gate_(gate),trace_(trace) {}
        Completion(const Completion&)=delete;
        Completion& operator=(const Completion&)=delete;
        ~Completion() {
            { std::lock_guard<std::mutex> guard(gate_.observation_mutex_); gate_.complete_ms_=GetTickCount64(); }
            if (trace_) gate_.record(*trace_,result_==State::ready?BStatus::succeeded:BStatus::refused,
                result_==State::ready?"startup_gate_installation_ready":result_==State::cancelled?
                    "startup_gate_installation_cancelled":"startup_gate_installation_failed",result_);
            gate_.state_.store(result_,std::memory_order_release); SetEvent(gate_.event_);
        }
        void ready() { result_=State::ready; }
        void cancel() { result_=State::cancelled; }
    private:
        StartupGate& gate_; BTrace* trace_; State result_=State::failed;
    };
private:
    HANDLE event_=nullptr;
    std::atomic<State> state_{State::unarmed};
    std::atomic<uint32_t> waiting_{0};
    uint64_t begin_ms_=0;
    std::atomic<uint64_t> first_wait_ms_{0};
    mutable std::mutex observation_mutex_;
    Arm arm_{}; uint64_t entry_ms_=0,complete_ms_=0,caller_rva_=0; uint32_t entry_thread_=0;
    bool root_equal_=false,caller_equal_=false;
};
}
