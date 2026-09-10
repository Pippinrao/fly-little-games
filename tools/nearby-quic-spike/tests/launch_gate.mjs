import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ProbeLaunchGate } from '../harmony/entry/src/main/ets/ProbeLaunchGate.ts';

test('no explicit request and malformed explicit request never start', () => {
  const gate = new ProbeLaunchGate();
  assert.equal(gate.request(false, true).id, 0);
  assert.equal(gate.consume(0), false);
  const invalid = gate.request(true, false);
  assert.match(invalid.error, /bind, peer and pin/);
  assert.equal(gate.consume(invalid.id), false);
});
test('request is consumed once across page remounts', () => {
  const gate = new ProbeLaunchGate();
  const request = gate.request(true, true);
  assert.equal(gate.consume(request.id), true);
  gate.finish();
  assert.equal(gate.consume(request.id), false);
});
test('new ability launch invalidates pending old request', () => {
  const gate = new ProbeLaunchGate();
  const request = gate.request(true, true);
  gate.beginLaunch();
  assert.equal(gate.consume(request.id), false);
  assert.equal(gate.request(false, true).id, 0);
  const fresh = gate.request(true, true);
  assert.equal(gate.consume(fresh.id), true);
});
test('latest plain or invalid launch supersedes an earlier unconsumed request', () => {
  for (const explicit of [false, true]) {
    const gate = new ProbeLaunchGate();
    const earlier = gate.request(true, true);
    gate.beginLaunch();
    const latest = gate.request(explicit, false);
    assert.equal(latest.id, 0);
    assert.equal(gate.consume(earlier.id), false);
    assert.equal(gate.isRunning(), false);
  }
});
test('busy explicit request visibly fails and never queues or cancels active worker', () => {
  const gate = new ProbeLaunchGate();
  assert.equal(gate.startManual(), true);
  const busy = gate.request(true, true);
  assert.match(busy.error, /already running/);
  assert.equal(busy.id, 0);
  assert.equal(gate.startManual(), false);
  gate.beginLaunch();
  assert.equal(gate.startManual(), false);
  gate.finish();
  assert.equal(gate.consume(busy.id), false);
  assert.equal(gate.startManual(), true);
});
