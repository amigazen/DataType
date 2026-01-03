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
#include <workbench/icon.h>
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

/* Macro to test if a datatype method is supported */
#ifndef IsDTMethodSupported
#define IsDTMethodSupported( o, id ) \
    ((BOOL)FindMethod(GetDTMethods( (o) ), (id) ))
#endif
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/datatypes.h>
#include <proto/iffparse.h>
#include <proto/utility.h>
#include <string.h>
#include <stdlib.h>

/* Library base pointers */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *IconBase;
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
LONG QueryDataType(STRPTR fileName, STRPTR outputFile, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail, BOOL convert, BOOL force);
ULONG ListAvailableFormats(struct DataType *sourceDtn, ULONG groupID);
struct DataType *SelectFormatFromList(ULONG groupID, LONG *selectedIndex);
BOOL ConvertToFormat(STRPTR inputFile, struct DataType *destDtn, STRPTR outputFile);
BOOL CheckOutputFileExists(STRPTR outputFile, BOOL force);
VOID PrintDataTypeInfo(struct DataType *dtn, STRPTR fileName);
VOID PrintDatatypeMetadata(Object *dtObject, ULONG groupID);
VOID PrintTools(struct DataType *dtn, STRPTR fileName, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail);
VOID PrintWriteCapabilities(Object *dtObject);
STRPTR GetToolModeName(UWORD toolWhich);
STRPTR GetLaunchTypeName(UWORD flags);
struct ToolNode *FindToolByType(struct DataType *dtn, UWORD toolType);
BOOL FindToolInDTYPFile(struct DataType *dtn, UWORD toolType, struct Tool *toolOut);
STRPTR FindDTYPFilePath(STRPTR baseName);
BOOL ParseToolFromDTYP(STRPTR dtypPath, UWORD toolType, struct Tool *toolOut);
VOID LaunchToolForFile(struct Tool *tool, STRPTR fileName);
BOOL IsDefIconsRunning(VOID);
STRPTR GetDefIconsTypeIdentifier(STRPTR fileName, BPTR fileLock);
STRPTR GetDefIconsDefaultTool(STRPTR typeIdentifier);
BOOL ConvertToIFF(STRPTR inputFile, STRPTR outputFile);

