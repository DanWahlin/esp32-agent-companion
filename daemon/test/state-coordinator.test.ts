import assert from 'node:assert/strict';
import {setTimeout as delay} from 'node:timers/promises';
import test from 'node:test';
import {StateCoordinator} from '../src/state-coordinator.js';
import type {CharacterState} from '../src/protocol.js';

test('tracks main and subagent work before completing', async () => {
  const states: CharacterState[] = [];
  const coordinator = new StateCoordinator(state => states.push(state), 5);
  coordinator.handle('sessionStart', {sessionId: 'one'});
  coordinator.handle('userPromptSubmitted', {sessionId: 'one'});
  coordinator.handle('preToolUse', {sessionId: 'one', toolName: 'view'});
  coordinator.handle('subagentStart', {sessionId: 'one', agentId: 'child'});
  coordinator.handle('agentStop', {sessionId: 'one'});
  assert.equal(coordinator.state, 'working');
  coordinator.handle('subagentStop', {sessionId: 'one', agentId: 'child'});
  assert.equal(coordinator.state, 'complete');
  await delay(10);
  assert.equal(coordinator.state, 'idle');
  assert.deepEqual(states, ['working', 'complete', 'idle']);
  coordinator.close();
});

test('keeps attention sticky until the developer responds', () => {
  const states: CharacterState[] = [];
  const coordinator = new StateCoordinator(state => states.push(state));
  coordinator.handle('userPromptSubmitted', {sessionId: 'one'});
  coordinator.handle('preToolUse', {sessionId: 'one'});
  coordinator.handle('notification', {sessionId: 'one', notification_type: 'permission_prompt'});
  coordinator.handle('agentStop', {sessionId: 'one'});
  assert.equal(coordinator.state, 'attention');
  coordinator.handle('userPromptSubmitted', {sessionId: 'one'});
  assert.equal(coordinator.state, 'working');
  assert.deepEqual(states, ['working', 'attention', 'working']);
  coordinator.close();
});

test('resumed tool activity clears an answered attention request', () => {
  const states: CharacterState[] = [];
  const coordinator = new StateCoordinator(state => states.push(state));
  coordinator.handle('userPromptSubmitted', {sessionId: 'one'});
  coordinator.handle('notification', {sessionId: 'one', notification_type: 'elicitation_dialog'});
  assert.equal(coordinator.state, 'attention');
  coordinator.handle('preToolUse', {sessionId: 'one', toolName: 'bash'});
  assert.equal(coordinator.state, 'working');
  assert.deepEqual(states, ['working', 'attention', 'working']);
  coordinator.close();
});

test('does not celebrate a turn that performed no work', () => {
  const states: CharacterState[] = [];
  const coordinator = new StateCoordinator(state => states.push(state));
  coordinator.handle('userPromptSubmitted', {sessionId: 'one'});
  coordinator.handle('agentStop', {sessionId: 'one'});
  assert.equal(coordinator.state, 'idle');
  assert.deepEqual(states, ['working', 'idle']);
  coordinator.close();
});
