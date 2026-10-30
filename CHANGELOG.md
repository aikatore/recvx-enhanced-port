# Changelog

## Unreleased

### New

These are bigger, experimental features: expect some parts not to work fully yet.

- **BioRand randomizer**: the title menu's EXTRA has BIORAND. It downloads BioRand's generator, sets
  its options in the game's own menus, generates a randomized game from a seed and starts it. Saves
  remember their seed, and in co-op the partner can generate the host's game with one click. Enemies
  are randomized by the port's own randomizer, which BioRand's menu now offers too.
- **Import from other discs**: EXTRA > IMPORT takes the playable characters and NPCs from your
  Resident Evil Outbreak File 2 disc as Character Models, and languages from your other Code: Veronica
  discs.
- **Languages and subtitles**: import French, German and Spanish from the European disc, Japanese from
  the Japanese disc, or Russian from the US disc with Brosko's fan translation. The game and our menus
  then play in that language, and cutscene subtitles can be shown in any imported language. Languages
  with their own letters (Japanese, Russian) show ??? at first: leave the settings once after choosing
  one, and the letters load.
  Our own menus and settings were translated with AI and may not read naturally yet; once most
  features are done, help from the community with proper translations would be very welcome.

### Gameplay

- **Custom cutscene cast**: Model Selection has a new Custom choice. Check the models you want in a
  list (F1 or the co-op lobby); the choice is saved, and in co-op the host's list counts.

### Menus

- **OPTION layout**: ENHANCED SETTINGS is now below VIBRATION, in place of DEFAULT (no longer
  offered), and in a game RETURN TO TITLE is the bottom row; EXIT is always at the bottom right.
- **Title**: BATTLE GAME moved into the new EXTRA entry below OPTION, so the title menu is tidier.

### Co-op

- **Clearer co-op texts**: some wording and information in the lobby and F3 was cleaned up.

### Fixes

- **Grenade launcher aim with another Character Model**: in third person the grenade launcher no longer
  points up while aiming with another model; it points at the crosshair.
- **Co-op card doors**: a door your partner opens with a card now also looks open for you.
- **Co-op levers**: the submarine lever and the hangar bridge switch can't be used again right after
  your partner used them, which used to undo the trip or the bridge for both.
- **Co-op airport bridge**: the hangar bridge now rises to the same height for both players.
- **Co-op sleeping enemies**: enemies that wait for a player to come near now wake for either player,
  can be hit right away, and no longer freeze while the host reads something.
- **Co-op stairs after a scene**: stairs no longer stay blocked after your partner's scene in the
  palace main hall.
- **Co-op security camera**: in the training facility only the player using the camera sees its view;
  neither of you ends up past the code door.
- **"Cannot reserve game memory" at start-up**: happens less often, and when another program blocks
  the game's memory, the message now names it. A full fix is planned for later, as it needs a larger
  rework done with care.

## 0.6.10 (2026-10-25)

### Gameplay

- **Rifle ammo stacks** (Gameplay, on by default): rifle and assault rifle bullets stack in one slot
  like the other ammo. Off: each pickup takes its own slot, as in the original.
- **Cutscene cast randomizer** (Gameplay, also in the co-op lobby): in cutscenes the story characters
  appear as other models from the enemy seed, the same one per character or a new one each cutscene;
  with cutscene models their faces keep moving with the lines.

### Fixes

- **Menu arrows**: the arrows in ENHANCED SETTINGS and the lobby no longer slide between entries when
  you move the cursor.
- **Damaged files**: damaged or crafted mod, room, texture and archive files no longer crash the game;
  a broken room file from the mods folder falls back to the disc's, and oversized HD textures are
  skipped.
- **Unusual save names**: the lobby no longer freezes or crashes with unusual save file names.
- **Crash reports on Linux**: written more reliably, also when the game runs out of stack.
- **Lighter and lockpick in the item box**: taking the lighter or Claire's lockpick out of the item box
  no longer makes the other one disappear; with its slot taken, it goes into your bag like any item.
  Picking up the lighter while holding the lockpick keeps both too.
- **Full shared item box** (co-op): when both players put items into the last free places at the
  same time, the partner's item comes back into its bag, or lies at its feet with the bag full,
  instead of being lost.
- **Partner's event items** (co-op): an item from an event lying on the floor for the partner no
  longer crashes the game in some rooms, such as the lockpick in the room where the game starts.
- **Albinoids and moth eggs**: an adult Albinoid no longer crashes the game while a young one holds
  you, and a moth egg laid on a co-op partner shown as another playable character is drawn properly.

## 0.6.9 (2026-10-24)

### Gameplay

- **More enemies per room**: a room now holds about twice as many enemies as before, so the enemy
  spawner, extra enemies and the randomizer can fill rooms with much bigger crowds, in co-op too.

### Controls

- **Combined controls** (CONTROLS, now the default): the left stick is for free 3D movement, the
  d-pad for tank controls, like in the Resident Evil HD remaster.
- **Aim without auto-aim**: hold the left trigger (E on the keyboard) to aim where your character
  faces, without turning to the nearest enemy; the normal aim button keeps the auto-aim.
- **Mouse buttons in fixed camera**: a mouse button bound to a game button now works while
  you play, e.g. right mouse to aim and left mouse to fire. Clicks in menus stay menu clicks.

### Co-op

- **Brighter lobby models**: the characters in the lobby are now lit like in a photo studio, so
  faces and clothes are clearly visible instead of almost black.
- **Model list in the lobby**: MODEL now opens a scrollable list of all models that shows each one
  in the preview as you move through it, so finding the one you want is quicker.
- **One player per puzzle**: while you use a puzzle, a code terminal or anything else you examined,
  your partner cannot open the same one at the same time ("... is using this").

### Fixes

- **Co-op puzzles solved by your partner**: now also count as solved for you when you are in the
  same room. Before, the puzzle could reopen empty and keep you stuck, or its door stayed locked.
- **Co-op: self-destruct code undone**: your partner entering the Antarctic base's terminal room
  after the code was entered no longer resets it, so the launch starts for both.
- **Co-op bosses**: the plane's Tyrant hurts both players again, Alexia's first form no longer gets
  stuck after grabbing the joining player, and the Gulp Worm and Nosferatu hit the joining player
  properly.
- **Co-op crash with Alexia's second form**: the joining player's game no longer crashes in certain
  conditions.
- **Reload with the side pack**: ammo in the bag's last two slots now reloads your weapon.
- **Sub machine gun reload crash**: reloading the sub machine gun no longer crashes the game.
- **Binding the triggers or sticks**: binding LT, RT or a stick direction in ENHANCED SETTINGS no
  longer switches the menu page.
