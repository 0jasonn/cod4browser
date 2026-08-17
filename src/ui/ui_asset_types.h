#pragma once

#include <cstdint>

// Canonical IW3 UI records serialized by the generated database loader.
// This header intentionally has no gameplay, renderer, or platform includes.

struct Material;
struct snd_alias_list_t;

enum operationEnum : std::int32_t
{
    OP_NOOP = 0x0,
    OP_RIGHTPAREN = 0x1,
    OP_MULTIPLY = 0x2,
    OP_DIVIDE = 0x3,
    OP_MODULUS = 0x4,
    OP_ADD = 0x5,
    OP_SUBTRACT = 0x6,
    OP_NOT = 0x7,
    OP_LESSTHAN = 0x8,
    OP_LESSTHANEQUALTO = 0x9,
    OP_GREATERTHAN = 0xA,
    OP_GREATERTHANEQUALTO = 0xB,
    OP_EQUALS = 0xC,
    OP_NOTEQUAL = 0xD,
    OP_AND = 0xE,
    OP_OR = 0xF,
    OP_LEFTPAREN = 0x10,
    OP_COMMA = 0x11,
    OP_BITWISEAND = 0x12,
    OP_BITWISEOR = 0x13,
    OP_BITWISENOT = 0x14,
    OP_BITSHIFTLEFT = 0x15,
    OP_BITSHIFTRIGHT = 0x16,
    OP_SIN = 0x17,
    OP_FIRSTFUNCTIONCALL = 0x17,
    OP_COS = 0x18,
    OP_MIN = 0x19,
    OP_MAX = 0x1A,
    OP_MILLISECONDS = 0x1B,
    OP_DVARINT = 0x1C,
    OP_DVARBOOL = 0x1D,
    OP_DVARFLOAT = 0x1E,
    OP_DVARSTRING = 0x1F,
    OP_STAT = 0x20,
    OP_UIACTIVE = 0x21,
    OP_FLASHBANGED = 0x22,
    OP_SCOPED = 0x23,
    OP_SCOREBOARDVISIBLE = 0x24,
    OP_INKILLCAM = 0x25,
    OP_PLAYERFIELD = 0x26,
    OP_SELECTINGLOCATION = 0x27,
    OP_TEAMFIELD = 0x28,
    OP_OTHERTEAMFIELD = 0x29,
    OP_MARINESFIELD = 0x2A,
    OP_OPFORFIELD = 0x2B,
    OP_MENUISOPEN = 0x2C,
    OP_WRITINGDATA = 0x2D,
    OP_INLOBBY = 0x2E,
    OP_INPRIVATEPARTY = 0x2F,
    OP_PRIVATEPARTYHOST = 0x30,
    OP_PRIVATEPARTYHOSTINLOBBY = 0x31,
    OP_ALONEINPARTY = 0x32,
    OP_ADSJAVELIN = 0x33,
    OP_WEAPLOCKBLINK = 0x34,
    OP_WEAPATTACKTOP = 0x35,
    OP_WEAPATTACKDIRECT = 0x36,
    OP_SECONDSASTIME = 0x37,
    OP_TABLELOOKUP = 0x38,
    OP_LOCALIZESTRING = 0x39,
    OP_LOCALVARINT = 0x3A,
    OP_LOCALVARBOOL = 0x3B,
    OP_LOCALVARFLOAT = 0x3C,
    OP_LOCALVARSTRING = 0x3D,
    OP_TIMELEFT = 0x3E,
    OP_SECONDSASCOUNTDOWN = 0x3F,
    OP_GAMEMSGWNDACTIVE = 0x40,
    OP_TOINT = 0x41,
    OP_TOSTRING = 0x42,
    OP_TOFLOAT = 0x43,
    OP_GAMETYPENAME = 0x44,
    OP_GAMETYPE = 0x45,
    OP_GAMETYPEDESCRIPTION = 0x46,
    OP_SCORE = 0x47,
    OP_FRIENDSONLINE = 0x48,
    OP_FOLLOWING = 0x49,
    OP_STATRANGEBITSSET = 0x4A,
    OP_KEYBINDING = 0x4B,
    OP_ACTIONSLOTUSABLE = 0x4C,
    OP_HUDFADE = 0x4D,
    OP_MAXPLAYERS = 0x4E,
    OP_ACCEPTINGINVITE = 0x4F,
#ifdef KISAK_MP
    OP_ISINTERMISSION = 0x50,
    NUM_OPERATORS = 0x51,
#else
    NUM_OPERATORS = 0x50,
#endif
};

struct rectDef_s
{
    float x;
    float y;
    float w;
    float h;
    int horzAlign;
    int vertAlign;
};

