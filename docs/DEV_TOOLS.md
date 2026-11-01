# Cutscene log (`--dev`)

A developer tool that names the cutscene playing and keeps the last ones, so a report of where a scene's models
need a change ("Cutscene cast", [ACTORS.md](ACTORS.md#cutscene-cast)) can say exactly which scene it is. The other
`--dev` tool is the object placer ([DEV_PLACEMENTS.md](DEV_PLACEMENTS.md)). English only.

Code: `pc/src/host/devcine.c` (the list, its text, the export; ctest `devcine`), `pc/glue/devcine_glue.c` (the game's
side, game thread), `overlay.cpp` (`devcine_small`, `devcine_window`; render thread), hooks in `event.c`
(`Event_init`, `bhCamSet`, `bhCineSet`, `bhMovieStart`). Without `--dev` the hooks return at once and nothing is drawn
(`layout_check.sh` byte-identical).

## Use

* Start with `--dev`. The small log sits at the top left, behind the F-windows, and takes no input: the scene playing
  now (amber, with the cast's swaps under it) or "No cutscene", then the last four.
* **F6** (Controls, hotkey row "Cutscene Log (Dev)", shown only with `--dev`) opens the log's window, newest first,
  the scene playing in amber:
  * **Show overlay**: the small log while the window is closed (ini `dev_cutscene_overlay`, default on; only an off
    is written). Off, nothing is drawn until F6 opens the window again; the log records either way.
  * **Copy**: the whole log to the clipboard, one line per scene. A click on a line copies that line.
  * **Export**: appends the log to `<save dir>/dev_cutscenes.txt` under a header line with the date (appended, so an
    earlier export is never lost).
  * **Clear**: the history goes (a scene playing now stays). Numbers count on.
* The log is kept for the session (32 scenes; then the oldest goes). The same scene again right after itself
  (same room, kind and first event or movie) is counted (`x2`), not listed twice.

## A line

```
#3 13:54:27 RM_1020 (stage 1 room 02 case 0) | event ev 1:1 | cinema+event camera+player hidden | 121.7 s | player Claire | cast seed 0 per cutscene (scene 1021): r1 Alfred(94)->Alexia Ashford
#2 13:41:19 RM_0000 (stage 0 room 00 case 0) | movie MV_000.PSS (ev 2:31) | movie+cinema+event camera+player hidden | 1.0 s | player Claire | ...
```

| Field | Meaning |
|---|---|
| `#3` | the scene's number this session |
| `RM_1020 (stage 1 room 02 case 0)` | the room file (`room_file_now`: stage * 1000 + room * 10 + case), as `cast_room` and `kept[]` name rooms |
| `event ev 1:1` | an in-engine scene: the room script's events (task level:event) whose scripts set its camera or cinema mode (`bhCamSet`, `bhCineSet`), in the order they did. `RECVX_TEST_EVENT="frame:1:1"` starts the same event. `?`: no op named it (the player hidden by other means); the event started last on an active task level |
| `movie MV_000.PSS (ev 2:31)` | a pre-rendered movie and the event whose `MovieStart` started it |
| `cinema+event camera+player hidden` | what made it a scene, over its whole run: the movie, the cinema mode (`cb_flg 0x4`), an event camera (`st_flg 0x1000000`), the player hidden by an event (`plp->stflg 0x1000000`) |
| `121.7 s`, `x2` | how long it ran (game frames / 30); `x2`: it played twice in a row |
| `player Claire` | the character played (`sys->ply_id`) |
| `cast seed 0 per cutscene (scene 1021): r1 Alfred(94)->Alexia Ashford` | the cutscene cast: the seed, the Model Assignment, `cast_model`'s scene (0 per character, room file + 1 per cutscene), then each room record given a model: record number, character (type), the model it is drawn as; `(your look)`: Claire or Chris, never cast, drawn as the player's look for that character; `(played: the double)`: Claire or Chris played now, whose double "Character Model" draws; `(played: as itself)`: Steve or Wesker played (or about to be), left to the game or "Character Model". Missing while the cast is off |
| `double -> ...`, `player -> ...` | the "Character Model" shown on the cutscene double of the character played (while one acts) and on the player |

The overlay's short form is `#3 RM_1020 ev 1:1  121.7 s  Claire`.

## When a scene counts

Every game frame (`cheat_frame` -> `devcine_frame`): a movie is set up or playing (`pc_movie_name`), else in a room the
cast's cutscene test holds (cinema mode, event camera or the player hidden by an event; `cast_glue.c cast_scene`).
Half a second without either ends a scene; a new room, a movie starting or ending, starts the next one, so connected
scenes (event, movie, event; a scene going on in the next room) are separate lines in order. Short cinemas count too:
a camera pan as a room is entered, a lever, a close-up (many of the 600 cinema events in the scripts are such).

## Limits

* Only the events whose scripts set the camera or cinema mode are named. A scene the script starts as several events
  at once (1020: events 1..9 on levels 1..9, where only event 1 sets the cinema) shows the first; replaying it needs
  them all (`pc/tools/rdx.py script RM_1020` lists them).
* The room's own scripts (`scd0`, `scd1`) run outside the task levels: an op there names nothing.
* Movies with `RECVX_SKIP_MOVIES` last one second.
* Co-op: the log is ours; ops of an event replayed from the partner (co-op's own replay task, no level) name nothing.

## Test aids

* `RECVX_SHOW_OVERLAY=5`: the window open at start.
* `RECVX_DEVCINE_LOG=1`: each scene's line, as Copy gives it, also in the log when the scene ends (`devcine: #2 ...`).
* `RECVX_KEYS="f:F6,..."` and clicks (`f:@x:y`) drive it; Copy and a line's click log what the clipboard reads back
  (`devcine: ... copied to the clipboard, n bytes read back` and the text), Export and Clear log a line too.
