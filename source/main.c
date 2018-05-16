#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#define ZELDA_TITLE_ID 0x01007ef00011e000
#define RUPEE_MAX 999999
#define RUPEE_MIN 0

typedef struct ZeldaSaveData {
  int saveSlot;
  u128 userId;
  char userName[0x21];
  size_t size;
  u8 *saveData;
} ZeldaSaveData_t;

int loadNewestSaveData(ZeldaSaveData_t *saveData);
int writeSaveFile(const ZeldaSaveData_t *saveData);

int main(int argc, char **argv) {
  gfxInitDefault();
  consoleInit(NULL);
  accountInitialize();

  u64 ZeldaTitleId = ZELDA_TITLE_ID;

  FsSaveDataIterator iterator;
  FsSaveDataInfo info;
  size_t total_entries = 0;
  u32 rupees = 0;

  ZeldaSaveData_t *zeldaSave;
  zeldaSave = malloc(sizeof(ZeldaSaveData_t));

  AccountProfile profile;
  memset(&profile, 0, sizeof(profile));

  AccountProfileBase profilebase;
  memset(&profilebase, 0, sizeof(profilebase));

  Result res = fsOpenSaveDataIterator(&iterator, FsSaveDataSpaceId_NandUser);
  printf("\x1b[1;1HOpened SaveDataIterator");
  u8 count = 0;
  while (1) {
    res = fsSaveDataIteratorRead(&iterator, &info, 1, &total_entries);
    printf("\x1b[2;1HReading SaveDataIterator...");
    if (R_FAILED(res) || total_entries == 0) {
      printf("\x1b[2;1HReading SaveDataIterator... Done");
      break;
    }

    if (info.SaveDataType == FsSaveDataType_SaveData) {
      u64 tid = info.titleID;
      u128 uid = info.userID;
      char username[0x21];

      // Get the account profile
      res = accountGetProfile(&profile, uid);

      // Get the account profile base
      accountProfileGet(&profile, NULL, &profilebase);

      // Get the username
      memset(username, 0, sizeof(username));
      strncpy(username, profilebase.username, sizeof(username) - 1);

      printf("\x1b[%d;1H%d Found SaveData with TID: %16lx", count + 4,
             count / 2 + 1, tid);
      // printf("\x1b[%d;1H%d Found SaveData with UserId: 0x%lx 0x%lx", count +
      // 5, count / 2 + 1, (u64)(uid >> 64), (u64)uid);
      printf("\x1b[%d;1H%d Found SaveData with UserName: %s", count + 5,
             count / 2 + 1, username);

      if (tid == ZeldaTitleId) {
        printf("\x1b[16;1HFound Zelda save at count %d", count / 2 + 1);

        zeldaSave->userId = uid;
        strncpy(zeldaSave->userName, username, sizeof(zeldaSave->userName) - 1);

        loadNewestSaveData(zeldaSave);

        break;
      }

      count += 2;
    }
  }

  fsSaveDataIteratorClose(&iterator);

  rupees = zeldaSave->saveData[0xE0A3] << 24 |
           zeldaSave->saveData[0xE0A2] << 16 |
           zeldaSave->saveData[0xE0A1] << 8 | zeldaSave->saveData[0xE0A0];

  printf("Rupees: %u\n", rupees);

  while (appletMainLoop()) {
    // Scan all the inputs. This should be done once for each frame
    hidScanInput();

    // hidKeysDown returns information about which buttons have been just
    // pressed (and they weren't in the previous frame)
    u32 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

    // Decrease rupees
    if (kDown & KEY_DOWN) {
      if (rupees > RUPEE_MIN) {
        rupees -= 1;
      }
      printf("\x1b[27;1HRupees: %u\n", rupees);
    }

    // Increase rupees
    if (kDown & KEY_UP) {
      if (rupees < RUPEE_MAX) {
        rupees += 1;
      }
      printf("\x1b[27;1HRupees: %u\n", rupees);
    }

    // Write the updated rupees value and save the file
    if (kDown & KEY_A) {
      printf("\x1b[32;1HRupees saved = %d", rupees);
      if (zeldaSave != NULL) {
        memcpy(&zeldaSave->saveData[0xE0A0], &rupees, sizeof(u32));
        int result = writeSaveFile(zeldaSave);

        if (result == 0) {
          printf("\x1b[40;HwriteSaveFile returned %d.\n", result);
        }
      }
    }

    if (kDown & KEY_PLUS)
      break; // break in order to return to hbmenu

    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
  }

  accountExit();
  gfxExit();
  return 0;
}

