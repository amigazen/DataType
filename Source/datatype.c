/*
 * DataType
 *
 * Copyright (c) 2025 amigazen project
 * Licensed under BSD 2-Clause License
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/animationclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <datatypes/textclass.h>
#include <libraries/iffparse.h>
#include <utility/tagitem.h>

/* These pragmas are currently missing from NDK3.2R4 */
#pragma libcall DataTypesBase FindToolNodeA f6 9802
#pragma tagcall DataTypesBase FindToolNode f6 9802
#pragma libcall DataTypesBase LaunchToolA fc A9803
#pragma tagcall DataTypesBase LaunchTool fc A9803

/* Function prototypes for pragma functions */
struct ToolNode *FindToolNodeA(struct List *, struct TagItem *);
ULONG LaunchToolA(struct Tool *, STRPTR, struct TagItem *);

/* Tool type constants */
#ifndef TW_INFO
#define TW_INFO      1
#define TW_BROWSE    2
#define TW_EDIT      3
#define TW_PRINT     4
#define TW_MAIL      5
#endif

/* Tool launch type constants */
#ifndef TF_SHELL
#define TF_SHELL     0x0001
#define TF_WORKBENCH 0x0002
#define TF_RX        0x0003
#endif

/* Tool attribute tags */
#ifndef TOOLA_Dummy
#define TOOLA_Dummy      (TAG_USER)
#define TOOLA_Program    (TOOLA_Dummy + 1)
#define TOOLA_Which      (TOOLA_Dummy + 2)
#define TOOLA_LaunchType (TOOLA_Dummy + 3)
#endif
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <proto/iffparse.h>
#include <proto/utility.h>
#include <string.h>
#include <stdlib.h>

/* Library base pointers */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *DataTypesBase;
extern struct Library *UtilityBase;
extern struct Library *IFFParseBase;

/* IFF chunk IDs */
#ifndef MAKE_ID
#define MAKE_ID(a,b,c,d) ((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif

#define ID_DTYP MAKE_ID('D','T','Y','P')
#define ID_DTHD MAKE_ID('D','T','H','D')
#define ID_DTTL MAKE_ID('D','T','T','L')
#define ID_FORM MAKE_ID('F','O','R','M')

/* Forward declarations */
BOOL InitializeLibraries(VOID);
VOID Cleanup(VOID);
VOID ShowUsage(VOID);
LONG QueryDataType(STRPTR fileName, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail);
VOID PrintDataTypeInfo(struct DataType *dtn, STRPTR fileName);
VOID PrintDatatypeMetadata(Object *dtObject, ULONG groupID);
VOID PrintTools(struct DataType *dtn, STRPTR fileName, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail);
STRPTR GetToolModeName(UWORD toolWhich);
STRPTR GetLaunchTypeName(UWORD flags);
struct ToolNode *FindToolByType(struct DataType *dtn, UWORD toolType);
BOOL FindToolInDTYPFile(struct DataType *dtn, UWORD toolType, struct Tool *toolOut);
STRPTR FindDTYPFilePath(STRPTR baseName);
BOOL ParseToolFromDTYP(STRPTR dtypPath, UWORD toolType, struct Tool *toolOut);
VOID LaunchToolForFile(struct Tool *tool, STRPTR fileName);

static const char *verstag = "$VER: DataType 47.1 (21.12.2025)\n";
static const char *stack_cookie = "$STACK: 4096\n";

