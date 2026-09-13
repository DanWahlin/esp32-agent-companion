import {execFileSync} from 'node:child_process';
import {chmodSync, mkdirSync, writeFileSync} from 'node:fs';
import {homedir} from 'node:os';
import {dirname, join} from 'node:path';

export type InstallerPlatform = 'darwin' | 'linux';

export interface InstallerContext {
  home: string;
  node: string;
  cli: string;
  uid: number;
  configHome?: string;
}

interface InstallFile {
  path: string;
  content: string;
  mode?: number;
}

interface InstallCommand {
  executable: string;
  arguments: string[];
  ignoreFailure?: boolean;
}

export interface InstallationPlan {
  files: InstallFile[];
  commands: InstallCommand[];
  messages: string[];
}

export function createInstallationPlan(
    platform: InstallerPlatform, context: InstallerContext): InstallationPlan {
  const hookPath = join(context.home, '.copilot', 'hooks', 'agent-companion.json');
  const files: InstallFile[] = [{
    path: hookPath,
    content: `${JSON.stringify(createHooks(context.node, context.cli), null, 2)}\n`,
    mode: 0o600,
  }];
  const messages = [`Installed Copilot hooks: ${hookPath}`];
  let commands: InstallCommand[] = [];

  if (platform === 'darwin') {
    const label = 'com.danwahlin.esp32-agent-companion';
    const plistPath = join(context.home, 'Library', 'LaunchAgents', `${label}.plist`);
    const logPath = join(context.home, 'Library', 'Logs', 'esp32-agent-companion.log');
    const domain = `gui/${context.uid}`;
    files.push({path: plistPath, content: launchAgent(context.node, context.cli, logPath)});
    commands = [
      {executable: 'launchctl', arguments: ['bootout', domain, plistPath], ignoreFailure: true},
      {executable: 'launchctl', arguments: ['bootstrap', domain, plistPath]},
      {executable: 'launchctl', arguments: ['kickstart', '-k', `${domain}/${label}`]},
    ];
    messages.push(`Installed launch agent: ${plistPath}`);
  } else if (platform === 'linux') {
    const configHome = context.configHome ?? join(context.home, '.config');
    const servicePath = join(configHome, 'systemd', 'user', 'esp32-agent-companion.service');
    files.push({path: servicePath, content: systemdService(context.node, context.cli)});
    commands = [
      {executable: 'systemctl', arguments: ['--user', 'daemon-reload']},
      {executable: 'systemctl', arguments: ['--user', 'enable', 'esp32-agent-companion.service']},
      {executable: 'systemctl', arguments: ['--user', 'restart', 'esp32-agent-companion.service']},
    ];
    messages.push(`Installed systemd user service: ${servicePath}`);
  }
  messages.push('Restart Copilot CLI to load the new hooks.');
  return {files, commands, messages};
}

export function installDaemon(cli: string): void {
  if (!isSupportedPlatform(process.platform))
    throw new Error(`Unsupported platform: ${process.platform}`);
  const plan = createInstallationPlan(process.platform, {
    home: homedir(),
    node: process.execPath,
    cli,
    uid: process.getuid?.() ?? 0,
    configHome: process.env.XDG_CONFIG_HOME,
  });
  for (const file of plan.files) {
    mkdirSync(dirname(file.path), {recursive: true});
    writeFileSync(file.path, file.content, file.mode ? {mode: file.mode} : undefined);
    if (file.mode) chmodSync(file.path, file.mode);
  }
  for (const command of plan.commands) {
    try {
      execFileSync(command.executable, command.arguments, {stdio: 'ignore'});
    } catch (error) {
      if (!command.ignoreFailure) throw error;
    }
  }
  for (const message of plan.messages) console.log(message);
}

function isSupportedPlatform(platform: NodeJS.Platform): platform is InstallerPlatform {
  return platform === 'darwin' || platform === 'linux';
}

function createHooks(node: string, cli: string): object {
  const command = (event: string) => [{
    type: 'command',
    exec: node,
    args: [cli, 'hook', event],
    timeoutSec: 2,
  }];
  return {
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
}

function launchAgent(node: string, cli: string, logPath: string): string {
  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.danwahlin.esp32-agent-companion</string>
  <key>ProgramArguments</key>
  <array>
    <string>${escapeXml(node)}</string>
    <string>${escapeXml(cli)}</string>
    <string>daemon</string>
  </array>
  <key>WorkingDirectory</key><string>${escapeXml(dirname(cli))}</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>${escapeXml(logPath)}</string>
  <key>StandardErrorPath</key><string>${escapeXml(logPath)}</string>
</dict>
</plist>
`;
}

function systemdService(node: string, cli: string): string {
  return `[Unit]
Description=ESP32 Agent Companion daemon
After=default.target

[Service]
Type=simple
ExecStart=${quoteSystemd(node)} ${quoteSystemd(cli)} daemon
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
`;
}

function escapeXml(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

function quoteSystemd(value: string): string {
  return `"${value.replaceAll('\\', '\\\\').replaceAll('"', '\\"')}"`;
}
