import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import {stat} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {spawn} from 'node:child_process';
import {SerialPort} from 'serialport';
import {isLikelyEsp32Port} from './usb-transport.js';

const protocol = 2;
const label = 'com.danwahlin.esp32-agent-companion';

export async function installOpenClaw(file: string, preferredPort?: string): Promise<void> {
  const details = await stat(file);
  if (!details.isFile() || details.size <= 0) throw new Error(`Character pack is empty: ${file}`);
  const resumeDaemon = await pauseDaemon();
  try {
    const path = await findPort(preferredPort ?? process.env.AGENT_COMPANION_PORT);
    const port = new SerialPort({path, baudRate: 115200, autoOpen: false, hupcl: false});
    await open(port);
    const lines = new SerialLines(port);
    try {
      const infoResponse = lines.wait(line => line.startsWith('INFO protocol='), 5000);
      await write(port, 'i');
      const info = await infoResponse;
      if (!new RegExp(`^INFO protocol=${protocol}(?: |$)`).test(info))
        throw new Error(`Character upload requires device protocol ${protocol}: ${info}`);

      const readyResponse = lines.wait(
        line => line.startsWith('UPLOAD_READY ') || line.startsWith('UPLOAD_ERROR '), 10000);
      await write(port, 'u');
      const ready = await readyResponse;
      if (ready.startsWith('UPLOAD_ERROR ')) throw new Error(ready);
      const match = /\bbytes=(\d+)\b/.exec(ready);
      if (!match || Number(match[1]) !== details.size)
        throw new Error(`Device expects a different OpenClaw pack: ${ready}`);

      const hash = createHash('sha256');
      let sent = 0;
      let shown = -1;
      let resultResponse: Promise<string> | undefined;
      for await (const chunk of createReadStream(file, {highWaterMark: 256})) {
        const data = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
        hash.update(data);
        const expected = sent + data.length;
        const acknowledgement = lines.wait(
          line => line.startsWith('UPLOAD_ACK ') || line.startsWith('UPLOAD_ERROR '), 10000);
        if (expected === details.size) {
          resultResponse = lines.wait(
            line => line.startsWith('UPLOAD_OK ') || line.startsWith('UPLOAD_ERROR '), 30000);
        }
        await write(port, data);
        sent += data.length;
        const acknowledged = await acknowledgement;
        if (acknowledged.startsWith('UPLOAD_ERROR ')) throw new Error(acknowledged);
        const received = /\breceived=(\d+)\b/.exec(acknowledged);
        if (!received || Number(received[1]) !== sent)
          throw new Error(`Device acknowledged an unexpected byte count: ${acknowledged}`);
        const percent = Math.floor(sent * 100 / details.size);
        if (percent >= shown + 5 || percent === 100) {
          shown = percent;
          process.stderr.write(`\rUploading OpenClaw: ${percent}%`);
        }
      }
      process.stderr.write('\n');
      if (sent !== details.size) throw new Error(`Character upload was truncated: ${sent}/${details.size}`);
      if (!resultResponse) throw new Error('Character upload produced no completion response.');
      const result = await resultResponse;
      if (!result.startsWith('UPLOAD_OK ')) throw new Error(result);
      console.log(`${result} sha256=${hash.digest('hex')}`);
    } finally {
      lines.close();
      await close(port);
    }
  } finally {
    await resumeDaemon();
  }
}

