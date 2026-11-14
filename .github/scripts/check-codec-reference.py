#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check doc/codec against the codec's stream source.

Usage:
    check-codec-reference.py [--repo PATH] [--ref REF]

Reads the names the stream source uses (control messages, configure and
request keys, output lines and the fields of each, error codes, rejected
reasons, pass_skipped reasons and stages, jtty_update states, and wsprd's
control messages, keys, lines, fields and codes) from lib/streaming_*.f90
and lib/wsprd/wsprd_stream.c, and the name tables of
doc/codec/interface.md. Every name in the source needs a row, and every
row a name in the source.
It also parses every json, jsonl and ndjson code block in doc/codec, and
each line that starts with {" in its text and unlabelled blocks, and
resolves every relative link and anchor there. It checks names, not types
or ranges.

Scope:
- Literals joined by // in Fortran, or adjacent in C, are read as one.
- Configure keys are the keys read outside the routines of
  streaming_control.f90 whose names contain "request"; request keys are
  those read inside them. Each has its own table.
- A reason, stage or state written in a literal belongs to the rejected,
  pass_skipped or jtty_update line its statement names, so it is written in
  that statement; in any other statement it is reported as unparsed, and so
  is a line, code, reason, stage or state written in a literal with a
  character outside a-z, 0-9 and _.
- Names are read from the forms the parser and the emitters use (the
  patterns below). These other forms are reported as unparsed, so that
  they fail rather than pass unseen: t_value used other than in its
  declaration, the read of "t" and a comparison with a literal; a code,
  reason or key passed as a variable, a keyword or a concatenation with
  anything but literals, or as
  a dummy argument outside the routines in PASSERS; a code, reason, stage
  or state written from outside a literal outside VALUE_WRITERS; a call of
  an error or rejection emitter that the stream files do not define; a key
  read outside streaming_control.f90; an output line or field name built by
  concatenation or written with a blank at its colon; in wsprd, event_type
  used other than in strcmp(event_type, "name"), and a json_ call that is
  not one of C_KEY_HELPERS with json first and a literal key.
- Still unread: names in Fortran literals in double quotes; a name passed
  as a literal to a procedure the check does not know, unless its name
  has emit and error or reject in it; a key that wsprd reads without its
  json_ helpers; names in any file but lib/streaming_*.f90 and
  lib/wsprd/wsprd_stream.c.
- A field belongs to the line its statement names, else to the line of a
  variable that statement builds or a later one uses, else to its routine's
  only line. A field that belongs to no line fails, but for the nested
  fields in NESTED_FIELDS.
- Only the tables under the headings in SETS and FIELD_SETS are checked,
  by their first cell. Other tables (the per-mode request table, the
  command-line options) and names in prose are not. A heading below one
  in SETS, a table in interface.md without a leading |, and a line right
  after a table that does not start with | (read as one of its rows), are
  reported, as their rows would go unchecked.
- Links: inline Markdown links and reference definitions, with a
  double-quoted title or none. HTML links, reference-style uses and
  single-quoted titles are not checked.

--repo PATH  the repository (default: the one holding this script).
--ref REF    read the source at REF; the documents always come from the
             working tree.

Exits 0 when everything matches, 1 with one line per problem, 2 when the
source cannot be read.
"""
import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

DOCS = "doc/codec"
REFERENCE = DOCS + "/interface.md"
CONTROL_SOURCE = "lib/streaming_control.f90"
STREAM_GLOB = re.compile(r"lib/streaming_[A-Za-z0-9_]+\.f90")
WSPRD_SOURCE = "lib/wsprd/wsprd_stream.c"

# Each name set, the heading of its table in interface.md, and a reader's label.
SETS = [
    ("control", "Control messages", "control message"),
    ("keys", "Configure keys", "configure key"),
    ("request_keys", "Request keys", "request key"),
    ("lines", "Output lines", "output line"),
    ("codes", "Error codes", "error code"),
    ("reasons", "Rejected reasons", "rejected reason"),
    ("wsprd_control", "wsprd control messages", "wsprd control message"),
    ("wsprd_keys", "wsprd keys", "wsprd key"),
    ("wsprd_lines", "wsprd output lines", "wsprd output line"),
    ("wsprd_codes", "wsprd error codes", "wsprd error code"),
    ("skip_reasons", "Pass skipped reasons", "pass_skipped reason"),
    ("skip_stages", "Pass skipped stages", "pass_skipped stage"),
    ("jtty_states", "JTTY update states", "jtty_update state"),
]
LABELS = {name: label for name, _, label in SETS}

# Fortran patterns, matched on code with comments removed and continuation
# lines joined.
CONTROL_T = re.compile(r"\bt_value\b\s*\)?\s*(?:\.eq\.|\.ne\.|==|/=)\s*'([a-z_0-9]+)'", re.I)
QUOTED_KEY = re.compile(r"'\"([A-Za-z0-9_]+)\"'")
KEY_ARRAY = re.compile(r"\[\s*character\s*\(\s*len\s*=\s*\d+\s*\)\s*::([^\]]*)\]")
ARRAY_NAME = re.compile(r"'([A-Za-z0-9_]+)\s*'")
LINE_T = re.compile(r'"t":"([a-z_0-9]+)"')
LINE_T_MERGE = re.compile(r"\"t\":\"'\s*//\s*(?:trim\s*\(\s*)?merge\s*\(\s*'([a-z_0-9]+)\s*'\s*,\s*'([a-z_0-9]+)\s*'")
CODE_CALL = re.compile(r"streaming_emit_error_code\s*\(\s*'([a-z_0-9]+)'")
CODE_LITERAL = re.compile(r'"code":"([a-z_0-9]+)"')
REASON_CALL = re.compile(r"\b(?:encode_emit_rejected|emit_rejected_)\s*\(\s*\w+\s*,\s*'([a-z_0-9]+)'")
REASON_FUNCTION = re.compile(r"\bfunction\s+tx_reason_\b(.*?)\bend\s+function\b", re.S | re.I)
REASON_ASSIGN = re.compile(r"\breason\s*=\s*'([a-z_0-9]+)'")

# C patterns for wsprd, matched on code with comments removed.
C_CONTROL_T = re.compile(r'strcmp\s*\(\s*event_type\s*,\s*"([a-z_0-9]+)"\s*\)')
C_KEY = re.compile(r'json_(?:get_string|get_int|get_double|find_key)\s*\(\s*json\s*,\s*"([A-Za-z0-9_]+)"')
C_LINE_T = re.compile(r'\\"t\\":\\"([a-z_0-9]+)\\"')
C_CODE = re.compile(r'wsprd_stream_emit_error_code\s*\(\s*"([a-z_0-9]+)"')

# Enumerated field values.
SKIP_REASON = re.compile(r"\bskip_pass\s*\([^,]*,\s*\.(?:true|false)\.\s*,\s*'([a-z_0-9]+)'", re.I)
SKIP_STAGE = re.compile(r"\bstage\s*=\s*merge\s*\(\s*'([a-z_0-9]+)\s*'\s*,\s*'([a-z_0-9]+)\s*'")
JTTY_STATE = re.compile(r"\bstate\s*=\s*'([a-z_0-9]+)'")
# A value written in a literal, and the set it joins for each line.
VALUE_LITERAL = re.compile(r'"(reason|stage|state)":"([a-z_0-9]+)"')
VALUE_LINES = {("rejected", "reason"): "reasons", ("pass_skipped", "reason"): "skip_reasons",
               ("pass_skipped", "stage"): "skip_stages", ("jtty_update", "state"): "jtty_states"}

# The fields of each output line: the tables under these headings, one
# fourth-level heading (`line`) a line.
FIELD_SETS = [("fields", "Output fields", "output line"), ("wsprd_fields", "wsprd output fields", "wsprd output line")]
FIELD = re.compile(r'"([A-Za-z0-9_]+)":')
C_FIELD = re.compile(r'\\"([A-Za-z0-9_]+)\\":')
ROUTINE_START = re.compile(r"^\s*(?:(?:pure|elemental|impure|recursive|module)\s+)*"
                           r"(?:(?:logical|integer|real|character|complex|type)\b[^=]*?\s+)?(?:subroutine|function)\s+(\w+)", re.I)
ROUTINE_END = re.compile(r"^\s*end\s*(?:subroutine|function)\b", re.I)
# A procedure that reads a key takes it as its second argument.
KEY_READERS = ("get_string_", "get_whole_string_", "get_int_", "get_real_", "get_bool_", "get_numbers_",
               "request_int_", "request_string_", "find_key_", "check_type_", "value_category_",
               "station_value_", "string_array_")
# (routine, field): fields of a nested object that the routine writes and
# that belong to no line of their own. frame_list_ writes each object of a
# segment line's frames, {"text": ...}.
NESTED_FIELDS = {("frame_list_", "text")}
# The routines that may write a code, reason, stage or state held outside a
# literal, or pass a dummy argument on as one: the check reads the names
# their callers pass or the values they assign. Any other routine that does
# so is reported.
VALUE_WRITERS = {
    "code": {"streaming_emit_error_code"},
    "reason": {"emit_rejected_", "streaming_emit_pass_skipped"},
    "stage": {"streaming_emit_pass_skipped"},
    "state": {"emit_updates_"},
}
PASSERS = {"reason": {"encode_emit_rejected", "skip_pass"}, "key": set(KEY_READERS)}
# wsprd's JSON helpers: the four that read a key from the control frame, json, and the escape.
C_KEY_HELPERS = {"json_get_string", "json_get_int", "json_get_double", "json_find_key"}
C_OTHER_HELPERS = {"json_escape_into"}
LITERAL = re.compile(r"^'((?:[^']|'')*)'$")
MERGE_TWO = re.compile(r"^\s*(?:trim\s*\(\s*)?merge\s*\(\s*'([a-z_0-9]+)\s*'\s*,\s*'([a-z_0-9]+)\s*'")
C_STRINGS = re.compile(r'"(?:[^"\\\n]|\\.)*"')
ASSIGNED = re.compile(r"^\s*(?:if\s*\(.*\)\s*)?(\w+)\s*=[^=>]", re.I)
STRINGS = re.compile(r"'(?:[^']|'')*'|\"(?:[^\"]|\"\")*\"")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```+|~~~+)\s*([A-Za-z0-9_+-]*)")
ROW_NAME = re.compile(r"^`([^`]+)`$")
NO_PIPE_DELIMITER = re.compile(r"^\s*:?-+:?\s*\|")
BLOCK_START = re.compile(r"^\s*(?:#|>|<|[-*+]\s|\d+[.)]\s|```|~~~)")
INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]+)(?:\s+\"[^\"]*\")?\s*\)")
REFERENCE_LINK = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)")
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


class SourceError(Exception):
    pass


class Source:
    """The stream source, from the working tree or at a git ref."""

    def __init__(self, repo, ref=None):
        self.repo = Path(repo)
        self.ref = ref

    def _git(self, *args):
        result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        if result.returncode != 0:
            raise SourceError(f"git {' '.join(args)}: {result.stderr.strip()}")
        return result.stdout

    def read(self, path):
        if self.ref:
            return self._git("show", f"{self.ref}:{path}")
        try:
            return (self.repo / path).read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            raise SourceError(f"{path}: {error.strerror}") from error

    def stream_files(self):
        if self.ref:
            names = self._git("ls-tree", "--name-only", self.ref, "lib/").split()
        else:
            names = [f"lib/{path.name}" for path in (self.repo / "lib").glob("streaming_*.f90")]
        files = sorted(name for name in names if STREAM_GLOB.fullmatch(name))
        if not files:
            raise SourceError("no lib/streaming_*.f90 files")
        return files


def fold_literals(statement):
    """Join single-quoted Fortran literals that // alone separates, as the compiler does."""
    pieces, end, open_literal = [], 0, None
    for match in STRINGS.finditer(statement):
        between, literal = statement[end:match.start()], match.group(0)
        if open_literal is not None and literal.startswith("'") and re.fullmatch(r"\s*//\s*", between):
            open_literal = open_literal[:-1] + literal[1:]
        else:
            if open_literal is not None:
                pieces.append(open_literal)
            pieces.append(between)
            if literal.startswith("'"):
                open_literal = literal
            else:
                open_literal = None
                pieces.append(literal)
        end = match.end()
    if open_literal is not None:
        pieces.append(open_literal)
    pieces.append(statement[end:])
    return "".join(pieces)


def fortran_code(text):
    """Return Fortran free-form text as logical lines, comments removed, joined literals folded."""
    lines, current, quote = [], "", None
    for raw in text.splitlines():
        line = raw.lstrip()
        if current and line.startswith("&"):
            line = line[1:]
        code, i = [], 0
        while i < len(line):
            char = line[i]
            if quote is None:
                if char == "!":
                    break
                if char in "'\"":
                    quote = char
            elif char == quote:
                if line[i + 1:i + 2] == quote:
                    code.append(char)
                    i += 1
                else:
                    quote = None
            code.append(char)
            i += 1
        code = "".join(code).rstrip()
        continued = code.endswith("&")
        if continued:
            code = code[:-1]
        elif quote is not None:
            quote = None
        current += code
        if not continued:
            lines.append(fold_literals(current))
            current = ""
    if current:
        lines.append(fold_literals(current))
    return "\n".join(lines)


def c_code(text):
    """Return C text with comments removed (string and character literals kept, adjacent strings joined)."""
    out, i, n = [], 0, len(text)
    while i < n:
        char = text[i]
        if char in "\"'":
            j = i + 1
            while j < n and text[j] != char and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            literal = text[i:j + 1]
            k = len(out)
            while k and out[k - 1].isspace():
                k -= 1
            previous = out[k - 1] if k else ""
            if char == '"' and literal.endswith('"') and len(previous) > 1 and previous[0] == previous[-1] == '"':
                out[k - 1] = previous[:-1] + literal[1:]
                del out[k:]
            else:
                out.append(literal)
            i = j + 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        else:
            out.append(char)
            i += 1
    return "".join(out)


def source_names(source):
    """Return {set: set of names} read from the stream source."""
    names = {name: set() for name, _, _ in SETS}
    control = fortran_code(source.read(CONTROL_SOURCE))
    names["control"].update(CONTROL_T.findall(control))
    stack = []
    for statement in control.split("\n"):
        if ROUTINE_END.match(statement):
            if stack:
                stack.pop()
            continue
        start = ROUTINE_START.match(statement)
        if start:
            stack.append(start.group(1).lower())
            continue
        keys = set(QUOTED_KEY.findall(statement))
        for body in KEY_ARRAY.findall(statement):
            keys.update(ARRAY_NAME.findall(body))
        names["request_keys" if any("request" in name for name in stack) else "keys"].update(keys)
    for path in source.stream_files():
        code = fortran_code(source.read(path))
        for statement in code.split("\n"):
            for field, value in VALUE_LITERAL.findall(statement):
                for set_name in value_sets(statement, field):
                    names[set_name].add(value)
        names["lines"].update(LINE_T.findall(code))
        for pair in LINE_T_MERGE.findall(code):
            names["lines"].update(pair)
        names["codes"].update(CODE_CALL.findall(code))
        names["codes"].update(CODE_LITERAL.findall(code))
        names["reasons"].update(REASON_CALL.findall(code))
        for body in REASON_FUNCTION.findall(code):
            names["reasons"].update(REASON_ASSIGN.findall(body))
    wsprd = c_code(source.read(WSPRD_SOURCE))
    names["wsprd_control"].update(C_CONTROL_T.findall(wsprd))
    names["wsprd_keys"].update(C_KEY.findall(wsprd))
    names["wsprd_lines"].update(C_LINE_T.findall(wsprd))
    names["wsprd_codes"].update(C_CODE.findall(wsprd))
    for path in source.stream_files():
        code = fortran_code(source.read(path))
        names["skip_reasons"].update(SKIP_REASON.findall(code))
        for pair in SKIP_STAGE.findall(code):
            names["skip_stages"].update(pair)
        names["jtty_states"].update(JTTY_STATE.findall(code))
    return names


def fortran_routines(path, text):
    """Return [(path, name, dummy arguments, statements)] for each routine, its own statements only."""
    routines, stack = [], []
    for statement in fortran_code(text).split("\n"):
        if ROUTINE_END.match(statement):
            if stack:
                routines.append(stack.pop())
            continue
        start = ROUTINE_START.match(statement)
        if start:
            arguments = re.match(r"\s*\(([^)]*)\)", statement[start.end():])
            dummies = {word.lower() for word in IDENTIFIER.findall(arguments.group(1))} if arguments else set()
            stack.append((path, start.group(1).lower(), dummies, []))
            continue
        if stack:
            stack[-1][3].append(statement)
    routines.extend(stack)
    return routines


def call_arguments(statement, procedure):
    """Return the argument list of each call of procedure in a statement."""
    calls = []
    for match in re.finditer(r"\b%s\s*\(" % re.escape(procedure), statement, re.I):
        depth, arguments, current, quote = 1, [], "", None
        for char in statement[match.end():]:
            if quote:
                quote = None if char == quote else quote
            elif char in "'\"":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    break
            elif char == "," and depth == 1:
                arguments.append(current.strip())
                current = ""
                continue
            current += char
        arguments.append(current.strip())
        calls.append(arguments)
    return calls


def identifiers(statement):
    return {word.lower() for word in IDENTIFIER.findall(STRINGS.sub("''", statement))}


def unparsed(path, statement, what):
    return f"{path}: unparsed {what}, which the check cannot read: {' '.join(statement.split())[:200]}"


def fortran_guards(path, routine, dummies, statements, arrays=frozenset(), defined=frozenset()):
    """Problems for the statements of one routine that name something in a form the patterns miss.

    arrays names the key lists whose constructors KEY_ARRAY reads; a key
    built from one of their elements is read. defined names the routines of
    the stream files, whose literals the check reads; a call of an error or
    rejection emitter that is not among them is reported.
    """
    problems = []
    from_array = re.compile(r"^'\"'\s*//\s*(?:trim\s*\(\s*)?(\w+)\s*\(\s*\w+\s*\)\s*\)?\s*//\s*'\"'$")

    def passed(argument, kind=None):
        return bool(LITERAL.match(argument)) or (argument.lower() in dummies and routine in PASSERS.get(kind, ()))

    for statement in statements:
        literals = [literal[1:-1] for literal in STRINGS.findall(statement)]
        # Every use of t_value is its declaration, the read of "t" into it, or a comparison with a literal.
        rest = re.sub(r"::.*|\bget_string_\s*\(\s*buf\s*,\s*'\"t\"'\s*,\s*t_value\b|"
                      r"(?:\btrim\s*\(\s*)?\bt_value\b\s*\)?\s*(?:\.eq\.|\.ne\.|==|/=)\s*'[a-z_0-9]+'", "", statement, flags=re.I)
        if re.search(r"\bt_value\b", rest, re.I):
            problems.append(unparsed(path, statement, "control-message form"))
        for arguments in call_arguments(statement, "streaming_emit_error_code"):
            if not passed(arguments[0], "code"):
                problems.append(unparsed(path, statement, "error code"))
        for name, writers in VALUE_WRITERS.items():
            if re.search(r'"%s":"\'' % name, statement) and routine not in writers:
                problems.append(unparsed(path, statement, f"{name}, written from outside a literal"))
        for field, value in VALUE_LITERAL.findall(statement):
            if not value_sets(statement, field):
                problems.append(unparsed(path, statement, f"{field} `{value}`, in a statement naming no line it belongs to"))
        for match in re.finditer(r"\b(\w*emit\w*)\s*\(", statement, re.I):
            if re.search(r"error|reject", match.group(1), re.I) and match.group(1).lower() not in defined:
                problems.append(unparsed(path, statement, f"error or rejection, through {match.group(1)}"))
        for literal in literals:
            if re.search(r'"\w+"\s+:|"(?:t|code|reason|stage|state)":\s+"', literal):
                problems.append(unparsed(path, statement, "field or value, with a blank at its colon"))
            if re.search(r'"(?:t|code|reason|stage|state)":"(?![a-z_0-9]+"|$)', literal):
                problems.append(unparsed(path, statement, "name with a character outside a-z, 0-9 and _"))
        for procedure in ("encode_emit_rejected", "emit_rejected_"):
            for arguments in call_arguments(statement, procedure):
                reason = arguments[1] if len(arguments) > 1 else ""
                if not passed(reason, "reason") and not re.match(r"tx_reason_\s*\(", reason, re.I):
                    problems.append(unparsed(path, statement, "rejected reason"))
        for procedure in KEY_READERS:
            for arguments in call_arguments(statement, procedure):
                key = arguments[1] if len(arguments) > 1 else ""
                if path != CONTROL_SOURCE:
                    problems.append(unparsed(path, statement, "key, read outside " + CONTROL_SOURCE))
                elif not QUOTED_KEY.fullmatch(key) and not passed(key, "key") and not (
                        from_array.match(key) and from_array.match(key).group(1).lower() in arrays):
                    problems.append(unparsed(path, statement, "key"))
        for arguments in call_arguments(statement, "unknown_key_"):
            if len(arguments) < 2 or not re.match(r"KEYS\s*\(", arguments[1]):
                problems.append(unparsed(path, statement, "key list"))
        for match in re.finditer(r"\"t\":\"'", statement):
            following = re.match(r"\s*//(.*)", statement[match.end():], re.S)
            if not following or not MERGE_TWO.match(following.group(1)):
                problems.append(unparsed(path, statement, "output line"))
        if any(literal.count('":') > len(FIELD.findall(literal)) for literal in literals):
            problems.append(unparsed(path, statement, "field"))
        for arguments in call_arguments(statement, "skip_pass"):
            if len(arguments) < 3 or not passed(arguments[2], "reason"):
                problems.append(unparsed(path, statement, "pass_skipped reason"))
        for arguments in call_arguments(statement, "streaming_emit_pass_skipped"):
            stage, reason = (arguments + ["", ""])[4:6]
            assigned = [other for other in statements if re.match(r"^\s*%s\s*=" % re.escape(stage), other, re.I)] if stage else []
            if not assigned or not all(SKIP_STAGE.search(other) for other in assigned):
                problems.append(unparsed(path, statement, "pass_skipped stage"))
            if LITERAL.match(reason) or not passed(reason, "reason"):
                problems.append(unparsed(path, statement, "pass_skipped reason"))
        for match in re.finditer(r"\"state\":\"'\s*//\s*(\w+)", statement):
            variable = match.group(1)
            assigned = [other for other in statements if re.match(r"^\s*%s\s*=" % re.escape(variable), other, re.I)]
            if not assigned or not all(JTTY_STATE.match(other.strip()) for other in assigned):
                problems.append(unparsed(path, statement, "jtty_update state"))
    return problems


def c_statements(text):
    """Split C text, comments removed, at ; { and } outside string and character literals."""
    statements, current, quote, escaped = [], [], None, False
    for char in c_code(text):
        if quote:
            if char == quote and not escaped:
                quote = None
            escaped = not escaped and char == "\\"
        elif char in "\"'":
            quote, escaped = char, False
        elif char in ";{}":
            statements.append("".join(current))
            current = []
            continue
        current.append(char)
    statements.append("".join(current))
    return [statement for statement in statements if statement.strip()]


def c_guards(text):
    """Problems for wsprd statements that name something in a form the patterns miss."""
    problems = []
    for statement in c_statements(text):
        literals = [literal[1:-1] for literal in C_STRINGS.findall(statement)]
        # Every use of event_type is its declaration, the read of "t" into it, or strcmp(event_type, "name").
        rest = re.sub(r'\bchar\s+event_type\s*\[[^\]]*\]|json_get_string\s*\(\s*json\s*,\s*"t"\s*,\s*event_type\b|'
                      r'strcmp\s*\(\s*event_type\s*,\s*"[a-z_0-9]+"\s*\)|sizeof\s+event_type\b', "", statement)
        if re.search(r"\bevent_type\b", rest):
            problems.append(unparsed(WSPRD_SOURCE, statement, "control-message form"))
        for match in re.finditer(r"\b(json_\w+)\s*\(\s*([^,)]*)(?:,\s*([^,)]+))?", statement):
            helper, first, key = match.group(1), match.group(2).strip(), (match.group(3) or "").strip()
            if helper in C_OTHER_HELPERS or re.search(r"(?:\bstatic|\bint|\bvoid|\bchar|\bdouble|\bconst)[\s*]*$",
                                                      statement[:match.start()]):
                continue
            # The json_* helpers pass their own key parameter on.
            if helper not in C_KEY_HELPERS or first != "json" or (not key.startswith('"') and key != "key"):
                problems.append(unparsed(WSPRD_SOURCE, statement, "key"))
        for match in re.finditer(r"(\bvoid\s+)?\bwsprd_stream_emit_error_code\s*\(\s*([^,)]+)", statement):
            if not match.group(1) and not match.group(2).strip().startswith('"'):
                problems.append(unparsed(WSPRD_SOURCE, statement, "error code"))
        if any(re.search(r'\\"\w+\\"\s+:|\\"(?:t|code)\\":\s+\\"', literal) for literal in literals):
            problems.append(unparsed(WSPRD_SOURCE, statement, "field or value, with a blank at its colon"))
        if any(re.search(r'\\"t\\":\\"(?![a-z_0-9]+\\")', literal) for literal in literals):
            problems.append(unparsed(WSPRD_SOURCE, statement, "output line"))
        if any(literal.count('\\":') > len(C_FIELD.findall(literal)) for literal in literals):
            problems.append(unparsed(WSPRD_SOURCE, statement, "field"))
    return problems


def source_guards(source):
    problems = []
    arrays = {name.lower() for name in re.findall(r"(\w+)\s*\(\s*\d+\s*\)\s*=\s*\[\s*character",
                                                  fortran_code(source.read(CONTROL_SOURCE)), re.I)}
    routines = [routine for path in source.stream_files() for routine in fortran_routines(path, source.read(path))]
    defined = {name for _, name, _, _ in routines}
    for path, routine, dummies, statements in routines:
        problems.extend(fortran_guards(path, routine, dummies, statements, arrays, defined))
    problems.extend(c_guards(source.read(WSPRD_SOURCE)))
    return list(dict.fromkeys(problems))


def line_types(statement):
    types = set(LINE_T.findall(statement))
    for pair in LINE_T_MERGE.findall(statement):
        types.update(pair)
    return types


def value_sets(statement, field):
    """The sets a reason, stage or state written in a literal of statement joins, by the lines it names."""
    return sorted(VALUE_LINES[(line, field)] for line in line_types(statement) if (line, field) in VALUE_LINES)


def fortran_fields(texts):
    """Return ({line: fields}, {(routine, field)} whose line cannot be told) for Fortran emitters.

    A statement's fields belong to the lines its own "t" literals name, else
    to those of a variable an earlier statement built a line in, else to the
    one line its routine writes. Fields assigned to a variable with no line
    yet go to the line of the first later statement that uses the variable.
    A routine that writes no line (a helper) gives its fields to the lines of
    the statements that call it.
    """
    routines = [routine for path, text in texts for routine in fortran_routines(path, text)]
    fields, unplaced, helpers, calls = defaultdict(set), set(), {}, []
    for _, name, _, statements in routines:
        writes = set().union(*(line_types(statement) for statement in statements)) if statements else set()
        bound, pending = {}, defaultdict(set)
        for statement in statements:
            names = {field for field in FIELD.findall(statement) if field not in ("v", "t")}
            words = identifiers(statement)
            target = line_types(statement)
            if not target:
                target = set().union(*(bound[word] for word in words if word in bound))
            if not target and len(writes) == 1:
                target = set(writes)
            assigned = ASSIGNED.match(statement)
            variable = assigned.group(1).lower() if assigned else None
            if target:
                for word in sorted(words & set(pending)):
                    names |= pending.pop(word)
                if variable:
                    bound[variable] = target
            calls.append((words, target))
            if not names:
                continue
            if target:
                for line in target:
                    fields[line] |= names
            elif not writes:
                helpers.setdefault(name, set()).update(names)
            elif variable:
                pending[variable] |= names
            else:
                unplaced |= {(name, field) for field in names}
        for names in pending.values():
            unplaced |= {(name, field) for field in names}
    for helper, names in helpers.items():
        targets = set().union(*(target for words, target in calls if helper in words))
        for line in targets:
            fields[line] |= names
        if not targets:
            unplaced |= {(helper, field) for field in names}
    return fields, unplaced


def c_fields(text):
    """Return ({line: fields}, fields whose line cannot be told) for wsprd's emitters."""
    fields, unplaced = defaultdict(set), set()
    for statement in c_statements(text):
        names = {field for field in C_FIELD.findall(statement) if field not in ("v", "t")}
        target = set(C_LINE_T.findall(statement))
        for line in target:
            fields[line] |= names
        if names and not target:
            unplaced |= {("wsprd", field) for field in names}
    return fields, unplaced


def source_fields(source):
    """Return {field set: ({line: fields}, unplaced fields)}."""
    return {
        "fields": fortran_fields([(path, source.read(path)) for path in source.stream_files()]),
        "wsprd_fields": c_fields(source.read(WSPRD_SOURCE)),
    }


def markdown_lines(text):
    """Yield (number, line, fence) for each line; fence is the open block's info string or None."""
    fence, marker = None, None
    for number, line in enumerate(text.splitlines(), 1):
        match = FENCE.match(line)
        if fence is None and match:
            fence, marker = match.group(2).lower(), match.group(1)
            yield number, line, "\0"
            continue
        if fence is not None and line.strip().startswith(marker[0] * len(marker)) and not line.strip().strip(marker[0]):
            fence, marker = None, None
            yield number, line, "\0"
            continue
        yield number, line, fence


def slug(heading, seen):
    """Return GitHub's anchor for a heading, counting repeats in seen."""
    text = re.sub(r"`([^`]*)`", r"\1", heading)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")
    count = seen.get(text, 0)
    seen[text] = count + 1
    return text if count == 0 else f"{text}-{count}"


def anchors(text):
    seen, found = {}, set()
    for _, line, fence in markdown_lines(text):
        match = HEADING.match(line) if fence is None else None
        if match:
            found.add(slug(match.group(2), seen))
    return found


def sections(text):
    """Return [(level, heading, line number, rows)], rows as [(name or None, line number, first cell)]."""
    found, rows, header_seen = [], None, False
    for number, line, fence in markdown_lines(text):
        if fence is not None:
            continue
        match = HEADING.match(line)
        if match:
            rows, header_seen = [], False
            found.append((len(match.group(1)), match.group(2), number, rows))
            continue
        stripped = line.strip()
        if not stripped.startswith("|"):
            header_seen = False
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if not header_seen:
            header_seen = True
            continue
        if all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells if cell):
            continue
        if rows is None:
            continue
        name = ROW_NAME.match(cells[0])
        rows.append((name.group(1) if name else None, number, cells[0]))
    return found