- **Co-op: inventory and actions locked**: after an enemy attack you could no longer open the
  inventory, examine or use doors until you changed rooms. Fixed.
- **Co-op: stuck after a moth larva**: the host could stay in the throwing-it-off animation for good.
  Fixed.
- **Co-op: items kept when joining again**: after the host's game crashed, the joining player
  could come back with only the starting items. They now keep their items.
- **Co-op crashes**: fixed several crashes, mostly for the joining player in the late game (moths,
  boss helpers) and when changing weapons while new enemies appeared.
- **Co-op black screen**: there were some occurrences of a black screen that did not fade away.
  Those cases were adjusted so it properly fades away again.
- **Crash reports**: a crash now always writes a small `recvx-crash.log` next to the game. Attach it
  to a bug report; IP addresses are hidden in it.

## 0.6.8 (2026-10-23)

### Display

- **Smooth menus** (DISPLAY, on by default): the inventory, map, files, item box, typewriter, title
  menu and OPTION now move at your display's frame rate like the game itself: panels slide, windows
  open and the cursor glides smoothly. Menus also react sooner to a press, and quick taps are no
  longer missed. Off: menus as before.

### Gameplay

- **Difficulty select** (GAMEPLAY, on by default): NEW GAME now lets you choose Normal, Easy or Very
  Easy, as in the European and Japanese versions; the menu was still hidden on the US disc (thanks to
  Piccio23 for pointing the way). Off: always Normal, as before.

### Controls

- **Vibration**: gamepads with rumble now vibrate as the original controller did, when you shoot,
  get grabbed or hit, and in big moments. OPTION's VIBRATION can now be chosen when such a gamepad
  is connected, and a new Vibration strength setting (CONTROLS) makes it softer.
- **Modern controls after stairs**: when the camera changes while you climb stairs or a ladder, or
  climb over something, the stick now follows the new camera as soon as you are back in control,
  instead of keeping the old camera's directions until you let go.

### Co-op

- **Quick start** (lobby GAME RULES, EXTRA EVENTS; off by default): a new co-op game starts with the manual
  already read and the combat knife in your bag instead of on the table, for both players.
- **Your partner aims up and down**: in third person your partner's arms and upper body now tilt up
  and down to where they aim, not only left and right.

### Fixes

- **Co-op: items from missed events**: a weapon or key item that an event gave your partner while you
  were elsewhere, such as the handgun in the prison courtyard, could be missing from the floor when
  you came in, if you had got that item from the same event in an earlier co-op game since starting
  the program. It now lies there, and it glints like other items on the floor so it is easy to spot.
- **Flickering bugs with display-rate frames**: in Chris's Antarctic Base, the swarm of bugs on the
  walls and floor of the ant hall jittered against the walls and flickered in and out of them
  while the camera moved, most of all in over-the-shoulder view. They now move smoothly with the
  rest of the picture.
- **Flashing walls with display-rate frames**: while the camera turned quickly in over-the-shoulder
  view, a wall, door or pillar coming into view beside the camera could jump for a moment, pop in
  too early or open a dark crack along its edge, for example at the elevator door in the Antarctic
  Base's pipe corridor. Such walls now move smoothly with the rest of the picture.
- **Co-op: items lost in a trade**: trading the combat knife, an empty gun or the Gold Lugers to
  your partner could make the item disappear from both inventories. It now arrives like any other
  item, and if your partner has no room for it, you keep it.

## 0.6.7 (2026-10-22)

### Gameplay

- **No "Now Loading..." text with fast loading**: with Fast loading on, the black load screen
  between rooms, after starting a new game and after loading a save no longer shows the
  "Now Loading..." text. With Fast loading off it shows as before.

### Co-op

- **Game over for both** (lobby GAME RULES): when one player dies, both get the game over and
  continue together from the last continue point.
- **Metal detector skipped** (lobby GAME RULES, EXTRA EVENTS; on by default): in co-op the prison's metal
  detector and its security boxes are skipped.
- **Tidier lobby game rules**: the enemy and randomizer options and the extra events have their own pages.

### Fixes

- **Title menu after returning to the title**: after RETURN TO TITLE, a game over or the end of a
  co-op session in third person, the gamepad could move in the title menu but not select anything.
- **ENHANCED SETTINGS pages**: with third person on, the keyboard's Q and W did not switch pages in
  the title menus and OPTION. The page row now names the buttons of your device.
- **Classic gamepad aiming in third person**: the crosshair only moved with the game's auto-aim.
  The right stick aims now, and the left stick and d-pad move the crosshair while aiming.
- **Modern gamepad in third person**: Start opens OPTION, which the gamepad could not reach before,
  and Y, View and Start close the inventory, map and OPTION again.
- **Co-op: stuck loading after key events**: a joining player following the host through a room
  that changes after a key item is used (like the prison yard after its gate opens) could hang on
  the load screen or disconnect. Following now loads the room as the host has it.

## 0.6.6 (2026-10-22)

### Gameplay

- **Auto-climb stairs** (GAMEPLAY, on by default): walking into stairs takes you up or down them
  without pressing the action button. Some stairs with an event, such as one in the Palace, need
  the button the first time.

### Third person

- **Precise hits** (THIRD PERSON, on by default): shots only hit where the crosshair is actually on
  an enemy's body, and head hits do more damage.

### Performance

- **Steady game speed with many effects**: in scenes with a lot of 2D effects the game itself could
  slow down. The renderer was adjusted so these scenes play smoother.

## 0.6.5 (2026-10-21)

### Co-op

- **Hide IP addresses**: a new option on the CO-OP settings page for streaming and screenshots.
  With it on, the lobby and the co-op window never show your or your partner's IP address, and
  the address fields show stars. Copying the join address and joining still work as before.

### Third person

- **Fog like the fixed cameras**: over the shoulder, fog and darkness were much weaker than with
  the fixed cameras, because the camera is so much closer to you. Fog now looks as the fixed
  camera shows it: you and your surroundings get the same haze or darkness, and it still fades
  smoothly between camera angles. Too much? The Fog intensity setting lowers it as before.

### Fixes

- **Zombie heads come off**: when a magnum shot or the shotgun aimed up close burst a zombie's
  head, the burst and the blood played but the zombie fell with its head still on, whatever look it
  had. The head is now gone and the zombie falls headless, on every zombie look, the randomizer's
  and the enemy spawner's included, and your co-op partner sees it too. The GAMEPLAY option "Head
  blow-offs" turns it off for the original look.
