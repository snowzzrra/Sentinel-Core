"""Render the protocol projections from ordered schema fields."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "protocol/schema.json"
FORMAT_REVISION = 1
WIDTHS = {f"{sign}{bits}": bits // 8 for sign in "ui" for bits in (8, 16, 32, 64)}
CPP = {name: ("uint" if name[0] == "u" else "int") + name[1:] + "_t" for name in WIDTHS}


def unique(items, key):
    values = [item[key] for item in items]
    if len(values) != len(set(values)):
        raise ValueError(f"duplicate {key}")


def identifier(name):
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        raise ValueError(f"invalid identifier: {name}")


def scalar_bounds(field):
    bits = WIDTHS[field["type"]] * 8
    signed = field["type"][0] == "i"
    low = -(1 << (bits - 1)) if signed else 0
    high = (1 << (bits - signed)) - 1
    values = [field.get("min", low), field.get("max", high)]
    values += field.get("enum", [])
    values += [field[key] for key in ("const", "mask") if key in field]
    if any(not isinstance(value, int) or not low <= value <= high for value in values):
        raise ValueError(f"impossible numeric bounds: {field['name']}")
    if field.get("min", low) > field.get("max", high):
        raise ValueError(f"reversed bounds: {field['name']}")


def field_size(field, types, stack):
    kind = field["type"]
    if kind == "text":
        low, high = field.get("min", 0), field["max"]
        if not 0 <= low <= high <= 65535:
            raise ValueError("impossible text size")
        return high + (0 if "length_from" in field else 2)
    size = WIDTHS[kind] if kind in WIDTHS else type_size(kind, types, stack)
    count = field.get("count", field.get("max_count", 1))
    if not isinstance(count, int) or not 1 <= count <= 16384:
        raise ValueError("impossible array size")
    return size * count + (4 if "max_count" in field else 0)


def type_size(name, types, stack=()):
    if name in stack or name not in types:
        raise ValueError(f"recursive or unknown type: {name}")
    return sum(field_size(f, types, (*stack, name)) for f in types[name]["fields"])


def minimum_size(name, types):
    def size(field):
        if "max_count" in field:
            return 4
        kind = field["type"]
        if kind == "text":
            return field.get("min", 0) + (0 if "length_from" in field else 2)
        width = WIDTHS[kind] if kind in WIDTHS else minimum_size(kind, types)
        return width * field.get("count", 1)
    return sum(size(field) for field in types[name]["fields"])


def validate_fields(entry, types):
    unique(entry["fields"], "name")
    seen = {}
    for field in entry["fields"]:
        identifier(field["name"])
        if field["type"] in WIDTHS:
            scalar_bounds(field)
        if "length_from" in field and seen.get(field["length_from"]) not in WIDTHS:
            raise ValueError("length must reference a preceding integer")
        if "count" in field and "max_count" in field:
            raise ValueError("conflicting array sizes")
        field_size(field, types, (entry["name"],))
        seen[field["name"]] = field["type"]
    for constraint in entry.get("constraints", []):
        validate_constraint(constraint, entry, types)


def validate_constraint(constraint, entry, types):
    fields = {f["name"]: f for f in entry["fields"]}
    if constraint["kind"] == "checked_arithmetic":
        if any(fields[name]["type"] not in WIDTHS for name in constraint["fields"]):
            raise ValueError("arithmetic requires integer fields")
    elif constraint["kind"] == "uniqueness":
        field = fields[constraint["field"]]
        if "count" not in field and "max_count" not in field:
            raise ValueError("uniqueness requires an array")
        keys = {f["name"] for f in types[field["type"]]["fields"]}
        if not set(constraint["keys"]) <= keys:
            raise ValueError("unknown uniqueness key")
    else:
        raise ValueError("unsupported structural constraint")


def validate_command(command, types):
    identifier(command["name"])
    if not 1 <= command["id"] <= 65535:
        raise ValueError("invalid command id")
    revisions = command["payload_revisions"]
    if not revisions or len(revisions) != len(set(revisions)) or min(revisions) < 1 or max(revisions) > 65535:
        raise ValueError("duplicate or invalid revisions")
    unique(command["payload"]["variants"], "tag")
    unique(command["payload"]["variants"], "name")
    unique(command["revision_types"], "revision")
    if {r["revision"] for r in command["revision_types"]} != set(revisions):
        raise ValueError("revision type mapping mismatch")
    type_size(command["result"], types)
    for variant in command["payload"]["variants"]:
        identifier(variant["name"])
        scalar_bounds(dict(name="tag", type=command["payload"]["tag_type"], const=variant["tag"]))
        type_size(variant["type"], types)
        if minimum_size(variant["type"], types) + 4 + 32 > 16384:
            raise ValueError("impossible command payload size")


def variant_types(command, revision):
    mapping = next(r for r in command["revision_types"] if r["revision"] == revision)
    if len(mapping["payload_variants"]) != len(command["payload"]["variants"]):
        raise ValueError("variant type mapping mismatch")
    return [dict(v, type=t) for v, t in zip(command["payload"]["variants"], mapping["payload_variants"])]


def validate(schema):
    unique(schema["types"], "name")
    unique(schema["commands"], "id")
    unique(schema["commands"], "name")
    unique(schema["native_bindings"], "command")
    if {b["command"] for b in schema["native_bindings"]} != {c["name"] for c in schema["commands"]}:
        raise ValueError("native binding coverage mismatch")
    for binding in schema["native_bindings"]:
        if not re.fullmatch(r"(?:::sc_[a-z_]+|NativeRead|NativeCampaign)", binding["request"]):
            raise ValueError("invalid native request type")
        if not re.fullmatch(r"::sc_[a-z_]+", binding["result"]):
            raise ValueError("invalid native result type")
        if "transfer" in binding and binding["transfer"] not in {t["name"] for t in schema["v1_transfers"]}:
            raise ValueError("unknown native transfer")
    unique(schema["protocol"]["versions"], "version")
    unique(schema["protocol"]["operations"], "id")
    validate_headers(schema["protocol"])
    types = {entry["name"]: entry for entry in schema["types"]}
    for entry in types.values():
        identifier(entry["name"])
        validate_fields(entry, types)
        if minimum_size(entry["name"], types) > schema["protocol"]["max_frame"]:
            raise ValueError(f"impossible type size: {entry['name']}")
    for command in schema["commands"]:
        validate_command(command, types)
        for revision in command["payload_revisions"]:
            for variant in variant_types(command, revision):
                type_size(variant["type"], types)
    for layout in schema["v1_layouts"]:
        if minimum_size(layout["type"], types) + 16 > schema["protocol"]["max_frame"]:
            raise ValueError("impossible frame size")
    validate_v1_transfers(schema)


def validate_v1_transfers(schema):
    unique(schema["v1_transfers"], "name")
    for entry in schema["v1_transfers"]:
        identifier(entry["name"])
        unique(entry["fields"], "member")
        for field in entry["fields"]:
            if not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*", field["member"]):
                raise ValueError("invalid member path")
            if "count_member" in field:
                identifier(field["count_member"])
                if not 0 < field["max_count"] <= 16384:
                    raise ValueError("impossible array size")
            elif field["type"] != "text":
                field_size(dict(name=field["member"], **field), {}, ())
    operations = schema["compatibility"]["operations"]
    unique(operations, "opcode")
    if [op["opcode"] for op in operations] != schema["compatibility"]["v1_operations"]:
        raise ValueError("incomplete v1 operations")
    for op in operations:
        if op["family"] not in schema["compatibility"]["families"]:
            raise ValueError("unknown v1 family")


def validate_headers(protocol):
    if protocol["byte_order"] != "little":
        raise ValueError("unsupported byte order")
    unique(protocol["wire_errors"], "id")
    for version in protocol["versions"]:
        size = sum(WIDTHS[f["type"]] for f in version["fields"])
        if size != version["header_size"] or size >= protocol["max_frame"]:
            raise ValueError("impossible header size")


def with_headers(schema):
    expanded = json.loads(json.dumps(schema))
    protocol = expanded["protocol"]
    for version in protocol["versions"]:
        fields = json.loads(json.dumps(version["fields"]))
        for field in fields:
            if field["name"] == "magic":
                field["const"] = protocol["magic"]
            if field["name"] == "version":
                field["const"] = version["version"]
            if field["name"] == "payload_length":
                field["max"] = protocol["max_frame"] - version["header_size"]
            if field["name"] == "result":
                field["enum"] = [e["id"] for e in protocol["wire_errors"]]
        expanded["types"].append(dict(name=f"wire_header_{version['version']}", fields=fields))
    return expanded


def ordered_types(schema):
    types = {entry["name"]: entry for entry in schema["types"]}
    ordered = {}
    def visit(name):
        if name in ordered:
            return
        for field in types[name]["fields"]:
            if field["type"] in types:
                visit(field["type"])
        ordered[name] = types[name]
    for name in types:
        visit(name)
    return list(ordered.values())


def cpp_type(field):
    value = CPP.get(field["type"], "std::string" if field["type"] == "text" else field["type"])
    if "count" in field:
        return f"std::array<{value}, {field['count']}>"
    if "max_count" in field:
        return f"std::vector<{value}>"
    return value


def cpp_struct(entry):
    fields = [f"    {cpp_type(f)} {f['name']}{{}};" for f in entry["fields"]]
    return "\n".join([f"struct {entry['name']} {{", *fields, "};"])


def cpp_literal(value):
    return str(value) if value < 0 else str(value) + "ULL"


def cpp_constraints(field, value):
    checks = []
    for key, operator in (("min", ">="), ("max", "<="), ("const", "==")):
        if key in field:
            literal = str(field[key]) if field["type"].startswith("i") else cpp_literal(field[key])
            checks.append(f"{value} {operator} {literal}")
    if "mask" in field:
        checks.append(f"(uint64_t({value}) & ~{cpp_literal(field['mask'])}) == 0")
    if "enum" in field:
        checks.append("(" + " || ".join(f"{value} == {v}" for v in field["enum"]) + ")")
    return checks


def cpp_field(field):
    value = "v." + field["name"]
    if field["type"] == "text":
        length = "v." + field["length_from"] if "length_from" in field else "SIZE_MAX"
        return [f"c.text({value}, {field.get('min', 0)}, {field['max']}, {length})"]
    if "count" in field or "max_count" in field:
        call = f"c.sequence({value}" + (f", {field['max_count']}" if "max_count" in field else "") + ")"
        checks = cpp_constraints(field, "item")
        if checks:
            call += f" && std::all_of({value}.begin(), {value}.end(), [](auto item) {{ return " + " && ".join(checks) + "; })"
        if field.get("encoding", "").startswith("lower_hex"):
            optional = ", true" if field["encoding"].endswith("_or_empty") else ""
            call += f" && hex_namespace({value}{optional})"
        if field.get("nonzero"):
            call += f" && std::any_of({value}.begin(), {value}.end(), [](auto item) {{ return item != 0; }})"
        return [call]
    return [f"c.item({value})", *cpp_constraints(field, value)]


def cpp_codec(entry):
    checks = [part for field in entry["fields"] for part in cpp_field(field)]
    for constraint in entry.get("constraints", []):
        checks.extend(cpp_structural_constraint(constraint))
    return f"template<class C> bool transfer(C& c, {entry['name']}& v) {{\n    (void)c; (void)v;\n    return " + (" &&\n        ".join(checks) or "true") + ";\n}"


def cpp_structural_constraint(constraint):
    if constraint["kind"] == "checked_arithmetic":
        values = ", ".join("uint64_t(v." + name + ")" for name in constraint["fields"])
        return [f"sum_fits({cpp_literal(constraint['max'])}, {{{values}}})"]
    return [f"unique_by(v.{constraint['field']}, [](const auto& item) {{ return item.{key}; }})" for key in constraint["keys"]]


def cpp_unions(command, revision=None):
    name = command["name"] + "_payload" + (f"_revision{revision}" if revision else "")
    variants = variant_types(command, revision) if revision else command["payload"]["variants"]
    header = "using " + name + " = std::variant<" + ", ".join(v["type"] for v in variants) + ">;"
    lines = [f"template<class C> bool transfer(C& c, {name}& v) {{", "    uint32_t tag = 0;"]
    for i, variant in enumerate(variants):
        lines.append(f"    if (v.index() == {i}) tag = {variant['tag']};")
    lines += ["    if (!c.item(tag)) return false;", "    switch (tag) {"]
    for variant in variants:
        t = variant["type"]
        lines += [f"    case {variant['tag']}: {{", f"        {t} value{{}};", f"        if constexpr (!C::reading) value = std::get<{t}>(v);", "        if (!transfer(c, value)) return false;", "        v = std::move(value); return true;", "    }"]
    lines += ["    default: return false;", "    }", "}"]
    return header, "\n".join(lines)


CPP_RUNTIME = r'''
namespace {
template<class T, class Key> bool unique_by(const T& values, Key key) {
    for (size_t i = 0; i < values.size(); ++i)
        for (size_t j = 0; j < i; ++j) if (key(values[i]) == key(values[j])) return false;
    return true;
}
template<size_t N> bool hex_namespace(const std::array<uint8_t, N>& value, bool allow_empty = false) {
    if (allow_empty && std::all_of(value.begin(), value.end(), [](auto c) { return c == 0; })) return true;
    static_assert(N == 64 || N == 65);
    if constexpr (N == 65) { if (value[64]) return false; }
    return std::all_of(value.begin(), value.begin() + 64, [](auto c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool sum_fits(uint64_t maximum, std::initializer_list<uint64_t> values) {
    uint64_t sum = 0;
    for (const auto value : values) { if (value > maximum - sum) return false; sum += value; }
    return true;
}
template<class T> bool text_valid(const T& text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = p[i++];
        if (cp < 32 || cp == 127) return false;
        if (cp < 128) continue;
        unsigned n = cp >= 0xc2 && cp <= 0xdf ? 1 : cp >= 0xe0 && cp <= 0xef ? 2 : cp >= 0xf0 && cp <= 0xf4 ? 3 : 0;
        if (!n || i + n > text.size()) return false;
        cp &= (1u << (6 - n)) - 1;
        for (unsigned j = 0; j < n; ++j) {
            const auto b = p[i++]; if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3f);
        }
        if (cp < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f)) return false;
    }
    return true;
}
struct Writer {
    static constexpr bool reading = false;
    Bytes data;
    template<class T> bool item(T& value) {
        if constexpr (std::is_integral_v<T>) {
            auto n = static_cast<uint64_t>(value);
            if (data.size() + sizeof(T) > max_frame) return false;
            for (size_t i = 0; i < sizeof(T); ++i) { data.push_back(uint8_t(n)); n >>= 8; }
            return true;
        } else return transfer(*this, value);
    }
    template<class T, size_t N> bool sequence(std::array<T, N>& values) {
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    template<class T> bool sequence(std::vector<T>& values, size_t maximum) {
        if (values.size() > maximum) return false;
        auto count = static_cast<uint32_t>(values.size());
        if (!item(count)) return false;
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    bool text(std::string& value, size_t minimum, size_t maximum, size_t length) {
        if (value.size() < minimum || value.size() > maximum || !text_valid(value)) return false;
        if (length == SIZE_MAX) { auto count = static_cast<uint16_t>(value.size()); if (!item(count)) return false; }
        else if (length != value.size()) return false;
        if (data.size() + value.size() > max_frame) return false;
        data.insert(data.end(), value.begin(), value.end()); return true;
    }
};
struct Reader {
    static constexpr bool reading = true;
    const Bytes& data;
    size_t pos = 0;
    template<class T> bool item(T& value) {
        if constexpr (std::is_integral_v<T>) {
            if (sizeof(T) > data.size() - pos) return false;
            std::make_unsigned_t<T> n = 0;
            for (size_t i = 0; i < sizeof(T); ++i) n |= std::make_unsigned_t<T>(uint64_t(data[pos++]) << (i * 8));
            std::memcpy(&value, &n, sizeof(T)); return true;
        } else return transfer(*this, value);
    }
    template<class T, size_t N> bool sequence(std::array<T, N>& values) {
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    template<class T> bool sequence(std::vector<T>& values, size_t maximum) {
        uint32_t count = 0; if (!item(count) || count > maximum) return false;
        values.resize(count);
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    bool text(std::string& value, size_t minimum, size_t maximum, size_t length) {
        if (length == SIZE_MAX) { uint16_t count = 0; if (!item(count)) return false; length = count; }
        if (length < minimum || length > maximum || length > data.size() - pos) return false;
        value.assign(reinterpret_cast<const char*>(data.data() + pos), length); pos += length;
        return text_valid(value);
    }
};
}
'''


def cpp_api(name):
    return [f"bool encode(const {name}&, Bytes&);", f"bool decode(const Bytes&, {name}&);"]


def cpp_api_body(name):
    return f'''bool encode(const {name}& source, Bytes& out) {{
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}}
bool decode(const Bytes& in, {name}& out) {{
    if (in.size() > max_frame) return false;
    Reader c{{in}}; {name} value{{}};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}}'''


def cpp_frame_api(layout):
    name, t = layout["name"], layout["type"]
    return [f"bool encode_{name}(const {t}&, Bytes&, uint32_t result = 0);", f"bool decode_{name}(const Bytes&, {t}&, uint32_t& result);"]


def cpp_frame_body(layout, schema):
    name, t, op = layout["name"], layout["type"], layout["operation"]
    response = str(layout["response"]).lower()
    magic = schema["protocol"]["magic"]
    max_error = max(e["id"] for e in schema["protocol"]["wire_errors"])
    return f'''bool encode_{name}(const {t}& value, Bytes& out, uint32_t result) {{
    if (result > {max_error} || (!{response} && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{{}}; header.magic = {magic}; header.version = 1;
    header.operation = {op}; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}}
bool decode_{name}(const Bytes& in, {t}& out, uint32_t& result) {{
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{{in}}; wire_header_1 header{{}};
    if (!transfer(c, header)) return false;
    if (header.operation != {op} || header.payload_length != in.size() - header_size_1 || (!{response} && header.result)) return false;
    if (header.result) {{ if (header.payload_length) return false; out = {{}}; result = header.result; return true; }}
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}}'''


def cpp_constants(schema):
    protocol = schema["protocol"]
    lines = [f"inline constexpr uint32_t wire_magic = {protocol['magic']};"]
    for version in protocol["versions"]:
        lines.append(f"inline constexpr size_t header_size_{version['version']} = {version['header_size']};")
    for name, entries, key in (("Operation", protocol["operations"], "id"), ("WireError", protocol["wire_errors"], "id"), ("CommandId", schema["commands"], "id")):
        lines.append(f"enum class {name} : uint16_t {{ " + ", ".join(f"{e['name']} = {e[key]}" for e in entries) + " };")
    for name, value in schema["limits"].items():
        if isinstance(value, int):
            lines.append(f"inline constexpr uint64_t limit_{name} = {cpp_literal(value)};")
        elif isinstance(value, list):
            lines.append(f"inline constexpr std::array<int64_t, {len(value)}> limit_{name} = {{" + ", ".join(map(str, value)) + "};")
        else:
            lines.append(f"inline constexpr char limit_{name}[] = {json.dumps(value)};")
    for name in ("execution_slots", "full_result_ms", "identity_records"):
        lines.append(f"inline constexpr size_t {name} = {schema['retention'][name]};")
    return lines


def cpp_v1_transfers(schema):
    lines = []
    for entry in schema.get("v1_transfers", []):
        lines.append(f"template<class C, class T> void v1_transfer_{entry['name']}(C& c, T& v) {{")
        for field in entry["fields"]:
            value = "v." + field["member"]
            if "count_member" in field:
                count = "v." + field["count_member"]
                lines.append(f"    if ({count} > {field['max_count']}) {{ c.valid = false; return; }}")
                item = f"c.text({value}[i])" if field["type"] == "text" else f"c.scalar({value}[i], {WIDTHS[field['type']]})"
                lines.append(f"    for (size_t i = 0; i < {count}; ++i) {item};")
            elif "count" in field:
                lines.append(f"    for (size_t i = 0; i < {field['count']}; ++i) c.scalar({value}[i], {WIDTHS[field['type']]});")
            elif field["type"] == "text":
                lines.append(f"    c.text({value});")
            else:
                lines.append(f"    c.scalar({value}, {WIDTHS[field['type']]});")
        lines.append("}")
    return lines


def cpp_v1_descriptors(schema):
    compatibility = schema["compatibility"]
    families = compatibility["families"]
    lines = ["enum class V1Family { " + ", ".join(families) + " };",
             "struct V1Descriptor { uint16_t version, opcode; uint64_t capability; size_t payload; V1Family family; bool subset, envelope, allow_zero_generation; uint32_t input_abi; };",
             "inline constexpr V1Descriptor v1_descriptors[] = {"]
    for op in compatibility["operations"]:
        payload = op["payload_bytes"] if isinstance(op["payload_bytes"], int) else 0
        flags = [op["capability_rule"] == "subset", op["envelope"], op["allow_zero_generation"]]
        values = ", ".join(str(flag).lower() for flag in flags)
        lines.append(f"    {{1, {op['opcode']}, {op['capability']}ULL, {payload}, V1Family::{op['family']}, {values}, {op['input_abi']}}},")
    return lines + ["};"]


def cpp_native_variants(schema):
    bindings = schema["native_bindings"]
    requests = list(dict.fromkeys(b["request"] for b in bindings))
    results = list(dict.fromkeys(b["result"] for b in bindings))
    lines = ["struct NativeRead { uint64_t cursor = 0; };",
            "struct NativeCampaign { ::sc_campaign_request request{}; ::sc_campaign_summary summary{}; ::sc_campaign_rewards rewards{}; };",
            "using NativeRequest = std::variant<std::monostate, " + ", ".join(requests) + ">;",
            "using NativeResult = std::variant<std::monostate, " + ", ".join(results) + ">;",
            "template<class T> struct NativeBinding;"]
    seen = set()
    for b in bindings:
        if b["request"] in seen:
            continue
        seen.add(b["request"])
        lines.append(f"template<> struct NativeBinding<{b['request']}> {{ using Result = {b['result']}; static constexpr CommandId id = CommandId::{b['command']}; }};")
        if "transfer" in b:
            fields = next(t["fields"] for t in schema["v1_transfers"] if t["name"] == b["transfer"])
            lines.append(f"template<class C> void native_transfer(C& c, const {b['request']}& v) {{")
            prefix = "v.request." if b["request"] == "NativeCampaign" else "v."
            for f in fields:
                if f["member"] == "namespace_id":
                    continue
                value = prefix + f["member"]
                if "count" in f:
                    lines.append(f"    for (size_t i = 0; i < {f['count']}; ++i) c.scalar({value}[i], {WIDTHS[f['type']]});")
                elif f["type"] == "text":
                    lines.append(f"    c.text({value});")
                else:
                    lines.append(f"    c.scalar({value}, {WIDTHS[f['type']]});")
            if b["request"] == "NativeCampaign":
                lines += ["    c.scalar(v.request.reward_presentation, 4);",
                          "    v1_transfer_campaign_summary(c, v.summary);",
                          "    for (const auto& challenge : v.summary.challenges) v1_transfer_campaign_challenge(c, challenge);",
                          "    v1_transfer_campaign_rewards_header(c, v.rewards);",
                          "    if (v.rewards.count > std::size(v.rewards.entries)) { c.valid = false; return; }",
                          "    for (size_t i = 0; i < v.rewards.count; ++i) v1_transfer_campaign_reward(c, v.rewards.entries[i]);"]
            lines.append("}")
            lines.append(f"inline const char* native_namespace(const {b['request']}& v) {{ return {prefix}namespace_id; }}")
    return lines


def cpp_registry(schema):
    payloads, results = [], []
    for command in schema["commands"]:
        for revision in command["payload_revisions"]:
            suffix = f"_revision{revision}" if variant_types(command, revision) != command["payload"]["variants"] else ""
            payloads.append(command["name"] + "_payload" + suffix)
        results.append(command["result"])
    header = ["using Payload = std::variant<" + ", ".join(dict.fromkeys(payloads)) + ">;",
              "using Result = std::variant<" + ", ".join(dict.fromkeys(results)) + ">;",
              "enum class Admission { P, G, S, D };",
              "enum class Scope { process, gameplay, save, gameplay_diagnostic };",
              "enum class NamespaceRule { process_zero, admitted_save, none };",
              "enum class GenerationRule { none, exact_nonzero, exact_allow_zero };",
              "enum class BindingRule { none, exact };",
              "struct AdmissionPolicy { Scope scope; NamespaceRule namespace_rule; GenerationRule generation; BindingRule save_binding; };",
              "inline constexpr AdmissionPolicy admission_policies[] = {"]
    for name in ("P", "G", "S", "D"):
        policy = schema["admission"][name]
        header.append(f"    {{Scope::{policy['scope']}, NamespaceRule::{policy['namespace']}, GenerationRule::{policy['generation']}, BindingRule::{policy['save_binding']}}},")
    header += ["};",
              "enum class Execution { snapshot, publication, native_frame, native_frame_async };",
              "enum class Evidence { feature_result, accepted_publication, dispatch_only };",
              "enum class Ack { none, terminal_consumed };",
              "struct VariantPolicy { uint32_t tag; Operation operation; Admission admission; Execution execution; const char* effect; const char* availability; };",
              "struct Descriptor { uint16_t id, revision; const char* name; const char* metadata; Admission admission; Execution execution; Evidence evidence; Ack ack; const VariantPolicy* variants; size_t variant_count; bool (*decode_payload)(const Bytes&, Payload&); bool (*encode_payload)(const Payload&, Bytes&); bool (*decode_result)(const Bytes&, Result&); bool (*encode_result)(const Result&, Bytes&); bool (*native_matches)(const NativeRequest&); };",
              "extern const Descriptor descriptors[];", "extern const size_t descriptor_count;",
              "template<class Factory> auto registry_handlers(Factory factory) { return std::array{"]
    header += [f"    factory.template entry<CommandId::{c['name']}>()," for c in schema["commands"]]
    header += ["}; }"]
    source = ["namespace {", "template<class T> bool native_matches(const NativeRequest& request) { return std::holds_alternative<T>(request); }",
              "template<class T, class V> bool decode_variant(const Bytes& bytes, V& out) { T value{}; if (!decode(bytes, value)) return false; out = std::move(value); return true; }",
              "template<class T, class V> bool encode_variant(const V& value, Bytes& out) { const auto* typed = std::get_if<T>(&value); return typed && encode(*typed, out); }"]
    for c in schema["commands"]:
        source.append(f"const VariantPolicy policies_{c['name']}[] = {{")
        for v in c["payload"]["variants"]:
            source.append(f'    {{{v["tag"]}, Operation::{v["operation"]}, Admission::{v["admission"]}, Execution::{v["execution"]}, "{v["effect"]}", "{v["availability"]}"}},')
        source.append("};")
    source += ["}", "const Descriptor descriptors[] = {"]
    for c in schema["commands"]:
        metadata = json.dumps(json.dumps(c, separators=(",", ":")))
        for revision in c["payload_revisions"]:
            suffix = f"_revision{revision}" if variant_types(c, revision) != c["payload"]["variants"] else ""
            payload = c["name"] + "_payload" + suffix
            native = next(b["request"] for b in schema["native_bindings"] if b["command"] == c["name"])
            source.append(f'    {{{c["id"]}, {revision}, "{c["name"]}", {metadata}, Admission::{c["admission"]}, Execution::{c["execution"]}, Evidence::{c["evidence"]}, Ack::{c["ack"]}, policies_{c["name"]}, std::size(policies_{c["name"]}), &decode_variant<{payload}, Payload>, &encode_variant<{payload}, Payload>, &decode_variant<{c["result"]}, Result>, &encode_variant<{c["result"]}, Result>, &native_matches<{native}>}},')
    source += ["};", "const size_t descriptor_count = std::size(descriptors);"]
    return header, source


def cpp_native_projections(schema):
    lines = []
    for item in ordered_types(schema):
        name = item["name"]
        if not name.startswith("sc_"):
            continue
        directions = ("to_wire", "from_wire") if name in {
            "sc_native_scope", "sc_diagnostic_request", "sc_combat_presentation_request",
            "sc_campaign_row", "sc_physical_challenge", "sc_campaign_summary",
        } else ("to_wire",)
        for direction in directions:
            source, target = (f"::{name}", name) if direction == "to_wire" else (name, f"::{name}")
            lines += [f"inline {target} {direction}(const {source}& v) {{", f"    {target} out{{}};"]
            for field in item["fields"]:
                member = field["name"]
                value = f"{direction}(v.{member})" if field["type"].startswith("sc_") else f"v.{member}"
                if "count" in field:
                    value = f"{direction}(v.{member}[i])" if field["type"].startswith("sc_") else f"v.{member}[i]"
                    lines.append(f"    for (size_t i = 0; i < {field['count']}; ++i) out.{member}[i] = {value};")
                else:
                    lines.append(f"    out.{member} = {value};")
            lines += ["    return out;", "}"]
    return lines


def render_cpp(schema, notice):
    types = ordered_types(schema)
    unions = [cpp_unions(c) for c in schema["commands"]]
    names = [t["name"] for t in types] + [c["name"] + "_payload" for c in schema["commands"]]
    for command in schema["commands"]:
        for revision in command["payload_revisions"]:
            if variant_types(command, revision) != command["payload"]["variants"]:
                unions.append(cpp_unions(command, revision))
                names.append(command["name"] + f"_payload_revision{revision}")
    header = [notice, "#pragma once", '#include "sentinel_inspection.h"', "#include <array>\n#include <cstdint>\n#include <string>\n#include <variant>\n#include <vector>", "namespace sentinel::generated {", "using Bytes = std::vector<uint8_t>;", f"inline constexpr size_t max_frame = {schema['protocol']['max_frame']};"]
    header += cpp_constants(schema) + [cpp_struct(t) for t in types] + [u[0] for u in unions]
    header += [line for name in names for line in cpp_api(name)]
    header += [line for layout in schema["v1_layouts"] for line in cpp_frame_api(layout)]
    header += cpp_v1_transfers(schema) + cpp_v1_descriptors(schema)
    registry_header, registry_source = cpp_registry(schema)
    header += cpp_native_projections(schema) + cpp_native_variants(schema) + registry_header + ["}"]
    forward = [f"template<class C> bool transfer(C&, {name}&);" for name in names]
    source = [notice, '#include "sentinel_protocol_generated.h"', "#include <algorithm>\n#include <cstring>\n#include <type_traits>\n#include <utility>", "namespace sentinel::generated {", "namespace {", *forward, "}", CPP_RUNTIME, "namespace {", *[cpp_codec(t) for t in types], *[u[1] for u in unions], "}"]
    source += [cpp_api_body(n) for n in names]
    source += [cpp_frame_body(layout, schema) for layout in schema["v1_layouts"]]
    source += registry_source + ["}"]
    return "\n".join(header) + "\n", "\n".join(source) + "\n"


PY_RUNTIME = r'''
TYPES = {entry["name"]: entry for entry in SCHEMA["types"]}
COMMANDS = {entry["id"]: entry for entry in SCHEMA["commands"]}
LAYOUTS = {entry["name"]: entry for entry in SCHEMA["v1_layouts"]}
WIDTHS = {f"{sign}{bits}": bits // 8 for sign in "ui" for bits in (8, 16, 32, 64)}


def _number(value, field):
    if not isinstance(value, int):
        raise ValueError("integer required")
    if value < field.get("min", value) or value > field.get("max", value):
        raise ValueError("out of range")
    if "const" in field and value != field["const"]:
        raise ValueError("reserved value")
    if "enum" in field and value not in field["enum"]:
        raise ValueError("unknown enum")
    if "mask" in field and value & ~field["mask"]:
        raise ValueError("unknown mask bits")
    return value


def _text(value, field):
    data = value.encode("utf-8")
    if not field.get("min", 0) <= len(data) <= field["max"]:
        raise ValueError("text size")
    if any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in value):
        raise ValueError("control character")
    return data


def _encode_item(field, value, context):
    kind = field["type"]
    if kind in WIDTHS:
        return _number(value, field).to_bytes(WIDTHS[kind], "little", signed=kind[0] == "i")
    if kind == "text":
        data = _text(value, field)
        if "length_from" in field:
            if len(data) != context[field["length_from"]]:
                raise ValueError("text length mismatch")
            return data
        return len(data).to_bytes(2, "little") + data
    return encode(kind, value)


def _encode_field(field, value, context):
    if "count" not in field and "max_count" not in field:
        return _encode_item(field, value, context)
    maximum = field.get("count", field.get("max_count"))
    if len(value) > maximum or ("count" in field and len(value) != maximum):
        raise ValueError("array size")
    _array_constraints(field, value)
    prefix = len(value).to_bytes(4, "little") if "max_count" in field else b""
    return prefix + b"".join(_encode_item(field, item, context) for item in value)


def encode(type_name, value):
    fields = TYPES[type_name]["fields"]
    if set(value) != {f["name"] for f in fields}:
        raise ValueError("unexpected or missing field")
    _constraints(type_name, value)
    data = b"".join(_encode_field(f, value[f["name"]], value) for f in fields)
    if len(data) > SCHEMA["protocol"]["max_frame"]:
        raise ValueError("oversized payload")
    return data


def _take(data, pos, count):
    end = pos + count
    if end > len(data):
        raise ValueError("truncated payload")
    return data[pos:end], end


def _decode_item(field, data, pos, context):
    kind = field["type"]
    if kind in WIDTHS:
        raw, pos = _take(data, pos, WIDTHS[kind])
        return _number(int.from_bytes(raw, "little", signed=kind[0] == "i"), field), pos
    if kind != "text":
        return _decode_type(kind, data, pos)
    length = context.get(field.get("length_from"))
    if length is None:
        raw, pos = _take(data, pos, 2)
        length = int.from_bytes(raw, "little")
    raw, pos = _take(data, pos, length)
    text = raw.decode("utf-8")
    _text(text, field)
    return text, pos


def _decode_field(field, data, pos, context):
    if "count" not in field and "max_count" not in field:
        return _decode_item(field, data, pos, context)
    count = field.get("count")
    if count is None:
        raw, pos = _take(data, pos, 4)
        count = int.from_bytes(raw, "little")
        if count > field["max_count"]:
            raise ValueError("array size")
    values = []
    for _ in range(count):
        value, pos = _decode_item(field, data, pos, context)
        values.append(value)
    _array_constraints(field, values)
    return values, pos


def _array_constraints(field, values):
    if field.get("nonzero") and not any(values):
        raise ValueError("zero identity")
    if field.get("encoding", "").startswith("lower_hex"):
        if field["encoding"].endswith("_or_empty") and not any(values):
            return
        if len(values) not in (64, 65) or any(v not in b"0123456789abcdef" for v in values[:64]):
            raise ValueError("namespace encoding")
        if len(values) == 65 and values[64]:
            raise ValueError("namespace terminator")


def _decode_type(type_name, data, pos):
    value = {}
    for field in TYPES[type_name]["fields"]:
        value[field["name"]], pos = _decode_field(field, data, pos, value)
    _constraints(type_name, value)
    return value, pos


def _constraints(type_name, value):
    for constraint in TYPES[type_name].get("constraints", []):
        if constraint["kind"] == "checked_arithmetic":
            if sum(value[name] for name in constraint["fields"]) > constraint["max"]:
                raise ValueError("checked arithmetic overflow")
        else:
            _unique(value[constraint["field"]], constraint["keys"])


def _unique(values, keys):
    for key in keys:
        items = [tuple(v[key]) if isinstance(v[key], list) else v[key] for v in values]
        if len(set(items)) != len(items):
            raise ValueError("duplicate array key")


def decode(type_name, data):
    if len(data) > SCHEMA["protocol"]["max_frame"]:
        raise ValueError("oversized payload")
    value, pos = _decode_type(type_name, data, 0)
    if pos != len(data):
        raise ValueError("trailing payload")
    return value


def encode_variant(command_id, revision, tag, value):
    command = COMMANDS[command_id]
    if revision not in command["payload_revisions"]:
        raise ValueError("unsupported revision")
    variant = next((v for v in command["payload"]["variants"] if v["tag"] == tag), None)
    if variant is None:
        raise ValueError("unknown tag")
    return tag.to_bytes(4, "little") + encode(_variant_type(command, revision, variant), value)


def decode_variant(command_id, revision, data):
    if len(data) < 4:
        raise ValueError("missing tag")
    tag = int.from_bytes(data[:4], "little")
    command = COMMANDS[command_id]
    variant = next((v for v in command["payload"]["variants"] if v["tag"] == tag), None)
    if revision not in command["payload_revisions"] or variant is None:
        raise ValueError("unsupported revision or tag")
    return tag, decode(_variant_type(command, revision, variant), data[4:])


def _variant_type(command, revision, variant):
    mapping = next(r for r in command["revision_types"] if r["revision"] == revision)
    index = command["payload"]["variants"].index(variant)
    return mapping["payload_variants"][index]


def encode_frame(layout_name, value, result=0):
    layout = LAYOUTS[layout_name]
    if result not in {e["id"] for e in SCHEMA["protocol"]["wire_errors"]} or (result and not layout["response"]):
        raise ValueError("invalid result")
    body = b"" if result else encode(layout["type"], value)
    header = encode("wire_header_1", dict(magic=SCHEMA["protocol"]["magic"], version=1, operation=layout["operation"], payload_length=len(body), result=result))
    if len(header) + len(body) > SCHEMA["protocol"]["max_frame"]:
        raise ValueError("oversized frame")
    return header + body


def decode_frame(layout_name, data):
    layout = LAYOUTS[layout_name]
    if not 16 <= len(data) <= SCHEMA["protocol"]["max_frame"]:
        raise ValueError("frame size")
    header, pos = _decode_type("wire_header_1", data, 0)
    length, result = header["payload_length"], header["result"]
    if header["operation"] != layout["operation"] or length != len(data) - pos:
        raise ValueError("frame header")
    if result and (not layout["response"] or length):
        raise ValueError("invalid result")
    return ({} if result else decode(layout["type"], data[pos:])), result
'''


def format_schema(value, depth=0):
    indent = "  " * depth
    if isinstance(value, dict) and any(isinstance(v, dict) and v or
                                     isinstance(v, list) and any(isinstance(item, (dict, list)) for item in v)
                                     for v in value.values()):
        entries = ["  " + indent + json.dumps(k) + ": " + format_schema(v, depth + 1) for k, v in value.items()]
        return "{\n" + ",\n".join(entries) + "\n" + indent + "}"
    if isinstance(value, list) and any(isinstance(v, (dict, list)) for v in value):
        return "[\n" + ",\n".join("  " + indent + format_schema(v, depth + 1) for v in value) + "\n" + indent + "]"
    return json.dumps(value, ensure_ascii=False)


def projection_schema(schema):
    # Keep the published projection metadata order independent of the source's v1 section.
    v1 = schema["v1"]
    before = {"types": "compatibility", "constraint_vocabulary": "v1_layouts", "native_bindings": "v1_transfers"}
    projection = {}
    for name, value in schema.items():
        if name in before:
            projection[before[name]] = v1[before[name]]
        if name != "v1":
            projection[name] = value
    return projection


def render(schema_bytes):
    schema = json.loads(schema_bytes)
    digest = hashlib.sha256(json.dumps(schema, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    schema = projection_schema(schema)
    validate(schema)
    schema = with_headers(schema)
    notice = f"Generated from protocol/schema.json; regenerate: python -B tools/generate_protocol.py; schema-sha256: {digest}; generator-format: {FORMAT_REVISION}. DO NOT EDIT."
    header, source = render_cpp(schema, "// " + notice)
    python = "# " + notice + "\nimport json\n\nSCHEMA = json.loads(" + repr(json.dumps(schema, separators=(",", ":"))) + ")\n" + PY_RUNTIME
    return {"include/sentinel_protocol_generated.h": header.encode(), "src/protocol_generated.cpp": source.encode(), "protocol/generated/sentinel_protocol.py": python.encode()}


def project(outputs, root, check):
    drift = []
    for relative, data in outputs.items():
        path = root / relative
        if check:
            if not path.is_file() or path.read_bytes() != data:
                drift.append(relative)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    return drift


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--format-schema", action="store_true", help="Write the canonical schema layout")
    parser.add_argument("--mod-root", type=Path)
    parser.add_argument("--output-root", type=Path, default=ROOT, help="Core projection root (also used by CMake)")
    args = parser.parse_args(argv)
    try:
        source = SCHEMA.read_bytes()
        formatted = (format_schema(json.loads(source)) + "\n").encode()
        if args.format_schema and not args.check:
            SCHEMA.write_bytes(formatted)
            source = formatted
        outputs = render(source)
        drift = project(outputs, args.output_root, args.check)
        if args.check and source != formatted:
            drift.append("protocol/schema.json (run --format-schema)")
        if args.mod_root:
            drift += project({"doom_eap/runtime/sentinel_protocol_generated.py": outputs["protocol/generated/sentinel_protocol.py"]}, args.mod_root, args.check)
        if drift:
            print("Generated protocol drift: " + ", ".join(drift), file=sys.stderr)
            return 1
    except (OSError, ValueError, KeyError) as error:
        print(f"Protocol generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