def table_rows(text):
    """Return {third-level heading: rows} (headings of other levels too, when no third-level one has that text)."""
    tables = {}
    for level, heading, _, rows in sorted(sections(text), key=lambda item: item[0] != 3):
        if level == 3 or heading not in tables:
            tables.setdefault(heading, []).extend(rows)
    return tables


def check_names(source_sets, reference_text):
    problems = []
    tables = table_rows(reference_text)
    documented = {name: set() for name, _, _ in SETS}
    for set_name, heading, label in SETS:
        if heading not in tables:
            problems.append(f"{REFERENCE}: no '{heading}' heading")
            continue
        if not tables[heading]:
            problems.append(f"{REFERENCE}: no table under '{heading}'")
        documented[set_name] |= row_problems(tables[heading], heading, label, source_sets[set_name], problems)
    for set_name, names in source_sets.items():
        for name in sorted(names - documented[set_name]):
            problems.append(f"{REFERENCE}: missing row: {LABELS[set_name]} `{name}` is in the source")
    found = sections(reference_text)
    headings = {heading for _, heading, _ in SETS}
    for index, (level, title, _, _) in enumerate(found):
        if title not in headings:
            continue
        for below, heading, number, _ in found[index + 1:]:
            if below <= level:
                break
            problems.append(f"{REFERENCE}:{number}: a heading under '{title}' takes its rows out of the check: {heading}")
    after_row = False
    for number, line, fence in markdown_lines(reference_text):
        if fence is None and NO_PIPE_DELIMITER.match(line):
            problems.append(f"{REFERENCE}:{number}: a table without a leading | is not checked")
        if fence is None and after_row and line.strip() and not line.lstrip().startswith("|") and not BLOCK_START.match(line):
            problems.append(f"{REFERENCE}:{number}: a line right after a table, without a leading |, is read as one of its rows and not checked")
        after_row = fence is None and line.lstrip().startswith("|")
    return problems


