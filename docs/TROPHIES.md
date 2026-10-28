# Local trophies

[← Documentation](README.md)

Trophies are recorded locally, per launcher profile. Nothing is sent to PSN, with or without the
online module.

## Recording

When the game unlocks a trophy, the runtime appends a line to `trophies-v1.log` in the active
`BB_USER_DIR` (next to `savedata/`, apart from shared caches):

```
v1 UNIX_SECONDS TITLE_ID TROPHY_ID
```

Writes are serialized, appended and synced; links and non-regular files are refused; a failed
write is logged and the game goes on. Valid ids are 0–127. The runtime prints
`trophy N unlocked`, and the launcher imports the log into its database (profile, title and id
unique; the earliest timestamp kept). Incomplete trailing lines are retried later; malformed
ones are ignored.

Platinum (CUSA03173) is awarded locally once the 33 other base-game trophies are recorded; The
Old Hunters' six are separate. Old unlocks are not inferred from a save.

## Edited saves

A file `savedata/.bb-edited` (any file there, even an invalid one) marks an edited save set.
While it is there the runtime records no trophies and the launcher imports none.
`BB_TROPHIES_ELIGIBLE=0` stops recording too; `=1` cannot override the marker. This is not
anti-cheat: it does not detect saves edited elsewhere. The launcher's save editor writes the
marker before it publishes an edited save, and nothing removes it; backups copy it, so restoring
a clean backup from before the first edit restores eligibility. Earned history stays in the
launcher's database through restores.

An edited save set also plays **offline only** (the maintainer's decision): the launcher refuses
online play for it, and `src/runtime_net.c` refuses it again when the marker exists or cannot be
checked.

## Notifications

The game draws newly recorded trophies in its own ImGui overlay, above the presented frame.
Events enter a 16-entry queue only after the log line is synced; entries older than 30 seconds
are dropped. Notices last five seconds (platinum seven), with a one-second gap. They appear
only over the game because they are part of its frame, and do not change window focus. Already
recorded trophies are not shown again.

`BB_TROPHY_OVERLAY=0` turns notices off (Advanced → Launcher → Trophy notifications), without
affecting local recording or the launcher's trophy view. `BB_TROPHY_OVERLAY_PREVIEW=1` draws
five labelled previews without recording anything. The runtime embeds a copy of the reviewed
public trophy catalog for display; the launcher keeps its own copy for the trophy list. The
catalog uses public names and original summaries, never game trophy files, artwork or keys.
