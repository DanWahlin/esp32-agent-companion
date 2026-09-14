import assert from 'node:assert/strict';
import test from 'node:test';
import {selectCharacterInstallerPort} from '../src/character-installer.js';
import {isLikelyEsp32Port} from '../src/usb-transport.js';

test('recognizes macOS and Linux USB serial device paths', () => {
  assert.equal(isLikelyEsp32Port('/dev/cu.usbmodem2101', 'darwin'), true);
  assert.equal(isLikelyEsp32Port('/dev/tty.usbmodem2101', 'darwin'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyACM0', 'linux'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyUSB12', 'linux'), true);
  assert.equal(isLikelyEsp32Port('/dev/ttyS0', 'linux'), false);
  assert.equal(isLikelyEsp32Port('COM5', 'linux'), false);
});

test('character installer strictly honors explicit ports and macOS aliases', () => {
  const ports = [
    {path: '/dev/tty.usbmodem2101', vendorId: '303A'},
    {path: '/dev/tty.usbmodem3101', vendorId: '303A'},
  ];
  assert.equal(
      selectCharacterInstallerPort(ports, '/dev/cu.usbmodem3101', 'darwin'),
      '/dev/cu.usbmodem3101');
  assert.throws(
      () => selectCharacterInstallerPort(ports, '/dev/cu.usbmodem9999', 'darwin'),
      /Requested ESP32 USB serial port was not found/);
});

test('character installer discovers a likely ESP32 only without an explicit port', () => {
  assert.equal(selectCharacterInstallerPort([
    {path: '/dev/ttyS0'},
    {path: '/dev/ttyACM1', vendorId: '303a'},
  ], undefined, 'linux'), '/dev/ttyACM1');
  assert.throws(
      () => selectCharacterInstallerPort([{path: '/dev/ttyS0'}], undefined, 'linux'),
      /No ESP32 USB serial port found/);
});
