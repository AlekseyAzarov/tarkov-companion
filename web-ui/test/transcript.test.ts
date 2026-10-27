import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { parseIncomingMessage, responseValidators, type RequestType } from '../src/bridge/protocol';

// contracts/fixtures/bridge_transcript.json is what the native core answers
// (checked by core/tests/bridge_transcript_test.cpp). Here: every answer is
// something this bundle accepts. "*" stands for any value; as a string it
// passes the string checks it is used for (sdkVersion).

interface Exchange {
  about: string;
  web: { type?: RequestType } | string;
  native: unknown[];
}

const transcript: { exchanges: Exchange[] } = JSON.parse(
  readFileSync(new URL('../../contracts/fixtures/bridge_transcript.json', import.meta.url), 'utf8'),
);

describe('bridge transcript', () => {
  it.each(transcript.exchanges.map((exchange) => [exchange.about, exchange] as const))('%s', (_about, exchange) => {
    const requestType = typeof exchange.web === 'string' ? undefined : exchange.web.type;

    for (const reply of exchange.native) {
      const incoming = parseIncomingMessage(JSON.stringify(reply));
      expect(incoming, JSON.stringify(reply)).not.toBeNull();

      if (incoming?.kind === 'result' && requestType && requestType in responseValidators) {
        expect(responseValidators[requestType](incoming.payload), JSON.stringify(incoming.payload)).toBe(true);
      }
    }
  });

  it('covers every request type the bundle sends', () => {
    const sentTypes = new Set(
      transcript.exchanges.map((exchange) => (typeof exchange.web === 'string' ? '' : exchange.web.type)),
    );

    for (const type of Object.keys(responseValidators)) {
      expect(sentTypes, type).toContain(type);
    }
  });
});