/* Main entry point */
int main(int argc, char *argv[])
{
    struct RDArgs *rda = NULL;
    LONG result = RETURN_OK;
    STRPTR fileName = NULL;
    BOOL edit = FALSE;
    BOOL browse = FALSE;
    BOOL info = FALSE;
    BOOL print = FALSE;
    BOOL mail = FALSE;
    
    /* Command template */
    static const char *template = "FILE/A,EDIT/S,VIEW/S,INFO/S,PRINT/S,MAIL/S";
    LONG args[6];
    
    /* Initialize args array */
    {
        LONG i;
        for (i = 0; i < 6; i++) {
            args[i] = 0;
        }
    }
    
    /* Parse command-line arguments */
    rda = ReadArgs(template, args, NULL);
    if (!rda) {
        LONG errorCode = IoErr();
        if (errorCode != 0) {
            PrintFault(errorCode, "DataType");
        } else {
            ShowUsage();
        }
        return RETURN_FAIL;
    }
    
    /* Extract arguments */
    fileName = (STRPTR)args[0];
    edit = (BOOL)(args[1] != 0);
    browse = (BOOL)(args[2] != 0);
    info = (BOOL)(args[3] != 0);
    print = (BOOL)(args[4] != 0);
    mail = (BOOL)(args[5] != 0);
    
    /* Initialize libraries */
    if (!InitializeLibraries()) {
        LONG errorCode = IoErr();
        PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "DataType");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    
    /* Query datatype and optionally launch tool */
    if (fileName) {
        result = QueryDataType(fileName, edit, browse, info, print, mail);
    } else {
        ShowUsage();
        result = RETURN_FAIL;
    }
    
    /* Cleanup */
    if (rda) {
        FreeArgs(rda);
    }
    
    Cleanup();
    
    return result;
}

