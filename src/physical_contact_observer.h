// Passive observations at native trigger contact, event and activation boundaries.
// Included inside campaign_menu_native.cpp's private native binding namespace.
using TriggerFilter=bool(*)(uintptr_t,uintptr_t);
using TriggerEvent=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,int);
TriggerFilter original_trigger_filter=nullptr;
TriggerEvent original_start_touch=nullptr,original_trigger_touch=nullptr;
Available original_trigger_gate=nullptr;
Populate original_trigger_dispatch=nullptr;
uintptr_t invalid_physics_body_address=0,contact_image_base=0;
constexpr uint32_t contact_ids[]={7770073,7770086};
constexpr save::BStage contact_physics_stages[]={save::BStage::flame_physics,save::BStage::crystal_physics};
constexpr save::BStage contact_touch_stages[]={save::BStage::flame_touch,save::BStage::crystal_touch};
constexpr save::BStage contact_filter_stages[]={save::BStage::flame_filter,save::BStage::crystal_filter};
constexpr save::BStage contact_dispatch_stages[]={save::BStage::flame_dispatch,save::BStage::crystal_dispatch};
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
struct ContactPhysics { uint32_t body=0,contents=0; bool valid=false,bounds_valid=false; float bounds[6]{}; };
ContactPhysics contact_physics(uintptr_t entity) {
    ContactPhysics result{};
    uintptr_t physics=0,table=0,physics_getter=0,body_getter=0,contents_getter=0,bounds_getter=0;
    uint32_t invalid=0;
    if (!read(entity,0,table) || !read(table,0xe78,physics_getter) || !physics_getter) return result;
    physics=reinterpret_cast<uintptr_t(*)(uintptr_t)>(physics_getter)(entity);
    if (!physics || !read(physics,0,table) ||
        !read(table,0x718,body_getter) || !body_getter ||
        !read(invalid_physics_body_address,0,invalid)) return result;
    const auto body=reinterpret_cast<uintptr_t(*)(uintptr_t)>(body_getter)(physics);
    if (!body || !read(body,0,result.body) || result.body==invalid) return result;
    result.valid=true;
    if (read(table,0x18,contents_getter) && contents_getter)
        result.contents=reinterpret_cast<uint32_t(*)(uintptr_t,int)>(contents_getter)(physics,-1);
    if (read(table,0x688,bounds_getter) && bounds_getter) {
        const auto bounds=reinterpret_cast<uintptr_t(*)(uintptr_t)>(bounds_getter)(physics);
        result.bounds_valid=bounds && read(bounds,0,result.bounds);
        for (unsigned i=0;i<3 && result.bounds_valid;++i)
            result.bounds_valid=std::isfinite(result.bounds[i]) && std::isfinite(result.bounds[i+3]) && result.bounds[i]<=result.bounds[i+3];
        if (!result.bounds_valid) for (auto& value:result.bounds) value=0;
    }
    return result;
}
void record_contact_physics(int index,uintptr_t entity,const ContactPhysics& p) {
    uint8_t flags=0,first_gate=0,once=0; uint32_t filter=0,clip_type=0;
    read(entity,0x2a2,flags); read(entity,0xb9e,first_gate); read(entity,0xb9c,once);
    read(entity,0xc30,filter); read(entity,0x420,clip_type);
    save::session().btrace.record(contact_physics_stages[index],save::BStatus::entered,"native_trigger_physics_observed",
        0,
        {{"id",contact_ids[index]},{"body",p.body},{"body_valid",p.valid},{"contents",p.contents},
         {"active",(flags>>7)!=0},{"first_gate",first_gate},{"once",once},{"filter_c30",filter},{"clip_type",clip_type},
         {"bounds_valid",p.bounds_valid},{"min_x_mm",std::llround(p.bounds[0]*1000)},{"min_y_mm",std::llround(p.bounds[1]*1000)},
         {"min_z_mm",std::llround(p.bounds[2]*1000)},{"max_x_mm",std::llround(p.bounds[3]*1000)},
         {"max_y_mm",std::llround(p.bounds[4]*1000)},{"max_z_mm",std::llround(p.bounds[5]*1000)}},entity);
}