- **Co-op: frozen at the turntable, the guillotine gate or the lift**: when one player used the
  turntable, the Eagle Plate or the Halberd while the other was in the middle of the same thing
  (answering "Move the turntable?", or the item screen still closing), one of them could stay stuck
  in the scene for good. Each player's scene now finishes as it does in single player.

## 0.6.4 (2026-10-20)

### Fixes

- **Stutter at camera changes with an HD texture pack**: the first time a camera angle showed
  textures not seen before, the game froze for a moment while it loaded their HD versions. They
  now load in the background: the game keeps running, and those textures switch to HD a moment
  later. Rooms still appear with their HD textures from the start.

## 0.6.3 (2026-10-19)

### Gameplay

- **Skippable ending and credits**: press any key, mouse button or gamepad button to skip the
  ending movie, and press again to skip the credits. The results screen, your unlocks and the
  save after the ending still come as usual, and the next movies keep their sound.

### Fixes

- **Tyrants from the spawner or the randomizer**: in a narrow room a Tyrant could push you
  through a wall. It no longer does.
- **Music after an interrupted co-op follow**: if the game ended (game over, quitting) while you
  were being moved to the host's room, the next door you used could get its music wrong.
- **Silent movies after skipping the intro**: skipping the intro movie at start-up left every
  later movie without its sound, such as the one in the graveyard. Movies now always play their
  sound, and the intro no longer gets louder for a moment when you skip it.
- **Borderless fullscreen under Windows**: it behaved like exclusive fullscreen, with a black
  screen when switching to other programs and nothing shown over the game. It is now a real
  borderless window: Alt+Tab is instant, and popups and overlays show over the game.
- **Quick knife after standing still**: once your character started the idle animation (after
  standing still for a few seconds, for example right after loading a save), the quick knife did
  nothing until you moved. It now draws the knife right away.
- **Underwater look in the submarine dock**: in over-the-shoulder the screen still waved and
  stretched as if seen from underwater, because some fixed cameras there look in from the water.
  Over-the-shoulder now shows the dock clearly; the fixed cameras keep the underwater look.
- **Black screen at Chris's oil pressure puzzle**: with "Screen effects" set to Off, the puzzle
  screen was hidden along with the effects, leaving it black. The puzzle now always shows; Off
  shows it in 4:3.
- **Floating dolls in Alexia's room (widescreen)**: from the camera by the doll cabinet, the
  dolls stood in mid-air at the side of the picture, without the shelves they sit on. They now
  stay hidden together with their shelves, as in 4:3.

## 0.6.2 (2026-10-18)

### Third person

- **Adjusted dual weapons in over-the-shoulder**: with dual weapons one gun now always shoots
  where you are aiming, and the other aims at nearby enemies up to 45 degrees away.

### Fixes

- **Moths and Black Widows**: fixed some bugs where their attacks could leave you stuck in an
  animation.
- **Crash when spawning an enemy in the moth corridor**: using the enemy spawner in the Antarctic
  base corridor with the moth nest crashed the game. It now works there.

## 0.6.1 (2026-10-18)

### Co-op

- **Lobby game rules: randomizer variety and event enemies**: the host can now set the
  randomizer's variety (Wild or Mild) and whether event enemies change too in the lobby's GAME
  RULES, and the joining player sees them there. Both show while the randomizer is on.

### Menus

- **Mouse on the save and load screens**: at a typewriter and in LOAD GAME, point at a save to
  select it and click to pick it, scroll with the wheel, and right click to go back. Yes/No
  questions such as "Use an ink ribbon?" and the memory card screens work with the mouse too.
  Keyboard and gamepad work as before.

### Display

- **Borderless or exclusive fullscreen**: the Fullscreen setting is now Display mode: Windowed,
  Borderless fullscreen (as fullscreen was before) or Exclusive fullscreen, which switches the
  screen to a resolution and refresh rate you pick, the desktop's by default. F11 switches
  between windowed and the fullscreen you chose last, and the window comes back where it was.
- **Adjusted culling in widescreen**: with the fixed cameras, the side margins now show the walls,
  floors and props the original leaves out for each camera angle, as the third-person views do.
  The middle of the picture stays exactly as the original, and walls the camera looks through
  stay hidden.
- **Scope fills the screen in widescreen**: the sniper rifle's and the linear launcher's scope
  now tints the whole picture instead of only the 4:3 middle, so the sides no longer look like
  dark bars. The reticle stays round in the middle and shots still go to its centre. "Screen
  effects" set to Original keeps the old look.

### Controls

- **Modern fixed-camera controls by default**: on a first start the fixed cameras use the modern,
  camera-relative controls. Classic tank controls are one setting away (CONTROLS); an existing
  settings file keeps your choice.
- **Mouse aiming through the scope**: in third person the sniper rifle's and the linear
  launcher's scope (Nosferatu, the final battle) now turns with the mouse, smoothly and slower
  while zoomed in, and the left mouse button fires. Hold Shift to zoom in. The gamepad and the
  fixed cameras work as before.

### Performance

- **Smoother rendering**: the game's constant texture switches cost much less time per frame, in
  every room, fight, menu and cutscene, which leaves more headroom for high frame rates.
- **No hitch when a game starts**: the short freeze when going from the title into the game is
  gone.

### Fixes

- **Music after a teleport**: teleporting out of a save room, or out of any room with its own
  music, kept that music playing in the new room. The music now stops or changes as it does when
  you walk through the door, and carries on when both rooms share it. The same goes for a room's
  background sounds, and for the partner following you in co-op.
- **Scope with a gamepad in third person**: with the Modern gamepad controls, the sniper rifle's
  and linear launcher's scope closed again as soon as it opened. Holding LT now keeps it up: aim
  with the right stick, click the right stick to zoom, RT fires and RB reloads.
- **Room parts popping at the screen edges**: in third person and with the fixed cameras in
  widescreen, walls, floors and props near the left and right edges could vanish and pop back
  while the camera turned, most visibly on ultrawide screens (in the prison courtyard the
  building beside the door to the right of the big gate was cut off). They now stay in the
  picture until they have really left it. The middle of the fixed cameras' picture stays as
  the original.
- **Mouse stuck after a puzzle**: backing out of a puzzle that asks you to pick a button or a
  number (the switch panel in the Gold Luger room, the dial in the Palace office, the door keypad
  at the bio-lab) left the mouse cursor showing, and in third person the camera would not turn
  until you left the room. The mouse now goes back to the camera as soon as you have control again.
- **Red light in the palace main hall**: in third person the hall could flash red while walking
  between camera areas, from the alarm lighting that should only show once the alarm is on. The
  lighting now matches the fixed cameras, and the alarm still lights the hall red when it sounds.
  Rooms with many lamps also light up the way the fixed cameras show them.