/* Initialize required libraries */
BOOL InitializeLibraries(VOID)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return FALSE;
    }
    
    UtilityBase = OpenLibrary("utility.library", 39L);
    if (!UtilityBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    DataTypesBase = OpenLibrary("datatypes.library", 45L);
    if (!DataTypesBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    IFFParseBase = OpenLibrary("iffparse.library", 39L);
    if (!IFFParseBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    return TRUE;
}

/* Cleanup libraries */
VOID Cleanup(VOID)
{
    if (IFFParseBase) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
    }
    
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
    
    if (UtilityBase) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

/* Show usage information */
VOID ShowUsage(VOID)
{
    Printf("Usage: DataType FILE=<filename> [EDIT] [BROWSE] [INFO] [PRINT] [MAIL]\n");
    Printf("\n");
    Printf("Options:\n");
    Printf("  FILE=<filename>  - File to query datatype for (required)\n");
    Printf("  EDIT             - Launch EDIT tool for the file\n");
    Printf("  BROWSE            - Launch BROWSE tool for the file\n");
    Printf("  INFO             - Launch INFO tool for the file\n");
    Printf("  PRINT            - Launch PRINT tool for the file\n");
    Printf("  MAIL             - Launch MAIL tool for the file\n");
    Printf("\n");
    Printf("If no tool switch is specified, displays datatype information and\n");
    Printf("available tools without launching anything.\n");
    Printf("\n");
    Printf("Examples:\n");
    Printf("  DataType FILE=test.txt          - Show datatype info for test.txt\n");
    Printf("  DataType FILE=image.ilbm EDIT   - Launch editor for image.ilbm\n");
    Printf("  DataType FILE=document.ftxt BROWSE - Launch browser for document.ftxt\n");
}

/* Query datatype for a file and optionally launch a tool */
LONG QueryDataType(STRPTR fileName, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail)
{
    BPTR lock = NULL;
    struct DataType *dtn = NULL;
    LONG result = RETURN_FAIL;
    LONG errorCode = 0;
    
    /* Lock the file */
    lock = Lock(fileName, ACCESS_READ);
    if (!lock) {
        errorCode = IoErr();
        PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "DataType");
        return RETURN_FAIL;
    }
    
    /* Obtain datatype for the file */
    dtn = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
    if (!dtn) {
        errorCode = IoErr();
        UnLock(lock);
        PrintFault(errorCode ? errorCode : ERROR_OBJECT_WRONG_TYPE, "DataType");
        return RETURN_FAIL;
    }
    
    /* Display datatype information */
    PrintDataTypeInfo(dtn, fileName);
    
    /* Check if any tool launch was requested */
    if (edit || browse || info || print || mail) {
        /* Launch requested tool */
        struct ToolNode *tn = NULL;
        UWORD toolType = 0;
        
        if (edit) {
            toolType = TW_EDIT;
        } else if (browse) {
            toolType = TW_BROWSE;
        } else if (info) {
            toolType = TW_INFO;
        } else if (print) {
            toolType = TW_PRINT;
        } else if (mail) {
            toolType = TW_MAIL;
        }
        
        if (toolType > 0) {
            tn = FindToolByType(dtn, toolType);
            if (tn) {
                Printf("\nLaunching tool: %s\n", tn->tn_Tool.tn_Program ? tn->tn_Tool.tn_Program : (STRPTR)"(NULL)");
                LaunchToolForFile(&tn->tn_Tool, fileName);
                result = RETURN_OK;
            } else {
                Printf("\nError: No %s tool available for this datatype\n", 
                       edit ? (STRPTR)"EDIT" : 
                       browse ? (STRPTR)"BROWSE" : 
                       info ? (STRPTR)"INFO" : 
                       print ? (STRPTR)"PRINT" : (STRPTR)"MAIL");
                result = RETURN_FAIL;
            }
        }
    } else {
        /* No tool launch requested - just show available tools */
        PrintTools(dtn, fileName, FALSE, FALSE, FALSE, FALSE, FALSE);
        result = RETURN_OK;
    }
    
    /* Cleanup */
    ReleaseDataType(dtn);
    UnLock(lock);
    
    return result;
}

/* Print datatype information in user-friendly format */
VOID PrintDataTypeInfo(struct DataType *dtn, STRPTR fileName)
{
    struct DataTypeHeader *dth;
    STRPTR groupName = NULL;
    Object *dtObject = NULL;
    BPTR fileLock = NULL;
    
    if (!dtn || !dtn->dtn_Header) {
        Printf("Error: Invalid datatype structure\n");
        return;
    }
    
    dth = dtn->dtn_Header;
    
    /* Get group name in plain English */
    groupName = GetDTString(dth->dth_GroupID);
    if (!groupName) {
        groupName = (STRPTR)"Unknown";
    }
    
    /* Display file type in file command style: filename: Group/BaseName description */
    Printf("%s: ", fileName ? fileName : (STRPTR)"(Unknown file)");
    
    /* Build descriptive type string with Group and BaseName */
    {
        STRPTR baseName = dth->dth_BaseName ? dth->dth_BaseName : (STRPTR)"Unknown";
        Printf("%s/%s", groupName, baseName);
        
        /* Add descriptive name if available and different from basename */
        if (dth->dth_Name && strcmp(dth->dth_Name, baseName) != 0) {
            Printf(" (%s)", dth->dth_Name);
        }
    }
    
    /* Try to create a datatype object to query metadata */
    if (fileName) {
        fileLock = Lock(fileName, ACCESS_READ);
        if (fileLock) {
            dtObject = NewDTObject((APTR)fileLock, TAG_DONE);
            if (dtObject) {
                PrintDatatypeMetadata(dtObject, dth->dth_GroupID);
                DisposeDTObject(dtObject);
            }
            UnLock(fileLock);
        }
    }
    
    Printf("\n");
}

/* Print datatype-specific metadata (appended to type line in human-readable format) */
VOID PrintDatatypeMetadata(Object *dtObject, ULONG groupID)
{
    ULONG width = 0;
    ULONG height = 0;
    UWORD depth = 0;
    ULONG frames = 0;
    ULONG numColors = 0;
    ULONG resultCount = 0;
    ULONG sampleLength = 0;
    UWORD samplesPerSec = 0;
    UWORD bitsPerSample = 0;
    STRPTR textBuffer = NULL;
    ULONG textBufferLen = 0;
    
    if (!dtObject) {
        return;
    }
    
    /* Query attributes based on group type and append descriptive metadata */
    if (groupID == GID_PICTURE) {
        /* Try to get picture dimensions using PDTA attributes */
        struct BitMapHeader *bmh = NULL;
        if (GetDTAttrs(dtObject, PDTA_BitMapHeader, &bmh, TAG_DONE) == 1 && bmh) {
            Printf(", %u x %u", bmh->bmh_Width, bmh->bmh_Height);
            if (bmh->bmh_Depth > 0) {
                Printf(", %u-bit", bmh->bmh_Depth);
                if (GetDTAttrs(dtObject, PDTA_NumColors, &numColors, TAG_DONE) == 1 && numColors > 0) {
                    Printf("/color, %lu colors", numColors);
                }
            }
        } else {
            /* Fallback to ADTA attributes (for picture.datatype compatibility) */
            resultCount = GetDTAttrs(dtObject,
                                     ADTA_Width, &width,
                                     ADTA_Height, &height,
                                     ADTA_Depth, &depth,
                                     ADTA_NumColors, &numColors,
                                     TAG_DONE);
            
            if (resultCount >= 2 && (width > 0 || height > 0)) {
                Printf(", %lu x %lu", width, height);
                if (depth > 0) {
                    Printf(", %u-bit", depth);
                    if (numColors > 0) {
                        Printf("/color, %lu colors", numColors);
                    }
                }
            }
        }
    } else if (groupID == GID_ANIMATION) {
        /* Try to get animation dimensions */
        resultCount = GetDTAttrs(dtObject,
                                 ADTA_Width, &width,
                                 ADTA_Height, &height,
                                 ADTA_Depth, &depth,
                                 ADTA_NumColors, &numColors,
                                 TAG_DONE);
        
        if (resultCount >= 2 && (width > 0 || height > 0)) {
            Printf(", %lu x %lu", width, height);
            if (depth > 0) {
                Printf(", %u-bit", depth);
                if (numColors > 0) {
                    Printf("/color, %lu colors", numColors);
                }
            }
        }
        
        /* Get frame count */
        if (GetDTAttrs(dtObject, ADTA_Frames, &frames, TAG_DONE) == 1 && frames > 0) {
            Printf(", %lu frame%s", frames, frames == 1 ? "" : "s");
        }
    } else if (groupID == GID_SOUND) {
        /* Get sound attributes */
        resultCount = GetDTAttrs(dtObject,
                                 SDTA_SampleLength, &sampleLength,
                                 SDTA_SamplesPerSec, &samplesPerSec,
                                 SDTA_BitsPerSample, &bitsPerSample,
                                 TAG_DONE);
        
        if (resultCount >= 1 && sampleLength > 0) {
            Printf(", %lu bytes", sampleLength);
            if (samplesPerSec > 0) {
                Printf(", %u Hz", samplesPerSec);
            }
            if (bitsPerSample > 0) {
                Printf(", %u-bit", bitsPerSample);
            }
        }
    } else if (groupID == GID_TEXT) {
        /* Get text attributes */
        if (GetDTAttrs(dtObject, TDTA_Buffer, &textBuffer, TDTA_BufferLen, &textBufferLen, TAG_DONE) >= 1) {
            if (textBufferLen > 0) {
                Printf(", %lu character%s", textBufferLen, textBufferLen == 1 ? "" : "s");
            }
        }
    }
}

/* Get tool mode name */
STRPTR GetToolModeName(UWORD toolWhich)
{
    switch (toolWhich) {
        case TW_INFO:
            return (STRPTR)"INFO";
        case TW_BROWSE:
            return (STRPTR)"VIEW";
        case TW_EDIT:
            return (STRPTR)"EDIT";
        case TW_PRINT:
            return (STRPTR)"PRINT";
        case TW_MAIL:
            return (STRPTR)"MAIL";
        default:
            return (STRPTR)"UNKNOWN";
    }
}

/* Get launch type name */
STRPTR GetLaunchTypeName(UWORD flags)
{
    UWORD launchType = flags & TF_LAUNCH_MASK;
    
    switch (launchType) {
        case TF_SHELL:
            return (STRPTR)"Shell";
        case TF_WORKBENCH:
            return (STRPTR)"Workbench";
        case TF_RX:
            return (STRPTR)"ARexx";
        default:
            return (STRPTR)"Unknown";
    }
}

/* Print available tools - one per line, human-readable format */
VOID PrintTools(struct DataType *dtn, STRPTR fileName, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail)
{
    struct ToolNode *tn = NULL;
    
    if (!dtn) {
        return;
    }
    
    /* Check for INFO tool */
    tn = FindToolByType(dtn, TW_INFO);
    if (tn && tn->tn_Tool.tn_Program) {
        Printf("  %s: %s\n", 
               GetToolModeName(tn->tn_Tool.tn_Which),
               tn->tn_Tool.tn_Program);
    }
    
    /* Check for BROWSE tool */
    tn = FindToolByType(dtn, TW_BROWSE);
    if (tn && tn->tn_Tool.tn_Program) {
        Printf("  %s: %s\n", 
               GetToolModeName(tn->tn_Tool.tn_Which),
               tn->tn_Tool.tn_Program);
    }
    
    /* Check for EDIT tool */
    tn = FindToolByType(dtn, TW_EDIT);
    if (tn && tn->tn_Tool.tn_Program) {
        Printf("  %s: %s\n", 
               GetToolModeName(tn->tn_Tool.tn_Which),
               tn->tn_Tool.tn_Program);
    }
    
    /* Check for PRINT tool */
    tn = FindToolByType(dtn, TW_PRINT);
    if (tn && tn->tn_Tool.tn_Program) {
        Printf("  %s: %s\n", 
               GetToolModeName(tn->tn_Tool.tn_Which),
               tn->tn_Tool.tn_Program);
    }
    
    /* Check for MAIL tool */
    tn = FindToolByType(dtn, TW_MAIL);
    if (tn && tn->tn_Tool.tn_Program) {
        Printf("  %s: %s\n", 
               GetToolModeName(tn->tn_Tool.tn_Which),
               tn->tn_Tool.tn_Program);
    }
}

/* Find a tool node by type */
struct ToolNode *FindToolByType(struct DataType *dtn, UWORD toolType)
{
    struct ToolNode *tn = NULL;
    struct List *toolList = NULL;
    struct TagItem tags[2];
    struct Tool fallbackTool;
    BOOL fallbackSuccess = FALSE;
    
    if (!dtn) {
        return NULL;
    }
    
    toolList = &dtn->dtn_ToolList;
    
    /* First try FindToolNodeA API */
    if (!IsListEmpty(toolList)) {
        tags[0].ti_Tag = TOOLA_Which;
        tags[0].ti_Data = (ULONG)toolType;
        tags[1].ti_Tag = TAG_DONE;
        
        tn = FindToolNodeA(toolList, tags);
    }
    
    /* If API failed, fall back to parsing DTYP file directly */
    if (!tn) {
        fallbackSuccess = FindToolInDTYPFile(dtn, toolType, &fallbackTool);
        if (fallbackSuccess && fallbackTool.tn_Program) {
            /* Create a temporary ToolNode structure to return */
            /* Note: This is a workaround - we'll allocate memory for it */
            static struct ToolNode staticToolNode;
            staticToolNode.tn_Tool = fallbackTool;
            staticToolNode.tn_Node.ln_Succ = NULL;
            staticToolNode.tn_Node.ln_Pred = NULL;
            staticToolNode.tn_Length = sizeof(struct ToolNode);
            tn = &staticToolNode;
        }
    }
    
    return tn;
}

/* Launch a tool for a file */
VOID LaunchToolForFile(struct Tool *tool, STRPTR fileName)
{
    ULONG result = 0;
    LONG errorCode = 0;
    
    if (!tool || !fileName) {
        PrintFault(ERROR_BAD_NUMBER, "DataType");
        return;
    }
    
    /* Clear any previous error */
    SetIoErr(0);
    
    /* Launch the tool using LaunchToolA */
    /* The project parameter is the filename */
    result = LaunchToolA(tool, fileName, NULL);
    
    errorCode = IoErr();
    
    if (result == 0 || errorCode != 0) {
        Printf("Error: Failed to launch tool\n");
        if (errorCode != 0) {
            PrintFault(errorCode, "DataType");
        } else {
            Printf("LaunchToolA returned FALSE\n");
        }
    }
}

/* Find tool in DTYP file (fallback when FindToolNodeA fails) */
BOOL FindToolInDTYPFile(struct DataType *dtn, UWORD toolType, struct Tool *toolOut)
{
    STRPTR dtypPath = NULL;
    BOOL result = FALSE;
    
    if (!dtn || !dtn->dtn_Header || !toolOut) {
        return FALSE;
    }
    
    /* Find the DTYP file path using BaseName */
    dtypPath = FindDTYPFilePath(dtn->dtn_Header->dth_BaseName);
    if (!dtypPath) {
        return FALSE;
    }
    
    /* Parse the DTYP file to find the tool */
    result = ParseToolFromDTYP(dtypPath, toolType, toolOut);
    
    /* Free the path string */
    if (dtypPath) {
        FreeMem(dtypPath, strlen(dtypPath) + 1);
    }
    
    return result;
}

/* Find DTYP file path for a given BaseName */
STRPTR FindDTYPFilePath(STRPTR baseName)
{
    BPTR lock = NULL;
    struct FileInfoBlock *fib = NULL;
    STRPTR datatypesPath = "DEVS:Datatypes";
    STRPTR resultPath = NULL;
    
    if (!baseName) {
        return NULL;
    }
    
    /* Lock the datatypes directory */
    lock = Lock(datatypesPath, ACCESS_READ);
    if (!lock) {
        return NULL;
    }
    
    /* Allocate FileInfoBlock */
    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
    if (!fib) {
        UnLock(lock);
        return NULL;
    }
    
    /* Scan directory for matching file */
    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            STRPTR fileName = fib->fib_FileName;
            LONG nameLen = 0;
            STRPTR ext;
            BOOL isInfoFile = FALSE;
            STRPTR fullPath = NULL;
            LONG pathLen;
            
            /* Find length of filename and check for .info extension */
            ext = fileName;
            while (*ext) {
                nameLen++;
                ext++;
            }
            
            /* Check if it's a .info file - skip those */
            if (nameLen > 5) {
                ext = fileName + nameLen - 5;
                if (strcmp(ext, ".info") == 0) {
                    isInfoFile = TRUE;
                }
            }
            
            /* All non-.info files are datatype descriptors */
            if (!isInfoFile && nameLen > 0) {
                /* Check if filename matches BaseName (case-insensitive) */
                /* We'll try to match by checking if BaseName appears in filename */
                /* For now, we'll check if the filename starts with BaseName */
                if (strlen(baseName) <= nameLen) {
                    STRPTR testName = (STRPTR)AllocMem(nameLen + 1, MEMF_CLEAR);
                    if (testName) {
                        LONG i;
                        /* Convert to lowercase for comparison */
                        for (i = 0; i < nameLen && fileName[i]; i++) {
                            if (fileName[i] >= 'A' && fileName[i] <= 'Z') {
                                testName[i] = fileName[i] + 32;
                            } else {
                                testName[i] = fileName[i];
                            }
                        }
                        testName[nameLen] = '\0';
                        
                        /* Check if BaseName matches (case-insensitive) */
                        if (strncmp(testName, baseName, strlen(baseName)) == 0) {
                            /* Found matching file - build full path */
                            pathLen = strlen(datatypesPath) + 1 + nameLen + 1;
                            fullPath = (STRPTR)AllocMem(pathLen, MEMF_CLEAR);
                            if (fullPath) {
                                strncpy(fullPath, datatypesPath, pathLen);
                                strncat(fullPath, "/", pathLen);
                                strncat(fullPath, fileName, pathLen);
                                resultPath = fullPath;
                                FreeMem(testName, nameLen + 1);
                                break;
                            }
                        }
                        FreeMem(testName, nameLen + 1);
                    }
                }
            }
        }
    }
    
    /* Cleanup */
    FreeMem(fib, sizeof(struct FileInfoBlock));
    UnLock(lock);
    
    return resultPath;
}

