# Native Wi-Fi Runtime qualification contract

The selected product successor adds cases42–45 to the existing42-case
Quick/native-alarm cohort. The standalone Wi-Fi presentation does not intrinsically
require Quick Actions. These additional product cases require its previously
qualified complete foreground/native-alarm preset. Generic Alarm presentation
remains eligible; its four compatibility cases remain31,32,35,40.

The runner reuses the actual target compile recipe and records its source hashes.
`badge_wifi_execution.inc` adds read-only controller and production-layout
observation. Inputs are physical CST reports, with content dimensions observed from
`wa->screen_width/height`, production native rectangle centers and one provider
safe-origin translation. There is no logical240 scaling, direct app-state mutation,
synthetic controller action, schedule insertion or credential insertion.

- **42:** root pagination to Scan; modeled unsupported, hidden and supported
  password-network rows; refusal messages; selected-network password page; nested
  Back; rescan and Back to freshly ready Home; real reopen/scan then modeled
  cancellation refusal retains the invocation with no subsequent observed
  hardware/service/KV callbacks. No fictional retry, writes or association.
- **43:** SSID and masked password input on all3 ASCII pages; Delete;
  SSID Done updates RAM only; password editor remains uncommitted through three
  returning Light cycles. Exact sleep tickets and successful resumes, preserved
  editor/draft, native restored pixels, then keyboard Cancel and Back to Home.
  No credential writes and no implicit connection.
- **44:** same real keyboard flow; Done updates the password draft; explicit Save
  alone writes the three namespace6 chunks/selector operations. Readback bytes
  match the real draft. Explicit Connect invokes a modeled refused radioJoin,
  never a successful association. Cancel Forget preserves those bytes; confirmed
  Forget writes the tombstone and four zero chunks. All eight writes require the
  corresponding actual explicit UI action. Back returns to ready Home.
- **45:** actual Countdown controller Save creates the schedule; real launcher
  selects Wi-Fi; the same unsaved password draft is interrupted by timed Light
  wake. A new physical key dismisses the alert durably. Pass requires completed
  substantive pixels inside the modeled circular aperture after the ACK,
  production foreground restore phase/ownership completion, unchanged final frame
  identity, exact preserved draft and no namespace6 writes.

Completed-frame captures include scan rows, each masked keyboard page, the
three-Light draft and the post-alarm password editor. Raw pixels are unchanged;
any circular viewing mask is diagnostic, not a device measurement.

These are host production-source executions with modeled GPIO/I2C/display,
wall time/reset, RAM KV and radio callbacks. They are not target instruction
execution, RF/association qualification, physical panel/touch or sleep-power
proof. A successful result is established only by matching normal and
ASan/UBSan receipts from the final clean source cohort, not this test contract.

## Fixture contract corrections during development

Initial focused runs exposed incorrect fixture assumptions, not established
production regressions: completed legacy scan polling can replace the transient
unsupported/hidden status string after250ms, so the proof observes the completed
feedback frame rather than requiring that RAM string650ms later; a refused scan
cancel selects retained custody in this profile, not a retry flow; and timed alarm
Light can retain the touch reset pin while CST sleep is acknowledged, matching the
existing alarm fixture's `!pin || touchSleeping` boundary. The corrected case42
first proves ordinary Back/Home navigation, then reopens and tests the terminal
retained refusal. No production branch was altered to make these checks pass.
