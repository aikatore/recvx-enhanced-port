/* The new menu's tree (menu_layout.h, docs/MENUS.md): every row of the settings table in its one home, in screen order.
   ctest "menu_layout" reads this file: keep each entry's kind and label together as ML_SET followed by the quoted label (also
   ML_SHORTCUT, ML_RETIRED) so the check finds them. Labels are the table's (settings.c); the words shown are .label /
   .names / .help when given. New words are English and go through tr() where they are drawn. */
#include "menu_layout.h"
#include "settings.h"
#include "bindings.h"
#include <string.h>

int g_new_menu = 1;
int g_legacy_menu;
int g_menu_style = 1;
int g_menu_cli = -1;
void menu_choice_apply(void) { g_new_menu = g_menu_cli >= 0 ? g_menu_cli : !(g_legacy_menu || g_menu_style == 0); }
int menu_redesigned(void) { return g_dev_tools && g_new_menu && g_menu_style == 2 && !g_legacy_menu; }

const MenuCat menu_cats[MC_COUNT] = {
    [MC_GAMEPLAY] = { MS_SETTINGS, "GAMEPLAY", -1 },
    [MC_CAMERA] = { MS_SETTINGS, "CAMERA AND HUD", -1 },
    [MC_CONTROLS] = { MS_SETTINGS, "CONTROLS", -1 },
    [MC_DISPLAY] = { MS_SETTINGS, "DISPLAY", -1 },
    [MC_GRAPHICS] = { MS_SETTINGS, "GRAPHICS", -1 },
    [MC_AUDIO] = { MS_SETTINGS, "AUDIO", -1 },
    [MC_LANGUAGE] = { MS_SETTINGS, "LANGUAGE", -1 },
    [MC_COOP] = { MS_SETTINGS, "CO-OP", -1 },
    [MC_CAMERA_ADV] = { MS_SETTINGS, "ADVANCED", MC_CAMERA },
    [MC_PAD_ADV] = { MS_SETTINGS, "ADVANCED", MC_CONTROLS },
    [MC_GRAPHICS_ADV] = { MS_SETTINGS, "ADVANCED", MC_GRAPHICS },
    [MC_BIND_KB] = { MS_SETTINGS, "KEYBOARD AND MOUSE", MC_CONTROLS },
    [MC_BIND_PAD] = { MS_SETTINGS, "CONTROLLER", MC_CONTROLS },
    [MC_OVERVIEW] = { MS_SETUP, "OVERVIEW", -1 },
    [MC_RULES] = { MS_SETUP, "RULES", -1 },
    [MC_ENEMIES] = { MS_SETUP, "ENEMIES", -1 },
    [MC_CHARACTERS] = { MS_SETUP, "CHARACTERS", -1 },
    [MC_BIORAND] = { MS_SETUP, "BIORAND", -1 },
    [MC_COOP_RULES] = { MS_SETUP, "CO-OP RULES", -1 },
    [MC_CHAR_ADV] = { MS_SETUP, "ADVANCED", MC_CHARACTERS },
    [MC_BIO_POOL] = { MS_SETUP, "ITEM POOL AND WEAPONS", MC_BIORAND },
    [MC_CHEATS] = { MS_CHEATS, "CHEATS AND TOOLS", -1 },
    [MC_IMPORT_CHARS] = { MS_IMPORT, "CHARACTERS", -1 },
    [MC_IMPORT_LANGS] = { MS_IMPORT, "LANGUAGES", -1 },
    [MC_IMPORT_SAVES] = { MS_IMPORT, "MEMORY CARD SAVES", -1 },
};

