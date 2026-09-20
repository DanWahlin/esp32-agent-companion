import assert from 'node:assert/strict';
import test from 'node:test';
import {handleRequest, type DaemonTransport} from '../src/daemon.js';
import {StateCoordinator} from '../src/state-coordinator.js';
import type {CharacterState, TiltTestPrompt, TiltTestSample} from '../src/protocol.js';

class FakeTransport implements DaemonTransport {
  connected = true;
  path: string | null = '/dev/cu.usbmodem1';
  state: CharacterState = 'idle';
  readonly confirmed: CharacterState[] = [];
  readonly transient: CharacterState[] = [];
  readonly tiltPrompts: TiltTestPrompt[] = [];
  tiltTestSample: TiltTestSample | null = null;

  setState(state: CharacterState): void {
    this.state = state;
  }

  async setStateConfirmed(state: CharacterState): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    this.state = state;
    this.confirmed.push(state);
  }

  async sendTransientState(state: CharacterState): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    this.transient.push(state);
  }

  async setTiltTestPrompt(prompt: TiltTestPrompt): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    this.tiltPrompts.push(prompt);
  }
}

test('maps plugin lease requests through the coordinator', async () => {
  const transport = new FakeTransport();
  const coordinator = new StateCoordinator(state => transport.setState(state), {sweepMs: 0});
  const response = await handleRequest({
    type: 'lease',
    leaseId: 'copilot-app:test',
    state: 'working',
  }, coordinator, transport);
  assert.deepEqual(response, {ok: true, state: 'working'});
  assert.deepEqual(transport.confirmed, ['working']);
  coordinator.close();
});

test('sends surprise without replacing the aggregate lease state', async () => {
  const transport = new FakeTransport();
  const coordinator = new StateCoordinator(state => transport.setState(state), {sweepMs: 0});
  coordinator.setLeaseState('copilot-app:test', 'working');
  const response = await handleRequest({
    type: 'lease',
    leaseId: 'copilot-app:test',
    state: 'surprise',
  }, coordinator, transport);
  assert.deepEqual(response, {
    ok: true,
    state: 'surprise',
    persistentState: 'working',
  });
  assert.deepEqual(transport.transient, ['surprise']);
  assert.equal(coordinator.state, 'working');
  coordinator.close();
});

test('reports disconnected devices instead of false success', async () => {
  const transport = new FakeTransport();
  transport.connected = false;
  const coordinator = new StateCoordinator(() => undefined, {sweepMs: 0});
  await assert.rejects(handleRequest({
    type: 'lease',
    leaseId: 'copilot-app:test',
    state: 'working',
  }, coordinator, transport), /USB device is not connected/);
  assert.equal(coordinator.state, 'idle');
  await assert.rejects(handleRequest({type: 'test'}, coordinator, transport),
                       /USB device is not connected/);
  coordinator.close();
});

test('rejects malformed external lease identifiers', async () => {
  const transport = new FakeTransport();
  const coordinator = new StateCoordinator(() => undefined, {sweepMs: 0});
  await assert.rejects(handleRequest({
    type: 'lease',
    leaseId: '../invalid lease',
    state: 'working',
  }, coordinator, transport), /Invalid lease ID/);
  coordinator.close();
});

test('sends finite tilt test prompts through the serial owner', async () => {
  const transport = new FakeTransport();
  const coordinator = new StateCoordinator(() => undefined, {sweepMs: 0});
  const response = await handleRequest({
    type: 'tiltTest',
    prompt: 'away',
  }, coordinator, transport);
  assert.deepEqual(response, {ok: true, prompt: 'away', tiltTest: null});
  assert.deepEqual(transport.tiltPrompts, ['away']);
  coordinator.close();
});
