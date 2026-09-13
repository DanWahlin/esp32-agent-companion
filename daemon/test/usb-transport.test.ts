import assert from 'node:assert/strict';
import test from 'node:test';
import {isLikelyEsp32Port} from '../src/usb-transport.js';

test('recognizes macOS and Linux USB serial device paths', () => {
  assert.equal(isLikelyEsp32Port('/dev/cu.usbmodem2101', 'darwin'), true);
  assert.equal(isLikelyEsp32Port('/dev/tty.usbmodem2101', 'darwin'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyACM0', 'linux'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyUSB12', 'linux'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyS0', 'linux'), false);
  assert.equal(isLikelyEsp32Port('COM5', 'linux'), false);
});
