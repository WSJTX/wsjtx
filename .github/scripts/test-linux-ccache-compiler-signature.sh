#!/usr/bin/env bash
set -euo pipefail

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/bin" "$fixture/libexec"

for component in cc1 cc1plus lto1 as; do
  printf '%s\n' "$component fixture contents" > "$fixture/libexec/$component"
done
printf '%s\n' 'fixture specs' > "$fixture/specs"
printf '%s\n' 'x86_64-fixture-linux-gnu' > "$fixture/target"
printf '%s\n' 'fixture runtime library' > "$fixture/libexec/libcompiler-runtime.so"

cat > "$fixture/bin/gcc-99" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$1" in
  -dumpfullversion) echo 99.1.0 ;;
  -dumpmachine) cat "$root/target" ;;
  -dumpspecs) cat "$root/specs" ;;
  -print-prog-name=*) echo "$root/libexec/${1#*=}" ;;
  *) exit 2 ;;
esac
EOF
cp "$fixture/bin/gcc-99" "$fixture/bin/g++-99"
cat > "$fixture/bin/ldd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
printf 'libcompiler-runtime.so => %s/libexec/libcompiler-runtime.so (0x00000000)\n' "$root"
EOF
chmod +x "$fixture/bin/gcc-99" "$fixture/bin/g++-99" "$fixture/bin/ldd"

signature() {
  PATH="$fixture/bin:$PATH" \
    .github/scripts/linux-ccache-compiler-signature.sh gcc-99
}

first="$(signature)"
IFS=$'\t' read -r version target driver_sha compatibility_id signature_sha <<< "$first"
test "$version" = 99.1.0
test "$target" = x86_64-fixture-linux-gnu
test "$compatibility_id" = gcc99-v2
[[ "$driver_sha" =~ ^[0-9a-f]{64}$ ]]
[[ "$signature_sha" =~ ^[0-9a-f]{64}$ ]]

touch "$fixture/bin/gcc-99" "$fixture/libexec/cc1"
test "$(signature)" = "$first"

previous=$first
for input in \
  "$fixture/bin/gcc-99" \
  "$fixture/bin/g++-99" \
  "$fixture/libexec/cc1" \
  "$fixture/libexec/cc1plus" \
  "$fixture/libexec/lto1" \
  "$fixture/libexec/as" \
  "$fixture/libexec/libcompiler-runtime.so" \
  "$fixture/specs"; do
  printf '%s\n' '# signature change' >> "$input"
  current="$(signature)"
  if [ "$current" = "$previous" ]; then
    echo "Expected compiler signature to change after modifying $input" >&2
    exit 1
  fi
  previous=$current
done

printf '%s\n' 'aarch64-fixture-linux-gnu' > "$fixture/target"
test "$(signature)" != "$previous"

echo "linux-ccache-compiler-signature.sh self-test passed"