- **Stuck after Alexia's flames**: after being set on fire by Alexia's first form, your character
  could freeze in the next hit, aim or step and never get control back. Also with "You appear as",
  the body no longer jumps to a wrong pose for a moment when an enemy grabs or burns you.
- **Crash at the title with "You appear as"**: with your model set to one of the other playable
  characters, the game no longer crashes when you go back to the title from a co-op game (as host
  or partner, also after a game over), or when you open or leave the co-op lobby after playing.
  The story's character switches and the Battle Game no longer crash with it either.
- **Room sounds after they stop**: rain, wind and other looping room sounds now fade out fully
  when they stop, as on the PS2. Before, a quiet trace of them went on playing, most noticeably on
  the title screen after RETURN TO TITLE, and it could build up over a long game.
- **Over-the-shoulder camera on stairs and ledges**: the camera now stays with the character
  while climbing stairs and climbing up or down a ledge, as it does while walking, instead of
  standing still and then jumping forward every few steps.
- **Several HD texture packs**: when two packs have the same texture, the one in the folder that
  comes first by name now always wins, so you can choose by naming the folders 1_..., 2_....
- **HD textures: an unreadable file no longer slows the game down**: a replacement that cannot be
  loaded is skipped after the first try and named once in the log, instead of being read again
  every time its texture is drawn.
- **Co-op randomizer**: when the joining player's game could not set up one of the host's random
  enemies, the rest of that room could come out differently from the host's. It now follows the
  host's choices.
- **Battle Game characters**: turning "Battle Game unlocked" off in the F1 window while on the
  title screen no longer leaves BATTLE GAME with only Claire to choose from.

## 0.6.0 (2026-10-17)

### Gameplay

- **Skippable intro**: with "Skip intro" off, the intro screens and the intro movie can now be
  skipped with any button, and any key or click gets past PRESS START.
- **Skip door animations: Off, Instant or Skippable** (GAMEPLAY): Off plays the original door,
  stairs and ladder animations, Instant (the default) skips them, Skippable plays them but any
  button cuts them short.
- **Battle Game: every character from the start**: all Battle Game characters are unlocked
  right away (with "Battle Game unlocked" on, the default).
- **Fast loading** (GAMEPLAY, on by default): room changes, starting a new game and loading a
  save are much quicker. The game no longer waits for the PS2's disc and sound chip, which the PC
  does not have: the next room's sounds and textures are ready at once, so the load screen is gone
  almost as soon as the screen has turned black, and with "Skippable" doors you can skip the
  animation much sooner. Turn it off for the original loading times. In co-op each player chooses
  for themselves.

### Controls

- **Reload with the fixed cameras, and Aim + button**: reloading now works out of the box in
  every camera and control scheme. On the keyboard R reloads everywhere; on the gamepad hold aim
  and press Triangle / Y (with the twin-stick third-person controls RB stays). The reload button
  can now share a button with another action instead of taking it away: pressed while you aim it
  reloads, and the other action does not happen; pressed without aiming the button does what it
  always did. CONTROLS shows such a binding as "Aim + button". Pressing aim and the button
  together also works, the reload follows as soon as the weapon is up. If you had left the reload
  unbound, you get the new defaults.

### Menus

- **Return to the title**: during a game the OPTION screen has a new entry, RETURN TO TITLE, on
  the DEFAULT row above ENHANCED SETTINGS.
- **ENHANCED SETTINGS tidied up**: your model settings ("You appear as", gender-matched
  animations, swapped model in cutscenes, talking faces) moved from the CO-OP page to a new
  APPEARANCE page, since they apply in single player too; F1 has the same section and can now
  change "You appear as" as well. The DISPLAY page has new WIDESCREEN and FOG AND BRIGHTNESS
  headings. Your settings stay as they were.

### Appearance

- **Swapped model in cutscenes** (APPEARANCE page, on by default): turn it off to keep playing as the
  model you chose while the cutscenes show the real Claire or Chris, with their own hands, faces
  and voice, ponytail included. Your model is back as soon as you are in control again. Scenes in
  which the character you play acts as itself, without a stand-in, keep your model. In co-op each
  player chooses this for their own screen.

### Display

- **Movie zoom** (DISPLAY, off by default): in widescreen the movies grow, without being
  stretched, until the black bars above and below them leave the screen, or until the picture
  reaches the sides of the window. Nothing of the picture itself is cut off. On a widescreen
  monitor thin bars stay, since the movies are a little wider than the screen; on an ultrawide
  one the bars are gone and the movie fills the full height. Fades over the movies cover the
  whole screen as before.
- **Fog intensity** (DISPLAY, full by default, as on the PS2): thins the game's fog, which on the
  PS2 can hide much of a room. Turn it down a little to see further into foggy places such as
  Rockfort's yards, the airport or the Antarctic base, or all the way to remove the fog. Many
  rooms also use a black fog as darkness, so these get lighter too. The change shows at once.
  Suggested by fmil95 of the decompilation project.
- **Widescreen menus**: menus now properly support widescreen and ultrawide.
- **HD texture packs**: put a PCSX2 HD texture pack for the game into `mods/textures` and the
  game uses it, as the pack comes. Rooms show the HD textures from their first frame; replacements
  that are broken are skipped. Redesigned menus, title and inventory from a pack are used too
  (DISPLAY: HD textures: UI); replacing the font is off by default (HD textures: font).

### Cheats

- **Teleport by name**: the teleport cheat (F2 and ENHANCED SETTINGS EXTRAS) now lets you pick an
  area, a room and an arrival point by name instead of numbers. In F2 you can also search rooms
  by name.

### Fixes

- **No more twitch with gender-matched animations**: the model no longer jerks back into the
  recoil after a shot, and no longer jumps when grabbed, at a quick turn or when raising the
  weapon. In co-op the partner's shots look smooth too.
- **Third person with "You appear as"**: when an enemy stepped right into the crosshair, the upper
  body no longer jumps up for a moment and settles back.

## 0.5.2 (2026-10-17)

### Fixes

- **Third person: crashes in four rooms**: with the over-the-shoulder camera on, the game crashed
  on entering the Antarctic base's weapons storage room (the missiles and the magnum), the ladder
  room between Rockfort's training facility and the Albinoid's pool, and the room the elevator
  behind the tiger statue arrives in. These rooms now play normally with the camera on.

## 0.5.1 (2026-10-16)

### Fixes

- **Co-op: grabbed while knifing or aiming**: when a zombie grabbed the joining player while the
  knife was swinging or a weapon was raised, the character was held off to the side of the
  zombie, twisted around one foot, and could end up inside or behind a wall when let go. The
  grab now holds the character where the zombie has it, as in single player. A grab that would
  put the character behind a wall is let go instead.

