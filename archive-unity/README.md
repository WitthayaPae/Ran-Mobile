# Archived — Unity-rewrite era (superseded 2026-08-24)

These documents describe the **abandoned approach**: re-implementing RAN in Unity/C#.
On 2026-08-24 the project switched to compiling the real PC client (`SOURCE/`) for
mobile — see `../STATUS.md`.

Nothing here is current guidance. It is kept only so decisions and measurements from
that period can be traced. The Unity project itself still exists, frozen, at
`../unity/`.

- `DECISIONS.md` — engine/platform choices made for the Unity build
- `COMPLETION-GAPS.md` — the Unity port's remaining-work tracker
- `HANDOVER.md` — Unity-era handover notes
- `HUD-REDESIGN-PLAN.md` — mobile HUD design for the Unity client
- `DEVICE-TEST-2026-08-22.md`, `DEVICE-TEST-2026-08-23.md` — on-device test logs for
  Unity builds that no longer exist

Genuinely engine-independent knowledge (decoded file formats, the asset pipeline,
content parity) was **not** archived — it lives in `../reference/`.
