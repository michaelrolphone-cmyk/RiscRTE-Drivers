# Remaining durable foreground Deep recovery

This is an actionable continuation of the pocket-battery goal, not a gate on
safe returning Light and not permission to discard app state.

1. Define an app-owned idle disposition: active work defers, reversible RAM
   suspension uses Light, and only a verified durable checkpoint may permit
   clean foreground exit followed by Home Deep. Do not reuse navigation guards
   whose Save/Discard/Cancel callbacks have user-visible side effects.
2. Reuse the existing scene-state encoding and app-data transaction contracts,
   but require each app to serialize its logical model and nested draft state.
   Native stacks, grant tokens, raw pointers and cached provider tables are not
   portable recovery data. The small retained-wake record is a locator/version
   hint, not a general stack checkpoint.
3. Store recovery separately from committed settings or credentials. Recovery
   must restore an unsaved draft, never silently commit it. Treat credential
   drafts conservatively; retain in RAM until a defined private recovery policy
   exists rather than creating an incidental persistent secret copy.
4. Use versioned, checksummed records with checked commit/readback and an explicit
   recovery lifecycle. Missing, corrupt, oversized or old-schema recovery must
   produce an honest choice/error, not a fabricated model or silent discard.
5. Prove repeat checkpoints and reset interruption at every write/rename/readback/
   handoff phase, including storage exhaustion and retained cleanup. A failed
   checkpoint preserves the live RAM session and blocks terminal Deep.
6. Restore a fresh invocation before exposing input. Rebind grants, reread time
   authority, neutralize wake input, and distinguish Stopwatch UTC progression
   from stateful operations that must not restart automatically.
7. Qualify actual app classes and hardware resets/current only after source and
   fault matrices pass. Include dirty Settings/Wi-Fi editors, Calculator history,
   timed work, active transfers and custom diagnostic app idle boundaries.

The delivered Light candidate does not claim this durable behavior.
