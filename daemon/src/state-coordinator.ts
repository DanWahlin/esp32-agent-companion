import type {CharacterState, HookEvent, HookPayload} from './protocol.js';

interface SessionState {
  active: boolean;
  attention: boolean;
  hadWork: boolean;
  completionPending: boolean;
  subagents: Set<string>;
}

export class StateCoordinator {
  readonly #sessions = new Map<string, SessionState>();
  readonly #onState: (state: CharacterState) => void;
  readonly #completeMs: number;
  #state: CharacterState = 'idle';
  #completeTimer: NodeJS.Timeout | undefined;

  constructor(onState: (state: CharacterState) => void, completeMs = 4000) {
    this.#onState = onState;
    this.#completeMs = completeMs;
  }

  get state(): CharacterState {
    return this.#state;
  }

  get sessionCount(): number {
    return this.#sessions.size;
  }

  handle(event: HookEvent, payload: HookPayload): void {
    const sessionId = this.#sessionId(payload);
    if (event === 'sessionEnd') {
      this.#sessions.delete(sessionId);
      this.#recompute();
      return;
    }
    const session = this.#session(sessionId);
    switch (event) {
      case 'sessionStart':
        break;
      case 'userPromptSubmitted':
        session.active = true;
        session.attention = false;
        session.hadWork = false;
        session.completionPending = false;
        this.#cancelComplete();
        break;
      case 'preToolUse':
        session.active = true;
        session.attention = false;
        session.hadWork = true;
        break;
      case 'subagentStart':
        session.hadWork = true;
        session.subagents.add(this.#agentId(payload));
        break;
      case 'subagentStop':
        session.subagents.delete(this.#agentId(payload));
        break;
      case 'notification':
      case 'errorOccurred':
        session.active = false;
        session.attention = true;
        this.#cancelComplete();
        break;
      case 'agentStop':
        session.active = false;
        session.completionPending = session.hadWork && !session.attention;
        break;
      default:
        event satisfies never;
    }
    this.#recompute();
  }

  close(): void {
    this.#cancelComplete();
  }

  #session(id: string): SessionState {
    let session = this.#sessions.get(id);
    if (!session) {
      session = {
        active: false,
        attention: false,
        hadWork: false,
        completionPending: false,
        subagents: new Set(),
      };
      this.#sessions.set(id, session);
    }
    return session;
  }

  #sessionId(payload: HookPayload): string {
    const value = payload.sessionId ?? payload.session_id;
    return typeof value === 'string' && value ? value : 'unknown-session';
  }

  #agentId(payload: HookPayload): string {
    const value = payload.agentId ?? payload.agent_id ?? payload.agentName ?? payload.agent_name;
    return typeof value === 'string' && value ? value : 'unknown-agent';
  }

  #recompute(): void {
    const sessions = [...this.#sessions.values()];
    if (sessions.some(session => session.attention)) {
      this.#setState('attention');
      return;
    }
    if (sessions.some(session => session.active || session.subagents.size > 0)) {
      this.#setState('working');
      return;
    }
    const completed = sessions.filter(session => session.completionPending);
    if (completed.length > 0) {
      for (const session of completed) session.completionPending = false;
      this.#pulseComplete();
      return;
    }
    if (!this.#completeTimer) this.#setState('idle');
  }

  #pulseComplete(): void {
    this.#cancelComplete();
    this.#setState('complete');
    this.#completeTimer = setTimeout(() => {
      this.#completeTimer = undefined;
      this.#recompute();
    }, this.#completeMs);
  }

  #cancelComplete(): void {
    if (!this.#completeTimer) return;
    clearTimeout(this.#completeTimer);
    this.#completeTimer = undefined;
  }

  #setState(state: CharacterState): void {
    if (this.#state === state) return;
    this.#state = state;
    this.#onState(state);
  }
}
