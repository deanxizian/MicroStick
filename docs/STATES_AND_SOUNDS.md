# UI, state, and sound behavior

## Home screen

The 135×240 home screen keeps all primary information on one Roxy-centered page:

- top row: one connection slot (`BLE` or `USB`) plus local battery and charge marker;
- 96×104 Roxy animation;
- selected slot as `AG1–AG6`, its semantic state, and active count `x/6`;
- six host-colored slot dots with an outline on the selected slot;
- 7D remaining percentage and progress bar.

USB replaces the BLE label while physical USB VIN is present; it disappears after cable removal. A green dot identifies USB and a blue dot identifies connected BLE. Battery is always local and remains independent of either connection.

Fresh 7D data is shown normally. Missing or expired data is retained but dimmed; the home screen deliberately has no extra freshness caption. The Usage detail page shows last-sync age and status.

## Agent state and Roxy

Host light data remains authoritative. Unassigned slots are hollow and dark; the selected slot receives an outer highlight. The active count includes Working, Awaiting approval, and Awaiting response, but excludes Idle, Off, and Complete/Unread.

Roxy aggregates all six slots in this priority order:

```text
Error
> Awaiting input
> Working
> Complete / unread
> Idle
> BLE offline
```

Complete is held briefly so the animation is visible, then follows later host state. An unknown light combination uses a safe unknown state rather than inventing an error or approval meaning.

## Voice overlay

When the front button reaches the 250 ms hold threshold, the screen keeps the top row and Roxy but replaces the Agent and Usage areas with:

- `正在聆听` and `松开结束` while Mic is pressed;
- `正在识别` while ChatGPT processes;
- `已写入` as a short completion fallback.

Roxy does not receive an added green ring. Host lighting is used when recognized; bounded local timers provide display feedback when the host does not expose a stable voice state.

## Control Center

The menu always opens on `Approve` and uses this order:

```text
Approve
Decline
Fast
Fork
Agents
Navigation
Usage
Device
```

Front short selects next; front long selects previous; side short executes; side long returns. Agent and Navigation open submenus. Usage and Device open aligned detail pages. Every detail page shows the same `长按侧键返回` hint. Control overlays close after eight seconds without input.

Decline requires a confirmation screen. A second side-button press confirms; front-button press or timeout cancels. A disconnected BLE link never displays a successful action toast.

## Tones

- BLE connection: short rising two-tone cue.
- Successful command: short high cue.
- cancellation, unavailable command, or failure: short low cue.

A tone already playing stops when PTT starts, and newly queued tones are discarded for the duration of PTT. The release uses a conservative fixed codec level.