int loadNewestSaveData(ZeldaSaveData_t *saveData) {
  FsFileSystem fileSystem;
  fsMount_SaveData(&fileSystem, ZELDA_TITLE_ID, saveData->userId);

  fsdevMountDevice("save", fileSystem);
  printf("\x1b[18;1HMounted Device...\n");

  u32 dates[6];

  for (int i = 0; i <= 5; i++) {

    char *path;
    path = malloc(sizeof(char) * 21);

    sprintf(path, "save:/%d/caption.sav", i);

    FILE *captionFile;
    captionFile = fopen(path, "r");
    if (captionFile != NULL) {
      fseek(captionFile, 0x10, SEEK_SET);

      char *date;
      date = malloc(sizeof(char) * 4);

      fread(date, sizeof(char), 4, captionFile);

      fclose(captionFile);

      dates[i] = (uint32_t)date[3] << 24 | (uint32_t)date[2] << 16 |
                 (uint32_t)date[1] << 8 | (uint32_t)date[0];

      printf("Save file %d has date %u\n", i, dates[i]);
    } else {
      printf("Unable to open file %d.\n", i);
    }
  }

  u32 newestDate = 0;
  for (int i = 0; i <= 5; i++) {
    if (dates[i] > newestDate) {
      newestDate = dates[i];
      saveData->saveSlot = i;
    }
  }

  printf("Newest save file is slot %d with date %u.\n", saveData->saveSlot,
         newestDate);

  char *savePath;
  savePath = malloc(sizeof(char) * 32);

  sprintf(savePath, "save:/%d/game_data.sav", saveData->saveSlot);
  FILE *saveFile;
  saveFile = fopen(savePath, "rb+");
  printf("Opened save file: %s\n", savePath);

  if (saveFile != NULL) {
    // Get file size
    fseek(saveFile, 0L, SEEK_END);
    saveData->size = ftell(saveFile);
    // printf("game_data.sav size = %d\n", fileSize);
    rewind(saveFile);

    saveData->saveData = malloc(saveData->size);
    fread(saveData->saveData, 1, saveData->size, saveFile);

    fclose(saveFile);
  } else {
    printf("Unable to open file.\n");
    return 1;
  }

  return 0;
}

int writeSaveFile(const ZeldaSaveData_t *saveData) {
  FsFileSystem fileSystem;
  fsMount_SaveData(&fileSystem, ZELDA_TITLE_ID, saveData->userId);
  printf("\x1b[29;1HMounted SaveData...");

  fsdevMountDevice("save", fileSystem);
  printf("\x1b[30;1HMounted Device...\n");

  char *savePath;
  savePath = malloc(sizeof(char) * 32);

  sprintf(savePath, "save:/%d/game_data.sav", saveData->saveSlot);
  FILE *saveFile;
  saveFile = fopen(savePath, "wb");
  printf("\x1b[31;1HOpened save file: %s\n", savePath);

  if (saveFile != NULL) {
    int result = fwrite(saveData->saveData, 1, saveData->size, saveFile);

    if (result == 0) {
      printf("\x1b[39;1HUnable to write save file. fwrite returned %d\n",
             result);
      fclose(saveFile);
      return 0;
    }

    result = fclose(saveFile);
    if (result != 0) {
      printf("\x1b[39;1HUnable to close save file. fclose returned %d\n",
             result);
      fclose(saveFile);
      return 0;
    }

    Result res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
      printf("\x1b[39;1HfsFsCommit Failed!\n");
	  return 1;
    }
  } else {
    printf("\x1b[31;1HUnable to open file.\n");
    return 1;
  }

  fsdevUnmountDevice("save");

  return 0;
}