def row_problems(rows, where, label, known, problems):
    """Check one table's rows against the names known; return the names documented."""
    seen = set()
    for name, number, cell in rows:
        if name is None:
            problems.append(f"{REFERENCE}:{number}: a row under '{where}' does not start with one `name`: {cell}")
            continue
        if name in seen:
            problems.append(f"{REFERENCE}:{number}: {label} `{name}` has two rows under '{where}'")
        seen.add(name)
        if name not in known:
            problems.append(f"{REFERENCE}:{number}: stale row: the source has no {label} `{name}`")
    return seen


def check_fields(field_sets, reference_text):
    problems = []
    found = sections(reference_text)
    for set_name, heading, label in FIELD_SETS:
        fields, unplaced = field_sets[set_name]
        starts = [index for index, (level, title, _, _) in enumerate(found) if level == 3 and title == heading]
        if not starts:
            problems.append(f"{REFERENCE}: no '{heading}' heading")
            continue
        documented, every = {}, set()
        for level, title, number, rows in found[starts[0] + 1:]:
            if level <= 3:
                break
            line = ROW_NAME.match(title)
            if level != 4 or not line:
                problems.append(f"{REFERENCE}:{number}: a heading under '{heading}' is not one `line`: {title}")
                continue
            line = line.group(1)
            if line in documented:
                problems.append(f"{REFERENCE}:{number}: {label} `{line}` has two tables under '{heading}'")
            if line not in fields:
                problems.append(f"{REFERENCE}:{number}: stale table: the source writes no {label} `{line}` with fields")
                documented.setdefault(line, set())
                continue
            names = row_problems(rows, f"{heading}: {line}", f"field of `{line}`", fields[line], problems)
            documented.setdefault(line, set()).update(names)
            every |= names
        for line in sorted(fields):
            if line not in documented:
                problems.append(f"{REFERENCE}: missing table: {label} `{line}` has fields in the source")
                continue
            for name in sorted(fields[line] - documented[line]):
                problems.append(f"{REFERENCE}: missing row: field `{name}` of {label} `{line}` is in the source")
        for routine, name in sorted(unplaced):
            if (routine, name) not in NESTED_FIELDS:
                problems.append(f"{REFERENCE}: field `{name}` in `{routine}` belongs to no {label} the check can tell")
            elif name not in every:
                problems.append(f"{REFERENCE}: missing row: nested field `{name}` of `{routine}` is in the source")
    return problems