## 0.5.0 (2026-10-16)

### Gameplay

- **More varied enemy randomizer**: a randomized room now draws from every enemy kind that works
  there, from anywhere in the game (zombies in all their looks, Cerberus, Bandersnatches, Hunters
  and Sweepers, Black Widows, anatomist zombies, and in the new *Wild* variety
  also Tyrants, Giant Black Widows, and bats and Spotters in place of bats), each kind weighted so
  zombies no longer dominate, and each enemy of a room a different kind where possible. Big
  enemies only appear where there is room around them, and rooms whose doors open once every
  enemy is dead only get enemies that stay and can be killed there. Rooms with exploding props,
  Spotters or the ceiling trap, and the prison's kill-to-open rooms, are randomized now too; boss
  rooms stay as they are. New setting "Randomizer variety": *Wild* (default) or *Mild* (two
  common kinds per room).
- **Event enemies are randomized**: enemies a room shows later (ambushes, enemies appearing after a
  story event) become other kinds as well, and enemies that a scene animates (zombies bursting in,
  the Hunters' entrances) get another look of their own kind, so the scene still plays as
  written. Kill flags and doors that open when enemies die keep working. New setting "Randomize
  event enemies" (on by default).
- **Co-op**: the protocol changed (both players need this build); the host's variety and event
  setting count for both.

### Controls

- **Twin-stick gamepad controls in third person**: with the over-the-shoulder camera the gamepad
  now has a modern control scheme: the left stick moves, the right stick turns
  the camera, the left trigger aims and the right trigger fires. These buttons can be remapped
  separately from the fixed-camera ones, and "Gamepad controls: Classic" brings back the old
  controls.
- **Reload button**: in third person, R (keyboard) or RB (gamepad) reloads your weapon whenever
  it isn't full, without opening the inventory, just like firing on an empty magazine. On the
  gamepad, quick turn moved to d-pad down.

## 0.4.0 (2026-10-16)

### Gameplay

- **Battle Game from the start**: BATTLE GAME is in the title menu without a finished
  playthrough (GAMEPLAY setting "Battle Game unlocked", on by default). It only changes what the
  menu offers: your saves and system data are not touched, and turned off the menu is as in the
  original again. Finishing a Battle Game saves its records but does not count as finishing
  the story. Its characters unlock as before: Claire from the start, Chris after finishing the
  story once, the others under the original's conditions.

### Renderer

- **Menu frames: no more dark lines**: the status screen, files, map and item screens showed thin
  dark lines between the pieces of their frames and backgrounds, most visible at higher internal
  resolutions (for example around the text box under the menu). The last pixels of each piece
  reached past its picture into the neighbouring one in the texture; each piece now samples only
  its own picture, as on the PS2. Fixed thanks to fmil95 (decompilation team), who found
  and tracked down the problem.
- **Steve in the gas room** (Palace): the monitor close-up when freeing Steve showed the game in a
  tiny window; it now shows the full view, and the gas effect fills the whole screen in
  widescreen. The memory card screen's save counts had the same kind of bug.

### Fixes

- **Alexia's tentacles** use their intended attack timing again: a decompilation bug read the wrong shape table. Fix by fmil95 (decompilation team).
- **Metal detector with "No ink ribbons needed"** (prison): with the setting on (or the co-op
  host's INK RIBBONS *Not needed*), the detector went off even with every metal item put in the
  trays. The setting made every check for an ink ribbon succeed, and the detector checks for ink
  ribbons too, since they count as metal there. The setting now only applies to the typewriters;
  the detector goes off for ink ribbons you actually carry, as in the original. Test aid:
  `RECVX_ITEM_TRACE=1` also logs the detector's state in that room.

## 0.3.1 (2026-10-16)

### Renderer

- **Camera cuts at high frame rates**: the in-between images no longer blend across a camera
  cut. The game changes the cut after drawing a frame, so the renderer took the cut one
  frame late and swept the camera from the old view to the new one, through walls and past room
  parts hidden for one of the two cameras. The cut is now taken from the frame as drawn, event
  cameras and camera jumps (a cutscene cut, a door animation's camera) count as cuts too, and
  the new view is shown at once. As a side effect the frame just before a cut is now smoothed too.
- **Effects follow what they are attached to**: the lighter's flame, muzzle flashes, smoke,
  sparks, fire and blood are placed in the in-between images like the 3D models, so the flame
  stays on Claire's lighter instead of jumping at the game's own rate. Their animation still steps
  at the game's rate. `RECVX_INTERP_STATS` also counts matched effect vertices and logs camera
  jumps.

### Picture calibration

- **Brightness, Contrast and Gamma** in DISPLAY / PICTURE (F1 and ENHANCED SETTINGS), with
  "Reset picture calibration". They adjust the game's picture, menus and movies included, but not
  the F1 overlay, and change it live. At the neutral values the picture is exactly as before.
- While one of them is chosen, both menus show a small test pattern of grey squares on a black and
  a white strip, to set the dark and light ends.

### You appear as

- **Models keep their own proportions when animated**: each model is moved by its own skeleton,
  with Claire's or Chris's joint rotations carried over relative to each one's rest pose, instead of
  fixed joint positions. Long or extra-jointed limbs now bend at the right joints: the Bandersnatch
  holds the weapon in its claw instead of mid-arm, Monster Steve moves his arms from the shoulders
  instead of the collarbones and holds the weapon in his hand instead of at the elbow.
- **Feet on the floor**: a taller or shorter model crouches and bobs in proportion, and a foot of
  another shape no longer lifts the body.
- **Aiming up with tall models** in third person: the camera's height, shoulder offset and distance
  follow the model's height, so it is no longer inside a Tyrant or Monster Steve while aiming; the
  aim pose is solved on the weapon as it is drawn, so its barrel points at the crosshair for every
  model and pitch. The shots are
  unchanged: they always went where the crosshair is.
- A weapon or gloved hand the model has no hand for (Nosferatu's right, the Bandersnatch's left)
  is no longer drawn floating in the air.
- The character played without another model is unchanged. Test aids: `RECVX_RETARGET=0` keeps
  the previous animation of models, `RECVX_RIG_DUMP=1` logs their skeletons and feet
  ([ACTORS.md](docs/ACTORS.md#retargeting)).

### Audio

- **Computer terminals click for every letter**: the Palace's security lock, the 3D scanner, the
  Albinoid file, the password and self-destruct terminals. Each letter the screen prints starts a
  short sound that the next letter stops one frame later. The emulated sound driver only moved on
  when the game sent its next sounds, so a letter's sound was started and stopped at the same
  moment and never heard: only the last letter of a line clicked. The driver now runs on the
  sound output's own clock, as the PS2's sound processor does beside the game: its timer ticks
  between the game's frames, and the game's sound commands are played in order at the time they
  were sent. Every letter clicks, evenly spaced.
- Other sounds lost the same way play too: a system sound cut by another one in the next frame,
  and very short notes of the rooms' background sound sequences. Sound effects and sequences also
  start without waiting for the game's next frame.
- Test aids: `RECVX_TEST_COMPUTER="frame:n"` opens computer terminal n; `RECVX_TRACE_SND` also
  logs notes released before they sounded.

### Fixes

- Third person: the knife no longer slashes upwards at an enemy standing right in front; it slashes level at anything not below the player (aiming down at crawlers still works).
- **No more twitching when aiming or using the knife as another model** in third person: with
  "You appear as" another model, or Claire's or Chris's model on the other character with
  gender-matched animations, the upper body jerked back and forth when a weapon was raised,
  fired or the knife swung. The aim on the weapon as drawn is now only adjusted while the aim pose
  holds still, smoothly, and not for the knife. The character played without another model was
  never affected. Test aid: `RECVX_POSE_TRACE=file` logs the player's body as drawn, per frame.
- **Hands in the lobby's model preview**: the characters in the MULTIPLAYER lobby had no hands.
  They now wear the hand pieces the game gives an unarmed player, on the same wrist joints: each
  player character its own; the story cast's models, which have none, Chris's (men) or Claire's
  (women). Zombies and creatures keep their own hands. The look in game is unchanged.
- **Movies play smoothly and in sync with their sound**: the intro, the opening, the in-game
  movies and the ending. Reading and decoding a movie now happen on a thread of their own
  instead of in the game's frames, the sound is kept queued ahead so it no longer runs dry
  (gaps, and silence over the last frames of a movie), the picture follows the sound's clock
  instead of running on its own, and the short freeze at the start of every movie is gone. Each
  frame is uploaded once and converted to RGB on the GPU, with smoother colour edges; it is no
  longer passed through the emulated GS memory. Skipping, pausing and the fades over a movie
  work as before. Test aids: `RECVX_MOVIE=n` plays movie n in place of every movie,
  `RECVX_MOVIE_TRACE=file` logs the frame timing and the sound's offset.

## 0.3.0 (2026-10-16)

### Controls

- **Modern controls for the fixed cameras** (option, off by default): CONTROLS -> "Fixed camera
  controls: Classic (tank) / Modern (camera-relative)" in F1 and ENHANCED SETTINGS, ini
  `fixed_cam_controls`. As the alternate controls of the RE HD remasters, the left stick or the
  d-pad (arrow keys) walks where it points on screen, turning quickly and smoothly;
  the stick pushed far or the run button runs. Through a camera cut the held direction keeps
  the previous camera's meaning until it is clearly changed or let go, so the character does
  not walk back and forth at cut boundaries. Aiming, the quick-turn row,
  stairs, doors and items work as before; third person keeps its own controls; in co-op only
  your own player ([USER_GUIDE.md](docs/USER_GUIDE.md#modern-controls-fixed-cameras)).
- Test aids: `RECVX_VPAD` holds axes at a value (`leftx=0.5`), `RECVX_MODERN_TRACE=1` logs the
  modern controls; ctest `modern` covers the direction math and the cut hold.

### Performance

- **VU0 runs as native code**: much less CPU time per frame on the game thread. The vertex
  program `VU0_CALCPOINT` ran through the VU interpreter whenever a point light was on (most
  rooms); it is now native for every light setup and computes whole vertex runs four vertices at
  a time, using AVX when the CPU supports it and SSE2 otherwise (`RECVX_VU_AVX=0` forces SSE2).
  The macro-mode VU0 operations of the translated EE assembly (skinning, matrices) are inlined.
  Results are bit-exact with the interpreter: frames are byte-identical, `RECVX_VU_INTERP=1`
  still forces the interpreter, `RECVX_VU_CHECK=1` compares every call in game, and ctest
  `vu`/`vu_portable` compare the native code with the interpreter on random and edge-case inputs.

### Renderer

- Font: no more seams around the letters of messages, item names and "Now Loading..." at higher
  internal resolutions. The glyph quads' texture coordinates reached a quarter texel into the
  neighbouring glyph of the font texture; they now sit half a texel inside the glyph's cell on
  every edge. Fix by fmil95 (decompilation team).

### Audio

- **Missing system sounds**: the typewriter's keys and bell while a save is typed in, the item
  box lid, the computer terminals' beeps and typed text (and other sounds the game plays as
  system MIDI, such as some effects and enemy events) were silent in every save mode. The game
  takes a sound's bank from its number divided by 256; the PS2 compiler rounds that down (an
  arithmetic shift), C rounds toward zero, so these numbers (bit 31 set) asked for bank 3, which
  holds the room's ambience, instead of bank 2.
- The emulated sound driver has the game's 10 MIDI ports: the system sounds rotate over ports
  2, 8 and 9, and 8 and 9 were played on ports 0 and 1, where they stopped the room's background
  ambience (with door animations on, the door and stairs sounds did).
- `RECVX_TRACE_SND` also logs requests the driver drops (bank not loaded, no such program or
  block) and voices stolen when all 48 are busy.

### Fixes

- **Metal detector security boxes** (prison, 0/9): taking an item out of the box onto the lighter
  ("STANDARD") slot no longer makes it vanish. As on PS2, the item goes into the bag (or "There's
  no space to put the item in.") and the lighter, if it was there, goes into the box; storing
  the lighter with a full bag no longer overwrites the last item, and the lighter can be taken
  out onto any slot again. The item box exchange (`ItemBoxChange`) was an inexact decompilation; it now does
  what the PS2 code does in every case, which also keeps the equipped weapon right when two-slot
  weapons move in or out of any item box and fills a partial ammo stack when ammo is taken out
  onto an empty slot. Test aid: `RECVX_ITEM_TRACE=1` logs the bag and both security boxes.

## 0.2.0 (2026-10-14)

### Third person

- Free aim hits zombies lying or crawling on the ground (and a knocked-down Hunter) with every
  weapon. The game only lets those take damage from a shot aimed down, and free aim always told
  it "straight": it now passes the aim height of where the crosshair shot goes (down below knee
  height or 22.5 degrees, up above 22.5 degrees) to the enemies, while the weapon keeps its
  straight motions.
- "Game auto-aim": W / S aim up / down while the weapon is raised, as the original's up / down
  (before, only the arrow keys and the gamepad did).
- Test aid: `RECVX_TPS_TRACE=2` keeps aiming at the locked enemy, `RECVX_TPS_AIM_AT=f` at a point
  along its hit capsule; the trace logs the aim height.

### Settings

- The OPTION menu's PC SETTINGS is now **ENHANCED SETTINGS**, right of EXIT as before, reached
  with left/right from EXIT (like STEREO / MONO) instead of down; up from it goes to DEFAULT, down
  wraps to the top. Mouse hover and click areas match.
- Pages in a new order, the same in F1: DISPLAY (window, picture, renderer: the former ENHANCED
  page is part of it), GAMEPLAY (also saves, ink ribbons and all enemy options, Extra enemies
  included), CONTROLS (also Mouse in menus), THIRD PERSON, AUDIO, CO-OP (also Show ping), EXTRAS.
  Headings group the rows of a page in both menus.
- ENHANCED SETTINGS shows 9 rows and three help lines (the Input source help was cut off); the
  mouse wheel moves the selection as in the game's menus instead of changing the hovered value.
- ctest `settings`: every `recvx.ini` key needs a row in the settings table (so both menus show
  it) or an entry in `pc/tests/check_settings.cmake` saying where it is set.

### Saves

- Native save files, the new default (**Save system: Native files**, ini `save_system = 0`): every
  save is its own file in `save/saves` (`001_Claire_Prison-front.sav`, ...), numbered without
  limit. The typewriter's save screen and LOAD GAME look as before but list the files, scroll
  through any number of them and offer a new save at the end; the memory card screens (slot
  selection, "checking memory card", creating save data) are gone. The system data (options,
  extras, best times) is `save/saves/system.dat`.
- A save file is the memory card's save with a 64-byte header (game, time, play time, character,
  room); invalid files are skipped. Writes are atomic; a save is only replaced when you overwrite
  it.
- The first start with native files copies the memory card's saves, system data and co-op partner
  files into `save/saves` (the card is kept); F1 / ENHANCED SETTINGS "Import memory card saves" does it
  again later.
- **Memory card (original)** (`save_system = 1`) keeps the emulated card exactly as before
  (frame-identical).
- The co-op lobby's START list, the joiner's ITEMS choice and the host's saved partner inventories
  (`save/saves/NNN.coop-partners.bin`) follow the active save system.
- Test aid `RECVX_TEST_TYPEWRITER`; `pc/tools/layout_check.sh` takes extra ini lines (`INI`).
- **No ink ribbons needed** (F1 / ENHANCED SETTINGS GAMEPLAY, ini `no_ink_ribbon`, default off; in co-op
  the host's lobby rule INK RIBBONS): typewriters save without an ink ribbon and use none up.

### Co-op

Co-op protocol version 19 (both players need this build).

- **Shared loot is one pool** (lobby LOOT *Shared (one pickup)*): each item lies there once and
  whoever takes it has it; the spot is gone for both and the partner gets nothing, so the players
  split ammo, herbs and other supplies. Before, both got a copy of everything either took. Key items
  and the lighter still go to both (a used-up key still goes for both), as do items from events that
  are for both (key items, the lighter; weapons and ink ribbons with the rule below); supplies an
  event hands over stay with the triggering player. If both take the same item at about the same
  moment, exactly one keeps it (the host when both happened within one round trip); the other's copy
  goes back ("the partner took ... first"). Decomp hook `coop_item_late` in `bhItmCk`. Individual
  loot is unchanged.
- Game rule **Important items for both** (shared loot only; lobby IMPORTANT ITEMS *For both* /
  *Shared*, F3, ENHANCED SETTINGS CO-OP; ini `coop_important`, default on): weapons and ink ribbons
  lie there for each player; one player's pickup leaves the partner's copy where it lies. Off, they
  are pool items like the rest. Each player's own pickups go with its items (saves, the host's saved
  partner inventories).
- Game rule **Synchronized cutscene skip** (lobby CUTSCENE SKIP, F3, ENHANCED SETTINGS CO-OP; ini
  `coop_skip_sync`, default on): a cutscene both players watch is skipped once both pressed skip;
  "Waiting for <partner> to skip..." / "<partner> wants to skip - press <button> to skip" at the
  top of the screen. Both scenes end together and leave both players where the scene puts them.
- The lobby's GAME RULES also set INK RIBBONS for the session.
- Protocol 19: game rules `OPT_IMPORTANT`, `OPT_NO_RIBBON`, `OPT_SKIP_SYNC`; pose flags `PF_CINE`,
  `PF_SKIP`; event `EV_SKIP` (host only); `EV_PICKUP` carries an item only for items both get, and
  `undo` (host only) for a spot both took; unknown pose flag and rule bits are refused. ctest `coop`
  covers them.
- Test aids `RECVX_COOP_GOTO` (our player alone to a spot) and `RECVX_COOP_PAD` (game pad presses at
  a host frame).
- Fixed: a crawling zombie's leg grab could repeat without end (the grabbed player in the attacked
  animation with the damage sound over and over). The grab ends with the player stomping the
  crawler dead; the grab failsafe let the player go mid-stomp and put the dead zombie back to
  standing, so it crawled on with no health and grabbed again. Now a dead zombie stays dead, the
  failsafe waits for the stomp, and a loop guard makes an enemy that attacks the same player three
  times within 90 frames leave it alone for 60 (logged).
- Enemy targets: the player right in front of an enemy (within 1.5 m, in its front quarter) is its
  target; only a grab or bite keeps a victim (one within 3 m), no longer an enemy's own hit or death
  states; a player on another floor counts as twice as far; a zombie's parts go for its victim.
- The shooter's aim height counts for hits in co-op: lying zombies and other enemies that ignore a
  shot aimed straight judged the shot by the aim of the player they chase (the partner's stand-in
  has none). The partner's hits carry their aim height (`EV_ENE_HIT`, protocol 19).
- `RECVX_ENEMY_TRACE` shows type, busy state and attacks on the partner; test aid
  `RECVX_COOP_CRAWL` (the host's zombies crawl).

## 0.1.0 (2026-10-13)

First release. Native Linux and Windows (32-bit, i686) port of Resident Evil Code: Veronica X
(PS2, SLUS-20184), built on the recvx-decomp decompilation. No game data is included; you need your
own disc image.
Co-op protocol version 18, rendezvous protocol version 3.

### Platform and port

- Game code, including the original PS2 Ninja/Shinobi layer, compiled from the decomp; DMA, VIF,
  GIF, GS, CD/DVD, pad, memory card and SIF RPC emulated underneath.
- VU0/VU1 microcode compiled statically to C (interpreter kept as a reference, `RECVX_VU_CHECK`
  compares both); hand-written fast paths for vertex lighting and strip clipping.
- Audio: ADX streams, TSNDDRV sound effects on modelled SPU2 voices with reverb, MIDI sequencer.
- FMV playback with its own MPEG-2 decoder.
- Windows build cross-compiled with mingw-w64 GCC (static, large-address-aware); runs the same
  saves and co-op sessions as Linux.
- First start without a disc image opens a file selector and remembers the choice (`iso` in
  `recvx.ini`).
- Atomic save writes, UTF-8 paths on Windows, glibc compatibility shim so the Linux binary runs on
  glibc 2.34 and newer.

### Renderer

- OpenGL renderer with runtime internal resolution, fullscreen (F11) and an FPS counter.
- Widescreen and ultrawide (hor+, up to 4:1): 3D fills the margins, room parts hidden for 4:3
  cameras are shown when they lie outside the 4:3 view, 2D and menus stay 4:3 centred; fades,
  pause darkening and the screen saver cover the whole view.
- Native 3D path for chunk models and ordering-table primitives, perspective-correct colour and
  fog; native TIM2 textures with real CLUT uploads and memory caps; render-to-texture effects
  captured on the GPU instead of read back.
- Enhanced mode: in-between frames at display rate (game logic stays at 30 Hz), blended from the
  executed frame's native geometry and resources; cheaper replays into a second render-target set.
- Frame rate limit (off/30/60/90/120/144), present pacing on the display refresh, optional
  "Frame blend (motion trail)", optional off-screen model culling.
- Larger PC room limits (bigger room arena, vertex and primitive buffers) for custom maps.

### Co-op

- Online two-player co-op (F3 or MULTIPLAYER on the title menu): host-authoritative enemies, flag
  and script-event sync, cutscene items and levers shared, independent rooms with optional follow.
- Lobby in the game's style, in steps (connection, host/join, game rules), with 3D previews,
  player names, name tags, ping display and a "Remove stuck player" action for the host.
- Shared or individual loot, own or shared item box, per-player inventories kept by the host,
  event item gifts for a partner who missed the event.
- Trade screen with both inventories, item requests and atomic swaps; optional enemy freeze while
  trading.
- Partner shown as any chosen model, with its weapon sounds, voice, lighter and effects; enemy
  effects (acid, vomit, grenades) shown and harmful on both sides.
- Enemies choose the nearer free player; zombie grabs work on both players.
- Direct play via `ip:port#CODE` with UPnP/NAT-PMP port mapping, or via a rendezvous server with
  hole punching and relay fallback; end-to-end encryption (X25519 + XChaCha20-Poly1305) in every
  server session.
- Rejoin with a per-install player ID; lost sessions time out; all datagrams validated before use.
- Local co-op on one PC: per-window input source (keyboard/mouse or a chosen gamepad).

### Server

- `recvx-server` rendezvous and relay server (C) with server-assigned room codes, Argon2id room
  passwords, authenticated relay frames, per-address and global rate limits and relay budgets.
- Config file with SIGHUP reload, Prometheus-style status metrics, anonymised logging, systemd
  unit, Dockerfile and a `recvx-client` CLI.

### Third person

- Over-the-shoulder camera (F5, `--third-person`) with WASD/mouse controls and free aim; spine
  and chest bend to the aim pitch.
- Two mouse/keyboard camera styles: behind the character, or a free orbit camera with
  camera-relative movement.
- Light and fog changes between camera cuts fade smoothly; effects, sound panning and culling use
  the third-person view.

### Actors (model swaps, voices)

- "You appear as": play as Claire, Chris, Steve, Wesker, costumes, or one of dozens of enemy and cast
  models retargeted onto the player skeleton; the choice applies to cutscene doubles too.
- Gender-matched animations: male models play the male equivalent of each motion.
- Replacement models hold weapons and the lighter in their own hands; shots leave from the muzzle.
- Optional talking faces for swapped models in cutscenes.
- Player voices follow the chosen appearance (Claire, Chris, Steve or Wesker voice banks).

### Enemies

- F2 enemy spawner for any type, loaded from its source room on demand with its sounds and
  effect textures.
- Enemy options: extra enemies, rearranged placements, randomizer with seed; synced to the co-op
  partner.
- Higher spawner limits (texture heap, spawn pool, helper slots).

### Maps and mods

- Mods folder (`RECVX_MODS`, default `./mods`) for replacement and new rooms.
- RDX room tooling: `rdx.py` (read and patch rooms), `rdxbuild.py` (build rooms from OBJ/PNG
  geometry, props, items, scripts, doors and sound banks), a "pc" budget for large rooms.
- Slope walking in rooms flagged for it, for terrain-based custom maps.
- Asset catalogue and kitbash tools to build new rooms from the player's own disc: the Quarantine
  Annex area and a village area as examples.
- RE4 village conversion tooling for a player's own RE4 export (materials mapped to CVX textures).
- Rooms built from game data must not be shared.

### Input and settings

- F1 settings overlay and a native PC SETTINGS page in the game's OPTION menu, both driven by one
  settings table and saved to `recvx.ini`.
- Rebinding for keyboard/mouse and gamepad (two slots per action).
- Mouse in the inventory and menus.
- Quick knife (hold Space / right trigger) and a trade request key.
- Skip intro and skip door animations (on by default on first start).
- F2 cheat menu: heal, items, infinite ammo, teleport, noclip with fly mode, enemy spawn.

### Tooling and tests

- ctest suites for the co-op and rendezvous protocols (including fuzzing), networking, VU,
  bindings, appearances, file I/O and movies.
- Determinism checks: `layout_check.sh` compares frames across binary layouts; replay-vs-executed
  frame checker; native vs emulated renderer comparison.
- Profiler (`--profile`), GPU timing, extensive `RECVX_*` debug and test switches.
- `decomp_warnings.py` regenerates the lists of missing returns and uninitialised locals.

### Fixes

- Decomp code made deterministic: a retail out-of-bounds motion key read and a stack-garbage near
  plane point replaced by the intended values.
- Ordering-table depth sorting fixed (transparency sorting was missing).
- MIDI volume fade-up no longer ends silent (follows the retail code's intent).
- C functions without a return checked against the retail ELF; several hand-written asm routines
  translated from the retail code.
- Idle screen dimming (the game's screen saver) is lifted by PC input, as a pad press does on PS2.
- Crash fixes: game over with a swapped model, lobby open/close, teleport during a grab, texture
  queue recursion, port mapping use-after-free.
- Graveyard effects drawn with the right textures after a door patch (room texture-list check).

Developed with AI coding agents (Claude; OpenAI Codex for parts of the map tooling), see README.
