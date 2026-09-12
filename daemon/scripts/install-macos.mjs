import {execFileSync} from 'node:child_process';
import {chmodSync, mkdirSync, writeFileSync} from 'node:fs';
import {homedir} from 'node:os';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const daemonRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const cli = join(daemonRoot, 'dist', 'src', 'cli.js');
const node = process.execPath;
const home = homedir();
const hooksDirectory = join(home, '.copilot', 'hooks');
const hookPath = join(hooksDirectory, 'agent-companion.json');
const launchAgents = join(home, 'Library', 'LaunchAgents');
const label = 'com.danwahlin.esp32-agent-companion';
const plistPath = join(launchAgents, `${label}.plist`);
const logDirectory = join(home, 'Library', 'Logs');

mkdirSync(hooksDirectory, {recursive: true});
mkdirSync(launchAgents, {recursive: true});
mkdirSync(logDirectory, {recursive: true});

const command = event => [{
  type: 'command',
  exec: node,
  args: [cli, 'hook', event],
  timeoutSec: 2,
}];
const hooks = {
  version: 1,
  hooks: {
    sessionStart: command('sessionStart'),
    userPromptSubmitted: command('userPromptSubmitted'),
    preToolUse: command('preToolUse'),
    postToolUse: command('postToolUse'),
    postToolUseFailure: command('postToolUseFailure'),
    subagentStart: command('subagentStart'),
    subagentStop: command('subagentStop'),
    agentStop: command('agentStop'),
    notification: [{
      ...command('notification')[0],
      matcher: 'permission_prompt|elicitation_dialog',
    }],
    errorOccurred: command('errorOccurred'),
    sessionEnd: command('sessionEnd'),
  },
};
writeFileSync(hookPath, `${JSON.stringify(hooks, null, 2)}\n`, {mode: 0o600});
chmodSync(hookPath, 0o600);

const escapeXml = value => value
  .replaceAll('&', '&amp;')
  .replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;');
const logPath = join(logDirectory, 'esp32-agent-companion.log');
const plist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>${label}</string>
  <key>ProgramArguments</key>
  <array>
    <string>${escapeXml(node)}</string>
    <string>${escapeXml(cli)}</string>
    <string>daemon</string>
  </array>
  <key>WorkingDirectory</key><string>${escapeXml(daemonRoot)}</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>${escapeXml(logPath)}</string>
  <key>StandardErrorPath</key><string>${escapeXml(logPath)}</string>
</dict>
</plist>
`;
writeFileSync(plistPath, plist);

const domain = `gui/${process.getuid()}`;
try {
  execFileSync('launchctl', ['bootout', domain, plistPath], {stdio: 'ignore'});
} catch {
  // The service was not loaded yet.
}
execFileSync('launchctl', ['bootstrap', domain, plistPath]);
execFileSync('launchctl', ['kickstart', '-k', `${domain}/${label}`]);

console.log(`Installed Copilot hooks: ${hookPath}`);
console.log(`Installed launch agent: ${plistPath}`);
console.log('Restart Copilot CLI to load the new hooks.');