struct windowDef_t
{
    const char *name;
    rectDef_s rect;
    rectDef_s rectClient;
    const char *group;
    int style;
    int border;
    int ownerDraw;
    int ownerDrawFlags;
    float borderSize;
    int staticFlags;
    int dynamicFlags[1];
    int nextTime;
    float foreColor[4];
    float backColor[4];
    float borderColor[4];
    float outlineColor[4];
    Material *background;
};

struct ItemKeyHandler
{
    int key;
    const char *action;
    ItemKeyHandler *next;
};

union operandInternalDataUnion
{
    operandInternalDataUnion() : intVal(0) {}
    operandInternalDataUnion(int value) : intVal(value) {}
    operandInternalDataUnion(float value) : floatVal(value) {}
    operandInternalDataUnion(const char *value) : string(value) {}
    operator int() { return intVal; }
    operator float() { return floatVal; }
    int intVal;
    float floatVal;
    const char *string;
};

enum expDataType : std::int32_t
{
    VAL_INT = 0,
    VAL_FLOAT = 1,
    VAL_STRING = 2,
};

struct Operand
{
    expDataType dataType;
    operandInternalDataUnion internals;
};

union entryInternalData
{
    operationEnum op;
    Operand operand;
};

struct expressionEntry
{
    int type;
    entryInternalData data;
};

struct statement_s
{
    int numEntries;
    expressionEntry **entries;
};

struct listBoxDef_s;
struct editFieldDef_s;
struct multiDef_s;
struct menuDef_t;

union itemDefData_t
{
    listBoxDef_s *listBox;
    editFieldDef_s *editField;
    multiDef_s *multi;
    const char *enumDvarName;
    void *data;
};

struct itemDef_s
{
    windowDef_t window;
    rectDef_s textRect[1];
    int type;
    int dataType;
    int alignment;
    int fontEnum;
    int textAlignMode;
    float textalignx;
    float textaligny;
    float textscale;
    int textStyle;
    int gameMsgWindowIndex;
    int gameMsgWindowMode;
    const char *text;
    int itemFlags;
    menuDef_t *parent;
    const char *mouseEnterText;
    const char *mouseExitText;
    const char *mouseEnter;
    const char *mouseExit;
    const char *action;
    const char *onAccept;
    const char *onFocus;
    const char *leaveFocus;
    const char *dvar;
    const char *dvarTest;
    ItemKeyHandler *onKey;
    const char *enableDvar;
    int dvarFlags;
    snd_alias_list_t *focusSound;
    float special;
    int cursorPos[1];
    itemDefData_t typeData;
    int imageTrack;
    statement_s visibleExp;
    statement_s textExp;
    statement_s materialExp;
    statement_s rectXExp;
    statement_s rectYExp;
    statement_s rectWExp;
    statement_s rectHExp;
    statement_s forecolorAExp;
};

struct menuDef_t
{
    windowDef_t window;
    const char *font;
    int fullScreen;
    int itemCount;
    int fontIndex;
    int cursorItem[1];
    int fadeCycle;
    float fadeClamp;
    float fadeAmount;
    float fadeInAmount;
    float blurRadius;
    const char *onOpen;
    const char *onClose;
    const char *onESC;
    ItemKeyHandler *onKey;
    statement_s visibleExp;
    const char *allowedBinding;
    const char *soundName;
    int imageTrack;
    float focusColor[4];
    float disableColor[4];
    statement_s rectXExp;
    statement_s rectYExp;
    itemDef_s **items;
};

struct MenuList
{
    const char *name;
    int menuCount;
    menuDef_t **menus;
};

struct columnInfo_s
{
    int pos;
    int width;
    int maxChars;
    int alignment;
};

struct listBoxDef_s
{
    int mousePos;
    int startPos[1];
    int endPos[1];
    int drawPadding;
    float elementWidth;
    float elementHeight;
    int elementStyle;
    int numColumns;
    columnInfo_s columnInfo[16];
    const char *doubleClick;
    int notselectable;
    int noScrollBars;
    int usePaging;
    float selectBorder[4];
    float disableColor[4];
    Material *selectIcon;
};

struct editFieldDef_s
{
    float minVal;
    float maxVal;
    float defVal;
    float range;
    int maxChars;
    int maxCharsGotoNext;
    int maxPaintChars;
    int paintOffset;
};

struct multiDef_s
{
    const char *dvarList[32];
    const char *dvarStr[32];
    float dvarValue[32];
    int count;
    int strDef;
};

#define KISAK_UI_ASSET_TYPES_DEFINED 1
