import {SerialPort} from 'serialport';
import type {CharacterState, TiltTestPrompt, TiltTestSample} from './protocol.js';

interface LineWaiter {
  match: (line: string) => boolean;
  resolve: (line: string) => void;
  reject: (error: Error) => void;
  timer: NodeJS.Timeout;
}

export class UsbTransport {
  readonly #preferredPort = process.env.AGENT_COMPANION_PORT;
  #port: SerialPort | undefined;
  #path: string | null = null;
  #desired: CharacterState = 'idle';
  #scanTimer: NodeJS.Timeout | undefined;
  #scanActive = false;
  #buffer = '';
  #waiters: LineWaiter[] = [];
  #commands = Promise.resolve();
  #tiltTestSample: TiltTestSample | null = null;

  get connected(): boolean {
    return this.#port?.isOpen === true;
  }

  get path(): string | null {
    return this.#path;
  }

  get state(): CharacterState {
    return this.#desired;
  }

  get tiltTestSample(): TiltTestSample | null {
    return this.#tiltTestSample;
  }

  start(): void {
    void this.#scan();
    this.#scanTimer = setInterval(() => void this.#scan(), 2000);
  }

  async stop(): Promise<void> {
    if (this.#scanTimer) clearInterval(this.#scanTimer);
    this.#scanTimer = undefined;
    this.#rejectWaiters(new Error('USB transport stopped.'));
    const port = this.#port;
    this.#port = undefined;
    this.#path = null;
    if (port?.isOpen) {
      await new Promise<void>(resolve => port.close(() => resolve()));
    }
  }

  setState(state: CharacterState): void {
    this.#desired = state;
    if (!this.connected) return;
    void this.#queueState(state).catch(() => undefined);
  }

  async setStateConfirmed(state: CharacterState): Promise<void> {
    this.#desired = state;
    if (!this.connected) throw new Error('USB device is not connected.');
    await this.#queueState(state);
  }

  async sendTransientState(state: CharacterState): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    await this.#queueState(state);
  }