static const char *verstag = "$VER: DataType 47.2 (2/1/2026)\n";
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
    static const char *template = "FILE/A,TARGET/K,CONVERT/S,EDIT/S,VIEW=BROWSE/S,INFO/S,PRINT/S,MAIL/S,FORCE/S";
    LONG args[9];
    STRPTR outputFile = NULL;
    BOOL convert = FALSE;
    BOOL force = FALSE;
    
    /* Initialize args array */
    {
        LONG i;
        for (i = 0; i < 9; i++) {
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
    outputFile = (STRPTR)args[1];
    convert = (BOOL)(args[2] != 0);
    edit = (BOOL)(args[3] != 0);
    browse = (BOOL)(args[4] != 0);
    info = (BOOL)(args[5] != 0);
    print = (BOOL)(args[6] != 0);
    mail = (BOOL)(args[7] != 0);
    force = (BOOL)(args[8] != 0);

    
    /* Initialize libraries */
    if (!InitializeLibraries()) {
        LONG errorCode = IoErr();
        PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "DataType");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    
    /* Query datatype and optionally launch tool or convert */
    if (fileName) {
        result = QueryDataType(fileName, outputFile, edit, browse, info, print, mail, convert, force);
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
    
    /* Open icon.library for DefIcons integration (optional) */
    IconBase = OpenLibrary("icon.library", 47L);
    /* Note: icon.library is optional - we continue even if it fails */
    
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
    
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
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
    Printf("Usage: DataType FILE=<filename> [OUTPUT=<outfile>] [CONVERT] [FORCE] [EDIT] [BROWSE] [INFO] [PRINT] [MAIL]\n");
    Printf("\n");
    Printf("Options:\n");
    Printf("  FILE=<filename>  - File to query datatype for (required)\n");
    Printf("  OUTPUT=<file>    - Output file for conversion (assumes IFF if CONVERT not specified)\n");
    Printf("  CONVERT          - List available formats and prompt for selection\n");
    Printf("  EDIT             - Launch EDIT tool for the file\n");
    Printf("  VIEW=BROWSE      - Launch VIEW tool for the file\n");
    Printf("  INFO             - Launch INFO tool for the file\n");
    Printf("  PRINT            - Launch PRINT tool for the file\n");
    Printf("  MAIL             - Launch MAIL tool for the file\n");
    Printf("  FORCE            - Overwrite existing output file\n");
    Printf("\n");
    Printf("If no tool switch is specified, displays datatype information and\n");
    Printf("available tools without launching anything.\n");
    Printf("\n");
    Printf("Examples:\n");
    Printf("  DataType FILE=test.txt          - Show datatype info for test.txt\n");
    Printf("  DataType FILE=image.ilbm EDIT   - Launch editor for image.ilbm\n");
    Printf("  DataType FILE=document.ftxt BROWSE - Launch browser for document.ftxt\n");
    Printf("  DataType FILE=pic.jpg OUTPUT=pic.ilbm - Convert pic.jpg to IFF format\n");
    Printf("  DataType FILE=pic.jpg CONVERT   - List formats and convert pic.jpg\n");
}

/* Query datatype for a file and optionally launch a tool or convert */
LONG QueryDataType(STRPTR fileName, STRPTR outputFile, BOOL edit, BOOL browse, BOOL info, BOOL print, BOOL mail, BOOL convert, BOOL force)
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
    
    /* Check if conversion was requested */
    /* If OUTPUT is specified without CONVERT, assume IFF conversion */
    if (convert || (outputFile && !convert)) {
        ULONG groupID = dtn->dtn_Header->dth_GroupID;
        struct DataType *destDtn = NULL;
        STRPTR finalOutputFile = outputFile;
        BOOL doIFFConversion = FALSE;
        
        /* Determine conversion mode */
        if (outputFile && !convert) {
            /* OUTPUT without CONVERT - do IFF conversion */
            doIFFConversion = TRUE;
        }
        
        /* If IFF conversion is requested, skip straight to IFF conversion */
        if (doIFFConversion) {
            if (!finalOutputFile) {
                Printf("\nError: OUTPUT file must be specified for IFF conversion\n");
                ReleaseDataType(dtn);
                UnLock(lock);
                return RETURN_FAIL;
            }
            
            /* Check if output file exists and FORCE is not specified */
            if (!CheckOutputFileExists(finalOutputFile, force)) {
                ReleaseDataType(dtn);
                UnLock(lock);
                return RETURN_FAIL;
            }
            
            /* Convert to IFF format */
            if (ConvertToIFF(fileName, finalOutputFile)) {
                Printf("\nSuccessfully converted %s to IFF format: %s\n", fileName, finalOutputFile);
                result = RETURN_OK;
            } else {
                LONG errorCode = IoErr();
                Printf("\nError: Failed to convert to IFF format\n");
                if (errorCode != 0) {
                    PrintFault(errorCode, "DataType");
                }
                result = RETURN_FAIL;
            }
            
            /* Cleanup and return */
            ReleaseDataType(dtn);
            UnLock(lock);
            return result;
        }
        
        /* CONVERT specified - list available formats and prompt user */
        if (convert) {
            ULONG formatCount = 0;
            
            /* List available formats in the same group and get count */
            formatCount = ListAvailableFormats(dtn, groupID);
            
            /* If no formats available, don't prompt - just exit */
            if (formatCount == 0) {
                Printf("\nNo formats available for conversion\n");
                ReleaseDataType(dtn);
                UnLock(lock);
                return RETURN_FAIL;
            }
            
            /* Prompt user to select a format */
            destDtn = SelectFormatFromList(groupID, NULL);
            
            if (!destDtn) {
                Printf("\nConversion cancelled or no format selected\n");
                ReleaseDataType(dtn);
                UnLock(lock);
                return RETURN_FAIL;
            }
            
            /* Get output filename if not specified */
            if (!finalOutputFile) {
                /* Prompt for output filename or derive from input */
                UBYTE outputBuffer[256];
                STRPTR filePart = NULL;
                STRPTR baseName = destDtn->dtn_Header->dth_BaseName;
                
                filePart = FilePart(fileName);
                if (filePart) {
                    /* Copy filename part and replace extension */
                    LONG nameLen = strlen(filePart);
                    LONG extPos = -1;
                    LONG i;
                    
                    /* Find last dot in filename */
                    for (i = nameLen - 1; i >= 0; i--) {
                        if (filePart[i] == '.') {
                            extPos = i;
                            break;
                        }
                    }
                    
                    if (extPos >= 0) {
                        /* Copy up to extension, then add new extension */
                        Strncpy(outputBuffer, filePart, extPos);
                        outputBuffer[extPos] = '\0';
                        SNPrintf(outputBuffer + extPos, sizeof(outputBuffer) - extPos, ".%s", baseName);
                    } else {
                        /* No extension, just add new one */
                        Strncpy(outputBuffer, filePart, sizeof(outputBuffer));
                        SNPrintf(outputBuffer + strlen(outputBuffer), sizeof(outputBuffer) - strlen(outputBuffer), ".%s", baseName);
                    }
                    finalOutputFile = (STRPTR)outputBuffer;
                } else {
                    Printf("\nError: Could not determine output filename\n");
                    ReleaseDataType(destDtn);
                    ReleaseDataType(dtn);
                    UnLock(lock);
                    return RETURN_FAIL;
                }
            }
            
            /* Check if output file exists and FORCE is not specified */
            if (!CheckOutputFileExists(finalOutputFile, force)) {
                ReleaseDataType(destDtn);
                ReleaseDataType(dtn);
                UnLock(lock);
                return RETURN_FAIL;
            }
            
            /* Perform conversion */
            if (ConvertToFormat(fileName, destDtn, finalOutputFile)) {
                Printf("\nSuccessfully converted %s to %s format: %s\n", 
                       fileName, destDtn->dtn_Header->dth_Name, finalOutputFile);
                result = RETURN_OK;
            } else {
                LONG errorCode = IoErr();
                Printf("\nError: Failed to convert file\n");
                if (errorCode != 0) {
                    PrintFault(errorCode, "DataType");
                }
                result = RETURN_FAIL;
            }
            
            ReleaseDataType(destDtn);
            ReleaseDataType(dtn);
            UnLock(lock);
            return result;
        }
    }
    
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
                STRPTR preferredToolName = edit ? (STRPTR)"EDIT" : 
                                           browse ? (STRPTR)"BROWSE" : 
                                           info ? (STRPTR)"INFO" : 
                                           print ? (STRPTR)"PRINT" : (STRPTR)"MAIL";
                STRPTR actualToolName = GetToolModeName(tn->tn_Tool.tn_Which);
                
                /* Check if we're using a fallback tool (different from requested) */
                if (tn->tn_Tool.tn_Which != toolType) {
                    Printf("\nNote: %s tool not available, using %s tool instead\n", 
                           preferredToolName, actualToolName);
                }
                Printf("\nLaunching tool: %s\n", tn->tn_Tool.tn_Program ? tn->tn_Tool.tn_Program : (STRPTR)"(NULL)");
                LaunchToolForFile(&tn->tn_Tool, fileName);
                result = RETURN_OK;
            } else {
                Printf("\nError: No tools available for this datatype\n");
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
    STRPTR defIconsType = NULL;
    STRPTR defIconsTool = NULL;
    BPTR parentLock = NULL;
    
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
    
    /* Try to get DefIcons type identifier if DefIcons is running */
    if (fileName && IconBase && IsDefIconsRunning()) {
        fileLock = Lock(fileName, ACCESS_READ);
        if (fileLock) {
            STRPTR filePartPtr;
            UBYTE fileNameCopy[256];
            STRPTR fileNamePart = NULL;
            
            /* Get just the filename part */
            filePartPtr = FilePart(fileName);
            
            /* Make a copy of the filename part to ensure it's valid */
            /* FilePart returns a pointer into the original string, which may become invalid */
            if (filePartPtr != NULL && *filePartPtr != '\0') {
                /* Use full buffer size - Strncpy will handle truncation and null-termination */
                Strncpy(fileNameCopy, filePartPtr, sizeof(fileNameCopy));
                fileNamePart = fileNameCopy;
            } else {
                /* Fallback: use the original fileName if FilePart fails */
                fileNamePart = fileName;
            }
            
            parentLock = ParentDir(fileLock);
            if (parentLock) {
                defIconsType = GetDefIconsTypeIdentifier(fileNamePart, parentLock);
                if (defIconsType && *defIconsType) {
                    /* Get DefIcons default tool */
                    defIconsTool = GetDefIconsDefaultTool(defIconsType);
                    if (defIconsTool && *defIconsTool) {
                        Printf(" [DefIcons: %s, Default: %s]", defIconsType, defIconsTool);
                        /* Free allocated memory */
                        FreeVec(defIconsTool);
                        defIconsTool = NULL;
                    } else {
                        Printf(" [DefIcons: %s]", defIconsType);
                    }
                }
                UnLock(parentLock);
            }
            UnLock(fileLock);
        }
    }
    
    /* Try to create a datatype object to query metadata and write capabilities */
    if (fileName) {
        fileLock = Lock(fileName, ACCESS_READ);
        if (fileLock) {
            dtObject = NewDTObject((APTR)fileLock, TAG_DONE);
            if (dtObject) {
                PrintDatatypeMetadata(dtObject, dth->dth_GroupID);
                PrintWriteCapabilities(dtObject);
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
    STRPTR defIconsType = NULL;
    STRPTR defIconsTool = NULL;
    BPTR fileLock = NULL;
    BPTR parentLock = NULL;
    
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
    
    /* Show DefIcons default tool if available */
    if (fileName && IconBase && IsDefIconsRunning()) {
        fileLock = Lock(fileName, ACCESS_READ);
        if (fileLock) {
            STRPTR filePartPtr;
            UBYTE fileNameCopy[256];
            STRPTR fileNamePart = NULL;
            
            /* Get just the filename part */
            filePartPtr = FilePart(fileName);
            
            /* Make a copy of the filename part to ensure it's valid */
            /* FilePart returns a pointer into the original string, which may become invalid */
            if (filePartPtr != NULL && *filePartPtr != '\0') {
                /* Use full buffer size - Strncpy will handle truncation and null-termination */
                Strncpy(fileNameCopy, filePartPtr, sizeof(fileNameCopy));
                fileNamePart = fileNameCopy;
            } else {
                /* Fallback: use the original fileName if FilePart fails */
                fileNamePart = fileName;
            }
            
            parentLock = ParentDir(fileLock);
            if (parentLock) {
                defIconsType = GetDefIconsTypeIdentifier(fileNamePart, parentLock);
                if (defIconsType && *defIconsType) {
                    defIconsTool = GetDefIconsDefaultTool(defIconsType);
                    if (defIconsTool && *defIconsTool) {
                        Printf("  DEFAULT (DefIcons): %s\n", defIconsTool);
                        /* Free allocated memory */
                        FreeVec(defIconsTool);
                        defIconsTool = NULL;
                    }
                }
                UnLock(parentLock);
            }
            UnLock(fileLock);
        }
    }
}

/* Find a tool node by type, or fall back to any available tool */
struct ToolNode *FindToolByType(struct DataType *dtn, UWORD toolType)
{
    struct ToolNode *tn = NULL;
    struct List *toolList = NULL;
    struct TagItem tags[2];
    struct Tool fallbackTool;
    BOOL fallbackSuccess = FALSE;
    struct Node *node = NULL;
    
    if (!dtn) {
        return NULL;
    }
    
    toolList = &dtn->dtn_ToolList;
    
    /* First try FindToolNodeA API for the preferred tool type */
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
    
    /* If preferred tool type not found, fall back to any available tool */
    if (!tn && !IsListEmpty(toolList)) {
        /* Iterate through tool list and return first available tool */
        for (node = toolList->lh_Head; node->ln_Succ; node = node->ln_Succ) {
            tn = (struct ToolNode *)node;
            /* Check if this tool node has a valid program */
            if (tn->tn_Tool.tn_Program && tn->tn_Tool.tn_Program[0] != '\0') {
                /* Found an available tool - use it */
                break;
            }
            tn = NULL;
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

/* Check if DefIcons is running by looking for its message port */
BOOL IsDefIconsRunning(VOID)
{
    struct MsgPort *port;
    
    if (!SysBase) {
        return FALSE;
    }
    
    /* Look for the DEFICONS message port */
    port = FindPort("DEFICONS");
    
    if (port != NULL) {
        return TRUE;
    }
    return FALSE;
}

/* Get file type identifier using icon.library identification (DefIcons) */
STRPTR GetDefIconsTypeIdentifier(STRPTR fileName, BPTR fileLock)
{
    static UBYTE typeBuffer[256];
    struct TagItem tags[4];
    LONG errorCode = 0;
    struct DiskObject *icon = NULL;
    BPTR oldDir = NULL;
    
    if (!IconBase || !fileName) {
        return NULL;
    }
    
    /* Initialize buffer */
    typeBuffer[0] = '\0';
    
    /* Change to file's directory for identification */
    if (fileLock != NULL) {
        oldDir = CurrentDir(fileLock);
    }
    
    /* Set up tags for identification only */
    tags[0].ti_Tag = ICONGETA_IdentifyBuffer;
    tags[0].ti_Data = (ULONG)typeBuffer;
    tags[1].ti_Tag = ICONGETA_IdentifyOnly;
    tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = ICONA_ErrorCode;
    tags[2].ti_Data = (ULONG)&errorCode;
    tags[3].ti_Tag = TAG_DONE;
    
    /* Get file type identifier */
    /* Note: With ICONGETA_IdentifyOnly, GetIconTagList returns NULL */
    /* but the type identifier is placed in the buffer */
    icon = GetIconTagList(fileName, tags);
    
    /* If icon was returned (shouldn't happen with IdentifyOnly), free it */
    if (icon) {
        FreeDiskObject(icon);
    }
    
    /* Restore original directory */
    if (oldDir != NULL) {
        CurrentDir(oldDir);
    }
    
    /* Return the type identifier, or NULL if identification failed */
    /* Check both errorCode and that buffer has content */
    if (errorCode == 0 && typeBuffer[0] != '\0') {
        return typeBuffer;
    }
    
    return NULL;
}

/* Get default tool from file type identifier (DefIcons) */
/* Returns the default tool, or NULL if not found */
STRPTR GetDefIconsDefaultTool(STRPTR typeIdentifier)
{
    struct DiskObject *defaultIcon = NULL;
    STRPTR defaultTool = NULL;
    UBYTE defIconName[64];
    BPTR oldDir = NULL;
    BPTR envDir = NULL;
    
    if (!IconBase || !typeIdentifier || *typeIdentifier == '\0') {
        return NULL;
    }
    
    /* Construct default icon name: def_XXX using SNPrintf */
    SNPrintf(defIconName, sizeof(defIconName), "def_%s", typeIdentifier);
    
    /* Get the default icon from ENVARC:Sys/ or ENV:Sys/ */
    
    /* Try ENV:Sys first */
    if ((envDir = Lock("ENV:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    /* If not found, try ENVARC:Sys */
    if (!defaultIcon && (envDir = Lock("ENVARC:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    if (defaultIcon) {
        /* Extract default tool from the icon */
        /* do_DefaultTool is a STRPTR - check if it's not NULL and not empty */
        if (defaultIcon->do_DefaultTool != NULL) {
            /* Check if the string has content (not just a null terminator) */
            if (defaultIcon->do_DefaultTool[0] != '\0') {
                /* Copy the default tool string before freeing the DiskObject */
                UBYTE toolBuffer[256];
                ULONG toolLen;
                
                toolLen = strlen(defaultIcon->do_DefaultTool);
                /* Pass the full buffer size (256) to Strncpy - it will handle truncation and null-termination */
                Strncpy(toolBuffer, defaultIcon->do_DefaultTool, 256);
                
                /* Allocate memory for the tool name to return */
                /* We need to allocate this because we're freeing the DiskObject */
                defaultTool = AllocVec(toolLen + 1, MEMF_CLEAR);
                if (defaultTool) {
                    Strncpy((UBYTE *)defaultTool, toolBuffer, toolLen + 1);
                }
            }
        }
        /* Note: If icon was found but has no default tool, defaultTool will be NULL */
        
        FreeDiskObject(defaultIcon);
    }
    
    return defaultTool;
}

/* Convert file to IFF format using datatypes.library */
BOOL ConvertToIFF(STRPTR inputFile, STRPTR outputFile)
{
    Object *dtObject = NULL;
    LONG errorCode = 0;
    BOOL result = FALSE;
    STRPTR errorString = NULL;
    
    if (!inputFile || !outputFile) {
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return FALSE;
    }
    
    /* Create datatype object from input file */
    dtObject = NewDTObjectA((APTR)inputFile, TAG_DONE);
    if (!dtObject) {
        errorCode = IoErr();
        if (errorCode == 0) {
            errorCode = ERROR_OBJECT_NOT_FOUND;
        }
        SetIoErr(errorCode);
        return FALSE;
    }
    
    /* Clear selection before writing */
    {
        struct dtGeneral clearMsg;
        clearMsg.MethodID = DTM_CLEARSELECTED;
        clearMsg.dtg_GInfo = NULL;
        DoDTMethodA(dtObject, NULL, NULL, (Msg)&clearMsg);
    }
    
    /* Save the datatype object to file in IFF format using SaveDTObjectA */
    /* SaveDTObjectA opens the file, calls DTM_WRITE, closes the file */
    /* Returns the value returned by DTM_WRITE or NULL for error */
    SetIoErr(0);
    result = SaveDTObjectA(dtObject, NULL, NULL, outputFile, DTWM_IFF, FALSE, TAG_DONE);
    if (!result) {
        /* Failure - get error code and error string */
        errorCode = IoErr();
        if (errorCode != 0) {
            errorString = GetDTString(errorCode);
            if (errorString && *errorString != '\0') {
                /* Error string is available - it will be printed by caller */
                SetIoErr(errorCode);
            } else {
                /* No error string, use the error code */
                SetIoErr(errorCode);
            }
        } else {
            /* No error code set, use generic error */
            SetIoErr(ERROR_WRITE_PROTECTED);
        }
        result = FALSE;
    } else {
        /* Success */
        result = TRUE;
    }
    
    /* Cleanup */
    if (dtObject) {
        DisposeDTObject(dtObject);
    }
    
    return result;
}

/* Print write capabilities of a datatype object */
VOID PrintWriteCapabilities(Object *dtObject)
{
    BOOL supportsWrite = FALSE;
    BOOL supportsIFF = FALSE;
    BOOL supportsRAW = FALSE;
    
    if (!dtObject) {
        return;
    }
    
    /* Check if DTM_WRITE method is supported using FindMethod */
    if (IsDTMethodSupported(dtObject, DTM_WRITE)) {
        supportsWrite = TRUE;
    }
    
    if (!supportsWrite) {
        /* Datatype doesn't support writing at all */
        return;
    }
    
    /* Test which write modes are supported by attempting writes to a temporary file */
    {
        UBYTE tempFileName[64];
        ULONG uniqueID;
        
        /* Generate unique temporary filename */
        uniqueID = GetUniqueID();
        SNPrintf(tempFileName, sizeof(tempFileName), "T:dtwrite%08lX", uniqueID);
        
        /* Clear selection before testing */
        {
            struct dtGeneral clearMsg;
            clearMsg.MethodID = DTM_CLEARSELECTED;
            clearMsg.dtg_GInfo = NULL;
            DoDTMethodA(dtObject, NULL, NULL, (Msg)&clearMsg);
        }
        
        /* Test DTWM_IFF mode using SaveDTObjectA */
        SetIoErr(0);
        if (SaveDTObjectA(dtObject, NULL, NULL, (STRPTR)tempFileName, DTWM_IFF, FALSE, TAG_DONE)) {
            supportsIFF = TRUE;
        }
        /* SaveDTObjectA deletes the file if DTM_WRITE returns 0, so no need to delete manually */
        
        /* Test DTWM_RAW mode */
        SNPrintf(tempFileName, sizeof(tempFileName), "T:dtwrite%08lX", uniqueID);
        SetIoErr(0);
        if (SaveDTObjectA(dtObject, NULL, NULL, (STRPTR)tempFileName, DTWM_RAW, FALSE, TAG_DONE)) {
            supportsRAW = TRUE;
        }
        /* SaveDTObjectA deletes the file if DTM_WRITE returns 0, so no need to delete manually */
    }
    
    /* Print write capabilities */
    if (supportsIFF || supportsRAW) {
        Printf(", Write: ");
        if (supportsIFF && supportsRAW) {
            Printf("IFF, Native");
        } else if (supportsIFF) {
            Printf("IFF");
        } else if (supportsRAW) {
            Printf("Native");
        }
    }
}

/* List available formats for conversion in the same group */
/* Returns the count of available formats */
ULONG ListAvailableFormats(struct DataType *sourceDtn, ULONG groupID)
{
    struct DataType *dtn = NULL;
    struct DataType *prevdtn = NULL;
    ULONG count = 0;
    STRPTR sourceBaseName = NULL;
    
    if (!sourceDtn) {
        return 0;
    }
    
    sourceBaseName = sourceDtn->dtn_Header->dth_BaseName;
    
    Printf("\nAvailable formats for conversion:\n");
    Printf("===================================\n");
    
    /* Enumerate all datatypes in the same group */
    {
        struct TagItem tags[4];
        tags[0].ti_Tag = DTA_DataType;
        tags[0].ti_Data = (ULONG)prevdtn;
        tags[1].ti_Tag = DTA_GroupID;
        tags[1].ti_Data = (ULONG)groupID;
        tags[2].ti_Tag = TAG_DONE;
        
        while ((dtn = ObtainDataTypeA(DTST_RAM, NULL, tags)) != NULL) {
        /* Skip system datatypes and invalid ones */
        if (dtn->dtn_Header->dth_GroupID == GID_SYSTEM || 
            dtn->dtn_Header->dth_GroupID == 0) {
            ReleaseDataType(prevdtn);
            prevdtn = dtn;
            continue;
        }
        
        count++;
        
        /* Print format info */
        {
            STRPTR name = dtn->dtn_Header->dth_Name ? dtn->dtn_Header->dth_Name : (STRPTR)"Unknown";
            STRPTR baseName = dtn->dtn_Header->dth_BaseName ? dtn->dtn_Header->dth_BaseName : (STRPTR)"unknown";
            BOOL isCurrent = (sourceBaseName && Stricmp(sourceBaseName, baseName) == 0);
            
            Printf("  %2lu. %s (%s)", count, name, baseName);
            if (isCurrent) {
                Printf(" [current]");
            }
            Printf("\n");
        }
        
            /* Release previous and keep current */
            if (prevdtn) {
                ReleaseDataType(prevdtn);
            }
            prevdtn = dtn;
            
            /* Update tags for next iteration */
            tags[0].ti_Data = (ULONG)prevdtn;
        }
    }
    
    /* Release last datatype */
    if (prevdtn) {
        ReleaseDataType(prevdtn);
    }
    
    return count;
}

/* Select format from list by prompting user */
struct DataType *SelectFormatFromList(ULONG groupID, LONG *selectedIndex)
{
    struct DataType *dtn = NULL;
    struct DataType *prevdtn = NULL;
    struct DataType *resultDtn = NULL;
    ULONG count = 0;
    LONG selection = 0;
    UBYTE inputBuffer[32];
    LONG inputLen = 0;
    LONG charsConverted = 0;
    
    Printf("\nSelect format number (or 0 to cancel): ");
    
    /* Read user input */
    inputLen = Read(Input(), inputBuffer, sizeof(inputBuffer) - 1);
    if (inputLen > 0) {
        inputBuffer[inputLen] = '\0';
        /* Remove newline if present */
        if (inputBuffer[inputLen - 1] == '\n') {
            inputBuffer[inputLen - 1] = '\0';
            inputLen--;
        }
        /* Convert string to long using StrToLong */
        charsConverted = StrToLong((STRPTR)inputBuffer, &selection);
        if (charsConverted < 0) {
            /* No digits found - treat as 0 (cancel) */
            selection = 0;
        }
    }
    
    if (selection <= 0) {
        return NULL;
    }
    
    /* Find the selected datatype */
    {
        struct TagItem tags[4];
        tags[0].ti_Tag = DTA_DataType;
        tags[0].ti_Data = (ULONG)prevdtn;
        tags[1].ti_Tag = DTA_GroupID;
        tags[1].ti_Data = (ULONG)groupID;
        tags[2].ti_Tag = TAG_DONE;
        
        while ((dtn = ObtainDataTypeA(DTST_RAM, NULL, tags)) != NULL) {
        count++;
        
        /* Skip system datatypes and invalid ones */
        if (dtn->dtn_Header->dth_GroupID == GID_SYSTEM || 
            dtn->dtn_Header->dth_GroupID == 0) {
            ReleaseDataType(prevdtn);
            prevdtn = dtn;
            continue;
        }
        
        if (count == selection) {
            /* Found the selected format - keep it and release others */
            resultDtn = dtn;
            if (selectedIndex) {
                *selectedIndex = selection;
            }
            /* Release previous but keep current */
            if (prevdtn) {
                ReleaseDataType(prevdtn);
            }
            prevdtn = NULL; /* Don't release resultDtn */
            break;
        }
        
            /* Release previous and keep current */
            if (prevdtn) {
                ReleaseDataType(prevdtn);
            }
            prevdtn = dtn;
            
            /* Update tags for next iteration */
            tags[0].ti_Data = (ULONG)prevdtn;
        }
    }
    
    /* Release any remaining datatypes */
    if (prevdtn && prevdtn != resultDtn) {
        ReleaseDataType(prevdtn);
    }
    
    return resultDtn;
}

/* Convert file to specified format */
BOOL ConvertToFormat(STRPTR inputFile, struct DataType *destDtn, STRPTR outputFile)
{
    Object *srcObject = NULL;
    LONG errorCode = 0;
    BOOL result = FALSE;
    ULONG groupID = 0;
    
    if (!inputFile || !destDtn || !outputFile) {
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return FALSE;
    }
    
    groupID = destDtn->dtn_Header->dth_GroupID;
    
    /* Create source object from input file */
    {
        struct TagItem tags[2];
        tags[0].ti_Tag = DTA_GroupID;
        tags[0].ti_Data = (ULONG)groupID;
        tags[1].ti_Tag = TAG_DONE;
        
        srcObject = NewDTObjectA((APTR)inputFile, tags);
    }
    if (!srcObject) {
        errorCode = IoErr();
        if (errorCode == 0) {
            errorCode = ERROR_OBJECT_NOT_FOUND;
        }
        SetIoErr(errorCode);
        return FALSE;
    }
    
    /* Use SaveDTObjectA to save in the target format */
    /* Note: SaveDTObjectA may not support direct format conversion.
     * For full conversion support, group-specific conversion functions
     * would be needed (like DTConvert uses).
     */
    if (SaveDTObjectA(srcObject, NULL, NULL, outputFile, DTWM_RAW, FALSE, TAG_DONE)) {
        result = TRUE;
    } else {
        errorCode = IoErr();
        if (errorCode == 0) {
            errorCode = ERROR_WRITE_PROTECTED;
        }
        SetIoErr(errorCode);
        result = FALSE;
    }
    
    /* Cleanup */
    if (srcObject) {
        DisposeDTObject(srcObject);
    }
    
    return result;
}

/* Check if output file exists and handle FORCE flag */
BOOL CheckOutputFileExists(STRPTR outputFile, BOOL force)
{
    BPTR fileLock = NULL;
    struct FileInfoBlock *fib = NULL;
    BOOL exists = FALSE;
    
    if (!outputFile) {
        return TRUE; /* No output file specified, nothing to check */
    }
    
    /* Try to lock the file to see if it exists */
    fileLock = Lock(outputFile, ACCESS_READ);
    if (fileLock) {
        /* File exists - check if it's a file (not a directory) */
        fib = (struct FileInfoBlock *)AllocVec(sizeof(struct FileInfoBlock), MEMF_CLEAR);
        if (fib) {
            if (Examine(fileLock, fib)) {
                if (fib->fib_DirEntryType == ST_FILE) {
                    exists = TRUE;
                }
            }
            FreeVec(fib);
        }
        UnLock(fileLock);
    }
    
    if (exists && !force) {
        /* File exists and FORCE not specified - error */
        Printf("\nError: Output file already exists: %s\n", outputFile);
        Printf("Use FORCE switch to overwrite existing file\n");
        SetIoErr(ERROR_OBJECT_EXISTS);
        return FALSE;
    }
    
    return TRUE;
}