def check_blocks_and_links(docs_root, repo_root):
    problems = []
    files = sorted(docs_root.rglob("*.md"))
    texts = {path: path.read_text(encoding="utf-8") for path in files}
    for path, text in texts.items():
        shown = path.relative_to(repo_root).as_posix()
        block, start, kind = None, 0, None
        for number, line, fence in markdown_lines(text):
            if fence == "\0":
                if block is None:
                    match = FENCE.match(line)
                    block, start, kind = [], number, match.group(2).lower()
                else:
                    if kind in ("json", "jsonl", "ndjson"):
                        problems.extend(f"{shown}:{start}: {problem}" for problem in json_problems(block, kind))
                    elif kind in ("text", ""):
                        problems.extend(f"{shown}:{start}: {problem}" for problem in json_line_problems(block, kind or "unlabelled"))
                    block = None
                continue
            if block is not None:
                block.append(line)
                continue
            for target in link_targets(line):
                problem = link_problem(path, target, texts, repo_root)
                if problem:
                    problems.append(f"{shown}:{number}: {problem}")
        if block is not None:
            problems.append(f"{shown}:{start}: code block is not closed")
    return problems, files


def json_problems(block, kind):
    text = "\n".join(block)
    if kind == "json":
        try:
            json.loads(text)
            return []
        except ValueError:
            pass
    problems = []
    for offset, line in enumerate(block, 1):
        if not line.strip():
            continue
        try:
            json.loads(line)
        except ValueError as error:
            problems.append(f"{kind} block line {offset} does not parse: {error}")
    return problems


