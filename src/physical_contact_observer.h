// included inside campaign_menu_native.cpp private native binding namespace
Available original_trigger_gate=nullptr;
constexpr uint32_t contact_ids[]={7770073,7770086};

bool entity_named(uintptr_t entity,const char* name) {
    save::NativeString value{};
    return read(entity,0x40,value) && value.data && value.length==static_cast<int32_t>(std::strlen(name)) &&
        std::memcmp(value.data,name,static_cast<size_t>(value.length))==0;
}
int observed_contact(uintptr_t entity) {
    if (entity_named(entity,"ap_independent_pickup_equipment_flame_belch_1")) return 0;
    if (entity_named(entity,"ap_independent_progress_argent_cell_1_1072112848")) return 1;
    return -1;
}

bool trigger_gate(uintptr_t entity) {
    const auto result=original_trigger_gate(entity);
    __try {
        if (active()) {
            const int index=observed_contact(entity);
            if (index>=0) {
                uint8_t dormant=0,suppressed=0,client=0;
                int64_t cooldown=0;
                int32_t type=0;
                const auto stage=index==0 ? save::BStage::flame_gate : save::BStage::crystal_gate;
                if (!read(entity,0x2a3,dormant) || !read(entity,0x600,suppressed) ||
                    !read(entity,0xc58,cooldown) || !read(entity,0x9b4,type) ||
                    !read(entity,0xc35,client)) {
                    save::session().btrace.record(stage,save::BStatus::blocked,"gate_fields_unavailable",0,{},entity);
                } else {
                    save::session().btrace.record(stage,save::BStatus::entered,
                        "native_activation_gate_returned",0,
                        {{"id",contact_ids[index]},{"accepted",result!=0},{"dormant",(dormant>>2)&1},
                         {"suppressed",suppressed},{"cooldown",cooldown},{"type",type},{"client",client}},entity);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}
