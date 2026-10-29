"""Measure C++ function complexity in src/ and tests/ and check saved ceilings.
This textual estimate ranks functions; it does not replace a C++ parser.
"""
import argparse
from bisect import bisect_right
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
EXCEPTIONS = ROOT / "tools/complexity_exceptions.txt"
HEADER = "# path::qualified_name(signature) cognitive cyclomatic\n"
LIMITS = {"cognitive": 25, "cyclomatic": 15}
LEX = re.compile(
    r'(?P<skip>//[^\n]*|/\*.*?\*/|^[ \t]*\#(?:[^\n\\]|\\.)*(?:\\\n(?:[^\n\\]|\\.)*)*)'
    r'|(?P<literal>(?:u8|[uUL])?R"(?P<raw>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=raw)"'
    r'|(?:u8|[uUL])?"(?:\\.|[^"\\])*"|(?:u8|[uUL])?\'(?:\\.|[^\'\\])*\')'
    r'|[A-Za-z_]\w*|\d+(?:[\w.]*\w)?|::|&&|\|\||->|[=!<>]=|\+\+|--|[^\s]',
    re.DOTALL | re.MULTILINE,
)
NAME = re.compile(r"(?:[A-Za-z_]\w*::)*(?:operator(?:\s+\w+|\s*[^\w]+)|~?[A-Za-z_]\w*)$")
CONTROLS = {"if", "for", "while", "switch", "catch"}
NOT_FUNCTIONS = CONTROLS | {"sizeof", "alignas", "decltype", "noexcept", "__declspec", "static_assert"}


def tokenize(source):
    tokens, offsets = [], []
    for match in LEX.finditer(source):
        if match.lastgroup == "skip":
            continue
        tokens.append('"literal"' if match.group("literal") else match.group())
        offsets.append(match.start())
    return tokens, offsets


def pairs_for(tokens):
    pairs, stack = {}, []
    for index, token in enumerate(tokens):
        if token in ("(", "[", "{"):
            stack.append(index)
        elif token in (")", "]", "}"):
            if not stack or tokens[stack[-1]] != {")": "(", "]": "[", "}": "{"}[token]:
                raise ValueError("unbalanced delimiters (conditional compilation may need a parser)")
            opening = stack.pop()
            pairs[opening] = index
            pairs[index] = opening
    if stack:
        raise ValueError("unclosed delimiter")
    return pairs


def scope_name(header):
    text = " ".join(without_template(header))
    namespace = re.fullmatch(r"(?:inline )?namespace(?: ([\w :]+))?", text)
    if namespace:
        return (namespace[1] or "(anonymous)").replace(" ", "")
    record = re.match(r"(?:class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)", text)
    if record:
        return record[1]
    return None


def without_template(header):
    if header[:2] != ["template", "<"]:
        return header
    depth = 0
    for index, token in enumerate(header[1:], 1):
        depth += int(token == "<") - int(token == ">")
        if depth == 0:
            return header[index + 1:]
    return header


def function_name(tokens, start, opening, pairs):
    index = start
    while index < opening:
        if tokens[index] == "(":
            prefix = " ".join(tokens[start:index]).replace(" :: ", "::").replace("~ ", "~")
            match = NAME.search(prefix)
            if match and match[0] not in NOT_FUNCTIONS and declaration_prefix(prefix[:match.start()]):
                if match[0] != "operator":
                    return match[0], pairs[index]
            index = pairs[index]
        index += 1
    return None


def declaration_prefix(prefix):
    return not any(token in prefix.split() for token in ("=", "return", "new", "typedef", "using"))


def qualified_name(scope, name):
    enclosing = "::".join(scope)
    if not enclosing or name.startswith(enclosing + "::"):
        return name
    return enclosing + "::" + name


def definitions(tokens, pairs, start=0, end=None, scope=()):
    end = len(tokens) if end is None else end
    index = start
    while index < end:
        token = tokens[index]
        if token in ("(", "["):
            index = pairs[index] + 1
            continue
        if token == "{":
            close = pairs[index]
            nested = scope_name(tokens[start:index])
            found = list(block_definitions(tokens, pairs, start, index, scope))
            yield from found
            index = close + 1
            if found or nested is not None:
                start = index
            continue
        if token == ";" or (token == ":" and tokens[index - 1] in ("public", "private", "protected")):
            start = index + 1
        index += 1


def block_definitions(tokens, pairs, start, opening, scope):
    header, close = tokens[start:opening], pairs[opening]
    nested = scope_name(header)
    if nested is not None:
        yield from definitions(tokens, pairs, opening + 1, close, (*scope, nested))
        return
    function = function_name(tokens, start, opening, pairs)
    if function:
        name, parameters_end = function
        signature_end = initializer_start(tokens, pairs, parameters_end + 1, opening)
        if signature_end < opening and tokens[opening - 1] not in (")", "}"):
            return
        parameters = " ".join(tokens[pairs[parameters_end] + 1:parameters_end])
        suffix = " ".join(tokens[parameters_end + 1:signature_end])
        signature = f"({parameters})" + (" " + suffix if suffix else "")
        yield qualified_name(scope, name), signature, opening, close
    elif header == ["extern", '"literal"']:
        yield from definitions(tokens, pairs, opening + 1, close, scope)


