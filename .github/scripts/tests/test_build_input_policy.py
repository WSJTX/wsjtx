# SPDX-License-Identifier: GPL-3.0-or-later
"""Build inputs that scope the Linux tests and how long CI packages are kept.

runtime_tests (include, exclude or only) keeps, drops or alone runs the
tests labelled runtime on Linux x86_64 and aarch64. Release builds ignore it
and keep their own exclusion, so the ctest line carries one chained
expression that emits at most one label filter: ctest ANDs repeated -LE
patterns.

artifact_retention_days sets retention-days on the package, binary and
tarball uploads only. Its default, 0, is upload-artifact's "repository
default", so callers that pass nothing (the release workflows) keep it.
"""

import os
import pathlib
import re
import subprocess
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
PAYLOAD = ".github/actions/build-linux-payload/action.yml"
TARBALLS = ".github/actions/upload-release-tarballs/action.yml"
LINUX = ".github/workflows/build-linux.yml"
MACOS = ".github/workflows/build-macos.yml"
WINDOWS = ".github/workflows/build-windows.yml"
VALIDATOR = ".github/scripts/validate-ci-inputs.sh"
RETENTION = "${{ inputs.artifact_retention_days }}"

# (job, step) of every upload that takes artifact_retention_days; composite actions have no job.
RETAINED = {
    PAYLOAD: {"Upload .deb", "Upload RPM", "Upload AppImage", "Upload build artifacts"},
    TARBALLS: {"Upload jt9 tarball", "Upload codec tarball", "Upload wsprd tarball",
               "Upload utilities tarball"},
    LINUX: {("test-armhf", "Upload .deb"), ("test-armhf", "Upload RPM"),
            ("test-armhf", "Upload AppImage"), ("test-armhf", "Upload build artifacts")},
    MACOS: {("build", "Upload pkg"), ("build", "Upload individual binaries")},
    WINDOWS: {("build", "Upload installer artifact"), ("build", "Upload build artifacts")},
}
# Jobs whose "Validate workflow inputs" step guards those uploads.
VALIDATED = {PAYLOAD: None, LINUX: "build-armhf", MACOS: "build", WINDOWS: "build"}

# GitHub compares strings without regard to case; every value here is lower case.
RELEASE_CONTEXTS = (
    ("refs/tags/v3.3.0", "push"),
    ("refs/tags/build/v3.3.0", "push"),
    ("refs/heads/release/3.3", "workflow_dispatch"),
)
ROUTINE_CONTEXTS = (
    ("refs/heads/develop", "push"),
    ("refs/heads/develop", "schedule"),
    ("refs/heads/develop", "workflow_dispatch"),
    ("refs/pull/852/merge", "pull_request"),
    ("refs/heads/release/3.3", "push"),
)
ROUTINE_FILTERS = {"include": "", "exclude": "-LE ^runtime$", "only": "-L ^runtime$"}
LABEL_FLAGS = {"-L", "-LE", "--label-regex", "--label-exclude"}


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def under(lines, key, indent):
    """The lines nested under `key:` at this indent, or None when it is absent."""
    for index, line in enumerate(lines):
        if line == " " * indent + key + ":":
            body = []
            for nested in lines[index + 1:]:
                if nested.strip() and len(nested) - len(nested.lstrip(" ")) <= indent:
                    break
                body.append(nested)
            return body
    return None


def mapping(lines, indent):
    """Keys at exactly this indent: {key: (inline value, nested lines)}."""
    result = {}
    key = None
    for line in lines:
        match = re.fullmatch(r" {%d}([A-Za-z_][\w-]*):(?: (.*))?" % indent, line)
        if match:
            key = match[1]
            result[key] = ((match[2] or "").strip(), [])
        elif key is not None:
            result[key][1].append(line)
    return result


def unquote(value):
    return value[1:-1] if len(value) >= 2 and value[0] == value[-1] == '"' else value


def steps(text):
    """Every step of every job: [(job or "", {key: (value, lines)}, step text)]."""
    lines = text.splitlines()
    found = []
    job = ""
    for index, line in enumerate(lines):
        job_match = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if job_match:
            job = job_match[1]
        match = re.fullmatch(r"( *)steps:", line)
        if not match:
            continue
        item = " " * (len(match[1]) + 2) + "- "
        current = None
        for nested in lines[index + 1:]:
            if nested.strip() and len(nested) - len(nested.lstrip(" ")) <= len(match[1]):
                break
            if nested.startswith(item):
                current = [" " * len(item) + nested[len(item):]]
                found.append((job, current))
            elif current is not None and (not nested.strip() or nested.startswith(" " * len(item))):
                current.append(nested)
            else:
                current = None
    return [(job, mapping(body, len(item)), "\n".join(body) + "\n") for job, body in found]


