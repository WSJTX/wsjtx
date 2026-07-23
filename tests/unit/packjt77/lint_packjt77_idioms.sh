#!/bin/sh

root=${1:-.}
fail=0

show_violation() {
  label=$1
  lines=$2
  echo "$label"
  echo "$lines"
  fail=1
}

pack_reads=$(grep -En "read *\(.*,\*" "$root/lib/77bit/packjt77.f90" || true)
if [ -n "$pack_reads" ]; then
  show_violation "list-directed read in packjt77.f90:" "$pack_reads"
fi

grammar_reads=$(grep -En "read *\(.*,\*" "$root/lib/77bit/packjt77_grammar.f90" || true)
grammar_read_count=0
grammar_read_extra=
while IFS= read -r line; do
  [ -z "$line" ] && continue
  case "$line" in
    *":  read(token,*,err=10) value")
      grammar_read_count=$((grammar_read_count + 1))
      ;;
    *)
      grammar_read_extra="${grammar_read_extra}
$line"
      ;;
  esac
done <<EOF
$grammar_reads
EOF
if [ "$grammar_read_count" -ne 2 ]; then
  show_violation "unexpected grammar list-directed read count:" "$grammar_reads"
fi
if [ -n "$grammar_read_extra" ]; then
  show_violation "unexpected grammar list-directed read:" "$grammar_read_extra"
fi

bracket_lines=$(grep -En "index *\(.*'[<>]'" "$root/lib/77bit/packjt77.f90" || true)
bracket_extra=
while IFS= read -r line; do
  [ -z "$line" ] && continue
  case "$line" in
    *":  i=index(cw,'>')"|\
    *":     i=index(cw,'>')"|\
    *":     i2=index(c13,'>')"|\
    *":  if(index(call_1,'<').le.0) then"|\
    *":  if(index(call_2,'<').le.0) then")
      ;;
    *)
      bracket_extra="${bracket_extra}
$line"
      ;;
  esac
done <<EOF
$bracket_lines
EOF
if [ -n "$bracket_extra" ]; then
  show_violation "unexpected bracket probing in packjt77.f90:" "$bracket_extra"
fi

legacy_lines=$(grep -En "call +(chkcall|split77)|(^|[^A-Za-z0-9_])(chkcall|split77) *\(" \
  "$root/lib/77bit/packjt77.f90" || true)
legacy_extra=
while IFS= read -r line; do
  [ -z "$line" ] && continue
  case "$line" in
    *":subroutine split77(msg,nwords,nw,w)"|\
    *":  call chkcall(w(3),bcall_1,ok1)"|\
    *":end subroutine split77")
      ;;
    *)
      legacy_extra="${legacy_extra}
$line"
      ;;
  esac
done <<EOF
$legacy_lines
EOF
if [ -n "$legacy_extra" ]; then
  show_violation "unexpected chkcall or split77 encode-path use:" "$legacy_extra"
fi

exit "$fail"
