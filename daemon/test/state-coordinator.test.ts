import assert from 'node:assert/strict';
import {setTimeout as delay} from 'node:timers/promises';
import test from 'node:test';
import {StateCoordinator, type StateCoordinatorOptions} from '../src/state-coordinator.js';
import type {CharacterState, HookPayload} from '../src/protocol.js';

function fixture(overrides: StateCoordinatorOptions = {}) {
  const states: CharacterState[] = [];
  let now = 1000;
  const coordinator = new StateCoordinator(state => states.push(state), {
    now: () => now,
    sweepMs: 0,
    ...overrides,
  });
  const payload = (sessionId: string, extra: HookPayload = {}): HookPayload => ({
    sessionId,
    timestamp: now,
    ...extra,
  });
  return {
    coordinator,
    states,
    payload,
    advance(milliseconds: number) { now += milliseconds; },
  };
}

test('tracks main and subagent work before completing', async () => {
  const {coordinator, states, payload} = fixture({completeMs: 5});
  coordinator.handle('sessionStart', payload('one'));
  coordinator.handle('userPromptSubmitted', payload('one'));
  coordinator.handle('preToolUse', payload('one', {toolName: 'view'}));
  coordinator.handle('subagentStart', payload('one', {agentId: 'child'}));
  coordinator.handle('agentStop', payload('one'));
  assert.equal(coordinator.state, 'working');
  coordinator.handle('subagentStop', payload('one', {agentId: 'child'}));
  assert.equal(coordinator.state, 'complete');
  await delay(10);
  assert.equal(coordinator.state, 'idle');
  assert.deepEqual(states, ['working', 'complete', 'idle']);
  coordinator.close();
});

test('aggregates overlapping CLI sessions before celebrating', () => {
  const {coordinator, states, payload, advance} = fixture();
  coordinator.handle('userPromptSubmitted', payload('one'));
  coordinator.handle('preToolUse', payload('one'));
  advance(1);
  coordinator.handle('userPromptSubmitted', payload('two'));
  coordinator.handle('preToolUse', payload('two'));
  advance(1);
  coordinator.handle('agentStop', payload('one'));
  assert.equal(coordinator.state, 'working');
  coordinator.handle('agentStop', payload('two'));
  assert.equal(coordinator.state, 'complete');
  assert.deepEqual(states, ['working', 'complete']);
  coordinator.close();
});

test('counts simultaneous subagents when hooks provide only a shared name', () => {
  const {coordinator, payload} = fixture();
  coordinator.handle('subagentStart', payload('one', {agentName: 'task'}));
  coordinator.handle('subagentStart', payload('one', {agentName: 'task'}));
  coordinator.handle('subagentStop', payload('one', {agentName: 'task'}));
  assert.equal(coordinator.state, 'working');
  coordinator.handle('subagentStop', payload('one', {agentName: 'task'}));
  assert.equal(coordinator.state, 'idle');
  coordinator.close();
});

test('keeps attention sticky until the developer resumes work', () => {
  const {coordinator, states, payload, advance} = fixture();
  coordinator.handle('userPromptSubmitted', payload('one'));
  advance(1);
  coordinator.handle('notification', payload('one', {notification_type: 'permission_prompt'}));
  coordinator.handle('agentStop', payload('one'));
  assert.equal(coordinator.state, 'attention');
  advance(1);
  coordinator.handle('preToolUse', payload('one', {toolName: 'bash'}));
  assert.equal(coordinator.state, 'working');
  assert.deepEqual(states, ['working', 'attention', 'working']);
  coordinator.close();
});

test('ignores delayed main and subagent events', () => {
  const {coordinator, states} = fixture();
  coordinator.handle('userPromptSubmitted', {sessionId: 'one', timestamp: 2000});
  coordinator.handle('notification', {sessionId: 'one', timestamp: 3000});
  assert.equal(coordinator.handle('preToolUse', {sessionId: 'one', timestamp: 2500}), false);
  assert.equal(coordinator.state, 'attention');
  coordinator.handle('subagentStart', {sessionId: 'one', agentId: 'child', timestamp: 4000});
  assert.equal(coordinator.handle(
    'subagentStop', {sessionId: 'one', agentId: 'child', timestamp: 3500}), false);
  assert.equal(coordinator.state, 'attention');
  assert.deepEqual(states, ['working', 'attention']);
  coordinator.close();
});

test('clamps implausible future timestamps to daemon time', () => {
  const {coordinator, payload, advance} = fixture();
  coordinator.handle('userPromptSubmitted', {sessionId: 'one', timestamp: 9_999_999});
  advance(1);
  assert.equal(coordinator.handle('preToolUse', payload('one')), true);
  assert.equal(coordinator.state, 'working');
  coordinator.close();
});

test('renews active work when a long-running tool finishes', () => {
  const {coordinator, payload, advance} = fixture({activeLeaseMs: 100});
  coordinator.handle('preToolUse', payload('one', {toolName: 'bash'}));
  advance(90);
  coordinator.handle('postToolUse', payload('one', {toolName: 'bash'}));
  advance(20);
  coordinator.sweep();
  assert.equal(coordinator.state, 'working');
  advance(81);
  coordinator.sweep();
  assert.equal(coordinator.state, 'idle');
  coordinator.close();
});

test('expires abandoned main, attention, and subagent leases', () => {
  const {coordinator, payload, advance} = fixture({
    activeLeaseMs: 100,
    attentionLeaseMs: 200,
    subagentLeaseMs: 300,
  });
  coordinator.handle('userPromptSubmitted', payload('main'));
  coordinator.handle('subagentStart', payload('child-session', {agentId: 'child'}));
  advance(101);
  coordinator.sweep();
  assert.equal(coordinator.state, 'working');
  advance(200);
  coordinator.sweep();
  assert.equal(coordinator.state, 'idle');
  coordinator.handle('notification', payload('attention'));
  assert.equal(coordinator.state, 'attention');
  advance(201);
  coordinator.sweep();
  assert.equal(coordinator.state, 'idle');
  coordinator.close();
});

test('restores only unexpired leases and never replays Complete', () => {
  const {coordinator, states} = fixture({
    restored: {
      version: 1,
      sessions: [{
        id: 'one',
        activeUntil: 2000,
        attentionUntil: 0,
        lastMainEventAt: 900,
        lastSeenAt: 900,
        hadWork: true,
        completionPending: true,
        subagents: [],
      }],
    },
  });
  assert.equal(coordinator.state, 'working');
  assert.deepEqual(states, ['working']);
  assert.equal(coordinator.snapshot().sessions[0]?.completionPending, false);
  coordinator.close();
});

test('does not celebrate a turn that performed no work', () => {
  const {coordinator, states, payload} = fixture();
  coordinator.handle('userPromptSubmitted', payload('one'));
  coordinator.handle('agentStop', payload('one'));
  assert.equal(coordinator.state, 'idle');
  assert.deepEqual(states, ['working', 'idle']);
  coordinator.close();
});
