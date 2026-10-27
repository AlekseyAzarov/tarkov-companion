import {
  OLDEST_SUPPORTED_NATIVE_PROTOCOL,
  PORT_HANDSHAKE,
  PROTOCOL_VERSION,
  type HelloResult,
  type IncomingMessage,
  type RequestType,
  type Requests,
  type Responses,
  parseIncomingMessage,
  responseValidators,
} from './protocol';
import { BridgeErrorCode, BridgeRequest } from '../generated/contract';

declare const __BUNDLE_VERSION__: string;

// Failures the page detects itself; the native side's codes are in contracts/error-codes.json.
export const WebErrorCode = {
  Timeout: 'timeout',
  InvalidResponse: 'invalid_response',
  NoNativePort: 'no_native_port',
} as const;

export class BridgeError extends Error {
  constructor(
    readonly code: string,
    message: string,
  ) {
    super(message);
    this.name = 'BridgeError';
  }
}

// A MessagePort in production, a fake in tests and development.
export interface Transport {
  post(message: string): void;
  listen(handler: (data: unknown) => void): void;
}

export interface BridgeApi {
  request<Type extends RequestType>(type: Type, payload: Requests[Type]): Promise<Responses[Type]>;
  fireAndForget<Type extends RequestType>(type: Type, payload: Requests[Type]): void;
  onEvent(name: string, handler: (payload: unknown) => void): void;
  readonly hello: HelloResult;
}

// Ids we generate start with this, so they're recognisable in native logs.
const REQUEST_ID_PREFIX = 'w';
const DEFAULT_TIMEOUT_MILLISECONDS = 15_000;

interface PendingRequest {
  type: RequestType;
  resolve: (payload: unknown) => void;
  reject: (error: BridgeError) => void;
  timeout: ReturnType<typeof setTimeout>;
}

export class Bridge {
  private sentRequestCount = 0;
  private readonly pendingRequestsById = new Map<string, PendingRequest>();
  private readonly eventHandlersByName = new Map<string, ((payload: unknown) => void)[]>();

  constructor(
    private readonly transport: Transport,
    private readonly timeoutMilliseconds = DEFAULT_TIMEOUT_MILLISECONDS,
  ) {
    transport.listen((data) => this.receive(parseIncomingMessage(data)));
  }

  request<Type extends RequestType>(type: Type, payload: Requests[Type]): Promise<Responses[Type]> {
    const id = `${REQUEST_ID_PREFIX}${++this.sentRequestCount}`;

    return new Promise<Responses[Type]>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingRequestsById.delete(id);
        reject(new BridgeError(WebErrorCode.Timeout, `${type} timed out`));
      }, this.timeoutMilliseconds);

      this.pendingRequestsById.set(id, { type, resolve: resolve as (payload: unknown) => void, reject, timeout });
      this.transport.post(JSON.stringify({ version: PROTOCOL_VERSION, id, type, payload }));
    });
  }

  fireAndForget<Type extends RequestType>(type: Type, payload: Requests[Type]): void {
    this.request(type, payload).catch(() => {
      // Nobody is waiting; native logs its side.
    });
  }

  onEvent(name: string, handler: (payload: unknown) => void): void {
    this.eventHandlersByName.set(name, [...(this.eventHandlersByName.get(name) ?? []), handler]);
  }

  async connectWithVersionHandshake(): Promise<BridgeApi> {
    const hello = await this.request(BridgeRequest.Hello, {
      protocol: PROTOCOL_VERSION,
      minimumNativeProtocol: OLDEST_SUPPORTED_NATIVE_PROTOCOL,
      bundleVersion: __BUNDLE_VERSION__,
    });
    if (hello.protocol < OLDEST_SUPPORTED_NATIVE_PROTOCOL) {
      throw new BridgeError(BridgeErrorCode.IncompatibleProtocol, `native protocol ${hello.protocol} is too old`);
    }

    return {
      hello,
      request: (type, payload) => this.request(type, payload),
      fireAndForget: (type, payload) => this.fireAndForget(type, payload),
      onEvent: (name, handler) => this.onEvent(name, handler),
    };
  }

  private receive(message: IncomingMessage | null): void {
    // Malformed: ignore rather than crash the UI.
    if (!message) {
      return;
    }

    if (message.kind === 'event') {
      for (const handler of this.eventHandlersByName.get(message.event) ?? []) {
        handler(message.payload);
      }
      return;
    }

    if (message.replyTo === null) {
      return;
    }

    const pendingRequest = this.pendingRequestsById.get(message.replyTo);
    // A late answer after a timeout.
    if (!pendingRequest) {
      return;
    }

    this.pendingRequestsById.delete(message.replyTo);
    clearTimeout(pendingRequest.timeout);

    if (message.kind === 'error') {
      pendingRequest.reject(new BridgeError(message.code, message.message));
    } else if (responseValidators[pendingRequest.type](message.payload)) {
      pendingRequest.resolve(message.payload);
    } else {
      const reason = `malformed ${pendingRequest.type} response`;
      pendingRequest.reject(new BridgeError(WebErrorCode.InvalidResponse, reason));
    }
  }
}

/** Only the first port native hands over is accepted. */
export function waitForNativePort(target: Window, timeoutMilliseconds: number): Promise<Transport> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      target.removeEventListener('message', onMessage);
      reject(new BridgeError(WebErrorCode.NoNativePort, 'native bridge did not connect'));
    }, timeoutMilliseconds);

    function onMessage(event: MessageEvent) {
      const port = event.ports[0];
      if (event.data !== PORT_HANDSHAKE || !port) {
        return;
      }

      target.removeEventListener('message', onMessage);
      clearTimeout(timeout);
      resolve({
        post: (message) => port.postMessage(message),
        listen: (handler) => {
          port.onmessage = (portEvent) => handler(portEvent.data);
        },
      });
    }

    target.addEventListener('message', onMessage);
  });
}
