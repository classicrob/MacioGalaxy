#!/usr/bin/env python3
"""The GX find_pipeline warm-path memo (native/gx/pipeline_fast_path.hpp) returns what the full lookup returns.

1. Source checks on the patched Aurora pipeline cache: the memo key is the exact runtime-key
   expression resolve_pipeline uses for GX; a miss takes the full lookup and stores only ready
   keys under the generation read before it; every mutation of state the full lookup reads
   (stage attribution, variant samples, the pipeline and known-config maps) happens inside a
   function that invalidates the memo on entry and exit.
2. A compiled model: random lookups, pipeline readiness and stage changes (including changes
   racing a full lookup) through the real header; the memoized result and the "bookkeeping a
   full lookup would still do" must match an always-full reference at every step.

usage: pipeline_fast_path_tests.py CXX --aurora AURORA_SOURCE_DIR
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('cxx')
parser.add_argument('--aurora', required=True, type=Path)
args = parser.parse_args()

sys.path.insert(0, str(root / 'native/gx'))
import patch_aurora_pipeline  # noqa: E402

text = patch_aurora_pipeline.patch((args.aurora / 'lib/gfx/pipeline_cache.cpp').read_text())


def function(signature):
    start = text.index(signature)
    depth = 0
    for i in range(text.index('{', start), len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise AssertionError(f'unterminated {signature}')


fast = function('PipelineRef find_pipeline(const gx::PipelineConfig& config, const RenderTargetLayout& layout) {')
full = function('static PipelineRef petari_find_pipeline_full(const gx::PipelineConfig& config, const RenderTargetLayout& layout) {')
resolve = function('static PipelineRef resolve_pipeline(ShaderType type, const gx::PipelineConfig& config, const RenderTargetLayout& layout,')

# The memo key is resolve_pipeline's runtime key for ShaderType::GX, and the full path returns resolve_pipeline's result.
key = re.search(r'const auto runtimeKey = (.+?);\n', resolve).group(1)
assert key == 'xxh3_hash(layout.key, xxh3_hash(config, static_cast<HashType>(type)))', key
assert f"const auto petariKey = {key.replace('(type)', '(ShaderType::GX)')};" in fast
assert re.search(r'return find_pipeline_impl\(runtimeKey,', resolve)
assert re.search(r'return resolve_pipeline\(ShaderType::GX, config, layout,', full)
# Generation read before the full lookup; hits return the key; misses store only ready, matching keys.
assert fast.index('PetariFastPipeline::current()') < fast.index('petari_find_pipeline_full(config, layout);')
assert re.search(r'if \(!PetariFastPipeline::checkEnabled\(\)\) return petariKey;', fast)
miss = fast[fast.index('PetariFastPipeline::misses'):]
assert re.search(r'if \(full == petariKey && current_frame\(\) != UINT32_MAX\)', miss)
assert miss.index('g_pipelines.contains(full)') < miss.index('PetariFastPipeline::store(petariKey, petariGeneration)')
assert 'return full;' in miss

# Every mutation of state the full lookup reads is inside an invalidating function.
invalidating = {}
for signature in ('void shutdown_pipeline_cache() {', 'static void petari_stage_reset() {',
                  'extern "C" void petari_gx_pipeline_stage_begin(const char* name, const char* seedPath) {'):
    body = function(signature)
    assert body.split('\n')[1].strip() == 'const PetariFastPipeline::InvalidateScope petariFastInvalidate;', signature
    start = text.index(body)
    invalidating[signature] = (start, start + len(body))
mutation = re.compile(r'petariActiveStage\s*=(?!=)|petariActiveStage\.(clear|assign|swap)|petariOverlayStages\.(insert|clear|erase|emplace)'
                      r'|petariStages\.(clear|erase)|\bstage\s*=\s*\{\};|\.configs\s*=(?!=)|petariPipelineSamples\.(clear|erase)'
                      r'|petariPipelineSamples\s*=(?!=)|g_pipelines\.(clear|erase)|g_knownPipelines\.(clear|erase)|\.drawRequested\s*=\s*false')
outside = [text.count('\n', 0, m.start()) + 1 for m in mutation.finditer(text)
           if not any(a <= m.start() < b for a, b in invalidating.values())]
assert not outside, f'state read by the full GX lookup changes outside an invalidating function at generated lines {outside}'
print('Fast path source checks: same runtime key, ready-only stores, all lookup-state mutations invalidate')

# Compiled model through the real header.
probe = r'''
#include "pipeline_fast_path.hpp"
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <thread>

namespace {
// What a full lookup reads and records: ready pipelines and per-stage first sightings.
std::set<std::uint64_t> ready;
int stage = 0;
std::set<std::pair<int, std::uint64_t>> noted;
long bookkeepingAfterHit = 0;

std::uint64_t full(std::uint64_t key) {
  noted.insert({stage, key});
  return key;
}
// The patched find_pipeline, against the model.
std::uint64_t find(std::uint64_t key, bool raceStage) {
  const auto generation = PetariFastPipeline::current();
  if (PetariFastPipeline::hit(key, generation)) {
    // A full lookup here would have to be a no-op.
    if (!ready.count(key) || !noted.count({stage, key})) ++bookkeepingAfterHit;
    return key;
  }
  const auto result = full(key);
  if (raceStage) { PetariFastPipeline::InvalidateScope scope; ++stage; }  // stage begin racing this lookup
  if (result == key && ready.count(key)) PetariFastPipeline::store(key, generation);
  return result;
}
}  // namespace

int main() {
  std::mt19937_64 random(7);
  long hits = 0;
  for (int step = 0; step < 400000; ++step) {
    const std::uint64_t key = random() % 3000 * 0x9E3779B97F4A7C15ull;  // collisions in the 2048 slots
    const int action = random() % 1000;
    if (action == 0) { PetariFastPipeline::InvalidateScope scope; ++stage; }
    if (action < 300) ready.insert(key);
    const auto before = PetariFastPipeline::current();
    const bool hit = PetariFastPipeline::hit(key, before);
    if (find(key, action == 1) != key) { std::puts("FAIL: memo returned a different key"); return 1; }
    hits += hit;
  }
  if (bookkeepingAfterHit != 0) { std::printf("FAIL: %ld hits skipped needed bookkeeping\n", bookkeepingAfterHit); return 1; }
  if (hits < 20000) { std::printf("FAIL: memo barely hit (%ld)\n", hits); return 1; }
  // Not ready: never memoized. Empty entries never match key 0.
  if (PetariFastPipeline::hit(0, PetariFastPipeline::current())) { std::puts("FAIL: empty entry matched"); return 1; }
  // Entries are per thread.
  const std::uint64_t key = 0x1234567890ull;
  ready.insert(key);
  find(key, false);
  if (!PetariFastPipeline::hit(key, PetariFastPipeline::current())) { std::puts("FAIL: ready key not memoized"); return 1; }
  bool other = true;
  std::thread([&] { other = PetariFastPipeline::hit(key, PetariFastPipeline::current()); }).join();
  if (other) { std::puts("FAIL: memo shared across threads"); return 1; }
  std::printf("Fast path model: %ld hits, no skipped bookkeeping, stage changes and races miss\n", hits);
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'probe.cpp'
    binary = Path(directory) / 'probe'
    source.write_text(probe)
    command = [args.cxx, '-std=c++20', '-O1', '-I', str(root / 'native/gx'), str(source), '-o', str(binary)]
    if sys.platform == 'darwin':
        command[1:1] = ['-isysroot', subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    print(result.stdout.strip())
    if result.returncode != 0:
        raise SystemExit(f'fast path model failed ({result.returncode})')
