import {chmod, mkdir, unlink} from 'node:fs/promises';
import {createConnection, createServer, type Socket} from 'node:net';
import {dirname} from 'node:path';
import {
  characterStates, hookEvents, tiltTestPrompts, type DaemonRequest, type DaemonStatus,
  type TiltTestPrompt, type TiltTestSample,
} from './protocol.js';
import {socketPath, statePath} from './paths.js';
import {StateCoordinator} from './state-coordinator.js';
import {StateStore} from './state-store.js';
import {UsbTransport} from './usb-transport.js';

export interface DaemonTransport {
  readonly connected: boolean;
  readonly path: string | null;
  readonly state: typeof characterStates[number];
  readonly tiltTestSample: TiltTestSample | null;
  setState(state: typeof characterStates[number]): void;
  setStateConfirmed(state: typeof characterStates[number]): Promise<void>;
  sendTransientState(state: typeof characterStates[number]): Promise<void>;
  setTiltTestPrompt(prompt: TiltTestPrompt): Promise<void>;
}

export async function runDaemon(): Promise<void> {
  const usb = new UsbTransport();
  const store = new StateStore(statePath());
  const restored = await store.load();
  const coordinator = new StateCoordinator(state => usb.setState(state), {
    restored,
    onMutation: state => store.schedule(state),
  });
  usb.setState(coordinator.state);
  const path = socketPath();
  await mkdir(dirname(path), {recursive: true, mode: 0o700});
  await removeStaleSocket(path);

  const server = createServer({allowHalfOpen: true},
                              socket => handleSocket(socket, coordinator, usb));
  server.on('error', error => {
    console.error(`[daemon] ${error.message}`);
    process.exitCode = 1;
  });
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(path, () => {
      server.off('error', reject);
      resolve();
    });
  });
  await chmod(path, 0o600);
  usb.start();
  console.log(`[daemon] listening ${path}`);

  const shutdown = async () => {
    coordinator.close();
    await store.flush(coordinator.snapshot());
    await usb.stop();
    await new Promise<void>(resolve => server.close(() => resolve()));
    await unlink(path).catch(() => undefined);
  };
  process.once('SIGINT', () => void shutdown().then(() => process.exit(0)));
  process.once('SIGTERM', () => void shutdown().then(() => process.exit(0)));
}

function handleSocket(socket: Socket, coordinator: StateCoordinator, usb: UsbTransport): void {
  socket.setEncoding('utf8');
  socket.setTimeout(10000, () => socket.destroy());
  socket.on('error', () => undefined);
  let input = '';
  socket.on('data', chunk => {
    input += chunk;
    if (input.length > 65536) socket.destroy();
  });
  socket.on('end', () => void (async () => {
    try {
      const request = JSON.parse(input.trim()) as DaemonRequest;
      respond(socket, await handleRequest(request, coordinator, usb));
    } catch (error) {
      respond(socket, {ok: false, error: error instanceof Error ? error.message : String(error)});
    }
  })());
}

export async function handleRequest(
    request: DaemonRequest, coordinator: StateCoordinator,
    usb: DaemonTransport): Promise<unknown> {
  if (request.type === 'hook' && hookEvents.includes(request.event)) {
    coordinator.handle(request.event, request.payload ?? {});
    return {ok: true, state: coordinator.state};
  }
  if (request.type === 'send' && characterStates.includes(request.state)) {
    await usb.setStateConfirmed(request.state);
    return {ok: true, state: request.state};
  }
  if (request.type === 'tiltTest' && tiltTestPrompts.includes(request.prompt)) {
    await usb.setTiltTestPrompt(request.prompt);
    return {ok: true, prompt: request.prompt, tiltTest: usb.tiltTestSample};
  }
  if (request.type === 'lease' && characterStates.includes(request.state)) {
    validateLeaseId(request.leaseId);
    if (!usb.connected) throw new Error('USB device is not connected.');
    if (request.state === 'surprise') {
      await usb.sendTransientState('surprise');
      return {ok: true, state: 'surprise', persistentState: coordinator.state};
    }
    coordinator.setLeaseState(request.leaseId, request.state);
    await usb.setStateConfirmed(coordinator.state);
    return {ok: true, state: coordinator.state};
  }
  if (request.type === 'test') {
    if (!usb.connected) throw new Error('USB device is not connected.');
    await usb.setStateConfirmed(coordinator.state);
    return daemonStatus(coordinator, usb);
  }
  if (request.type === 'status') return daemonStatus(coordinator, usb);
  throw new Error('Unknown daemon request.');
}

function daemonStatus(
    coordinator: StateCoordinator, usb: DaemonTransport): DaemonStatus {
  return {
    state: usb.state,
    transport: usb.connected ? 'usb' : null,
    connected: usb.connected,
    port: usb.path,
    sessions: coordinator.sessionCount,
    tiltTest: usb.tiltTestSample,
  };
}

function validateLeaseId(value: string): void {
  if (typeof value !== 'string' || value.length < 1 || value.length > 128
      || !/^[A-Za-z0-9._:-]+$/.test(value)) {
    throw new Error('Invalid lease ID.');
  }
}

function respond(socket: Socket, response: unknown): void {
  socket.end(`${JSON.stringify(response)}\n`);
}

async function removeStaleSocket(path: string): Promise<void> {
  const active = await new Promise<boolean>(resolve => {
    const socket = createConnection(path);
    socket.once('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.once('error', error => {
      const code = (error as NodeJS.ErrnoException).code;
      resolve(code !== 'ENOENT' && code !== 'ECONNREFUSED');
    });
  });
  if (active) throw new Error(`Agent Companion daemon is already listening at ${path}`);
  try {
    await unlink(path);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error;
  }
}
