import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Bridge, BridgeError, type Transport } from '../src/bridge/bridge';

// In-memory native side: records what the web sent, lets the test answer.
class FakeNative implements Transport {
  sentMessages: { version: number; id: string; type: string; payload: unknown }[] = [];
  private handler: (data: unknown) => void = () => {};

  post(message: string) {
    this.sentMessages.push(JSON.parse(message));
  }

  listen(handler: (data: unknown) => void) {
    this.handler = handler;
  }

  deliver(body: object) {
    this.handler(JSON.stringify({ version: 1, ...body }));
  }

  deliverRaw(data: unknown) {
    this.handler(data);
  }

  lastSentId() {
    return this.sentMessages.at(-1)!.id;
  }
}

const TIMEOUT_MILLISECONDS = 1000;
const HELLO = { protocol: 1, sdkVersion: '0.1.0', features: ['search'], initialState: { route: 'search' } };

describe('Bridge', () => {
  let native: FakeNative;
  let bridge: Bridge;

  beforeEach(() => {
    vi.useFakeTimers();
    native = new FakeNative();
    bridge = new Bridge(native, TIMEOUT_MILLISECONDS);
  });
  afterEach(() => vi.useRealTimers());

  it('sends versioned requests with unique ids', () => {
    void bridge.request('search', { query: 'salewa' }).catch(() => {});
    void bridge.request('search', { query: 'gpu' }).catch(() => {});

    expect(native.sentMessages).toHaveLength(2);
    expect(native.sentMessages[0]).toMatchObject({ version: 1, type: 'search', payload: { query: 'salewa' } });
    expect(native.sentMessages[0]!.id).not.toBe(native.sentMessages[1]!.id);
  });

  it('correlates responses by id, regardless of order', async () => {
    const first = bridge.request('track', { event: 'first' });
    const firstId = native.lastSentId();
    const second = bridge.request('track', { event: 'second' });

    native.deliver({ replyTo: native.lastSentId(), ok: true, payload: {} });
    native.deliver({ replyTo: firstId, ok: true, payload: {} });

    await expect(Promise.all([first, second])).resolves.toEqual([{}, {}]);
  });

  it('rejects with the native error code', async () => {
    const request = bridge.request('recommend', { itemId: 'x' });

    native.deliver({ replyTo: native.lastSentId(), ok: false, error: { code: 'not_found', message: 'nope' } });

    await expect(request).rejects.toMatchObject({ name: 'BridgeError', code: 'not_found' });
  });

  it('rejects responses that fail validation instead of passing junk to the UI', async () => {
    const request = bridge.request('search', { query: 'salewa' });

    native.deliver({
      replyTo: native.lastSentId(),
      ok: true,
      payload: { items: [{ id: 1 }], fromCache: false, cacheAgeSeconds: 0 },
    });

    await expect(request).rejects.toMatchObject({ code: 'invalid_response' });
  });

  it('times out, and ignores the late answer', async () => {
    const request = bridge.request('track', { event: 'slow' });
    const requestId = native.lastSentId();

    vi.advanceTimersByTime(TIMEOUT_MILLISECONDS + 1);

    await expect(request).rejects.toMatchObject({ code: 'timeout' });
    expect(() => native.deliver({ replyTo: requestId, ok: true, payload: {} })).not.toThrow();
  });

  it('ignores malformed and unknown messages', () => {
    const handler = vi.fn();
    bridge.onEvent('navigate_back', handler);
    const junkMessages = ['', 'not json', '{"version":0,"event":"navigate_back"}', '[]', '{"version":1,"replyTo":"w999","ok":true}'];

    for (const junk of junkMessages) {
      native.deliverRaw(junk);
    }

    expect(handler).not.toHaveBeenCalled();
  });

  it('dispatches native events', () => {
    const handler = vi.fn();
    bridge.onEvent('navigate_back', handler);

    native.deliver({ event: 'navigate_back', payload: {} });

    expect(handler).toHaveBeenCalledOnce();
  });

  describe('connectWithVersionHandshake', () => {
    it('sends protocol range and bundle version, returns the api', async () => {
      const connecting = bridge.connectWithVersionHandshake();

      expect(native.sentMessages[0]).toMatchObject({
        type: 'hello',
        payload: { protocol: 1, minimumNativeProtocol: 1, bundleVersion: '0.0.0-test' },
      });

      native.deliver({ replyTo: native.lastSentId(), ok: true, payload: HELLO });
      const connectedBridge = await connecting;

      expect(connectedBridge.hello.sdkVersion).toBe('0.1.0');
    });

    it('surfaces incompatible_protocol from native', async () => {
      const connecting = bridge.connectWithVersionHandshake();

      native.deliver({
        replyTo: native.lastSentId(),
        ok: false,
        error: { code: 'incompatible_protocol', message: 'web protocol 1 is no longer supported' },
      });

      await expect(connecting).rejects.toBeInstanceOf(BridgeError);
      await expect(connecting).rejects.toMatchObject({ code: 'incompatible_protocol' });
    });

    it('refuses a native side older than this bundle supports', async () => {
      const connecting = bridge.connectWithVersionHandshake();

      native.deliver({ replyTo: native.lastSentId(), ok: true, payload: { ...HELLO, protocol: 0 } });

      await expect(connecting).rejects.toMatchObject({ code: 'incompatible_protocol' });
    });
  });
});