def step(text, name, job=None):
    """The one step with this name (in this job): (fields, step text)."""
    matches = [(fields, body) for owner, fields, body in steps(text)
               if fields.get("name", ("",))[0] == name and job in (None, owner)]
    if len(matches) != 1:
        raise AssertionError(f"expected one step named {name!r} in job {job!r}, found {len(matches)}")
    return matches[0]


def job_needs(text, job):
    """The jobs a workflow job lists in an inline needs: [a, b] or needs: a."""
    body = re.search(r"^  %s:\n(?P<body>(?:    .*\n|\n)*)" % re.escape(job), text, re.MULTILINE)
    if body is None:
        raise AssertionError(f"no job {job!r}")
    needs = re.search(r"^    needs: (.+)$", body["body"], re.MULTILINE)
    return [] if needs is None else [name.strip() for name in needs[1].strip("[]").split(",")]


def nested(fields, key, body):
    """{key: value} one level under a step key such as with or env."""
    indent = len(body) - len(body.lstrip(" ")) + 2
    return {name: value for name, (value, _) in mapping(fields.get(key, ("", []))[1], indent).items()}


def inputs(text, trigger=None):
    """Input declarations: {name: {property: value}} for a workflow trigger or an action."""
    lines = text.splitlines()
    if trigger is None:
        declared, indent = under(lines, "inputs", 0), 2
    else:
        declared, indent = under(under(under(lines, "on", 0), trigger, 2), "inputs", 4), 6
    result = {}
    for name, (_, body) in mapping(declared, indent).items():
        properties = {key: value for key, (value, _) in mapping(body, indent + 2).items()}
        options = mapping(body, indent + 2).get("options", ("", []))[1]
        properties["options"] = [option.strip().removeprefix("- ") for option in options if option.strip()]
        result[name] = properties
    return result


def evaluate(expression, ref, event_name, runtime_tests):
    """Evaluate a GitHub expression made of startsWith, ==, !=, &&, || and literals."""
    values = {"github.ref": ref, "github.event_name": event_name,
              "inputs.runtime_tests": runtime_tests}
    python = re.sub(r"\b(?:github\.ref|github\.event_name|inputs\.runtime_tests)\b",
                    lambda match: repr(values[match[0]]), expression)
    if re.search(r"\b(?:github|inputs|env|steps|needs|matrix|vars)\.", python):
        raise AssertionError(f"unexpected context in {expression}")
    python = python.replace("&&", " and ").replace("||", " or ")
    python = re.sub(r"!(?!=)", " not ", python)
    return eval(python, {"__builtins__": {}},
                {"startsWith": lambda text, prefix: text.lower().startswith(prefix.lower())})


