#ifndef USERDATA_H
#define USERDATA_H

#define GLOBALVAR_COUNT (0x100)

#define ACHIEVEMENT_COUNT (0x40)
#define LEADERBOARD_COUNT (0x80)

#define MOD_COUNT (0x100)

#define SAVEDATA_SIZE (0x4000)

#ifndef RETRO_CHECKUPDATE
#define RETRO_CHECKUPDATE (1)
#endif

enum OnlineMenuTypes {
    ONLINEMENU_ACHIEVEMENTS = 0,
    ONLINEMENU_LEADERBOARDS = 1,
};

struct Achievement {
    char name[0x40];
    int status;
};

struct LeaderboardEntry {
    int score;
};

extern int globalVariablesCount;
extern int globalVariables[GLOBALVAR_COUNT];
extern char globalVariableNames[GLOBALVAR_COUNT][0x20];

extern char gamePath[0x100];
extern int saveRAM[SAVEDATA_SIZE];
extern Achievement achievements[ACHIEVEMENT_COUNT];
extern LeaderboardEntry leaderboards[LEADERBOARD_COUNT];

extern int controlMode;
extern bool disableTouchControls;
extern int disableFocusPause;
extern int disableFocusPause_Config;
extern int CheckForthemUpdates;

#if RETRO_USE_MOD_LOADER || !RETRO_USE_ORIGINAL_CODE
extern bool forceUseScripts;
extern bool forceUseScripts_Config;
#endif

inline int GetGlobalVariableByName(const char *name)
{
    for (int v = 0; v < globalVariablesCount; ++v) {
        if (StrComp(name, globalVariableNames[v]))
            return globalVariables[v];
    }
    return 0;
}

inline void SetGlobalVariableByName(const char *name, int value)
{
    for (int v = 0; v < globalVariablesCount; ++v) {
        if (StrComp(name, globalVariableNames[v])) {
            globalVariables[v] = value;
            break;
        }
    }
}

extern bool useSGame;
bool ReadSaveRAMData();
bool WriteSaveRAMData();

void InitUserdata();
void WriteSettings();
void ReadUserdata();
void WriteUserdata();

void AwardAchievement(int id, int status);
void SetAchievement(int achievementID, int achievementDone);
void GetAchievement(int achievementID, int achievementDone);
void SetLeaderboard(int leaderboardID, int result);
inline void LoadAchievementsMenu() { ReadUserdata(); }
inline void LoadLeaderboardsMenu() { ReadUserdata(); }
void SetScreenWidth(int width, int unused);
void GetWindowFullScreen();
void SetWindowFullScreen(int *fullscreen);
int CheckUpdates(char website[]);
void GetWindowScale();
void SetWindowScale(int *scale);

#endif //! USERDATA_H