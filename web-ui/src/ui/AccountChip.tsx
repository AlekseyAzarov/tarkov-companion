import { useEffect, useState } from 'preact/hooks';
import type { BridgeApi } from '../bridge/bridge';
import { parseAccountChangedEvent, type AccountView } from '../bridge/protocol';
import { BridgeEvent, BridgeFeature, BridgeRequest } from '../generated/contract';

// A closed browser sends no account_changed: don't stay disabled forever.
const SIGN_IN_BUTTON_COOLDOWN_MILLISECONDS = 3_000;

/** The result of a sign-in arrives as account_changed, whoever started it. */
export function AccountChip({ bridge }: { bridge: BridgeApi }) {
  const [account, setAccount] = useState<AccountView>(bridge.hello.account ?? { signedIn: false });
  const [signInInProgress, setSignInInProgress] = useState(false);

  useEffect(
    () =>
      bridge.onEvent(BridgeEvent.AccountChanged, (payload) => {
        const changedAccount = parseAccountChangedEvent(payload);
        if (changedAccount) {
          setAccount(changedAccount);
        }
        setSignInInProgress(false);
      }),
    [],
  );

  if (!bridge.hello.features.includes(BridgeFeature.SignIn)) {
    return null;
  }

  const startSignIn = () => {
    setSignInInProgress(true);
    setTimeout(() => setSignInInProgress(false), SIGN_IN_BUTTON_COOLDOWN_MILLISECONDS);
    bridge.fireAndForget(BridgeRequest.SignIn, {});
  };

  if (!account.signedIn) {
    return (
      <button class="chip" disabled={signInInProgress} onClick={startSignIn}>
        Sign in
      </button>
    );
  }

  return (
    <span class="account">
      <span class="account-name">{account.displayName}</span>
      <button class="chip" onClick={() => bridge.fireAndForget(BridgeRequest.SignOut, {})}>
        Sign out
      </button>
    </span>
  );
}
