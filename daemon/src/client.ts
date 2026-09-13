import {createConnection} from 'node:net';
import type {DaemonRequest} from './protocol.js';
import {socketPath} from './paths.js';

export function requestDaemon(request: DaemonRequest, timeoutMs = 500): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const socket = createConnection(socketPath());
    let buffer = '';
    const timer = setTimeout(() => {
      socket.destroy();
      reject(new Error('Agent Companion daemon did not respond.'));
    }, timeoutMs);
    socket.setEncoding('utf8');
    socket.on('connect', () => socket.end(`${JSON.stringify(request)}\n`));
    socket.on('data', chunk => {
      buffer += chunk;
      if (buffer.length <= 65536) return;
      clearTimeout(timer);
      socket.destroy();
      reject(new Error('Agent Companion daemon returned an oversized response.'));
    });
    socket.on('end', () => {
      clearTimeout(timer);
      try {
        resolve(buffer.trim() ? JSON.parse(buffer) : {});
      } catch {
        reject(new Error('Agent Companion daemon returned invalid JSON.'));
      }
    });
    socket.on('error', error => {
      clearTimeout(timer);
      reject(error);
    });
  });
}
