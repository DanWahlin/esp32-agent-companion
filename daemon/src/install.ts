import {fileURLToPath} from 'node:url';
import {dirname, join} from 'node:path';
import {installDaemon} from './installer.js';

installDaemon(join(dirname(fileURLToPath(import.meta.url)), 'cli.js'));
