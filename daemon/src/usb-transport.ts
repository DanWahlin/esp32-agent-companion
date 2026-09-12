import {SerialPort} from 'serialport';
import type {CharacterState} from './protocol.js';

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

  get connected(): boolean {
    return this.#port?.isOpen === true;
  }

  get path(): string | null {
    return this.#path;
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
    this.#commands = this.#commands
      .then(() => this.#sendState(state))
      .catch(error => console.error(`[usb] ${this.#message(error)}`));
  }

  async #scan(): Promise<void> {
    if (this.#scanActive || this.connected) return;
    this.#scanActive = true;
    try {
      const ports = await SerialPort.list();
      const candidate = ports.find(port => port.path === this.#preferredPort)
        ?? ports.find(port => port.vendorId?.toLowerCase() === '303a'
          && /^\/dev\/(?:cu|tty)\.usbmodem/i.test(port.path))
        ?? ports.find(port => /^\/dev\/(?:cu|tty)\.usbmodem/i.test(port.path));
      if (!candidate) return;
      const path = candidate.path.replace(/^\/dev\/tty\./, '/dev/cu.');
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
      if (!/^INFO protocol=1(?: |$)/.test(info)) {
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
    if (!this.connected) return;
    await this.#request(`!${state}\n`, line => line === `COMMAND accepted=${state}`);
    console.log(`[state] ${state} via usb`);
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
    for (;;) {
      const newline = this.#buffer.indexOf('\n');
      if (newline < 0) break;
      const line = this.#buffer.slice(0, newline).trim();
      this.#buffer = this.#buffer.slice(newline + 1);
      if (!line) continue;
      const waiter = this.#waiters.find(candidate => candidate.match(line));
      if (!waiter) continue;
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
