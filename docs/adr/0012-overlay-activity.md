# ADR 0012: The overlay is an activity of its own, not a view in the game's window

**Status:** accepted · 2026-10

## Context

`showItemLookup()` starts `CompanionActivity`, a translucent activity over
the game. The game's activity therefore gets `onPause` and loses window
focus while the overlay is open, as it would for a system dialog. Many
engines pause the game loop and the audio in `onPause`, so for those games
the world behind the overlay stops.

An SDK that lives in someone else's game can also show its UI inside the
game's own window: a `Dialog` or a view added to the activity's decor view.
The game's activity then stays resumed and focused, and nothing about its
lifecycle changes. Account UIs embedded in games often work this way.

## Options

| | Activity of its own (now) | Dialog or view in the game's window |
|---|---|---|
| Game's lifecycle while open | `onPause`, focus lost; the surface stays (no `onStop`) | unchanged |
| Rotation, resize, dark mode | its own `configChanges`; the WebView and the page survive | follows the game's activity: if the game doesn't handle a change, the activity is recreated and the overlay with it |
| Process death | restores the route, query and item (`onSaveInstanceState`) | the game would have to save and reopen it; the SDK can't hook the game's `onSaveInstanceState` |
| Back button, predictive back | its own `OnBackInvokedDispatcher` | a `Dialog` has one; a bare view has to intercept the game's |
| Keyboard (IME) | `adjustResize` on its own window | depends on the game's `windowSoftInputMode`, often `adjustNothing` in full-screen games: the search field ends up under the keyboard |
| Insets, cutouts, immersive mode | its own window, its own insets | inherits the game's flags: immersive games hide the bars the page lays out around |
| Integration | one call, any activity | the same call, plus rules for the game's window flags and configChanges |

## Decision

Keep the overlay an activity of its own. The in-window overlay wins one
property (the game stays resumed). Where it loses, the cause lies in the
game's manifest or window flags, which the SDK can't fix from its side. The
activity's lifecycle cost is predictable and documented; the in-window
overlay's costs are not.

What the game sees is spelled out in the [integration guide](../integration-guide.md#4-open-it):
`onPause` without `onStop`, so the surface and the GL context stay, and a
game that wants its world to keep moving behind the overlay keeps rendering
in `onPause`.

## Consequences

- Games that pause in `onPause` show a frozen frame behind the overlay,
  acceptable for a lookup that takes seconds.
- The overlay's lifecycle bugs are the SDK's own and covered by its tests
  (rotation, process death, renderer crash), not spread across game teams.
- If a game needs the in-window form (for example a multiplayer game that
  must not pause), it can be added as a second presentation of the same
  bridge and page: the protocol and the core don't depend on which window
  hosts the WebView ([ADR 0007](0007-bridge-protocol-in-core.md)). It would
  ship with the restrictions in the table as documented requirements.