def json_line_problems(block, kind):
    """Problems for the lines of a text or unlabelled block that start with {" and do not parse."""
    problems = []
    for offset, line in enumerate(block, 1):
        if not re.match(r'\s*\{\s*"', line):
            continue
        try:
            json.loads(line)
        except ValueError as error:
            problems.append(f"{kind} block line {offset} does not parse: {error}")
    return problems


def link_targets(line):
    code_free = re.sub(r"`[^`]*`", "", line)
    targets = [match.group(1) for match in INLINE_LINK.finditer(code_free)]
    reference = REFERENCE_LINK.match(code_free)
    if reference:
        targets.append(reference.group(1))
    return [target[1:-1] if target.startswith("<") else target for target in targets]


def link_problem(path, target, texts, repo_root):
    if SCHEME.match(target) or target.startswith("//"):
        return None
    file_part, _, anchor = target.partition("#")
    destination = path if not file_part else (path.parent / file_part).resolve()
    if file_part:
        try:
            destination.relative_to(repo_root.resolve())
        except ValueError:
            return f"link {target} leaves the repository"
        if not destination.exists():
            return f"link {target} does not resolve"
    if anchor and destination.suffix == ".md":
        text = texts.get(destination)
        if text is None:
            text = destination.read_text(encoding="utf-8")
        if anchor not in anchors(text):
            return f"link {target} names no heading"
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check doc/codec against the codec's stream source.")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2],
                        help="the repository (default: the one holding this script)")
    parser.add_argument("--ref", help="read the source at this git ref; the documents come from the working tree")
    args = parser.parse_args(argv)
    repo = args.repo.resolve()
    reference = repo / REFERENCE
    try:
        source = Source(repo, args.ref)
        source_sets = source_names(source)
        field_sets = source_fields(source)
        reference_text = reference.read_text(encoding="utf-8")
    except (SourceError, OSError) as error:
        print(f"check-codec-reference: {error}", file=sys.stderr)
        return 2
    problems = source_guards(source) + check_names(source_sets, reference_text) + check_fields(field_sets, reference_text)
    link_problems, files = check_blocks_and_links(repo / DOCS, repo)
    problems.extend(link_problems)
    for problem in problems:
        print(problem)
    where = f"at {args.ref}" if args.ref else "in the working tree"
    counts = [f"{len(source_sets[name])} {LABELS[name]}s" for name in dict.fromkeys(name for name, _, _ in SETS)]
    for set_name, _, label in FIELD_SETS:
        fields, _ = field_sets[set_name]
        counts.append(f"{sum(map(len, fields.values()))} fields of {len(fields)} {label}s")
    counts = ", ".join(counts)
    status = f"{len(problems)} problem(s)" if problems else "OK"
    print(f"codec reference {status}: source {where}: {counts}; {len(files)} document(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
