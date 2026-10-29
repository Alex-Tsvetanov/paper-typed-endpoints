#!/usr/bin/env bash
# Deterministic constant-evaluation cost (not timing): the smallest -fconstexpr-ops-limit under
# which GCC accepts a stage of the reflection pipeline for a generated schema. Bisection.
# Usage: constexpr_ops.sh <wide|many> <n> <tokenize|parser|parse|tables|define>   (run inside papers-dev; CXX picks the compiler, default g++; CXXFLAGS adds flags, e.g. -O2,
# which drops libstdc++'s default assertions of unoptimized builds)
set -euo pipefail
shape=$1 n=$2 stage=$3
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
"${PYTHON:-python3}" "$repo/bench/gen_schema.py" schema --shape "$shape" --n "$n" ${GEN_FLAGS:-} > "$work/schema.proto"
{
  echo '#include <string_view>'
  case $stage in
    tokenize|parser|parse) echo '#include "tep/schema.hpp"' ;;
    *) echo '#include "tep/reflect.hpp"' ;;
  esac
  echo 'struct schema { static constexpr char data[] = {'
  echo '#embed "schema.proto"'
  echo ", '\0'}; static constexpr std::string_view text{data, sizeof data - 1}; };"
  case $stage in
    tokenize) echo 'static_assert(tep::schema::tokenize(schema::text).has_value());' ;;
    parser) echo 'static_assert([] { auto t = tep::schema::tokenize(schema::text); tep::schema::detail::parser p(std::move(*t)); return p.parse_file(); }());' ;;
    parse) echo 'static_assert(tep::schema::parse(schema::text).has_value());' ;;
    tables) echo 'static_assert(!tep::tables<schema>.fields.empty());' ;;
    define) echo 'consteval { tep::define_messages<schema>(); }' ;;
  esac
} > "$work/tu.cpp"
flags=(-std=c++26 -fsyntax-only -I "$repo/include" ${CXXFLAGS:-})
case $stage in tables|define) flags+=(-freflection) ;; esac
ok() { "${CXX:-g++}" "${flags[@]}" "-fconstexpr-ops-limit=$1" "$work/tu.cpp" > /dev/null 2>&1; }
hi=${MAX:-33554432}  # GCC's default limit; MAX raises the ceiling to count past it
if ! ok $hi; then echo "$shape $n $stage: above $hi"; rm -rf "$work"; exit 0; fi
lo=0
while [ $((hi - lo)) -gt $((hi / 200 + 1)) ]; do
  mid=$(((lo + hi) / 2))
  if ok $mid; then hi=$mid; else lo=$mid; fi
done
echo "$shape $n $stage: ops <= $hi (within 0.5%)"
rm -rf "$work"