class RuntimeTestsPolicyTests(unittest.TestCase):
    def ctest_command(self):
        value, lines = step(read(PAYLOAD), "Run tests")[0]["run"]
        self.assertEqual(value, ">-")
        return " ".join(line.strip() for line in lines if line.strip())

    def ctest_expression(self):
        expressions = re.findall(r"\$\{\{(.*?)\}\}", self.ctest_command())
        self.assertEqual(len(expressions), 1, "one ${{ }} on the ctest line")
        return expressions[0]

    def ctest_arguments(self, ref, event_name, runtime_tests):
        command = self.ctest_command()
        flags = evaluate(self.ctest_expression(), ref, event_name, runtime_tests)
        command = re.sub(r"\$\{\{.*?\}\}", flags, command)
        arguments = command.split(" ctest ", 1)[1]
        result = subprocess.run(["bash", "-c", "printf '%s\\n' " + arguments],
                                capture_output=True, text=True, check=True)
        return flags, result.stdout.splitlines()

    def test_the_ctest_line_has_one_expression_and_anchored_labels(self):
        expression = self.ctest_expression()
        self.assertIn("'-LE ^runtime$'", expression)
        self.assertIn("'-L ^runtime$'", expression)
        self.assertIsNone(re.search(r"-LE? (?!\^runtime\$)\^?runtime", expression))
        self.assertEqual(self.ctest_command().count("ctest "), 1)

    def test_release_builds_keep_their_exclusion_whatever_the_input(self):
        for ref, event_name in RELEASE_CONTEXTS:
            for runtime_tests in ROUTINE_FILTERS:
                with self.subTest(ref=ref, event_name=event_name, runtime_tests=runtime_tests):
                    flags, arguments = self.ctest_arguments(ref, event_name, runtime_tests)
                    self.assertEqual(flags, "-LE decode-thread-long")
                    self.assertEqual(arguments[2:4], ["-LE", "decode-thread-long"])
                    self.assertEqual(sum(argument in LABEL_FLAGS for argument in arguments), 1)

    def test_other_builds_select_the_runtime_label(self):
        for ref, event_name in ROUTINE_CONTEXTS:
            for runtime_tests, expected in ROUTINE_FILTERS.items():
                with self.subTest(ref=ref, event_name=event_name, runtime_tests=runtime_tests):
                    flags, arguments = self.ctest_arguments(ref, event_name, runtime_tests)
                    self.assertEqual(flags, expected)
                    self.assertEqual(arguments[:2], ["--parallel", "3"])
                    self.assertEqual(arguments[2:-3], expected.split())
                    self.assertEqual(arguments[-3:], ["--output-on-failure", "--output-junit",
                                                      "ctest-results.xml"])
                    self.assertLessEqual(sum(argument in LABEL_FLAGS for argument in arguments), 1)

    def test_runtime_tests_defaults_to_include(self):
        payload = inputs(read(PAYLOAD))["runtime_tests"]
        self.assertEqual(payload["default"], '"include"')
        self.assertEqual(payload["required"], "false")
        workflow = read(LINUX)
        called = inputs(workflow, "workflow_call")["runtime_tests"]
        self.assertEqual((called["type"], called["default"]), ("string", '"include"'))
        dispatched = inputs(workflow, "workflow_dispatch")["runtime_tests"]
        self.assertEqual((dispatched["type"], unquote(dispatched["default"])), ("choice", "include"))
        self.assertEqual(dispatched["options"], ["include", "exclude", "only"])

    def test_both_native_payload_calls_pass_runtime_tests(self):
        calls = [(fields, body) for _, fields, body in steps(read(LINUX))
                 if fields.get("uses", ("",))[0] == "./.github/actions/build-linux-payload"]
        self.assertEqual(len(calls), 2)
        for fields, body in calls:
            self.assertEqual(nested(fields, "with", body).get("runtime_tests"), "${{ inputs.runtime_tests }}")

    def test_the_payload_validates_runtime_tests_before_it_runs_ctest(self):
        text = read(PAYLOAD)
        names = [fields.get("name", ("",))[0] for _, fields, _ in steps(text)]
        self.assertLess(names.index("Validate workflow inputs"), names.index("Run tests"))
        validate, body = step(text, "Validate workflow inputs")
        self.assertEqual(validate["run"][0], VALIDATOR)
        self.assertEqual(nested(validate, "env", body).get("RUNTIME_TESTS"), "${{ inputs.runtime_tests }}")

    def test_the_validator_accepts_only_the_three_choices(self):
        for value, accepted in (("include", True), ("exclude", True), ("only", True),
                                ("Include", False), ("EXCLUDE", False), ("", False), ("all", False)):
            with self.subTest(value=value):
                result = subprocess.run(["bash", str(ROOT / VALIDATOR)],
                                        env={"PATH": os.environ["PATH"], "RUNTIME_TESTS": value},
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_armhf_rejects_anything_but_include(self):
        resolve = [(fields, body) for job, fields, body in steps(read(LINUX))
                   if job == "resolve-image" and fields.get("id", ("",))[0] == "resolve"]
        self.assertEqual(len(resolve), 1)
        fields, body = resolve[0]
        self.assertEqual(nested(fields, "env", body).get("RUNTIME_TESTS"), "${{ inputs.runtime_tests }}")
        script = textwrap.dedent("\n".join(fields["run"][1]))
        guard = re.search(r'^if \[ "\$RUNTIME_TESTS" != include \].*?^fi$', script, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(guard, "runtime_tests guard in resolve-image")
        for arch in ("x86_64", "aarch64", "armhf"):
            for runtime_tests in ("include", "exclude", "only"):
                with self.subTest(arch=arch, runtime_tests=runtime_tests):
                    result = subprocess.run(["bash", "-c", guard[0]], capture_output=True, text=True,
                                            env={"PATH": os.environ["PATH"], "ARCH": arch,
                                                 "RUNTIME_TESTS": runtime_tests}, check=False)
                    rejected = arch == "armhf" and runtime_tests != "include"
                    self.assertEqual(result.returncode, 2 if rejected else 0, result.stdout)


class ArtifactRetentionPolicyTests(unittest.TestCase):
    def test_exactly_the_package_binary_and_tarball_uploads_take_the_input(self):
        for path, expected in RETAINED.items():
            with self.subTest(path=path):
                text = read(path)
                uploads = [(job, fields, body) for job, fields, body in steps(text)
                           if fields.get("uses", ("",))[0].startswith("actions/upload-artifact@")]
                self.assertEqual(len(uploads), text.count("uses: actions/upload-artifact@"))
                retained = set()
                for job, fields, body in uploads:
                    self.assertLessEqual(body.count("retention-days:"), 1)
                    if nested(fields, "with", body).get("retention-days") == RETENTION:
                        name = fields["name"][0]
                        retained.add(name if path in (PAYLOAD, TARBALLS) else (job, name))
                self.assertEqual(retained, expected)
                self.assertEqual(text.count(f"retention-days: {RETENTION}"), len(expected))

    def test_every_tarball_upload_passes_the_input(self):
        for path in (PAYLOAD, LINUX, MACOS, WINDOWS):
            with self.subTest(path=path):
                text = read(path)
                calls = [(fields, body) for _, fields, body in steps(text)
                         if fields.get("uses", ("",))[0] == "./.github/actions/upload-release-tarballs"]
                self.assertEqual(len(calls), text.count("upload-release-tarballs"))
                self.assertEqual(len(calls), 1)
                for fields, body in calls:
                    self.assertEqual(nested(fields, "with", body).get("artifact_retention_days"), RETENTION)

    def test_the_payload_passes_the_input_from_both_native_jobs(self):
        calls = [(fields, body) for _, fields, body in steps(read(LINUX))
                 if fields.get("uses", ("",))[0] == "./.github/actions/build-linux-payload"]
        self.assertEqual(len(calls), 2)
        for fields, body in calls:
            self.assertEqual(nested(fields, "with", body).get("artifact_retention_days"), RETENTION)

    def test_artifact_retention_days_defaults_to_the_repository_setting(self):
        for path in (LINUX, MACOS, WINDOWS):
            for trigger in ("workflow_call", "workflow_dispatch"):
                with self.subTest(path=path, trigger=trigger):
                    declared = inputs(read(path), trigger)["artifact_retention_days"]
                    self.assertEqual((declared["type"], declared["default"], declared["required"]),
                                     ("number", "0", "false"))
        for path in (PAYLOAD, TARBALLS):
            with self.subTest(path=path):
                declared = inputs(read(path))["artifact_retention_days"]
                self.assertEqual((declared["default"], declared["required"]), ('"0"', "false"))

    def test_the_input_is_validated_before_the_uploads(self):
        for path, job in VALIDATED.items():
            with self.subTest(path=path):
                text = read(path)
                found = steps(text)
                validations = [index for index, (owner, fields, _) in enumerate(found)
                               if fields.get("name", ("",))[0] == "Validate workflow inputs"
                               and job in (None, owner)]
                self.assertEqual(len(validations), 1)
                owner, fields, body = found[validations[0]]
                self.assertEqual(fields["run"][0], VALIDATOR)
                self.assertEqual(nested(fields, "env", body).get("ARTIFACT_RETENTION_DAYS"), RETENTION)
                # Each upload that takes the input runs after the validation: later in
                # the same job, or in a job that needs the validating one.
                uploads = [(index, upload_job, upload.get("name", ("",))[0])
                           for index, (upload_job, upload, upload_body) in enumerate(found)
                           if nested(upload, "with", upload_body).get("retention-days") == RETENTION
                           or upload.get("uses", ("",))[0] == "./.github/actions/upload-release-tarballs"]
                self.assertTrue(uploads)
                for index, upload_job, name in uploads:
                    if upload_job == owner:
                        self.assertLess(validations[0], index, f"{name} runs before the validation")
                    else:
                        self.assertIn(owner, job_needs(text, upload_job), f"{upload_job} does not need {owner}")
        for value, accepted in (("0", True), ("1", True), ("7", True), ("90", True), ("91", False),
                                ("-1", False), ("7.5", False), ("07", False), ("", False)):
            with self.subTest(value=value):
                result = subprocess.run(["bash", str(ROOT / VALIDATOR)],
                                        env={"PATH": os.environ["PATH"], "ARTIFACT_RETENTION_DAYS": value},
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_release_builds_keep_the_repository_retention(self):
        for path in (".github/workflows/release.yml", ".github/workflows/public-release.yml",
                     ".github/workflows/sign-windows-release.yml",
                     ".github/workflows/build-selected-artifacts.yml"):
            with self.subTest(path=path):
                self.assertNotIn("artifact_retention_days", read(path))


if __name__ == "__main__":
    unittest.main()