class SerialLines {
  readonly #port: SerialPort;
  #buffer = '';
  #waiters: Array<{
    match: (line: string) => boolean;
    resolve: (line: string) => void;
    reject: (error: Error) => void;
    timer: NodeJS.Timeout;
  }> = [];

  constructor(port: SerialPort) {
    this.#port = port;
    port.on('data', this.#onData);
    port.on('close', this.#onClose);
  }

  wait(match: (line: string) => boolean, timeout: number): Promise<string> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.#waiters = this.#waiters.filter(waiter => waiter.timer !== timer);
        reject(new Error('Timed out waiting for the device.'));
      }, timeout);
      this.#waiters.push({match, resolve, reject, timer});
    });
  }

  close(): void {
    this.#port.off('data', this.#onData);
    this.#port.off('close', this.#onClose);
    this.#rejectAll(new Error('Serial session closed.'));
  }

  #onData = (data: Buffer): void => {
    this.#buffer += data.toString('utf8');
    for (;;) {
      const newline = this.#buffer.indexOf('\n');
      if (newline < 0) break;
      const line = this.#buffer.slice(0, newline).replace(/\r$/, '');
      this.#buffer = this.#buffer.slice(newline + 1);
      const index = this.#waiters.findIndex(waiter => waiter.match(line));
      if (index < 0) continue;
      const [waiter] = this.#waiters.splice(index, 1);
      if (!waiter) continue;
      clearTimeout(waiter.timer);
      waiter.resolve(line);
    }
  };

  #onClose = (): void => this.#rejectAll(new Error('Device disconnected during character upload.'));

  #rejectAll(error: Error): void {
    for (const waiter of this.#waiters) {
      clearTimeout(waiter.timer);
      waiter.reject(error);
    }
    this.#waiters = [];
  }
}

async function findPort(preferred?: string): Promise<string> {
  const ports = await SerialPort.list();
  return selectCharacterInstallerPort(ports, preferred, process.platform);
}

interface SerialPortInfo {
  path: string;
  vendorId?: string;
}

export function selectCharacterInstallerPort(
    ports: SerialPortInfo[], preferred?: string, platform = process.platform): string {
  const normalize = (path: string) => platform === 'darwin'
    ? path.replace(/^\/dev\/tty\./, '/dev/cu.')
    : path;
  if (preferred) {
    const normalized = normalize(preferred);
    const candidate = ports.find(port => normalize(port.path) === normalized);
    if (!candidate) throw new Error(`Requested ESP32 USB serial port was not found: ${preferred}`);
    return normalize(candidate.path);
  }
  const candidate = ports.find(port => port.vendorId?.toLowerCase() === '303a'
      && isLikelyEsp32Port(port.path, platform))
    ?? ports.find(port => isLikelyEsp32Port(port.path, platform));
  if (!candidate) throw new Error('No ESP32 USB serial port found.');
  return normalize(candidate.path);
}

async function open(port: SerialPort): Promise<void> {
  await new Promise<void>((resolve, reject) =>
    port.open(error => error ? reject(error) : resolve()));
}

async function close(port: SerialPort): Promise<void> {
  if (!port.isOpen) return;
  await new Promise<void>(resolve => port.close(() => resolve()));
}

async function write(port: SerialPort, data: string | Buffer): Promise<void> {
  await new Promise<void>((resolve, reject) => {
    port.write(data, error => {
      if (error) reject(error);
      else port.drain(drainError => drainError ? reject(drainError) : resolve());
    });
  });
}

async function pauseDaemon(): Promise<() => Promise<void>> {
  if (process.platform === 'darwin' && process.getuid) {
    const domain = `gui/${process.getuid()}`;
    const service = `${domain}/${label}`;
    const plist = join(homedir(), 'Library', 'LaunchAgents', `${label}.plist`);
    const active = await command('launchctl', ['print', service], true);
    if (!active) return async () => {};
    await command('launchctl', ['bootout', domain, plist], false);
    return async () => {
      await command('launchctl', ['bootstrap', domain, plist], false);
      await command('launchctl', ['kickstart', '-k', service], false);
    };
  }
  if (process.platform === 'linux') {
    const active = await command(
      'systemctl', ['--user', 'is-active', '--quiet', 'esp32-agent-companion.service'], true);
    if (!active) return async () => {};
    await command('systemctl', ['--user', 'stop', 'esp32-agent-companion.service'], false);
    return async () => {
      await command('systemctl', ['--user', 'start', 'esp32-agent-companion.service'], false);
    };
  }
  return async () => {};
}

async function command(executable: string, arguments_: string[], probe: boolean): Promise<boolean> {
  return await new Promise<boolean>((resolve, reject) => {
    const child = spawn(executable, arguments_, {stdio: 'ignore'});
    child.on('error', error => probe ? resolve(false) : reject(error));
    child.on('exit', code => {
      if (code === 0) resolve(true);
      else if (probe) resolve(false);
      else reject(new Error(`${executable} exited with code ${code ?? 'unknown'}.`));
    });
  });
}