  async setTiltTestPrompt(prompt: TiltTestPrompt): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    await this.#request(`!test${prompt}\n`, line => line === `TILT_TEST prompt=${prompt}`);
    if (prompt === 'cancel') this.#tiltTestSample = null;
  }

  async #scan(): Promise<void> {
    if (this.#scanActive || this.connected) return;
    this.#scanActive = true;
    try {
      const ports = await SerialPort.list();
      const candidate = ports.find(port => port.path === this.#preferredPort)
        ?? ports.find(port => port.vendorId?.toLowerCase() === '303a'
          && isLikelyEsp32Port(port.path))
        ?? ports.find(port => isLikelyEsp32Port(port.path));
      if (!candidate) return;
      const path = process.platform === 'darwin'
        ? candidate.path.replace(/^\/dev\/tty\./, '/dev/cu.')
        : candidate.path;
      await this.#open(path);
    } catch (error) {
      console.error(`[usb] discovery failed: ${this.#message(error)}`);
    } finally {
      this.#scanActive = false;
    }
  }

  async #open(path: string): Promise<void> {
    const port = new SerialPort({
      path,
      baudRate: 115200,
      autoOpen: false,
      hupcl: false,
    });
    port.on('data', data => this.#onData(data as Buffer));
    port.on('error', error => console.error(`[usb] ${error.message}`));
    port.on('close', () => {
      console.log(`[usb] disconnected ${path}`);
      this.#rejectWaiters(new Error('USB device disconnected.'));
      if (this.#port === port) {
        this.#port = undefined;
        this.#path = null;
      }
    });
    await new Promise<void>((resolve, reject) => {
      port.open(error => error ? reject(error) : resolve());
    });
    this.#port = port;
    this.#path = path;
    try {
      const info = await this.#request('i', line => line.startsWith('INFO protocol='));
      if (!/^INFO protocol=[12](?: |$)/.test(info)) {
        throw new Error(`Unsupported device protocol: ${info}`);
      }
      console.log(`[usb] ${info}`);
      console.log(`[usb] connected ${path}`);
      await this.#sendState(this.#desired);
    } catch (error) {
      await new Promise<void>(resolve => port.close(() => resolve()));
      throw error;
    }
  }

  async #sendState(state: CharacterState): Promise<void> {
    if (!this.connected) throw new Error('USB device is not connected.');
    await this.#request(`!${state}\n`, line => line === `COMMAND accepted=${state}`);
    console.log(`[state] ${state} via usb`);
  }

  #queueState(state: CharacterState): Promise<void> {
    const result = this.#commands.then(() => this.#sendState(state));
    this.#commands = result.catch(error => {
      console.error(`[usb] ${this.#message(error)}`);
    });
    return result;
  }

  async #request(text: string, match: (line: string) => boolean): Promise<string> {
    const response = this.#waitFor(match, 5000);
    try {
      await this.#write(text);
      return await response;
    } catch (error) {
      this.#rejectWaiters(error instanceof Error ? error : new Error(String(error)));
      throw error;
    }
  }

  async #write(text: string): Promise<void> {
    const port = this.#port;
    if (!port?.isOpen) throw new Error('USB device is not connected.');
    await new Promise<void>((resolve, reject) => {
      port.write(text, error => {
        if (error) reject(error);
        else port.drain(drainError => drainError ? reject(drainError) : resolve());
      });
    });
  }

  #onData(data: Buffer): void {
    this.#buffer += data.toString('utf8');
    if (this.#buffer.length > 65536) {
      console.error('[usb] discarded oversized unterminated serial response');
      this.#buffer = '';
      this.#rejectWaiters(new Error('USB device returned an oversized response.'));
      return;
    }
    for (;;) {
      const newline = this.#buffer.indexOf('\n');
      if (newline < 0) break;
      const line = this.#buffer.slice(0, newline).trim();
      this.#buffer = this.#buffer.slice(newline + 1);
      if (!line) continue;
      const waiter = this.#waiters.find(candidate => candidate.match(line));
      if (!waiter) {
        const sample = parseTiltTestSample(line);
        if (sample) this.#tiltTestSample = sample;
        if (line.startsWith('TILT ') || line.startsWith('POSE ')
            || line.startsWith('TILT_TEST')) console.log(`[device] ${line}`);
        continue;
      }
      clearTimeout(waiter.timer);
      this.#waiters.splice(this.#waiters.indexOf(waiter), 1);
      waiter.resolve(line);
    }
  }

  #waitFor(match: (line: string) => boolean, timeoutMs: number): Promise<string> {
    return new Promise((resolve, reject) => {
      const waiter: LineWaiter = {
        match,
        resolve,
        reject,
        timer: setTimeout(() => {
          this.#waiters.splice(this.#waiters.indexOf(waiter), 1);
          reject(new Error('Timed out waiting for device acknowledgement.'));
        }, timeoutMs),
      };
      this.#waiters.push(waiter);
    });
  }

  #rejectWaiters(error: Error): void {
    for (const waiter of this.#waiters.splice(0)) {
      clearTimeout(waiter.timer);
      waiter.reject(error);
    }
  }

  #message(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
  }
}

export function isLikelyEsp32Port(
    path: string, platform: NodeJS.Platform = process.platform): boolean {
  if (platform === 'darwin') return /^\/dev\/(?:cu|tty)\.usbmodem/i.test(path);
  return /^\/dev\/tty(?:ACM|USB)\d+$/i.test(path);
}

export function parseTiltTestSample(line: string): TiltTestSample | null {
  const match = /^TILT_TEST_SAMPLE prompt=(center|away|toward|left|right|done) active=([01]) direction=(\d+) direction_name=([A-Z ]+) depth=([\d.]+) screen_x=(-?[\d.]+) screen_y=(-?[\d.]+) magnitude=([\d.]+) pose_direction=(\d+) pose_frame=(\d+)$/.exec(line);
  if (!match) return null;
  return {
    prompt: match[1] as TiltTestSample['prompt'],
    active: match[2] === '1',
    direction: Number(match[3]),
    directionName: match[4]!,
    depth: Number(match[5]),
    screenX: Number(match[6]),
    screenY: Number(match[7]),
    magnitude: Number(match[8]),
    poseDirection: Number(match[9]),
    poseFrame: Number(match[10]),
  };
}
