#include <efi.h>
#include <efilib.h>

#define MAX_OS 10
#define MAX_PATH 256
#define TIMEOUT_SECONDS 5

typedef struct {
    CHAR16 Name[32];
    CHAR16 Path[MAX_PATH];
} OS_ENTRY;

EFI_STATUS EFIAPI LoadOS(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, CHAR16 *Path) {
    EFI_STATUS Status;
    EFI_HANDLE OSHandle = NULL;

    Status = SystemTable->BootServices->LoadImage(FALSE, ImageHandle, NULL, NULL, 0, &OSHandle);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_PROTOCOL *Root;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    Status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs
    );
    if (EFI_ERROR(Status)) return Status;

    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_PROTOCOL *File;
    Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return Status;

    Status = SystemTable->BootServices->LoadImage(FALSE, ImageHandle, File, NULL, 0, &OSHandle);
    if (EFI_ERROR(Status)) return Status;

    return SystemTable->BootServices->StartImage(OSHandle, NULL, NULL);
}

EFI_STATUS EFIAPI ReadConfig(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, OS_ENTRY *Entries, UINTN *Count) {
    EFI_FILE_PROTOCOL *Root;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    EFI_FILE_PROTOCOL *File;
    CHAR16 *ConfigPath = L"\\EFI\\MultiEFI\\MultiEFI.cfg";
    CHAR16 Buffer[256];
    UINTN BufferSize;

    *Count = 0;

    SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Fs);
    Fs->OpenVolume(Fs, &Root);
    if (Root->Open(Root, &File, ConfigPath, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS)
        return EFI_NOT_FOUND;

    while (TRUE) {
        BufferSize = sizeof(Buffer);
        EFI_STATUS Status = File->Read(File, &BufferSize, Buffer);
        if (EFI_ERROR(Status) || BufferSize == 0) break;

        // Simple parsing: Name=Path
        CHAR16 *Eq = StrStr(Buffer, L"=");
        if (Eq) {
            *Eq = 0;
            StrCpy(Entries[*Count].Name, Buffer);
            StrCpy(Entries[*Count].Path, Eq + 1);
            (*Count)++;
            if (*Count >= MAX_OS) break;
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"=== MultiEFI Loader ===\n");

    OS_ENTRY Entries[MAX_OS];
    UINTN OSCount = 0;

    if (ReadConfig(ImageHandle, SystemTable, Entries, &OSCount) != EFI_SUCCESS || OSCount == 0) {
        Print(L"Config not found or empty!\n");
        return EFI_NOT_FOUND;
    }

    // Display menu
    for (UINTN i = 0; i < OSCount; i++) {
        Print(L"%d. %s\n", i + 1, Entries[i].Name);
    }
    Print(L"Select OS (default in %d seconds): ", TIMEOUT_SECONDS);

    EFI_INPUT_KEY Key;
    UINTN Index;
    UINTN DefaultIndex = 0;

    EFI_EVENT TimerEvent;
    SystemTable->BootServices->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    SystemTable->BootServices->SetTimer(TimerEvent, TimerRelative, TIMEOUT_SECONDS * 10000000);

    EFI_EVENT WaitList[2] = { SystemTable->ConIn->WaitForKey, TimerEvent };
    UINTN WaitIndex;
    SystemTable->BootServices->WaitForEvent(2, WaitList, &WaitIndex);

    if (WaitIndex == 0) {
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
        if (Key.UnicodeChar >= '1' && Key.UnicodeChar <= '0' + OSCount)
            DefaultIndex = Key.UnicodeChar - '1';
    }

    Print(L"\nBooting %s...\n", Entries[DefaultIndex].Name);
    LoadOS(ImageHandle, SystemTable, Entries[DefaultIndex].Path);

    return EFI_SUCCESS;
}