#define N(...) (const char* const[]){ __VA_ARGS__ }
const MenuRow menu_rows[] = {
    /* ---------------------------------------------------------------- SETTINGS > GAMEPLAY */
    { MC_GAMEPLAY, "GENERAL", ML_SET, "Skip Door Animations" },
    { MC_GAMEPLAY, NULL, ML_SET, "Fast Loading" },
    { MC_GAMEPLAY, NULL, ML_SET, "Quick Knife" },
    { MC_GAMEPLAY, NULL, ML_SET, "Auto-climb Stairs" },
    { MC_GAMEPLAY, NULL, ML_SHORTCUT, "Character Model", MC_CHARACTERS, "Your Character", .help = "Your look and who looks like whom in cutscenes. Shortcut: GAME SETUP > CHARACTERS." },
    { MC_GAMEPLAY, "COMBAT", ML_SET, "Aim Assist" },
    { MC_GAMEPLAY, NULL, ML_SET, "Move While Aiming", .label = "Move While Aiming (Third Person)" },
    { MC_GAMEPLAY, NULL, ML_SET, "Move While Aiming (Fixed Cameras)" },
    { MC_GAMEPLAY, NULL, ML_SET, "Aim Scoped Weapons Normally" },
    { MC_GAMEPLAY, NULL, ML_SET, "Precise Shots" },
    { MC_GAMEPLAY, NULL, ML_SET, "Locational Hit Reactions" },
    { MC_GAMEPLAY, NULL, ML_SET, "Zombie Decapitation" },
    { MC_GAMEPLAY, NULL, ML_SET, "Stack Rifle Ammo" },
    { MC_GAMEPLAY, NULL, ML_SHORTCUT, "Headshots & Piercing", MC_RULES, "Headshots and Piercing",
      .help = "A rule of this game: set in GAME SETUP > RULES. Shortcut: GAME SETUP > RULES." },
    { MC_GAMEPLAY, "SAVES", ML_SET, "Save System" },
    { MC_GAMEPLAY, NULL, ML_SHORTCUT, "Import Memory Card Saves", MC_IMPORT_SAVES,
      .help = "Copy memory card saves to separate files. Shortcut: EXTRA > IMPORT > MEMORY CARD SAVES." },
    { MC_GAMEPLAY, "STARTUP AND UNLOCKS", ML_SET, "Skip Intro" },
    { MC_GAMEPLAY, NULL, ML_SET, "Check For Updates", .label = "Check for Game Updates" },
    { MC_GAMEPLAY, NULL, ML_SET, "Battle Game Unlocked", .label = "Unlock Battle Game" },
    { MC_GAMEPLAY, NULL, ML_SET, "Menu Style" },
    { MC_GAMEPLAY, NULL, ML_RETIRED, "Difficulty Select" },   /* D4: Difficulty is a GAME SETUP row (P2); the old menu keeps it */

    /* ---------------------------------------------------------------- SETTINGS > CAMERA AND HUD */
    { MC_CAMERA, "THIRD-PERSON CAMERA", ML_SET, "Over-the-shoulder Camera", .label = "Third-person Camera",
      .help = "Play with a controllable camera and free aim. Events, doors and menus keep their original cameras. Also F5 and the pause menu." },
    { MC_CAMERA, NULL, ML_SET, "Field Of View", .label = "Field of View" },
    { MC_CAMERA, NULL, ML_SET, "Aiming Field of View",
      .help = "The view while aiming, at 4:3. Smaller zooms in." },
    { MC_CAMERA, NULL, ML_SET, "Left Shoulder", .label = "Shoulder", .names = N("Right", "Left"),
      .help = "Which shoulder the camera looks over." },
    { MC_CAMERA, NULL, ML_SET, "Lighting and Fog", .label = "Camera Lighting and Fog" },
    { MC_CAMERA, NULL, ML_PAGE, NULL, MC_CAMERA_ADV, "Advanced", .help = "Camera distances, height and shoulder offset." },
    { MC_CAMERA_ADV, NULL, ML_SET, "Camera Distance",
      .help = "How far behind your character the camera follows." },
    { MC_CAMERA_ADV, NULL, ML_SET, "Aiming Camera Distance",
      .help = "How far behind your character the camera is while aiming." },
    { MC_CAMERA_ADV, NULL, ML_SET, "Camera Height",
      .help = "How high above your character's feet the camera looks from." },
    { MC_CAMERA_ADV, NULL, ML_SET, "Shoulder Offset",
      .help = "How far to the side the camera sits, over the shoulder." },
    { MC_CAMERA, "HUD", ML_SET, "HUD" },
    { MC_CAMERA, NULL, ML_SET, "HUD Position" },
    { MC_CAMERA, NULL, ML_SET, "HUD Style" },
    { MC_CAMERA, NULL, ML_SET, "Crosshair",
      .help = "A crosshair at the screen centre while aiming in third person." },

    /* ---------------------------------------------------------------- SETTINGS > CONTROLS */
    { MC_CONTROLS, "INPUT", ML_SET, "Fixed Camera Controls", .label = "Fixed-camera Movement" },
    { MC_CONTROLS, NULL, ML_SET, "Input Devices" },
    { MC_CONTROLS, NULL, ML_SET, "Gamepad", .label = "Active Controller" },
    { MC_CONTROLS, NULL, ML_SCREEN, NULL, 1, "Control Type",
      .help = "The game's button layouts, Type A to D. Opens the original KEY CONFIG screen; Confirm and Cancel follow the type." },
    { MC_CONTROLS, NULL, ML_SET, "Weapon Shortcuts" },
    { MC_CONTROLS, NULL, ML_SET, "Mouse In Menus", .label = "Mouse in Menus" },
    { MC_CONTROLS, NULL, ML_SET, "Button Icons" },
    { MC_CONTROLS, NULL, ML_SET, "Vibration Strength", .label = "Vibration", .flags = MLF_VIBRATION,
      .help = "The controller's vibration strength. Off turns vibration off." },
    { MC_CONTROLS, "MOUSE", ML_SET, "Mouse Camera Style" },
    { MC_CONTROLS, NULL, ML_SET, "Horizontal Mouse Sensitivity",
      .help = "How fast the mouse turns the camera left and right." },
    { MC_CONTROLS, NULL, ML_SET, "Vertical Mouse Sensitivity",
      .help = "How fast the mouse moves the camera up and down." },
    { MC_CONTROLS, NULL, ML_SET, "Aiming Sensitivity", .label = "Mouse Aiming Sensitivity" },
    { MC_CONTROLS, NULL, ML_SET, "Invert Mouse Y",
      .help = "Moving the mouse forward looks down." },
    { MC_CONTROLS, "CONTROLLER", ML_SET, "Gamepad Controls", .label = "Third-person Controller Layout" },
    { MC_CONTROLS, NULL, ML_SET, "Horizontal Camera Speed" },
    { MC_CONTROLS, NULL, ML_SET, "Vertical Camera Speed",
      .help = "How fast the right stick moves the camera up and down." },
    { MC_CONTROLS, NULL, ML_SET, "Stick Aiming Speed" },
    { MC_CONTROLS, NULL, ML_SET, "Invert Stick Y",
      .help = "Pushing the right stick up looks down." },
    { MC_CONTROLS, NULL, ML_PAGE, NULL, MC_PAD_ADV, "Advanced", .help = "The sticks' dead zones." },
    { MC_PAD_ADV, NULL, ML_SET, "Movement Dead Zone",
      .help = "How far the left stick moves before your character does. Raise it if your character drifts." },
    { MC_PAD_ADV, NULL, ML_SET, "Camera Dead Zone",
      .help = "How far the right stick moves before the camera does. Raise it if the camera drifts." },
    { MC_CONTROLS, "BUTTONS", ML_PAGE, NULL, MC_BIND_KB, "Customize Keyboard and Mouse",
      .help = "Keys and mouse buttons for game actions, third person and hotkeys." },
    { MC_CONTROLS, NULL, ML_PAGE, NULL, MC_BIND_PAD, "Customize Controller", .help = "Controller buttons for game actions and third person." },

    /* ---------------------------------------------------------------- SETTINGS > DISPLAY */
    { MC_DISPLAY, "WINDOW", ML_SET, "Display Mode" },
    { MC_DISPLAY, NULL, ML_SET, "Fullscreen Resolution" },
    { MC_DISPLAY, NULL, ML_SET, "Window Size",
      .help = "The window's size in windowed mode. Dragging the window's edge gives any other size." },
    { MC_DISPLAY, NULL, ML_SET, "VSync",
      .help = "Show each frame with the display's refresh: no tearing, a little more input delay." },
    { MC_DISPLAY, NULL, ML_SET, "Frame Rate Limit" },
    { MC_DISPLAY, "PICTURE", ML_SET, "Aspect Ratio" },
    { MC_DISPLAY, NULL, ML_SET, "Brightness" },
    { MC_DISPLAY, NULL, ML_SET, "Contrast" },
    { MC_DISPLAY, NULL, ML_SET, "Gamma" },
    { MC_DISPLAY, NULL, ML_SET, "Reset Picture Calibration", .label = "Reset Brightness, Contrast and Gamma" },
    { MC_DISPLAY, NULL, ML_SCREEN, NULL, 2, "Screen Position", .help = "Move the picture on the screen (the original SCREEN SETUP)." },
    { MC_DISPLAY, NULL, ML_SCREEN, NULL, 3, "Monitor Test", .help = "The original MONITOR ADJUSTMENT: colour bars to set up your display." },
    { MC_DISPLAY, "WIDESCREEN", ML_SET, "Cutscene Bars" },
    { MC_DISPLAY, NULL, ML_SET, "Camera Vignette" },
    { MC_DISPLAY, NULL, ML_SET, "Screen Effects" },
    { MC_DISPLAY, NULL, ML_SET, "Movie Zoom" },
    { MC_DISPLAY, "INFO", ML_SET, "Show Frame Rate",
      .help = "The frame rate in the top right corner (in co-op also the ping)." },
    { MC_DISPLAY, NULL, ML_SET, "Performance Graph" },

    /* ---------------------------------------------------------------- SETTINGS > GRAPHICS */
    { MC_GRAPHICS, NULL, ML_PRESET, NULL, 0, "Preset",
      .help = "Original Look: 4:3, the original resolution and motion, no smoothing. Enhanced: the port's defaults. Changing a row below makes it Custom." },
    { MC_GRAPHICS, "QUALITY", ML_SET, "Internal Resolution" },
    { MC_GRAPHICS, NULL, ML_SET, "Anti-Aliasing" },
    { MC_GRAPHICS, NULL, ML_SET, "Dithering", .label = "Banding Reduction", .names = N("Off", "Dithering", "Deband") },
    { MC_GRAPHICS, NULL, ML_SET, "Fog Intensity" },
    { MC_GRAPHICS, "MOTION", ML_SET, "Smoother Motion", .label = "Smooth Motion" },
    { MC_GRAPHICS, NULL, ML_SET, "Smooth Menus", .label = "Smooth Menu Motion" },
    { MC_GRAPHICS, NULL, ML_SET, "Motion Trails" },
    { MC_GRAPHICS, "TEXTURE PACK", ML_SET, "HD Textures" },
    { MC_GRAPHICS, NULL, ML_SET, "HD Font" },
    { MC_GRAPHICS, NULL, ML_SET, "HD Menus" },
    { MC_GRAPHICS, NULL, ML_PAGE, NULL, MC_GRAPHICS_ADV, "Advanced", .help = "Renderer options." },
    { MC_GRAPHICS_ADV, NULL, ML_SET, "Hardware 3D Rendering" },
    { MC_GRAPHICS_ADV, NULL, ML_SET, "Texture Caching" },
    { MC_GRAPHICS_ADV, NULL, ML_SET, "Hide Edge Models", .label = "Hide Models at Screen Edges" },

    /* ---------------------------------------------------------------- SETTINGS > AUDIO */
    { MC_AUDIO, NULL, ML_SET, "Music And Voice", .label = "Music and Voice Volume",
      .help = "The volume of the music, the voices and the movies." },
    { MC_AUDIO, NULL, ML_SET, "Sound Effects", .label = "Sound Effects Volume",
      .help = "The volume of steps, shots, doors and every other sound." },
    { MC_AUDIO, NULL, ML_OUTPUT, NULL, 0, "Output", .help = "Stereo or mono sound (the original AUDIO SETUP)." },

    /* ---------------------------------------------------------------- SETTINGS > LANGUAGE (always shown) */
    { MC_LANGUAGE, NULL, ML_SET, "Language", .flags = MLF_ALWAYS },
    { MC_LANGUAGE, NULL, ML_SET, "Subtitles" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Pictures" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Title Call" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Overlay Windows" },
    { MC_LANGUAGE, NULL, ML_IMPORT, NULL, MC_IMPORT_LANGS, "Import Languages",
      .help = "Add languages from your own disc of another edition of the game. Shortcut: EXTRA > IMPORT > LANGUAGES." },

    /* ---------------------------------------------------------------- SETTINGS > CO-OP */
    { MC_COOP, "PERSONAL", ML_TEXT, NULL, 0, "Player Name", .help = "Your name, as your partner sees it. Confirm: type it." },
    { MC_COOP, NULL, ML_SET, "Partner Name Tag" },
    { MC_COOP, NULL, ML_SET, "Show Ping" },
    { MC_COOP, NULL, ML_SET, "Hide IP Addresses" },
    { MC_COOP, NULL, ML_SET, "Follow Host" },
    { MC_COOP, "HOSTING", ML_SET, "Remember Partner Items",
      .help = "Only when you host: keep what your partner carried, and give it back when the same partner joins again." },
    { MC_COOP, NULL, ML_SET, "Host Port" },
    { MC_COOP, NULL, ML_SET, "Automatic Port Forwarding" },
    { MC_COOP, "CONNECTION", ML_SET, "Connection", .names = N("Automatic", "Direct (IP Address)", "Always Relay") },
    { MC_COOP, NULL, ML_TEXT, NULL, 1, "Co-op Server", .help = "The server for room codes and the relay (name or name:port; empty: none). Confirm: type it." },

    /* ---------------------------------------------------------------- GAME SETUP > OVERVIEW (NEW GAME opens it on START GAME) */
    { MC_OVERVIEW, NULL, ML_VAR, NULL, SV_GAME_TYPE, "Game Type", N("Standard", "BioRand"),
      .help = "Standard: the game with the modifiers below. BioRand: doors and items randomized by BioRand (the BIORAND category); START GAME generates it if needed." },
    { MC_OVERVIEW, NULL, ML_VAR, NULL, SV_DIFFICULTY, "Difficulty", N("Normal", "Easy", "Very Easy"),
      .help = "The game's difficulty. Easy and Very Easy give more ammunition and take less damage." },
    { MC_OVERVIEW, NULL, ML_VAR, NULL, SV_START_FROM, "Start From",
      .help = "The co-op lobby's: a new game, or one of your saves (you both go on from there; its BioRand state counts)." },
    { MC_OVERVIEW, NULL, ML_VAR, NULL, SV_PRESET, "Preset", N("Original", "Remix", "Chaos", "Custom"),
      .help = "Original: no modifiers. Remix: rearranged and extra enemies, more item boxes and herbs. Chaos: wild and event enemies, "
              "any model in cutscenes, voices match the used character. Custom: your own." },
    { MC_OVERVIEW, NULL, ML_SHORTCUT, "Character Model", MC_CHARACTERS, "Your Character",
      .help = "Your look, and who looks like whom in cutscenes. Shortcut: GAME SETUP > CHARACTERS." },
    { MC_OVERVIEW, "ACTIVE MODIFIERS", ML_INFO, NULL, SI_MODIFIERS },
    { MC_OVERVIEW, NULL, ML_ACTION, NULL, SA_START, "START GAME",
      .help = "Start a new game with these rules. Your choices are kept for the next game." },

    /* ---------------------------------------------------------------- GAME SETUP > RULES */
    { MC_RULES, NULL, ML_SET, "Free Typewriter Saves", .label = "Save Without Ink Ribbons" },
    { MC_RULES, NULL, ML_SET, "Extra Item Boxes and Herbs" },
    { MC_RULES, NULL, ML_SET, "Headshots & Piercing", .label = "Headshots and Piercing" },
    { MC_RULES, NULL, ML_SET, "Break Free Pushes Enemies" },

    /* ---------------------------------------------------------------- GAME SETUP > ENEMIES */
    { MC_ENEMIES, NULL, ML_SET, "Extra Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Rearranged Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Randomizer" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Selection", .label = "Enemy Pool" },
    { MC_ENEMIES, NULL, ML_SET, "Types per Room", .label = "Max Enemy Types per Room" },
    { MC_ENEMIES, NULL, ML_SET, "Randomize Event Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Seed", .label = "Enemy Randomizer Seed",
      .help = "The same seed gives the same enemies, cutscene looks and random look. Confirm: type a number. In co-op, the host's seed counts." },
    { MC_ENEMIES, NULL, ML_ACTION, NULL, SA_ENEMY_SEED, "RANDOM SEED", .help = "Pick a new Enemy Randomizer Seed." },

    /* ---------------------------------------------------------------- GAME SETUP > CHARACTERS (proposal §8: who looks like whom) */
    { MC_CHARACTERS, "YOU", ML_VAR, NULL, SV_LOOK, "You Look Like", N("Original", "Chosen Model", "Random"),
      .help = "Original: Claire or Chris. Chosen Model: any look, e.g. Wesker. Random: picked at a new game from the Enemy Randomizer "
              "Seed (BioRand: its seed) and kept in its saves; choosing a look in game drops it." },
    { MC_CHARACTERS, NULL, ML_MERGED, "Character Models" },   /* (Claire and Chris below) */
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_LOOK_MODE, "Claire and Chris", N("Same Look", "Separate Looks"),
      .help = "Same Look: one look for whoever you play; the other stays original in cutscenes. Separate Looks: each has their own, "
              "also in the other's cutscenes. Random gives Steve's section one too." },
    { MC_CHARACTERS, NULL, ML_SET, "Character Model", .label = "Your Look", .help = "Your look for whoever you play. Your co-op partner sees it too." },
    { MC_CHARACTERS, NULL, ML_SET, "Claire's Model", .label = "Claire's Look", .help = "Claire's look, also in the cutscenes of Chris's part." },
    { MC_CHARACTERS, NULL, ML_SET, "Chris's Model", .label = "Chris's Look", .help = "Chris's look, also in the cutscenes of Claire's part." },
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_RANDOM_FROM, "Random From", N("Story Characters", "Any Model", "Custom"),
      .help = "Story Characters: the cast's talking models. Any Model: also creatures, zombies and player files. Custom: the looks you check." },
    { MC_CHARACTERS, NULL, ML_SET, "Custom Cutscene Model", .label = "In Cutscenes", .names = N("Original Character", "Your Look"),
      .help = "Your Look: cutscenes show your look. Original Character: cutscenes show Claire or Chris as in the original game." },
    { MC_CHARACTERS, NULL, ML_PAGE, NULL, MC_CHAR_ADV, "Advanced", .help = "Match Model Animations and Animated Faces." },
    { MC_CHAR_ADV, NULL, ML_SET, "Match Model Animations" },
    { MC_CHAR_ADV, NULL, ML_SET, "Animated Faces (Experimental)" },
    { MC_CHARACTERS, NULL, ML_IMPORT, NULL, MC_IMPORT_CHARS, "Import Characters",
      .help = "Add characters from Resident Evil Outbreak File 2 and Resident Evil: The Umbrella Chronicles as looks. Shortcut: EXTRA > IMPORT > CHARACTERS." },
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_GAME_UC, "Umbrella Chronicles", N("Off", "On"),
      .help = "On: its imported characters are in the look lists and random looks, also as story characters in cutscenes, with their voice lines. In co-op, both players need the same games on." },
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_GAME_OB, "Outbreak File 2", N("Off", "On"),
      .help = "On: its imported characters are in the look lists and random looks, also in cutscenes with Any Model or Custom (no voice lines). In co-op, both players need the same games on." },
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_GAME_DC, "Code: Veronica (Dreamcast)", N("Off", "On"),
      .help = "On: its Steve models are in the look lists and random looks. The Dreamcast Steve setting (IMPORT) works either way. In co-op, both players need the same games on." },
    { MC_CHARACTERS, "STORY CHARACTERS IN CUTSCENES", ML_VAR, NULL, SV_STORY, "Story Characters", N("Original", "Swapped", "Any Model", "Custom"),
      .help = "Steve, Wesker, Alfred, Alexia and Rodrigo in cutscenes. Swapped: each other's looks, e.g. Alfred as Steve. Any Model: "
              "also creatures, e.g. Rodrigo as a Hunter. Claire and Chris follow YOU." },
    { MC_CHARACTERS, NULL, ML_MERGED, "Randomize Cutscene Cast" },
    { MC_CHARACTERS, NULL, ML_MERGED, "Model Selection" },
    { MC_CHARACTERS, NULL, ML_SET, "Model Assignment", .label = "Keep Their Look", .names = N("Whole Game", "New Each Scene"),
      .help = "Whole Game: Steve keeps one look all game. New Each Scene: a new look in every cutscene." },
    { MC_CHARACTERS, NULL, ML_VAR, NULL, SV_SHOW_PICKS, "Show This Game's Looks", N("Off", "On"),
      .help = "In game only, a spoiler: the preview names who each story character looks like in this game (with Keep Their Look Whole Game)." },
    { MC_CHARACTERS, "MONSTERS IN CUTSCENES", ML_VAR, NULL, SV_MONSTERS, "Cutscene Monsters", N("Original", "Any Monster", "Like Story Characters", "Custom"),
      .help = "Monsters only cutscenes show, e.g. the Tyrant on the plane. Like Story Characters takes their choice. Monsters you "
              "fight are ENEMIES'." },
    { MC_CHARACTERS, NULL, ML_MERGED, "Enemy Models" },
    { MC_CHARACTERS, "VOICES", ML_SET, "Randomize Voice Lines", .label = "Voices Match Used Character",
      .help = "Anyone who looks like another character speaks with their lines, e.g. Steve as Wesker says Wesker's; as a monster, he "
              "makes its sounds. Works with every look on this page." },
    { MC_CHARACTERS, NULL, ML_SET, "Talking Mouths", .label = "Extra Mouth Movement", .flags = MLF_ALWAYS,
      .help = "Moves mouths for new lines where the original had none. Yours alone: each co-op screen has its own." },

    /* ---------------------------------------------------------------- GAME SETUP > BIORAND (Game Type BioRand; biorand_opts.c's rows by key) */
    { MC_BIORAND, "SEED", ML_BIO, "Seed" },
    { MC_BIORAND, NULL, ML_ACTION, NULL, SA_BIO_RANDOM, "RANDOM SEED", .help = "Pick a new seed. START GAME generates the game for it (about 20 s) if needed." },
    { MC_BIORAND, NULL, ML_ACTION, NULL, SA_BIO_COPY, "COPY SEED STRING", .help = "Copy the seed string to share the seed and BioRand options." },
    { MC_BIORAND, NULL, ML_ACTION, NULL, SA_BIO_PASTE, "PASTE SEED STRING", .help = "Paste a seed string to use its seed and BioRand options." },
    { MC_BIORAND, "DOORS", ML_BIO, "RandomDoors", .label = "Randomize Doors" },
    { MC_BIORAND, NULL, ML_BIO, "AreaCount", .label = "Number of Segments" },
    { MC_BIORAND, NULL, ML_BIO, "AreaSize", .label = "Segment Size" },
    { MC_BIORAND, NULL, ML_BIO, "PrioritiseCutscenes", .label = "Prioritise Rooms with Cutscenes" },
    { MC_BIORAND, "ITEMS", ML_BIO, "RandomItems", .label = "Randomize Items" },
    { MC_BIORAND, NULL, ML_BIO, "AlternativeRoutes", .label = "Allow Alternative Routes" },
    { MC_BIORAND, NULL, ML_BIO, "HiddenKeyItems", .label = "Hidden Key Items" },
    { MC_BIORAND, NULL, ML_SET, "Show Randomized Item Models" },
    { MC_BIORAND, NULL, ML_PAGE, NULL, MC_BIO_POOL, "Item Pool and Weapons", .help = "Placement, the item pool's shares, the start inventory and the weapons BioRand may place." },
    { MC_BIO_POOL, NULL, ML_BIO, "Segmented", .label = "Segmented Placement" },
    { MC_BIO_POOL, NULL, ML_BIO, "ShuffleItems", .label = "Custom Item Pool" },
    { MC_BIO_POOL, NULL, ML_BIO, "RatioGunpowder", .label = "Gunpowder Probability" },
    { MC_BIO_POOL, NULL, ML_BIO, "RatioAmmo", .label = "Ammo Probability" },
    { MC_BIO_POOL, NULL, ML_BIO, "RatioHealth", .label = "Health Probability" },
    { MC_BIO_POOL, NULL, ML_BIO, "RatioInkRibbons", .label = "Ink Ribbon Probability" },
    { MC_BIO_POOL, NULL, ML_BIO, "AmmoQuantity", .label = "Ammo Average Quantity" },
    { MC_BIO_POOL, NULL, ML_BIO, "RandomInventory", .label = "Random Inventory" },
    { MC_BIO_POOL, NULL, ML_BIO, "Weapon0", .label = "Weapon 1" },
    { MC_BIO_POOL, NULL, ML_BIO, "Weapon1", .label = "Weapon 2" },
    { MC_BIO_POOL, "WEAPONS", ML_BIO, "EnabledWeapons[0]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[1]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[2]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[3]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[4]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[5]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[6]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[7]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[8]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[9]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[10]" },
    { MC_BIO_POOL, NULL, ML_BIO, "EnabledWeapons[11]" },
    { MC_BIORAND, "ENEMIES", ML_INFO, NULL, SI_BIO_ENEMIES },
    { MC_BIORAND, "BIORAND PROGRAM", ML_ACTION, NULL, SA_BIO_CHECK, "Check for BioRand Updates",
      .help = "Check GitHub for the latest BioRand generator and offer to download it." },

    /* ---------------------------------------------------------------- GAME SETUP > CO-OP RULES */
    { MC_COOP_RULES, NULL, ML_SET, "Loot Sharing" },
    { MC_COOP_RULES, NULL, ML_SET, "Duplicate Weapons and Ribbons" },
    { MC_COOP_RULES, NULL, ML_SET, "Skip Cutscenes Together" },
    { MC_COOP_RULES, NULL, ML_SET, "Game Over Rule" },
    { MC_COOP_RULES, NULL, ML_SET, "Metal Detector", .label = "Metal Detector Check", .names = N("Original", "Skip") },
    { MC_COOP_RULES, NULL, ML_SET, "Quick Start" },
    { MC_COOP_RULES, NULL, ML_SET, "Pause Enemies During Trades" },
    { MC_COOP_RULES, NULL, ML_SET, "Shared Enemy Control" },

    /* ---------------------------------------------------------------- EXTRA > IMPORT (import_menu.c shows its categories; F9 the same in a window) */
    { MC_IMPORT_SAVES, NULL, ML_SET, "Import Memory Card Saves" },

    /* ---------------------------------------------------------------- PAUSE > CHEATS AND TOOLS */
    { MC_CHEATS, "CHEATS", ML_SET, "Infinite Health",
      .help = "You take no damage." },
    { MC_CHEATS, NULL, ML_SET, "Infinite Ammo",
      .help = "Weapons never run out of ammunition." },
    { MC_CHEATS, NULL, ML_SET, "Pass Through Walls" },
    { MC_CHEATS, NULL, ML_SET, "Unlock With No Key", .label = "Open Locks Without Keys" },
    { MC_CHEATS, "ACTIONS", ML_SET, "Heal Now",
      .help = "Health back to full." },
    { MC_CHEATS, NULL, ML_SET, "Save Now" },
    { MC_CHEATS, NULL, ML_SET, "Load Game", .label = "Load Anywhere" },
    { MC_CHEATS, NULL, ML_SET, "Open Item Box" },
    { MC_CHEATS, "ITEMS", ML_GIVE, NULL, .label = "Give Item",
      .help = "Choose from every item with its icon. It goes into your inventory at once; the menu stays open." },
    { MC_CHEATS, NULL, ML_RETIRED, "Item" },   /* (Give Item's list: the item, its amount, adding it at once; F2 and the old menu keep these) */
    { MC_CHEATS, NULL, ML_RETIRED, "Quantity" },
    { MC_CHEATS, NULL, ML_RETIRED, "Add To Inventory" },
    { MC_CHEATS, "SPAWN", ML_SET, "Enemy" },
    { MC_CHEATS, NULL, ML_SET, "Spawn Ahead",
      .help = "Puts the chosen enemy in front of you." },
    { MC_CHEATS, "TELEPORT", ML_SET, "Location" },
    { MC_CHEATS, NULL, ML_SET, "Room",
      .help = "The room in the chosen location that Teleport Now goes to." },
    { MC_CHEATS, NULL, ML_SET, "Arrival Point" },
    { MC_CHEATS, NULL, ML_SET, "Use Current Room" },
    { MC_CHEATS, NULL, ML_SET, "Teleport", .label = "Teleport Now",
      .help = "Goes to the chosen room and arrival point." },
    { MC_CHEATS, "TOOLS", ML_WINDOW, NULL, 2, "Advanced Tools",
      .help = "The cheats window (F2): inventory editor, teleport search, room numbers. Opens when you leave this menu; the controller works in it, Back closes it." },
};
const int menu_rows_n = (int)(sizeof(menu_rows) / sizeof(menu_rows[0]));

