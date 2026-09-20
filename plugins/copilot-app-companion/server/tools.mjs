import {randomUUID} from 'node:crypto';
import {characterStates, requestDaemon} from './daemon-client.mjs';

const tools = [
  {
    name: 'status',
    description: 'Report the ESP32 Agent Companion daemon, USB connection, port, aggregate state, and active lease count.',
    inputSchema: {type: 'object', additionalProperties: false},
  },
  {
    name: 'test_connection',
    description: 'Verify end-to-end communication through the daemon and receive an acknowledgement from the ESP32 device.',
    inputSchema: {type: 'object', additionalProperties: false},
  },
  {
    name: 'set_state',
    description: 'Set this Copilot session lease to working, attention, complete, surprise, or idle. Persistent states are aggregated by the daemon; surprise is transient.',
    inputSchema: {
      type: 'object',
      properties: {
        state: {
          type: 'string',
          enum: characterStates,
          description: 'The validated companion state.',
        },
      },
      required: ['state'],
      additionalProperties: false,
    },
  },
];

export function createToolHandler({
  request = requestDaemon,
  leaseId = `copilot-app:${randomUUID()}`,
} = {}) {
  return async function handleTool(name, input = {}) {
    try {
      if (name === 'status') {
        validateNoArguments(input);
        return success(await request({type: 'status'}));
      }
      if (name === 'test_connection') {
        validateNoArguments(input);
        return success(await request({type: 'test'}));
      }
      if (name === 'set_state') {
        validateStateInput(input);
        return success(await request({type: 'lease', leaseId, state: input.state}));
      }
      throw new Error(`Unknown tool: ${name}`);
    } catch (error) {
      return failure(error instanceof Error ? error.message : String(error));
    }
  };
}

export function listTools() {
  return tools;
}

function validateNoArguments(input) {
  if (!isPlainObject(input) || Object.keys(input).length !== 0) {
    throw new Error('This tool does not accept arguments.');
  }
}

function validateStateInput(input) {
  if (!isPlainObject(input) || Object.keys(input).length !== 1
      || !characterStates.includes(input.state)) {
    throw new Error(`state must be one of: ${characterStates.join(', ')}.`);
  }
}

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function success(value) {
  return {
    content: [{type: 'text', text: JSON.stringify(value)}],
    structuredContent: value,
  };
}

function failure(message) {
  return {
    isError: true,
    content: [{type: 'text', text: message}],
  };
}