bool trigger_filter(uintptr_t entity,uintptr_t other) {
    const auto result=original_trigger_filter(entity,other);
    __try {
        if (active() && entity_named(other,"player1")) {
            const int index=observed_contact(entity);
            if (index>=0) {
                save::session().btrace.record(contact_filter_stages[index],save::BStatus::entered,
                    "native_player_filter_returned",0,
                    {{"id",contact_ids[index]},{"accepted",result!=0},{"player",other},
                     {"caller_rva",reinterpret_cast<uintptr_t>(_ReturnAddress())-contact_image_base}},entity);
                record_contact_physics(index,entity,contact_physics(entity));
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::contact_observer,save::BStatus::blocked,
            "filter_physics_observation_fault",0,{},entity);
    }
    return result;
}
void observe_trigger_event(uintptr_t entity,uintptr_t other,int clip,bool start) {
    __try {
        if (!active()) return;
        const int index=observed_contact(entity); if (index<0) return;
        uint8_t flags=0,on_touch=0,no_start=0,first_gate=0;
        read(entity,0x2a2,flags); read(entity,0xb9f,on_touch);
        read(entity,0xba0,no_start); read(entity,0xb9e,first_gate);
        const auto stage=start ? (index==0 ? save::BStage::flame_start_touch : save::BStage::crystal_start_touch) : contact_touch_stages[index];
        save::session().btrace.record(stage,save::BStatus::entered,
            start ? "native_start_touch_entered" : "native_touch_entered",0,
            {{"id",contact_ids[index]},{"other",other},{"player",entity_named(other,"player1")},
             {"clip_model_id",clip},{"active",(flags>>7)!=0},{"on_touch",on_touch},
             {"no_start",no_start},{"first_gate",first_gate}},entity);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::contact_observer,save::BStatus::blocked,
            "touch_event_observation_fault",0,{},entity);
    }
}
uintptr_t start_touch(uintptr_t entity,uintptr_t out,uintptr_t other,int clip) {
    observe_trigger_event(entity,other,clip,true);
    return original_start_touch(entity,out,other,clip);
}
uintptr_t trigger_touch(uintptr_t entity,uintptr_t out,uintptr_t other,int clip) {
    observe_trigger_event(entity,other,clip,false);
    return original_trigger_touch(entity,out,other,clip);
}
bool trigger_gate(uintptr_t entity) {
    const auto result=original_trigger_gate(entity);
    __try {
        if (active()) {
            const int index=observed_contact(entity);
            if (index>=0) save::session().btrace.record(
                index==0 ? save::BStage::flame_gate : save::BStage::crystal_gate,
                save::BStatus::entered,"native_activation_gate_returned",0,
                {{"id",contact_ids[index]},{"accepted",result!=0}},entity);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}
void trigger_dispatch(uintptr_t entity,uintptr_t activator) {
    __try {
        if (active()) {
            const int index=observed_contact(entity);
            if (index>=0) {
                uintptr_t table=0,count_fn=0,target_fn=0; int count=-1; bool ap_target=false;
                if (read(entity,0,table) && read(table,0xca0,count_fn) && count_fn)
                    count=reinterpret_cast<int(*)(uintptr_t)>(count_fn)(entity);
                if (read(table,0xc90,target_fn) && target_fn)
                    for (int i=0;i<count;++i) {
                        const auto target=reinterpret_cast<uintptr_t(*)(uintptr_t,int)>(target_fn)(entity,i);
                        if (target && entity_named(target,index==0?"AP_CHECK_PICKUP_EQUIPMENT_FLAME_BELCH_1":"AP_CHECK_PROGRESS_ARGENT_CELL_1_1072112848")) ap_target=true;
                    }
                save::session().btrace.record(contact_dispatch_stages[index],save::BStatus::entered,
                    "native_target_dispatch_entered",0,
                    {{"id",contact_ids[index]},{"live_targets",count},{"ap_target_resolved",ap_target},{"activator",activator}},entity);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    original_trigger_dispatch(entity,activator);
}