/* GRAPHICS Preset "Original Look"; "Enhanced" is these rows at their defaults */
const MenuPreset menu_preset[] = {
    { "Aspect Ratio", 0 }, { "Internal Resolution", 1 }, { "Anti-Aliasing", 0 }, { "Dithering", 0 }, { "Fog Intensity", 100 },
    { "Smoother Motion", 0 }, { "Motion Trails", 1 }, { "Cutscene Bars", 0 }, { "Camera Vignette", 0 }, { "Screen Effects", 0 },
    { "Lighting and Fog", 0 },
};
const int menu_preset_n = (int)(sizeof(menu_preset) / sizeof(menu_preset[0]));

/* GAME SETUP Preset (proposal §1.4, D2): the rows it owns, put to their defaults first (Original is all of them there),
   then Remix's or Chaos's values. Not the Enemy Randomizer Seed, Extra Mouth Movement (yours), BioRand, co-op rules or SETTINGS. */
const char* const menu_setup_owned[] = {
    "Free Typewriter Saves", "Extra Item Boxes and Herbs", "Headshots & Piercing", "Break Free Pushes Enemies",
    "Extra Enemies", "Rearranged Enemies", "Enemy Randomizer", "Enemy Selection", "Types per Room", "Randomize Event Enemies",
    "Randomize Cutscene Cast", "Model Selection", "Enemy Models", "Model Assignment", "Randomize Voice Lines",
};
const int menu_setup_owned_n = (int)(sizeof(menu_setup_owned) / sizeof(menu_setup_owned[0]));
const MenuPreset menu_remix[] = { { "Rearranged Enemies", 1 }, { "Extra Enemies", 2 }, { "Extra Item Boxes and Herbs", 1 } };
const int menu_remix_n = (int)(sizeof(menu_remix) / sizeof(menu_remix[0]));
const MenuPreset menu_chaos[] = {   /* (Enemy Selection 1: Wild; Model Selection 1: All Models, the cast's "Any Model") */
    { "Enemy Randomizer", 1 }, { "Enemy Selection", 1 }, { "Randomize Event Enemies", 1 }, { "Randomize Cutscene Cast", 1 }, { "Model Selection", 1 },
    { "Randomize Voice Lines", 1 },
};
const int menu_chaos_n = (int)(sizeof(menu_chaos) / sizeof(menu_chaos[0]));

int menu_setting(const char* label)
{
    for (int i = 0; label && i < g_settings_n; i++)
        if (!strcmp(g_settings[i].label, label)) return i;
    return -1;
}

static int defaults[512];
static unsigned char has_default[512];

void menu_defaults_capture(void)
{
    for (int i = 0; i < g_settings_n && i < (int)sizeof(has_default); i++)
        if (g_settings[i].val && !(g_settings[i].flags & SET_ACTION)) { defaults[i] = *g_settings[i].val; has_default[i] = 1; }
}

int menu_default(int setting, int* v)
{
    if (setting < 0 || setting >= (int)sizeof(has_default) || !has_default[setting]) return 0;
    *v = defaults[setting];
    return 1;
}