def initializer_start(tokens, pairs, index, end):
    while index < end:
        if tokens[index] == ":":
            return index
        index = pairs[index] + 1 if tokens[index] in ("(", "[", "{") else index + 1
    return end


def condition_end(tokens, pairs, index):
    index += 1
    if tokens[index] == "constexpr":
        index += 1
    return pairs[index] + 1


def statement_end(tokens, pairs, index):
    if tokens[index] == "{":
        return pairs[index] + 1
    if tokens[index] == "do":
        tail = statement_end(tokens, pairs, index + 1)
        return pairs[tail + 1] + 2
    if tokens[index] in CONTROLS:
        end = statement_end(tokens, pairs, condition_end(tokens, pairs, index))
        if tokens[index] == "if" and end < len(tokens) and tokens[end] == "else":
            return statement_end(tokens, pairs, end + 1)
        return end
    while index < len(tokens) and tokens[index] not in (";", "}"):
        index = pairs[index] + 1 if tokens[index] in ("(", "[", "{") else index + 1
    return index + 1


def expression_end(tokens, pairs, index, end):
    while index < end and tokens[index] not in (";", ",", ")", "]", "}"):
        index = pairs[index] + 1 if tokens[index] in ("(", "[", "{") else index + 1
    return index


def decision(tokens, pairs, index, end, do_tails):
    token = tokens[index]
    if index in do_tails:
        return index, 0, 0
    if token in CONTROLS:
        return statement_end(tokens, pairs, condition_end(tokens, pairs, index)), 1, 1
    if token == "?":
        return expression_end(tokens, pairs, index + 1, end), 1, 1
    if token == "else":
        return statement_end(tokens, pairs, index + 1), int(tokens[index + 1] != "if"), 0
    if token == "do":
        return statement_end(tokens, pairs, index + 1), 1, 1
    return index, 0, 0


def lambda_end(tokens, pairs, index):
    if tokens[index] != "[":
        return index
    if re.fullmatch(r"\w+", tokens[index - 1]) and tokens[index - 1] != "return":
        return index
    tail = pairs[index] + 1
    if tail < len(tokens) and tokens[tail] == "(":
        tail = pairs[tail] + 1
    tail = lambda_suffix_end(tokens, pairs, tail)
    if tail < len(tokens) and tokens[tail] == "{":
        return pairs[tail]
    return index


def lambda_suffix_end(tokens, pairs, tail):
    while tail < len(tokens) and tokens[tail] in ("mutable", "noexcept", "constexpr", "->"):
        if tokens[tail] == "->":
            return next((index for index in range(tail + 1, len(tokens)) if tokens[index] in ("{", ";")), len(tokens))
        tail += 1
        if tokens[tail] == "(":
            tail = pairs[tail] + 1
    return tail


def flow_increment(tokens, index, increment, depth):
    if tokens[index] == "if" and tokens[index - 1] == "else":
        return increment
    return increment * (1 + depth)


def logical_increment(token, previous):
    if token in ("&&", "||"):
        return int(previous != token), token
    return 0, None if token in (";", "{", "}", "?", ":") else previous


def recursive_call(tokens, opening, close, name):
    short_name = name.split("::")[-1]
    return any(tokens[index] == short_name and tokens[index + 1] == "(" for index in range(opening + 1, close))


def complexity(tokens, pairs, opening, close, name):
    cognitive, cyclomatic, nesting = 0, 1, 0
    spans, logical = [], None
    do_tails = {statement_end(tokens, pairs, index + 1) for index in range(opening + 1, close) if tokens[index] == "do"}
    for index in range(opening + 1, close):
        token = tokens[index]
        spans = [(end, weight) for end, weight in spans if end > index]
        depth = sum(weight for _, weight in spans)
        end, increment, weight = decision(tokens, pairs, index, close, do_tails)
        cognitive += flow_increment(tokens, index, increment, depth) if weight else increment
        if increment:
            spans.append((end, weight))
            nesting = max(nesting, depth + weight)
        cyclomatic += int(token in {"if", "for", "while", "catch", "case", "?", "&&", "||"})
        extra, logical = logical_increment(token, logical)
        cognitive += extra
        cognitive += int(token == "goto")
        end = lambda_end(tokens, pairs, index)
        if end > index:
            spans.append((end, 1))
            nesting = max(nesting, depth + 1)
    cognitive += int(recursive_call(tokens, opening, close, name))
    return {"cognitive": cognitive, "cyclomatic": cyclomatic, "nesting": nesting}


