import {homedir, tmpdir, userInfo} from 'node:os';
import {join} from 'node:path';

export function socketPath(): string {
  if (process.env.AGENT_COMPANION_SOCKET) return process.env.AGENT_COMPANION_SOCKET;
  return defaultSocketPath(process.platform, homedir(), process.env, tmpdir(),
                           process.getuid?.() ?? userInfo().uid);
}

export function defaultSocketPath(
    platform: NodeJS.Platform, home: string, environment: NodeJS.ProcessEnv,
    temporary: string, uid: number): string {
  if (platform === 'darwin')
    return join(defaultDataDirectory(platform, home, environment), 'daemon.sock');
  const runtime = environment.XDG_RUNTIME_DIR;
  return runtime
    ? join(runtime, 'esp32-agent-companion', 'daemon.sock')
    : join(temporary, `esp32-agent-companion-${uid}`, 'daemon.sock');
}

export function statePath(): string {
  if (process.env.AGENT_COMPANION_STATE) return process.env.AGENT_COMPANION_STATE;
  return join(defaultDataDirectory(process.platform, homedir(), process.env), 'state.json');
}

export function defaultDataDirectory(
    platform: NodeJS.Platform, home: string, environment: NodeJS.ProcessEnv): string {
  if (platform === 'darwin')
    return join(home, 'Library', 'Application Support', 'ESP32 Agent Companion');
  return join(environment.XDG_STATE_HOME ?? join(home, '.local', 'state'),
              'esp32-agent-companion');
}
