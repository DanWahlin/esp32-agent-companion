import {homedir} from 'node:os';
import {join} from 'node:path';

export function socketPath(): string {
  if (process.env.AGENT_COMPANION_SOCKET) return process.env.AGENT_COMPANION_SOCKET;
  return join(homedir(), 'Library', 'Application Support', 'ESP32 Agent Companion', 'daemon.sock');
}
