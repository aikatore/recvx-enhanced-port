/* The new menu's tree (menu_layout.h, docs/MENUS.md): every row of the settings table in its one home, in screen order.
   ctest "menu_layout" reads this file: keep each entry's kind and label together as ML_SET followed by the quoted label (also
   ML_SHORTCUT, ML_RETIRED) so the check finds them. Labels are the table's (settings.c); the words shown are .label /
   .names / .help when given. New words are English and go through tr() where they are drawn. */
#include "menu_layout.h"
#include "settings.h"
#include <string.h>

int g_new_menu;

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
    [MC_RULES] = { MS_SETUP, "RULES", -1 },
    [MC_ENEMIES] = { MS_SETUP, "ENEMIES", -1 },
    [MC_CHARACTERS] = { MS_SETUP, "CHARACTERS", -1 },
    [MC_COOP_RULES] = { MS_SETUP, "CO-OP RULES", -1 },
    [MC_CHAR_ADV] = { MS_SETUP, "ADVANCED", MC_CHARACTERS },
    [MC_CHEATS] = { MS_CHEATS, "CHEATS AND TOOLS", -1 },
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
    { MC_GAMEPLAY, NULL, ML_SET, "Move While Aiming" },
    { MC_GAMEPLAY, NULL, ML_SET, "Precise Shots" },
    { MC_GAMEPLAY, NULL, ML_SET, "Locational Hit Reactions" },
    { MC_GAMEPLAY, NULL, ML_SET, "Zombie Decapitation" },
    { MC_GAMEPLAY, NULL, ML_SET, "Stack Rifle Ammo" },
    { MC_GAMEPLAY, NULL, ML_SHORTCUT, "Headshots & Piercing", MC_RULES, "Headshots and Piercing",
      .help = "A rule of this game: set in GAME SETUP > RULES. Shortcut: GAME SETUP > RULES." },
    { MC_GAMEPLAY, "SAVES", ML_SET, "Save System" },
    { MC_GAMEPLAY, NULL, ML_SET, "Import Memory Card Saves" },
    { MC_GAMEPLAY, "STARTUP AND UNLOCKS", ML_SET, "Skip Intro" },
    { MC_GAMEPLAY, NULL, ML_SET, "Check For Updates", .label = "Check for Game Updates" },
    { MC_GAMEPLAY, NULL, ML_SET, "Battle Game Unlocked", .label = "Unlock Battle Game" },
    { MC_GAMEPLAY, NULL, ML_RETIRED, "Difficulty Select" },   /* D4: Difficulty is a GAME SETUP row (P2); the old menu keeps it */

    /* ---------------------------------------------------------------- SETTINGS > CAMERA AND HUD */
    { MC_CAMERA, "THIRD-PERSON CAMERA", ML_SET, "Over-the-shoulder Camera", .label = "Third-person Camera",
      .help = "Play with a controllable camera and free aim. Events, doors and menus keep their original cameras. Also F5 and the pause menu." },
    { MC_CAMERA, NULL, ML_SET, "Field Of View", .label = "Field of View" },
    { MC_CAMERA, NULL, ML_SET, "Aiming Field of View" },
    { MC_CAMERA, NULL, ML_SET, "Left Shoulder", .label = "Shoulder", .names = N("Right", "Left") },
    { MC_CAMERA, NULL, ML_SET, "Lighting and Fog", .label = "Camera Lighting and Fog" },
    { MC_CAMERA, NULL, ML_PAGE, NULL, MC_CAMERA_ADV, "Advanced", .help = "Camera distances, height and shoulder offset." },
    { MC_CAMERA_ADV, NULL, ML_SET, "Camera Distance" },
    { MC_CAMERA_ADV, NULL, ML_SET, "Aiming Camera Distance" },
    { MC_CAMERA_ADV, NULL, ML_SET, "Camera Height" },
    { MC_CAMERA_ADV, NULL, ML_SET, "Shoulder Offset" },
    { MC_CAMERA, "HUD", ML_SET, "HUD" },
    { MC_CAMERA, NULL, ML_SET, "HUD Position" },
    { MC_CAMERA, NULL, ML_SET, "HUD Style" },
    { MC_CAMERA, NULL, ML_SET, "Crosshair" },

    /* ---------------------------------------------------------------- SETTINGS > CONTROLS */
    { MC_CONTROLS, "INPUT", ML_SET, "Fixed Camera Controls", .label = "Fixed-camera Movement" },
    { MC_CONTROLS, NULL, ML_SET, "Input Devices" },
    { MC_CONTROLS, NULL, ML_SET, "Gamepad", .label = "Active Controller" },
    { MC_CONTROLS, NULL, ML_SCREEN, NULL, 1, "Control Type",
      .help = "The game's button layouts, Type A to D. Opens the original KEY CONFIG screen; Confirm and Cancel follow the type." },
    { MC_CONTROLS, NULL, ML_SET, "Weapon Shortcuts" },
    { MC_CONTROLS, NULL, ML_SET, "Mouse In Menus", .label = "Mouse in Menus" },
    { MC_CONTROLS, NULL, ML_SET, "Vibration Strength", .label = "Vibration", .flags = MLF_VIBRATION,
      .help = "The controller's vibration strength. Off turns vibration off." },
    { MC_CONTROLS, "MOUSE", ML_SET, "Mouse Camera Style" },
    { MC_CONTROLS, NULL, ML_SET, "Horizontal Mouse Sensitivity" },
    { MC_CONTROLS, NULL, ML_SET, "Vertical Mouse Sensitivity" },
    { MC_CONTROLS, NULL, ML_SET, "Aiming Sensitivity", .label = "Mouse Aiming Sensitivity" },
    { MC_CONTROLS, NULL, ML_SET, "Invert Mouse Y" },
    { MC_CONTROLS, "CONTROLLER", ML_SET, "Gamepad Controls", .label = "Third-person Controller Layout" },
    { MC_CONTROLS, NULL, ML_SET, "Horizontal Camera Speed" },
    { MC_CONTROLS, NULL, ML_SET, "Vertical Camera Speed" },
    { MC_CONTROLS, NULL, ML_SET, "Stick Aiming Speed" },
    { MC_CONTROLS, NULL, ML_SET, "Invert Stick Y" },
    { MC_CONTROLS, NULL, ML_PAGE, NULL, MC_PAD_ADV, "Advanced", .help = "The sticks' dead zones." },
    { MC_PAD_ADV, NULL, ML_SET, "Movement Dead Zone" },
    { MC_PAD_ADV, NULL, ML_SET, "Camera Dead Zone" },
    { MC_CONTROLS, "BUTTONS", ML_PAGE, NULL, MC_BIND_KB, "Customize Keyboard and Mouse",
      .help = "Keys and mouse buttons for game actions, third person and hotkeys." },
    { MC_CONTROLS, NULL, ML_PAGE, NULL, MC_BIND_PAD, "Customize Controller", .help = "Controller buttons for game actions and third person." },

    /* ---------------------------------------------------------------- SETTINGS > DISPLAY */
    { MC_DISPLAY, "WINDOW", ML_SET, "Display Mode" },
    { MC_DISPLAY, NULL, ML_SET, "Fullscreen Resolution" },
    { MC_DISPLAY, NULL, ML_SET, "Window Size" },
    { MC_DISPLAY, NULL, ML_SET, "VSync" },
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
    { MC_DISPLAY, "INFO", ML_SET, "Show Frame Rate" },

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
    { MC_AUDIO, NULL, ML_SET, "Music And Voice", .label = "Music and Voice Volume" },
    { MC_AUDIO, NULL, ML_SET, "Sound Effects", .label = "Sound Effects Volume" },
    { MC_AUDIO, NULL, ML_OUTPUT, NULL, 0, "Output", .help = "Stereo or mono sound (the original AUDIO SETUP)." },

    /* ---------------------------------------------------------------- SETTINGS > LANGUAGE (always shown) */
    { MC_LANGUAGE, NULL, ML_SET, "Language", .flags = MLF_ALWAYS },
    { MC_LANGUAGE, NULL, ML_SET, "Subtitles" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Pictures" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Title Call" },
    { MC_LANGUAGE, NULL, ML_SET, "Translated Overlay Windows" },
    { MC_LANGUAGE, NULL, ML_WINDOW, NULL, 4, "Import Languages",
      .help = "Add languages from other editions of the game. Opens the import window (F9) when you leave this menu." },

    /* ---------------------------------------------------------------- SETTINGS > CO-OP */
    { MC_COOP, "PERSONAL", ML_TEXT, NULL, 0, "Player Name", .help = "Your name, as your partner sees it. Edit it in MULTIPLAYER or the co-op window (F3)." },
    { MC_COOP, NULL, ML_SET, "Partner Name Tag" },
    { MC_COOP, NULL, ML_SET, "Show Ping" },
    { MC_COOP, NULL, ML_SET, "Hide IP Addresses" },
    { MC_COOP, NULL, ML_SET, "Follow Host" },
    { MC_COOP, "HOSTING", ML_SET, "Remember Partner Items",
      .help = "Only when you host: keep what your partner carried, and give it back when the same partner joins again." },
    { MC_COOP, NULL, ML_SET, "Host Port" },
    { MC_COOP, NULL, ML_SET, "Automatic Port Forwarding" },
    { MC_COOP, "CONNECTION", ML_SET, "Connection", .names = N("Automatic", "Direct (IP Address)", "Always Relay") },
    { MC_COOP, NULL, ML_TEXT, NULL, 1, "Co-op Server", .help = "The server for room codes and the relay. Edit it in MULTIPLAYER or the co-op window (F3)." },

    /* ---------------------------------------------------------------- GAME SETUP > RULES */
    { MC_RULES, NULL, ML_SET, "Free Typewriter Saves", .label = "Save Without Ink Ribbons" },
    { MC_RULES, NULL, ML_SET, "Extra Item Boxes and Herbs" },
    { MC_RULES, NULL, ML_SET, "Headshots & Piercing", .label = "Headshots and Piercing" },

    /* ---------------------------------------------------------------- GAME SETUP > ENEMIES */
    { MC_ENEMIES, NULL, ML_SET, "Extra Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Rearranged Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Randomizer" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Selection", .label = "Enemy Pool" },
    { MC_ENEMIES, NULL, ML_SET, "Types per Room", .label = "Max Enemy Types per Room" },
    { MC_ENEMIES, NULL, ML_SET, "Randomize Event Enemies" },
    { MC_ENEMIES, NULL, ML_SET, "Enemy Seed", .label = "Enemy Randomizer Seed" },

    /* ---------------------------------------------------------------- GAME SETUP > CHARACTERS (§8; the merged rows come with P2) */
    { MC_CHARACTERS, "YOU", ML_SET, "Character Models", .label = "Claire and Chris", .names = N("Same Look", "Separate Looks") },
    { MC_CHARACTERS, NULL, ML_SET, "Character Model", .label = "Your Look" },
    { MC_CHARACTERS, NULL, ML_SET, "Claire's Model", .label = "Claire's Look" },
    { MC_CHARACTERS, NULL, ML_SET, "Chris's Model", .label = "Chris's Look" },
    { MC_CHARACTERS, NULL, ML_SET, "Custom Cutscene Model", .label = "In Cutscenes", .names = N("Original Character", "Your Look") },
    { MC_CHARACTERS, NULL, ML_PAGE, NULL, MC_CHAR_ADV, "Advanced", .help = "Match Model Animations and Animated Faces." },
    { MC_CHAR_ADV, NULL, ML_SET, "Match Model Animations" },
    { MC_CHAR_ADV, NULL, ML_SET, "Animated Faces (Experimental)" },
    { MC_CHARACTERS, "STORY CHARACTERS IN CUTSCENES", ML_SET, "Randomize Cutscene Cast", .label = "Story Characters", .names = N("Original", "Swapped") },
    { MC_CHARACTERS, NULL, ML_SET, "Model Selection", .label = "Their Looks From", .names = N("Story Characters", "Any Model", "Custom") },
    { MC_CHARACTERS, NULL, ML_SET, "Model Assignment", .label = "Keep Their Look", .names = N("Whole Game", "New Each Scene") },
    { MC_CHARACTERS, NULL, ML_SET, "Include Imported Models" },
    { MC_CHARACTERS, "MONSTERS IN CUTSCENES", ML_SET, "Enemy Models", .label = "Cutscene Monsters",
      .names = N("Like Story Characters", "Any Monster", "Custom", "Original") },
    { MC_CHARACTERS, "VOICES", ML_SET, "Randomize Voice Lines", .label = "Voices Follow the Look" },
    { MC_CHARACTERS, NULL, ML_SET, "Talking Mouths", .label = "Extra Mouth Movement" },

    /* ---------------------------------------------------------------- GAME SETUP > CO-OP RULES */
    { MC_COOP_RULES, NULL, ML_SET, "Loot Sharing" },
    { MC_COOP_RULES, NULL, ML_SET, "Duplicate Weapons and Ribbons" },
    { MC_COOP_RULES, NULL, ML_SET, "Skip Cutscenes Together" },
    { MC_COOP_RULES, NULL, ML_SET, "Game Over Rule" },
    { MC_COOP_RULES, NULL, ML_SET, "Metal Detector", .label = "Metal Detector Check", .names = N("Original", "Skip") },
    { MC_COOP_RULES, NULL, ML_SET, "Quick Start" },
    { MC_COOP_RULES, NULL, ML_SET, "Pause Enemies During Trades" },
    { MC_COOP_RULES, NULL, ML_SET, "Shared Enemy Control" },

    /* ---------------------------------------------------------------- PAUSE > CHEATS AND TOOLS */
    { MC_CHEATS, "CHEATS", ML_SET, "Infinite Health" },
    { MC_CHEATS, NULL, ML_SET, "Infinite Ammo" },
    { MC_CHEATS, NULL, ML_SET, "Pass Through Walls" },
    { MC_CHEATS, NULL, ML_SET, "Unlock With No Key", .label = "Open Locks Without Keys" },
    { MC_CHEATS, "ACTIONS", ML_SET, "Heal Now" },
    { MC_CHEATS, NULL, ML_SET, "Save Now" },
    { MC_CHEATS, NULL, ML_SET, "Load Game", .label = "Load Anywhere" },
    { MC_CHEATS, NULL, ML_SET, "Open Item Box" },
    { MC_CHEATS, "ITEMS", ML_SET, "Item" },
    { MC_CHEATS, NULL, ML_SET, "Quantity" },
    { MC_CHEATS, NULL, ML_SET, "Add To Inventory", .label = "Add to Inventory" },
    { MC_CHEATS, "SPAWN", ML_SET, "Enemy" },
    { MC_CHEATS, NULL, ML_SET, "Spawn Ahead" },
    { MC_CHEATS, "TELEPORT", ML_SET, "Location" },
    { MC_CHEATS, NULL, ML_SET, "Room" },
    { MC_CHEATS, NULL, ML_SET, "Arrival Point" },
    { MC_CHEATS, NULL, ML_SET, "Use Current Room" },
    { MC_CHEATS, NULL, ML_SET, "Teleport", .label = "Teleport Now" },
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
