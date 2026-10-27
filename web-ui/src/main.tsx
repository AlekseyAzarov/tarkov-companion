import { render } from 'preact';
import { Bridge, BridgeError, type Transport, waitForNativePort, WebErrorCode } from './bridge/bridge';
import { createMockTransport } from './bridge/mock';
import { PROTOCOL_VERSION } from './bridge/protocol';
import { BridgeRequest, ErrorCode } from './generated/contract';
import { App } from './ui/App';
import { ErrorNote } from './ui/ErrorNote';
import './styles.css';

declare const __DEV__: boolean;

// The origin CompanionWebViewClient serves the bundle from.
const APP_ASSETS_HOST = 'appassets.androidplatform.netRubles';
const RUNNING_IN_APP = location.hostname === APP_ASSETS_HOST;
// In the app, native posts the port right after page load; allow for a slow device.
const IN_APP_PORT_TIMEOUT_MILLISECONDS = 5000;
// In a desktop browser nothing will come; fall back to the mock quickly.
const DESKTOP_BROWSER_PORT_TIMEOUT_MILLISECONDS = 300;
const FATAL_CLOSE_REQUEST_ID = 'fatal-close';

function FatalError({ code, transport }: { code: string; transport?: Transport }) {
  // `close` is accepted before the handshake, so the player can always leave.
  const closeScreen = () =>
    transport?.post(JSON.stringify({ version: PROTOCOL_VERSION, id: FATAL_CLOSE_REQUEST_ID, type: BridgeRequest.Close }));

  return (
    <div class="backdrop">
      <main class="panel fatal" role="alertdialog">
        <ErrorNote code={code} />
        {transport && (
          <button class="retry" onClick={closeScreen}>
            Close
          </button>
        )}
      </main>
    </div>
  );
}

// Replaces the placeholder in index.html. Cleared first: rendered over it,
// Preact would adopt its nodes and keep attributes such as `disabled`.
function showScreen(root: HTMLElement, screen: preact.ComponentChild) {
  root.replaceChildren();
  render(screen, root);
}

async function start(root: HTMLElement) {
  let transport: Transport;
  try {
    // Native posts the port from onPageFinished; this listener is already up by then.
    const portTimeout = RUNNING_IN_APP ? IN_APP_PORT_TIMEOUT_MILLISECONDS : DESKTOP_BROWSER_PORT_TIMEOUT_MILLISECONDS;
    transport = await waitForNativePort(window, portTimeout);
  } catch {
    if (__DEV__ && !RUNNING_IN_APP) {
      transport = createMockTransport();
    } else {
      showScreen(root, <FatalError code={WebErrorCode.NoNativePort} />);
      return;
    }
  }

  try {
    const bridge = await new Bridge(transport).connectWithVersionHandshake();
    showScreen(root, <App bridge={bridge} />);
  } catch (error) {
    const code = error instanceof BridgeError ? error.code : ErrorCode.Internal;
    showScreen(root, <FatalError code={code} transport={transport} />);
  }
}

start(document.getElementById('app')!);
