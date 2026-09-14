#!/usr/bin/env node
import {runDaemon} from './daemon.js';
import {requestDaemon} from './client.js';
import {characterStates, hookEvents, type HookEvent, type HookPayload} from './protocol.js';
import {installOpenClaw} from './character-installer.js';

async function main(): Promise<void> {
  const [command, argument, file, ...options] = process.argv.slice(2);
  if (command === 'daemon') {
    await runDaemon();
    return;
  }
  if (command === 'status') {
    console.log(JSON.stringify(await requestDaemon({type: 'status'}), null, 2));
    return;
  }
  if (command === 'send' && characterStates.includes(argument as never)) {
    console.log(JSON.stringify(await requestDaemon({type: 'send', state: argument as typeof characterStates[number]})));
    return;
  }
  if (command === 'hook' && hookEvents.includes(argument as never)) {
    try {
      const input = await readStdin();
      const payload = input.trim() ? JSON.parse(input) as HookPayload : {};
      await requestDaemon({type: 'hook', event: argument as HookEvent, payload}, 300);
    } catch {
      // Hooks are notifications only; a missing device or daemon must never block Copilot.
    }
    return;
  }
  if (command === 'install-character' && argument === 'openclaw' && file) {
    const portIndex = options.indexOf('--port');
    const port = portIndex >= 0 ? options[portIndex + 1] : undefined;
    if (portIndex >= 0 && !port) throw new Error('--port requires a serial device path.');
    await installOpenClaw(file, port);
    return;
  }
  throw new Error(
    'Usage: agent-companion {daemon|status|send STATE|hook EVENT|install-character openclaw FILE [--port PATH]}');
}

async function readStdin(): Promise<string> {
  let input = '';
  process.stdin.setEncoding('utf8');
  for await (const chunk of process.stdin) input += chunk;
  return input;
}

main().catch(error => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});
