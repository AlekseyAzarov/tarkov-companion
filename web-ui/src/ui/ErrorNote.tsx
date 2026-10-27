import { WebErrorCode } from '../bridge/bridge';
import { BridgeErrorCode, ErrorCode } from '../generated/contract';

const MESSAGES_BY_ERROR_CODE: Record<string, string> = {
  [ErrorCode.Network]: 'No connection, and nothing cached for this yet.',
  [ErrorCode.Api]: 'tarkov.dev is having trouble. Try again in a minute.',
  [ErrorCode.SchemaMismatch]: 'The price source changed format. A game update will fix this.',
  [ErrorCode.NotFound]: 'This item no longer exists.',
  [WebErrorCode.Timeout]: 'That took too long.',
  [BridgeErrorCode.IncompatibleProtocol]: 'This feature needs a game update.',
  [BridgeErrorCode.BundleDisabled]: 'This feature is temporarily unavailable.',
  [WebErrorCode.NoNativePort]: 'Could not connect to the game.',
};

const TRANSIENT_ERROR_CODES_WORTH_RETRYING: readonly string[] = [
  ErrorCode.Network,
  ErrorCode.Api,
  WebErrorCode.Timeout,
];

export function messageForErrorCode(code: string): string {
  return MESSAGES_BY_ERROR_CODE[code] ?? 'Something went wrong.';
}

export function ErrorNote({ code, onRetry }: { code: string; onRetry?: () => void }) {
  const retryable = onRetry && TRANSIENT_ERROR_CODES_WORTH_RETRYING.includes(code);

  return (
    <div class="error" role="alert">
      <p>{messageForErrorCode(code)}</p>
      {retryable && (
        <button class="retry" onClick={onRetry}>
          Try again
        </button>
      )}
    </div>
  );
}