/* Parse tool from DTYP file */
BOOL ParseToolFromDTYP(STRPTR dtypPath, UWORD toolType, struct Tool *toolOut)
{
    struct IFFHandle *iff = NULL;
    BPTR fileHandle = 0;
    LONG errorCode = 0;
    BOOL result = FALSE;
    BOOL foundDTHD = FALSE;
    LONG progLen = 0;
    STRPTR progCopy = NULL;
    
    if (!dtypPath || !toolOut) {
        return FALSE;
    }
    
    /* Open file */
    fileHandle = Open(dtypPath, MODE_OLDFILE);
    if (!fileHandle) {
        return FALSE;
    }
    
    /* Allocate IFF handle */
    iff = AllocIFF();
    if (!iff) {
        Close(fileHandle);
        return FALSE;
    }
    
    /* Initialize IFF for DOS file */
    InitIFFasDOS(iff);
    iff->iff_Stream = (ULONG)fileHandle;
    
    /* First, find DTHD chunk to skip past it */
    if (StopChunk(iff, ID_DTYP, ID_DTHD) != 0) {
        FreeIFF(iff);
        Close(fileHandle);
        return FALSE;
    }
    
    /* Open IFF for reading */
    errorCode = OpenIFF(iff, IFFF_READ);
    if (errorCode != 0) {
        FreeIFF(iff);
        Close(fileHandle);
        return FALSE;
    }
    
    /* Parse until we find DTHD */
    errorCode = ParseIFF(iff, IFFPARSE_SCAN);
    if (errorCode == 0) {
        struct ContextNode *cn = CurrentChunk(iff);
        if (cn && cn->cn_Type == ID_DTYP && cn->cn_ID == ID_DTHD) {
            foundDTHD = TRUE;
            PopChunk(iff);
        }
    }
    
    /* Now search for DTTL chunks */
    if (foundDTHD) {
        /* Use StopChunk to stop at each DTTL chunk */
        if (StopChunk(iff, ID_DTYP, ID_DTTL) == 0) {
            /* Continue parsing to find all DTTL chunks */
            while ((errorCode = ParseIFF(iff, IFFPARSE_SCAN)) == 0) {
                struct ContextNode *cn = CurrentChunk(iff);
                if (cn && cn->cn_Type == ID_DTYP && cn->cn_ID == ID_DTTL) {
                    /* Found a DTTL chunk - read it */
                    ULONG chunkSize = cn->cn_Size;
                    UBYTE *toolData = NULL;
                    
                    if (chunkSize > 0 && chunkSize < 1000) {
                        toolData = (UBYTE *)AllocMem(chunkSize, MEMF_CLEAR);
                        if (toolData) {
                            LONG bytesRead = ReadChunkBytes(iff, toolData, chunkSize);
                            if (bytesRead == chunkSize) {
                                /* Parse the Tool structure */
                                UWORD toolWhich;
                                ULONG programOffset;
                                STRPTR programName = NULL;
                                
                                toolWhich = ((UWORD)toolData[0] << 8) | toolData[1];
                                
                                /* Check if this is the tool type we're looking for */
                                if (toolWhich == toolType) {
                                    toolOut->tn_Which = toolWhich;
                                    toolOut->tn_Flags = ((UWORD)toolData[2] << 8) | toolData[3];
                                    programOffset = ((ULONG)toolData[4] << 24) | ((ULONG)toolData[5] << 16) |
                                                   ((ULONG)toolData[6] << 8) | (ULONG)toolData[7];
                                    
                                    /* Extract program name from offset */
                                    if (programOffset > 0 && programOffset < chunkSize) {
                                        programName = (STRPTR)(toolData + programOffset);
                                        /* Allocate memory for program name and copy it */
                                        progLen = strlen(programName) + 1;
                                        progCopy = (STRPTR)AllocMem(progLen, MEMF_CLEAR);
                                        if (progCopy) {
                                            strncpy(progCopy, programName, progLen);
                                            toolOut->tn_Program = progCopy;
                                            result = TRUE;
                                        }
                                    }
                                    FreeMem(toolData, chunkSize);
                                    break;
                                }
                            }
                            if (!result) {
                                FreeMem(toolData, chunkSize);
                            }
                        }
                    }
                    
                    /* Pop the chunk to continue */
                    PopChunk(iff);
                } else {
                    /* Not a DTTL chunk, stop parsing */
                    break;
                }
            }
        }
    }
    
    /* Cleanup */
    CloseIFF(iff);
    FreeIFF(iff);
    Close(fileHandle);
    
    return result;
}

