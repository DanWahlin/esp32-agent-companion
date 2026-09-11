#pragma once

namespace copilot {
// Initialize once before starting rendering; the read-only mapping lives for the application's lifetime.
bool initializeSpriteStorage();
const char* spriteStorageError();
}
