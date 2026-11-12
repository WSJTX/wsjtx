# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "check-codec-reference.py"
REPO = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("check_codec_reference", SCRIPT)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def run(repo, *args):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), "--repo", str(repo), *args],
                          capture_output=True, text=True)


def copy_repository(source, target):
    """Copy the stream source and the documents, with the files and directories outside them that they link to."""
    for path in [*source.glob("lib/streaming_*.f90"), source / check.WSPRD_SOURCE]:
        copy = target / path.relative_to(source)
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(path, copy)
    shutil.copytree(source / check.DOCS, target / check.DOCS)
    for document in (source / check.DOCS).rglob("*.md"):
        for line in document.read_text(encoding="utf-8").splitlines():
            for link in check.link_targets(line):
                path = link.strip("<>").split("#", 1)[0]
                if not path or ":" in path:
                    continue
                linked = (document.parent / path).resolve()
                if source.resolve() not in linked.parents or (source / check.DOCS).resolve() in [linked, *linked.parents]:
                    continue
                copy = target / linked.relative_to(source.resolve())
                if linked.is_dir():
                    shutil.copytree(linked, copy, dirs_exist_ok=True)
                elif linked.is_file():
                    copy.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy(linked, copy)


class RepositoryTest(unittest.TestCase):
    def test_repository_reference_matches_its_source(self):
        result = run(REPO)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class CopyTest(unittest.TestCase):
    """The repository's own source and documents, copied so that a test can change them."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Path(directory.name)
        copy_repository(REPO, self.repo)
        self.reference = self.repo / check.REFERENCE

    def edit(self, path, old, new):
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    def row(self, name):
        for line in self.reference.read_text(encoding="utf-8").splitlines():
            if line.startswith(f"| `{name}` |"):
                return line + "\n"
        self.fail(f"no row for {name}")

    def section_row(self, heading, name):
        """The row for name in the first table after heading."""
        lines = self.reference.read_text(encoding="utf-8").splitlines()
        start = lines.index(heading)
        for line in lines[start + 1:]:
            if line.startswith(f"| `{name}` |"):
                return line + "\n"
        self.fail(f"no row for {name} after {heading}")

    def assertFails(self, *expected):
        result = run(self.repo)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for text in expected:
            self.assertIn(text, result.stdout)
        return result.stdout

    def test_copy_passes(self):
        result = run(self.repo)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_row_fails(self):
        self.edit(self.reference, self.row("busy_events"), "")
        self.assertFails("missing row: configure key `busy_events` is in the source")

    def test_missing_rows_in_every_set_fail(self):
        for name in ("flush", "pcm", "odd_audio_frame", "invalid_runtime", "dialfreq", "insufficient_audio"):
            self.edit(self.reference, self.row(name), "")
        self.assertFails("control message `flush`", "output line `pcm`", "error code `odd_audio_frame`",
                         "rejected reason `invalid_runtime`", "wsprd key `dialfreq`", "wsprd error code `insufficient_audio`")

    def test_stale_row_fails(self):
        self.edit(self.reference, self.row("busy_events"), self.row("busy_events") + "| `busy_lines` | boolean |  | Gone. |\n")
        self.assertFails("stale row: the source has no configure key `busy_lines`")

    def test_a_key_under_the_other_table_fails(self):
        row = self.row("busy_events")
        self.edit(self.reference, row, "")
        self.edit(self.reference, self.section_row("### Request keys", "text"), self.section_row("### Request keys", "text") + row)
        self.assertFails("stale row: the source has no request key `busy_events`", "missing row: configure key `busy_events` is in the source")

    def test_rows_out_of_the_check_fail(self):
        row = self.row("busy_events")
        self.edit(self.reference, row, row + "\n#### Retired\n\n| Key | Type |\n|---|---|\n| `gone_key` | boolean |\n")
        self.assertFails("a heading under 'Configure keys' takes its rows out of the check: Retired")
        self.edit(self.reference, "## Names\n", "## Names\n\nKey | Type\n-|-\n`gone` | boolean\n")
        self.assertFails("a table without a leading | is not checked")

    def test_a_line_read_as_a_table_row_fails(self):
        row = self.row("busy_events")
        self.edit(self.reference, row, row + "`gone_key` | boolean |  | false | Gone. |\n")
        self.assertFails("a line right after a table, without a leading |, is read as one of its rows and not checked")

    def test_a_name_added_to_the_source_needs_a_row(self):
        control = self.repo / check.CONTROL_SOURCE
        self.edit(control, "    call pack_phase9_(cfg, terr)\n", "    call get_int_(buf, '\"spectrum_rows\"', int_val, ok)\n    call pack_phase9_(cfg, terr)\n")
        emit = self.repo / "lib/streaming_emit.f90"
        self.edit(emit, "contains\n", "contains\n  subroutine emit_spectrum_()\n    call put_('{\"v\":1,\"t\":\"spectrum\"}')\n"
                  "    call streaming_emit_error_code('spectrum_file', 'x')\n  end subroutine emit_spectrum_\n")
        self.assertFails("missing row: configure key `spectrum_rows`", "missing row: output line `spectrum`",
                         "missing row: error code `spectrum_file`")

    def test_a_name_removed_from_the_source_leaves_a_stale_row(self):
        wsprd = self.repo / check.WSPRD_SOURCE
        self.edit(wsprd, 'wsprd_stream_emit_error_code("insufficient_audio",', 'wsprd_stream_emit_error_code("short_audio",')
        self.assertFails("stale row: the source has no wsprd error code `insufficient_audio`",
                         "missing row: wsprd error code `short_audio`")

    def test_enumerated_values_need_rows_both_ways(self):
        for heading, name in (("### Pass skipped reasons", "backoff"), ("### Pass skipped stages", "early"),
                              ("### JTTY update states", "expired")):
            self.edit(self.reference, self.section_row(heading, name), "")
        self.assertFails("missing row: pass_skipped reason `backoff`", "missing row: pass_skipped stage `early`",
                         "missing row: jtty_update state `expired`")

    def test_stale_enumerated_values_fail(self):
        for heading, name in (("### Pass skipped reasons", "busy"), ("### Pass skipped stages", "final"),
                              ("### JTTY update states", "ended")):
            row = self.section_row(heading, name)
            self.edit(self.reference, row, row + row.replace(f"`{name}`", "`gone`", 1))
        self.assertFails("stale row: the source has no pass_skipped reason `gone`",
                         "stale row: the source has no pass_skipped stage `gone`",
                         "stale row: the source has no jtty_update state `gone`")

    def test_missing_field_rows_fail(self):
        self.edit(self.reference, self.section_row("#### `decode`", "pass"), "")
        self.edit(self.reference, self.section_row("#### `encoded`", "samples"), "")
        wsprd = self.reference.read_text(encoding="utf-8").split("### wsprd output fields")[1]
        drift = next(line for line in wsprd.splitlines() if line.startswith("| `drift` |"))
        self.edit(self.reference, drift + "\n", "")
        self.assertFails("missing row: field `pass` of output line `decode`",
                         "missing row: field `samples` of output line `encoded`",
                         "missing row: field `drift` of wsprd output line `decode`")

    def test_stale_field_rows_fail(self):
        row = self.section_row("#### `busy`", "again")
        self.edit(self.reference, row, row + "| `again_count` | integer | Gone. |\n")
        self.edit(self.reference, "| `drift` | integer |", "| `drift_hz` | integer | Gone. |\n| `drift` | integer |")
        self.assertFails("stale row: the source has no field of `busy` `again_count`",
                         "stale row: the source has no field of `decode` `drift_hz`")

    def test_field_tables_both_ways(self):
        self.edit(self.reference, "#### `planning`\n", "#### `planned`\n")
        self.assertFails("missing table: output line `planning` has fields in the source",
                         "stale table: the source writes no output line `planned` with fields")

    def test_a_field_added_to_an_emitter_needs_a_row(self):
        emit = self.repo / "lib/streaming_emit.f90"
        self.edit(emit, "if (present(ap)) marks = marks // ',\"ap\":'", "marks = marks // ',\"hash\":1'\n    if (present(ap)) marks = marks // ',\"ap\":'")
        self.edit(emit, "'\",\"period_end\":\"' //  &", "'\",\"label\":\"' // trim(label) // '\",\"period_end\":\"' //  &")
        encode = self.repo / "lib/streaming_encode.f90"
        self.edit(encode, "    if (req%fast) line = line // ',\"fast\":true'", "    if (req%fast) line = line // ',\"fast\":true,\"speed\":2'")
        wsprd = self.repo / check.WSPRD_SOURCE
        self.edit(wsprd, '"\\"drift\\":%d,', '"\\"drift\\":%d,\\"bins\\":0,')
        self.assertFails("missing row: field `hash` of output line `decode`",
                         "missing row: field `label` of output line `busy`",
                         "missing row: field `label` of output line `pass_progress`",
                         "missing row: field `speed` of output line `encoded`",
                         "missing row: field `bins` of wsprd output line `decode`")

    def test_a_field_in_a_variable_goes_to_the_line_that_uses_it(self):
        jtty = self.repo / "lib/streaming_jtty.f90"
        self.edit(jtty, "    call emit_line_('{\"v\":1,\"t\":\"' // trim(merge('rendered', 'packed  ', render)) // &",
                  "    extra = ',\"snr\":0'\n    call emit_line_('{\"v\":1,\"t\":\"' // trim(merge('rendered', 'packed  ', render)) // trim(extra) // &")
        self.assertFails("missing row: field `snr` of output line `packed`", "missing row: field `snr` of output line `rendered`")

    def test_a_field_with_no_line_fails(self):
        jtty = self.repo / "lib/streaming_jtty.f90"
        self.edit(jtty, "    call emit_line_('{\"v\":1,\"t\":\"' // trim(merge('rendered', 'packed  ', render)) // &",
                  "    unused = ',\"orphan\":0'\n    call emit_line_('{\"v\":1,\"t\":\"' // trim(merge('rendered', 'packed  ', render)) // &")
        self.edit(jtty, "       list = list // '{\"text\":\"'", "       list = list // '{\"kind\":0,\"text\":\"'")
        self.assertFails("field `orphan` in `jtty_encode` belongs to no output line the check can tell",
                         "field `kind` in `frame_list_` belongs to no output line the check can tell")

    def test_missing_fields_heading_fails(self):
        self.edit(self.reference, "### Output fields\n", "### Fields\n")
        self.assertFails("no 'Output fields' heading")

    def test_duplicate_row_fails(self):
        self.edit(self.reference, self.row("halt"), self.row("halt") * 2)
        self.assertFails("control message `halt` has two rows")

    def test_row_without_a_name_fails(self):
        self.edit(self.reference, self.row("halt"), self.row("halt") + "| halt again | Twice. |\n")
        self.assertFails("does not start with one `name`")

    def test_missing_heading_fails(self):
        self.edit(self.reference, "### Rejected reasons\n", "### Refusals\n")
        self.assertFails("no 'Rejected reasons' heading")

    def test_bad_json_block_fails(self):
        self.edit(self.reference, "## Names\n", "## Names\n\n```json\n{\"t\":\"configure\",}\n```\n")
        self.assertFails("json block line 1 does not parse")

    def test_json_lines_blocks_parse(self):
        self.edit(self.reference, "## Names\n", "## Names\n\n```json\n{\"t\":\"halt\"}\n{\"t\":\"flush\"}\n```\n\n"
                  "```jsonl\n{\"v\":1,\"t\":\"busy\",\"busy\":false}\n\n{\"v\":1}\n```\n")
        result = run(self.repo)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.edit(self.reference, "{\"v\":1}\n```", "{\"v\":1\n```")
        self.assertFails("jsonl block line 3 does not parse")

    def test_json_lines_in_text_blocks_parse(self):
        self.edit(self.reference, "## Names\n", "## Names\n\n```text\n$ wsjt-codec\n{\"v\":1,\"t\":\"ready\"}\n```\n\n"
                  "```\n{\"t\":\"halt\"}\n```\n")
        result = run(self.repo)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.edit(self.reference, "{\"v\":1,\"t\":\"ready\"}\n", "{\"v\":1,\"t\":\"ready\",}\n")
        self.edit(self.reference, "{\"t\":\"halt\"}\n", "{\"t\":\"halt\"\n")
        self.assertFails("text block line 2 does not parse", "unlabelled block line 1 does not parse")

    def test_broken_links_fail(self):
        readme = self.repo / check.DOCS / "README.md"
        self.edit(readme, "[interface.md](interface.md)", "[interface.md](interfaces.md)")
        self.edit(self.reference, "## Names\n", "## Names\n\nSee [frames](#frame) and [keys](interface.md#configure-keys).\n")
        self.assertFails("README.md:", "link interfaces.md does not resolve", "link #frame names no heading")

    def test_links_that_resolve_pass(self):
        self.edit(self.reference, "## Names\n", "## Names\n\nSee [frames](#frames), [the index](README.md), "
                  "[wsprd](#wsprd-error-codes), [a site](https://example.com/x) and `[code](nowhere.md)`.\n")
        result = run(self.repo)
        self.assertEqual(result.returncode, 0, result.stdout)


class CopyRepositoryTest(unittest.TestCase):
    def test_linked_files_and_directories_are_copied(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        source, target = Path(directory.name) / "source", Path(directory.name) / "target"
        (source / "lib/wsprd").mkdir(parents=True)
        (source / check.WSPRD_SOURCE).write_text("")
        (source / "lib/streaming_io.f90").write_text("")
        (source / "CMake").mkdir()
        (source / "CMake/rules.cmake").write_text("x")
        (source / "NOTES.md").write_text("y")
        (source / check.DOCS).mkdir(parents=True)
        (source / check.DOCS / "README.md").write_text("[CMake](../../CMake/), [notes](../../NOTES.md#top) and [here](README.md)\n")
        copy_repository(source, target)
        self.assertEqual((target / "CMake/rules.cmake").read_text(), "x")
        self.assertEqual((target / "NOTES.md").read_text(), "y")
        self.assertTrue((target / "lib/streaming_io.f90").is_file())


class PatternTest(unittest.TestCase):
    """Every call form the source uses for a name, and comments that must not count."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Path(directory.name)
        (self.repo / "lib/wsprd").mkdir(parents=True)
        (self.repo / check.CONTROL_SOURCE).write_text(
            "! {\"t\":\"comment_only\"} '\"comment_key\"'\n"
            "    if (trim(t_value) .eq. 'halt') then\n"
            "    if (trim(t_value) .ne. 'configure') then   ! .eq. 'ignored'\n"
            "    call get_int_(buf, '\"depth\"', v, ok)  ! '\"not_a_key\"'\n"
            "    character(len=*), parameter :: KEYS(3) = [character(len=8) :: 't', 'id', &\n"
            "         'freq    ']\n"
            "    s = 'it''s ! not a comment' // x // '\"quoted\"'\n")
        (self.repo / "lib/streaming_emit.f90").write_text(
            "    call put_('{\"v\":1,\"t\":\"ready\"}')  ! \"t\":\"comment_line\"\n"
            "    call put_('{\"v\":1,\"t\":\"error\",\"code\":\"configure_type_error\",\"key\":\"' // k)\n"
            "    call streaming_emit_error_code('frame_too_large', &\n"
            "         'detail')\n")
        (self.repo / "lib/streaming_jtty.f90").write_text(
            "    call emit_line_('{\"v\":1,\"t\":\"' // trim(merge('rendered', 'packed  ', render)) // &\n"
            "         '\",\"id\":' // id)\n"
            "    call emit_rejected_(id, 'bad_request', key, detail)\n"
            "  function tx_reason_(status) result(reason)\n"
            "       reason = 'missing'\n"
            "  end function tx_reason_\n"
            "    reason = 'not_a_rejected_reason'\n")
        (self.repo / "lib/streaming_encode.f90").write_text(
            "       call encode_emit_rejected(id, 'too_long', 'text', 'x')\n")
        (self.repo / "lib/not_streaming.f90").write_text("    call put_('{\"v\":1,\"t\":\"elsewhere\"}')\n")
        (self.repo / check.WSPRD_SOURCE).write_text(
            "/* {\\\"t\\\":\\\"block_comment\\\"} */\n"
            "// wsprd_stream_emit_error_code(\"line_comment\", \"\");\n"
            "if (strcmp(event_type, \"halt\") == 0) {}\n"
            "json_get_string(json, \"date\", cfg->date, sizeof cfg->date);\n"
            "json_get_double(json, \"dialfreq\", &cfg->dialfreq);\n"
            "fputs(\"{\\\"v\\\":1,\\\"t\\\":\\\"ready\\\"}\\n\", stdout);\n"
            "wsprd_stream_emit_error_code(\"odd_audio_frame\", \"x\");\n")

    def test_names_from_each_call_form(self):
        names = check.source_names(check.Source(self.repo))
        self.assertEqual(names["control"], {"halt", "configure"})
        self.assertEqual(names["keys"], {"depth", "quoted", "t", "id", "freq"})
        self.assertEqual(names["lines"], {"ready", "error", "rendered", "packed"})
        self.assertEqual(names["codes"], {"configure_type_error", "frame_too_large"})
        self.assertEqual(names["reasons"], {"bad_request", "missing", "too_long"})
        self.assertEqual(names["wsprd_control"], {"halt"})
        self.assertEqual(names["wsprd_keys"], {"date", "dialfreq"})
        self.assertEqual(names["wsprd_lines"], {"ready"})
        self.assertEqual(names["wsprd_codes"], {"odd_audio_frame"})

    def test_fields_go_to_the_line_they_are_written_in(self):
        (self.repo / "lib/streaming_fields.f90").write_text(
            "module m\n"
            "contains\n"
            "  subroutine one_line_(x)\n"
            "    marks = ',\"extra\":1'  ! \"commented\":1\n"
            "    call put_('{\"v\":1,\"t\":\"alpha\",\"a\":' // marks // '}')\n"
            "  end subroutine one_line_\n"
            "  subroutine two_lines_(x)\n"
            "    line = '{\"v\":1,\"t\":\"beta\",\"b\":' // &\n"
            "         x\n"
            "    if (x) line = line // ',\"b2\":1'\n"
            "    call emit_line_('{\"v\":1,\"t\":\"gamma\",\"c\":1}')\n"
            "    call emit_line_(line // ',\"b3\":1}')\n"
            "    call helper_(list)\n"
            "    list2 = ',\"lost\":1'\n"
            "    later = ',\"late\":1'\n"
            "    call emit_line_(line // trim(later) // '}')\n"
            "  end subroutine two_lines_\n"
            "  function shared_() result(f)\n"
            "    f = '\"s\":1'\n"
            "  end function shared_\n"
            "  subroutine helper_(list)\n"
            "    list = '{\"h\":1}'\n"
            "  end subroutine helper_\n"
            "  subroutine three_()\n"
            "    call put_('{\"v\":1,\"t\":\"delta\",' // shared_() // '}')\n"
            "  end subroutine three_\n"
            "end module m\n")
        fields, unplaced = check.source_fields(check.Source(self.repo))["fields"]
        self.assertEqual(fields["alpha"], {"a", "extra"})
        self.assertEqual(fields["beta"], {"b", "b2", "b3", "late"})
        self.assertEqual(fields["gamma"], {"c"})
        self.assertEqual(fields["delta"], {"s"})
        self.assertEqual(unplaced, {("helper_", "h"), ("two_lines_", "lost")})
        wsprd_fields, wsprd_unplaced = check.source_fields(check.Source(self.repo))["wsprd_fields"]
        self.assertEqual(dict(wsprd_fields), {"ready": set()})
        self.assertEqual(wsprd_unplaced, set())

    def test_enumerated_values_from_each_call_form(self):
        (self.repo / "lib/streaming_decode.f90").write_text(
            "    call skip_pass(op%nzhsym, .false., 'busy', op%params, op%form)\n"
            "       if (deferred) call skip_pass(deferred_params%nzhsym, .true.,         &\n"
            "            'cancelled', deferred_params, deferred_form)\n"
            "    stage = merge('final', 'early', final)  ! stage = merge('later', 'sooner', x)\n")
        (self.repo / "lib/streaming_jtty.f90").write_text("             state = 'growing'\n")
        names = check.source_names(check.Source(self.repo))
        self.assertEqual(names["skip_reasons"], {"busy", "cancelled"})
        self.assertEqual(names["skip_stages"], {"final", "early"})
        self.assertEqual(names["jtty_states"], {"growing"})

    def test_joined_literals_and_literal_values_are_read(self):
        for name in ("streaming_jtty.f90", "streaming_encode.f90"):
            (self.repo / "lib" / name).unlink()
        (self.repo / check.CONTROL_SOURCE).write_text(
            "  subroutine parse_control_frame(buf)\n"
            "    call get_int_(buf, '\"' // 'rows' // '\"', v, ok)\n"
            "    call get_int_(buf, '\"mode\"', v, ok)\n"
            "  end subroutine parse_control_frame\n"
            "  subroutine parse_slot_request(buf)\n"
            "    call request_int_(buf, '\"mode\"', v, present, ok)\n"
            "    call request_int_(buf, '\"fre' // &\n"
            "         'q\"', v, present, ok)\n"
            "  end subroutine parse_slot_request\n")
        (self.repo / "lib/streaming_emit.f90").write_text(
            "    call put_('{\"v\":1,\"t\":' // '\"flushed\"}')\n"
            "    call put_('{\"v\":1,\"t\":\"flu' // 'shing\"}')\n"
            "    call put_('{\"v\":1,\"t\":\"error\",\"code\":\"disk_' // 'full\"}')\n"
            "    call put_('{\"v\":1,\"t\":\"rejected\",\"id\":1,\"reason\":\"too_fast\"}')\n"
            "    call put_('{\"v\":1,\"t\":\"pass_skipped\",\"stage\":\"middle\",\"reason\":\"late\"}')\n"
            "    call emit_line_('{\"v\":1,\"t\":\"jtty_update\",\"state\":\"paused\"}')\n")
        (self.repo / check.WSPRD_SOURCE).write_text(
            "fputs(\"{\\\"v\\\":1,\\\"t\\\":\" \"\\\"flushed\\\"}\\n\", stdout);\n"
            "fputs(\"{\\\"v\\\":1,\\\"t\\\":\\\"de\"\n      \"code\\\"}\\n\", stdout);\n")
        names = check.source_names(check.Source(self.repo))
        self.assertEqual(names["keys"], {"rows", "mode"})
        self.assertEqual(names["request_keys"], {"mode", "freq"})
        self.assertEqual(names["lines"], {"flushed", "flushing", "error", "rejected", "pass_skipped", "jtty_update"})
        self.assertEqual(names["codes"], {"disk_full"})
        self.assertEqual(names["reasons"], {"too_fast"})
        self.assertEqual(names["skip_reasons"], {"late"})
        self.assertEqual(names["skip_stages"], {"middle"})
        self.assertEqual(names["jtty_states"], {"paused"})
        self.assertEqual(names["wsprd_lines"], {"flushed", "decode"})
        problems = self.guards("lib/streaming_receive.f90", "    call put_('{\"v\":1,\"t\":\"busy\",\"state\":\"idle\"}')\n")
        self.assertIn("unparsed state `idle`, in a statement naming no line it belongs to", problems)

    def test_keys_in_a_request_routines_internal_procedure_are_request_keys(self):
        (self.repo / check.CONTROL_SOURCE).write_text(
            "  subroutine parse_slot_request(buf)\n"
            "  contains\n"
            "    subroutine refuse_(x)\n"
            "      call request_int_(buf, '\"tone_count\"', v, present, ok)\n"
            "    end subroutine refuse_\n"
            "  end subroutine parse_slot_request\n")
        names = check.source_names(check.Source(self.repo))
        self.assertEqual(names["request_keys"], {"tone_count"})
        self.assertEqual(names["keys"], set())

    def test_names_outside_the_name_characters_fail(self):
        problems = self.guards("lib/streaming_receive.f90",
                               "    call put_('{\"v\":1,\"t\":\"Flushed\"}')\n"
                               "    call put_('{\"v\":1,\"t\":\"rejected\",\"reason\":\"too-fast\"}')\n")
        self.assertEqual(problems.count("unparsed name with a character outside a-z, 0-9 and _"), 2)

    def guards(self, path, body, routine="probe_"):
        """The guard problems for body, written as a routine in path, beside stubs of the stream's emitters."""
        stubs = ["streaming_emit_error_code", "streaming_emit_error_range", "encode_emit_rejected", "emit_rejected_",
                 "streaming_emit_pass_skipped", "skip_pass"]
        (self.repo / "lib/streaming_stubs.f90").write_text(
            "".join(f"  subroutine {name}()\n  end subroutine {name}\n" for name in stubs))
        (self.repo / path).write_text(f"module g\ncontains\n  subroutine {routine}(dummy)\n{body}  end subroutine {routine}\nend module g\n")
        return "\n".join(check.source_guards(check.Source(self.repo)))

    def test_control_messages_read_with_either_operator(self):
        (self.repo / check.CONTROL_SOURCE).write_text("    if (trim(t_value) == 'spectrum') then\n    if (t_value /= 'rows') then\n")
        self.assertEqual(check.source_names(check.Source(self.repo))["control"], {"spectrum", "rows"})

    def test_unparsed_control_messages_fail(self):
        problems = self.guards(check.CONTROL_SOURCE, "    select case (trim(t_value))\n    case ('spectrum')\n    end select\n"
                               "    if ('rows' == trim(t_value)) then\n    kind_value = t_value\n")
        self.assertIn("unparsed control-message form, which the check cannot read: select case (trim(t_value))", problems)
        self.assertIn("unparsed control-message form, which the check cannot read: if ('rows' == trim(t_value))", problems)
        self.assertIn("unparsed control-message form, which the check cannot read: kind_value = t_value", problems)

    def test_unparsed_error_codes_fail(self):
        problems = self.guards("lib/streaming_receive.f90",
                               "    call streaming_emit_error_code(name, 'x')\n"
                               "    call streaming_emit_error_code(code='file_error', detail='x')\n"
                               "    call put_('{\"v\":1,\"t\":\"error\",\"code\":\"' // trim(name) // '\"}')\n"
                               "    call streaming_emit_error_code(dummy, 'passed on')\n")
        self.assertIn("unparsed error code, which the check cannot read: call streaming_emit_error_code(name, 'x')", problems)
        self.assertIn("unparsed error code, which the check cannot read: call streaming_emit_error_code(code='file_error'", problems)
        self.assertIn("unparsed code, written from outside a literal, which the check cannot read: call put_(", problems)
        self.assertIn("unparsed error code, which the check cannot read: call streaming_emit_error_code(dummy, 'passed on')", problems)

    def test_error_emitters_the_check_cannot_see_fail(self):
        problems = self.guards("lib/streaming_receive.f90",
                               "    call emit_error_('file_error', 'x')\n"
                               "    call streaming_emit_error_range('phase_eq', 'MSK144', 'x')\n"
                               "    call put_('{\"v\":1,\"t\":\"error\",\"code\":\"' // trim(dummy) // '\"}')\n")
        self.assertIn("unparsed error or rejection, through emit_error_, which the check cannot read: call emit_error_('file_error'", problems)
        self.assertNotIn("streaming_emit_error_range", problems)
        self.assertIn("unparsed code, written from outside a literal", problems)
        problems = self.guards("lib/streaming_emit.f90",
                               "    call put_('{\"v\":1,\"t\":\"error\",\"code\":\"' // trim(dummy) // '\"}')\n",
                               routine="streaming_emit_error_code")
        self.assertNotIn("lib/streaming_emit.f90", problems)

    def test_unparsed_rejected_reasons_fail(self):
        problems = self.guards("lib/streaming_encode.f90",
                               "    call encode_emit_rejected(id, reason='gone', key='', detail='x')\n"
                               "    call emit_rejected_(id, why, key, detail)\n"
                               "    call emit_rejected_(id, tx_reason_(status), key, detail)\n")
        self.assertIn("unparsed rejected reason, which the check cannot read: call encode_emit_rejected(id, reason='gone'", problems)
        self.assertIn("unparsed rejected reason, which the check cannot read: call emit_rejected_(id, why", problems)
        self.assertNotIn("tx_reason_", problems)
        self.assertIn("unparsed rejected reason, which the check cannot read: call emit_rejected_(id, dummy", self.guards(
            "lib/streaming_encode.f90", "    call emit_rejected_(id, dummy, key, detail)\n"))
        self.assertEqual(self.guards("lib/streaming_encode.f90", "    call emit_rejected_(id, dummy, key, detail)\n",
                                     routine="encode_emit_rejected"), "")

    def test_unparsed_keys_fail(self):
        problems = self.guards(check.CONTROL_SOURCE,
                               "    character(len=*), parameter :: KEYS(1) = [character(len=8) :: 'id']\n"
                               "    call get_int_(buf, '\"' // 'rows' // '\"', v, ok)\n"
                               "    call value_category_(buf, '\"' // trim(KEYS(i)) // '\"', cat)\n"
                               "    call value_category_(buf, '\"' // trim(OTHER(i)) // '\"', cat)\n"
                               "    call get_int_(buf, dummy, v, ok)\n"
                               "    key = unknown_key_(buf, ALLOWED)\n")
        self.assertNotIn("'rows'", problems)
        self.assertIn("unparsed key, which the check cannot read: call value_category_(buf, '\"' // trim(OTHER(i))", problems)
        self.assertNotIn("KEYS(i)", problems)
        self.assertIn("unparsed key, which the check cannot read: call get_int_(buf, dummy", problems)
        self.assertEqual(self.guards(check.CONTROL_SOURCE, "    call find_key_(buf, dummy, vstart, blen, found)\n",
                                     routine="get_int_"), "")
        self.assertIn("unparsed key list, which the check cannot read: key = unknown_key_(buf, ALLOWED)", problems)
        problems = self.guards("lib/streaming_receive.f90", "    call get_bool_(buf, '\"spectrum\"', v, ok)\n")
        self.assertIn("unparsed key, read outside lib/streaming_control.f90", problems)

    def test_blanks_at_a_colon_fail(self):
        problems = self.guards("lib/streaming_receive.f90",
                               "    call put_('{\"v\":1,\"t\": \"rows\"}')\n"
                               "    call put_('{\"v\":1,\"t\":\"rows\",\"snr\" :1}')\n"
                               "    call put_('{\"v\":1,\"t\":\"error\",\"code\": \"x\"}')\n")
        self.assertEqual(problems.count("unparsed field or value, with a blank at its colon"), 3)
        (self.repo / check.WSPRD_SOURCE).write_text(
            'printf("{\\"v\\":1,\\"t\\": \\"rows\\"}");\n'
            'printf("{\\"v\\":1,\\"t\\":\\"decode\\",\\"snr\\" :1}");\n')
        problems = "\n".join(check.source_guards(check.Source(self.repo)))
        self.assertEqual(problems.count("lib/wsprd/wsprd_stream.c: unparsed field or value, with a blank at its colon"), 2)

    def test_unparsed_lines_and_fields_fail(self):
        problems = self.guards("lib/streaming_receive.f90",
                               "    call put_('{\"v\":1,\"t\":\"' // trim(name) // '\"}')\n"
                               "    call put_('{\"v\":1,\"t\":\"rows\",\"' // trim(field) // '\":1}')\n")
        self.assertIn("unparsed output line, which the check cannot read: call put_('{\"v\":1,\"t\":\"' // trim(name)", problems)
        self.assertIn("unparsed field, which the check cannot read: call put_('{\"v\":1,\"t\":\"rows\"", problems)

    def test_unparsed_enumerated_values_fail(self):
        problems = self.guards("lib/streaming_decode.f90",
                               "    call skip_pass(n, .true., why, p, f)\n"
                               "    stage = kinds(i)\n"
                               "    call streaming_emit_pass_skipped(m, u, h, n, stage, why)\n")
        self.assertIn("unparsed pass_skipped reason, which the check cannot read: call skip_pass(n, .true., why", problems)
        self.assertIn("unparsed pass_skipped stage, which the check cannot read: call streaming_emit_pass_skipped", problems)
        self.assertIn("unparsed pass_skipped reason, which the check cannot read: call streaming_emit_pass_skipped", problems)
        problems = self.guards("lib/streaming_jtty.f90",
                               "    state = names(i)\n    call emit_line_(',\"state\":\"' // state // '\"')\n")
        self.assertIn("unparsed jtty_update state", problems)

    def test_unparsed_wsprd_forms_fail(self):
        (self.repo / check.WSPRD_SOURCE).write_text(
            'char event_type[16];\n'
            'if (json_get_string(json, "t", event_type, sizeof event_type) != 0) {}\n'
            'if (strcmp(event_type, "halt") == 0) {}\n'
            'if (strncmp(event_type, "flush", 5) == 0) {}\n'
            'json_get_int(json, name, &x);\n'
            'json_get_int(body, "rows", &x);\n'
            'json_get_bool(json, "quiet", &x);\n'
            'json_escape_into(message, out, sizeof out);\n'
            'static const char *json_find_key(const char *json, const char *key) { return json_find_key(json, key); }\n'
            'void wsprd_stream_emit_error_code(const char *code, const char *detail) {}\n'
            'wsprd_stream_emit_error_code(code_name, "x");\n'
            'fprintf(stdout, "{\\"v\\":1,\\"t\\":\\"%s\\"}", t);\n'
            'fprintf(stdout, "{\\"v\\":1,\\"t\\":\\"decode\\",\\"%s\\":1}", f);\n')
        problems = "\n".join(check.source_guards(check.Source(self.repo)))
        self.assertIn('unparsed control-message form, which the check cannot read: if (strncmp(event_type, "flush", 5)', problems)
        self.assertEqual(problems.count("control-message form"), 1)
        self.assertIn("unparsed key, which the check cannot read: json_get_int(json, name", problems)
        self.assertIn('unparsed key, which the check cannot read: json_get_int(body, "rows"', problems)
        self.assertIn('unparsed key, which the check cannot read: json_get_bool(json, "quiet"', problems)
        self.assertEqual(problems.count("unparsed key"), 3)
        self.assertIn("unparsed error code, which the check cannot read: wsprd_stream_emit_error_code(code_name", problems)
        self.assertEqual(problems.count("unparsed error code"), 1)
        self.assertIn("unparsed output line", problems)
        self.assertIn("unparsed field", problems)

    def test_ref_reads_the_source_at_the_ref(self):
        env = {**{k: v for k, v in os.environ.items() if not k.startswith("GIT_")},
               "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
               "GIT_AUTHOR_NAME": "A", "GIT_AUTHOR_EMAIL": "a@example.com",
               "GIT_COMMITTER_NAME": "C", "GIT_COMMITTER_EMAIL": "c@example.com"}

        def git(*args):
            subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True, env=env)

        git("init", "-q")
        git("config", "maintenance.auto", "false")
        git("add", "-A")
        git("commit", "-q", "-m", "start")
        with open(self.repo / check.CONTROL_SOURCE, "a", encoding="utf-8") as source:
            source.write("    call get_int_(buf, '\"later\"', v, ok)\n")
        self.assertIn("later", check.source_names(check.Source(self.repo))["keys"])
        self.assertNotIn("later", check.source_names(check.Source(self.repo, "HEAD"))["keys"])
        with self.assertRaises(check.SourceError):
            check.source_names(check.Source(self.repo, "no-such-ref"))


class SlugTest(unittest.TestCase):
    def test_github_anchors(self):
        seen = {}
        self.assertEqual(check.slug("wsprd error codes", seen), "wsprd-error-codes")
        self.assertEqual(check.slug("`OMP_STACKSIZE` and threads", seen), "omp_stacksize-and-threads")
        self.assertEqual(check.slug("Pack, render & JTTY", seen), "pack-render--jtty")
        self.assertEqual(check.slug("wsprd error codes", seen), "wsprd-error-codes-1")


if __name__ == "__main__":
    unittest.main()