def scan_source(source, relative):
    # shortcut: preprocessor branches share one stream; use a C++ parser for macro-generated bodies.
    tokens, offsets = tokenize(source)
    pairs = pairs_for(tokens)
    newlines = [match.start() for match in re.finditer("\n", source)]
    results = {}
    for name, signature, opening, close in definitions(tokens, pairs):
        key = f"{relative}::{name}{signature}"
        if key in results:
            raise ValueError(f"duplicate function key: {key}")
        row = complexity(tokens, pairs, opening, close, name)
        row.update(file=relative, name=name, line=bisect_right(newlines, offsets[opening]) + 1)
        row["lines"] = bisect_right(newlines, offsets[close]) + 2 - row["line"]
        results[key] = row
    return results


def scan_tree(root):
    rows = {}
    for directory in ("src", "tests"):
        files = sorted((root / directory).rglob("*"))
        for path in files:
            if path.relative_to(root).as_posix() == "src/protocol_generated.cpp":
                continue
            if path.suffix.lower() not in (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc"):
                continue
            try:
                rows.update(scan_source(path.read_text(encoding="utf-8-sig"), path.relative_to(root).as_posix()))
            except ValueError as error:
                raise ValueError(f"{path}: {error}") from error
    if not rows:
        raise ValueError("no C++ functions found in src/ and tests/")
    return rows


def above_limit(row):
    return any(row[metric] > limit for metric, limit in LIMITS.items())


def read_exceptions(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != HEADER.rstrip():
        raise ValueError("invalid exceptions header")
    ceilings = {}
    for line in lines[1:]:
        key, cognitive, cyclomatic = line.rsplit(" ", 2)
        if "::" not in key or "(" not in key or key in ceilings:
            raise ValueError("invalid or duplicate exception")
        metrics = {"cognitive": int(cognitive), "cyclomatic": int(cyclomatic)}
        if min(metrics.values()) < 0 or not above_limit(metrics):
            raise ValueError(f"exception must exceed a function limit: {key}")
        ceilings[key] = metrics
    if list(ceilings) != sorted(ceilings):
        raise ValueError("exceptions must be sorted")
    return ceilings


def check(rows, ceilings, updating=False):
    failures = []
    for key, row in rows.items():
        limits = ceilings.get(key, LIMITS)
        for metric, limit in limits.items():
            if row[metric] > limit:
                failures.append(f"{location(row)}: {metric} {row[metric]} > {limit}")
        if not updating and key in ceilings and not above_limit(row):
            failures.append(f"{location(row)}: remove exception with --update")
    for key in (() if updating else ceilings.keys() - rows.keys()):
        failures.append(f"{key}: remove absent function with --update")
    return failures


def lowered_ceilings(rows, ceilings):
    return {key: {metric: min(old[metric], rows[key][metric]) for metric in LIMITS}
            for key, old in ceilings.items() if key in rows and above_limit(rows[key])}


def write_exceptions(path, ceilings):
    lines = [f"{key} {row['cognitive']} {row['cyclomatic']}\n" for key, row in sorted(ceilings.items())]
    path.write_text(HEADER + "".join(lines), encoding="utf-8")


def location(row):
    return f"{row['file']}:{row['line']}: {row['name']}"


def report(rows):
    values = list(rows.values())
    counts = ", ".join(f">{limit}: {sum(row['cognitive'] > limit for row in values)}" for limit in (15, 25, 50))
    print(f"Functions: {len(values)}; cognitive {counts}")
    for row in sorted(values, key=lambda row: (-row["cognitive"], row["file"], row["name"]))[:10]:
        print(f"TOP {location(row)}: cognitive={row['cognitive']} cyclomatic={row['cyclomatic']}")
    for row in values:
        if 16 <= row["cognitive"] <= 25 or row["nesting"] > 4 or row["lines"] > 150:
            print(f"WARN {location(row)}: cognitive={row['cognitive']} nesting={row['nesting']} lines={row['lines']}")


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exceptions", type=Path, default=EXCEPTIONS)
    parser.add_argument("--verbose", action="store_true", help="show measurements and warnings")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--update", action="store_true", help="remove or lower existing exceptions")
    mode.add_argument("--write-exceptions", action="store_true", help="regenerate exceptions after a reviewed scanner change")
    return parser.parse_args()


def main():
    args = arguments()
    try:
        rows = scan_tree(ROOT)
        ceilings = read_exceptions(args.exceptions) if not args.write_exceptions else {
            key: {metric: row[metric] for metric in LIMITS} for key, row in rows.items() if above_limit(row)}
        failures = check(rows, ceilings, args.update)
        if args.verbose:
            report(rows)
        if failures:
            print("\n".join("FAIL " + failure for failure in failures), file=sys.stderr)
            return 1
        if args.update:
            ceilings = lowered_ceilings(rows, ceilings)
        if args.write_exceptions or args.update:
            write_exceptions(args.exceptions, ceilings)
    except (OSError, ValueError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        return 1
    print("Complexity gate passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
