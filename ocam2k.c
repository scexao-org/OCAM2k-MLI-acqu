#include "math.h"

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <semaphore.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/mman.h>
#include <sched.h>


//#include <ImageStruct.h>
//#include "/home/scexao/src/Cfits/src/ImageCreate/ImageCreate.h"

#include "/home/scexao/src/cacao/src/ImageStreamIO/ImageStruct.h"
#include "/home/scexao/src/cacao/src/ImageStreamIO/ImageStreamIO.h"


#include "mil.h"
#include "ocam2_sdk.h"


#define SNAME "ocam2ksem"   // semaphore name
IMAGE *imarray;
sem_t *sem;

int RAWSAVEMODE = 0; // 0: save descrambled image, 1: save raw buffer

// Added: Number of Lines per Slice.
int NUM_LINE_SLICE = 4;
int sliceNB;
int NBslices;
long nbpixslice[100];


char const DESCRAMBLE_FILE[] = "ocam2_descrambling.txt";
char const MIL_DCF_BINNING[] = "ocam2_mil_binning.dcf";
#define IMAGE_WIDTH_RAW OCAM2_IMAGE_WIDTH_RAW_BINNING
#define IMAGE_HEIGHT_RAW OCAM2_IMAGE_HEIGHT_RAW_BINNING

int IMAGE_WIDTH = 120;
int IMAGE_HEIGHT = 120;

/* MIL */
MIL_ID MilApplication;        /* Application identifier.  */
MIL_ID MilSystem;             /* System identifier.       */
MIL_ID MilDigitizer;          /* Digitizer identifier.    */
MIL_ID *MilGrabBufferList;    /* Image buffer identifier. */
MIL_ID MilMutex;




/* User's processing function hook data structure. */
typedef struct
{
    MIL_ID MilDigitizer;
    MIL_INT ProcessedImageCount;
    MIL_INT ProcessedSliceCount;
    MIL_INT slice;
    ocam2_id id;
    unsigned short imagearray[OCAM2_PIXELS_IMAGE_BINNING];
    short *dataptr;
    int slice_firstelem[100];
    int slice_lastelem[100];
    short *buff1;
  
    long *imgcnt;

    long framecnt;

    unsigned short *pixr;
    unsigned short *pixi;
    unsigned short *rc;
    
    int hookinit;
    short *pMilGrabBuff;
} HookDataStruct;


void initMilError(char const errorMsg[]);
void initMil();
void exitMil();
/* User's processing function prototype. */
MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType, MIL_ID HookId, void* HookDataPtr);
MIL_INT MFTYPE Grab_nth_Line(MIL_INT HookType, MIL_ID EventId, void* HookDataPtr);


/* Number of images in the buffering grab queue.
Generally, increasing this number gives a better real-time grab.
*/
#define BUFFERING_SIZE_MAX 22


ocam2_id ocamid;






//  clock_gettime(CLOCK_REALTIME, &tnow);

/* tdiff = info_time_diff(data.image[aoconfID_looptiming].md[0].wtime, tnow);
            tdiffv = 1.0*tdiff.tv_sec + 1.0e-9*tdiff.tv_nsec;
            data.image[aoconfID_looptiming].array.F[20] = tdiffv;
*/



struct timespec info_time_diff(struct timespec start, struct timespec end)
{
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}




















int OcamInit()
{
    ocam2_rc rc;

    rc = ocam2_init(OCAM2_BINNING, DESCRAMBLE_FILE, &ocamid);

    if (OCAM2_OK == rc)
    {
        printf("ocam2_init: success, get id:%d\n", ocamid);
        printf("Mode is: %s \n", ocam2_modeStr(ocam2_getMode(ocamid)));

    }
    else
        exit (EXIT_FAILURE);

    return(0);
}





void initMilError(char const errorMsg[])
{
    printf("MIL init ERROR: %s\n", errorMsg);
    exitMil();
    exit (EXIT_FAILURE);
}



void exitMil()
{
    long i;

    for(i=0; i<BUFFERING_SIZE_MAX; i++)
    {
        if (M_NULL!=MilGrabBufferList[i])
            MbufFree(MilGrabBufferList[i]);
    }

    free(MilGrabBufferList);

    if (M_NULL!=MilDigitizer)
        MdigFree(MilDigitizer);
    if (M_NULL!=MilMutex)
        MthrFree(MilMutex);
    if (M_NULL!=MilSystem)
        MsysFree(MilSystem);
    if (M_NULL!=MilApplication)
        MappFree(MilApplication);
}





int main(int argc, char *argv[])
{
    ocam2_rc rc;
//    MIL_INT MilGrabBufferListSize;
    MIL_INT ProcessFrameCount = 0;
    MIL_DOUBLE ProcessFrameRate = 0;
//    MIL_INT NbFrames = 0, n = 0;
    HookDataStruct UserHookData;
    MIL_INT64 BufSizeX;
    MIL_INT64 BufSizeY;
    int i;
    unsigned int number;

    int naxis = 3;
	uint8_t atype;
    uint32_t *imsize;
    int NBkw;

    long ii, jj;
    short *buffraw1;
//    short *buffraw2;
    short *buffim1;
//    short *buffim2;

    long cntpix;
    long maxcnt;
    
    FILE *fp;
    
    int RT_priority = 80; //any number from 0-99
    struct sched_param schedpar;


	jj = 0;

    if (argc != 2)
    {
        printf("%s takes 1 argument: Number of frame grabber lines per read\n", argv[0]);
        exit(0);
    }
    else
    {
        NUM_LINE_SLICE = atoi(argv[1]);
        printf("NUM_LINE_SLICE = %d\n", NUM_LINE_SLICE);
    }


    schedpar.sched_priority = RT_priority;
    sched_setscheduler(0, SCHED_FIFO, &schedpar); //other option is SCHED_RR, might be faster

  
    imarray = (IMAGE*) malloc(sizeof(IMAGE)*100); // 100 images max

    imsize = (uint32_t*) malloc(sizeof(uint32_t)*3);
    if(RAWSAVEMODE==1)
    {
        imsize[0] = OCAM2_IMAGE_WIDTH_RAW_BINNING;
        imsize[1] = OCAM2_IMAGE_HEIGHT_RAW_BINNING;
    }
    else
    {
        imsize[0] = IMAGE_WIDTH;
        imsize[1] = IMAGE_HEIGHT;
    }
    //  size[2] = 5;  // circular buffer depth


	atype = _DATATYPE_UINT16;
	NBkw = 0;
//	ImageCreate(&imarray[0], "ocam2k", 2, imsize, atype, 1, NBkw);
	ImageStreamIO_createIm(&imarray[0], "ocam2k", 2, imsize, atype, 1, NBkw);
	
    //create_image_shm(0, "ocam2k", 2, size, USHORT, 0);
    //COREMOD_MEMORY_image_set_createsem(0, 2);
 
    imarray[0].md[0].status=1;


    if ((sem = sem_open(SNAME, O_CREAT, 0644, 1)) == SEM_FAILED) {
        perror("semaphore initilization");
        exit(1);
    }
    UserHookData.dataptr = (uint16_t*) imarray[0].array.UI16;
    UserHookData.imgcnt = &imarray[0].md[0].cnt0;


    printf("\nOcam2 sdk version:%s  build:%s\n",ocam2_sdkVersion(), ocam2_sdkBuild());
    OcamInit();
    UserHookData.id = ocamid;

    printf("OCAM2_IMAGE_WIDTH_RAW_BINNING = %d\n", OCAM2_IMAGE_WIDTH_RAW_BINNING);
    printf("OCAM2_IMAGE_HEIGHT_RAW_BINNING = %d\n", OCAM2_IMAGE_HEIGHT_RAW_BINNING);
    printf("OCAM2_PIXELS_IMAGE_BINNING = %d\n", OCAM2_PIXELS_IMAGE_BINNING);

    printf("OCAM2_IMAGE_NB_OFFSET = %d\n", OCAM2_IMAGE_NB_OFFSET);





    // pixel mapping

    NBslices = OCAM2_IMAGE_HEIGHT_RAW_BINNING/NUM_LINE_SLICE/2+1;
    if(NBslices>31)
        NBslices = 31;
    printf("NBslices = %d  ( %d / %d)\n", NBslices, OCAM2_IMAGE_HEIGHT_RAW_BINNING, NUM_LINE_SLICE);
    printf("%d raw pixel per slice\n", NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
    fflush(stdout);
    if(1)
    {
        buffraw1 = (short*) malloc(sizeof(short)*OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING);
        buffim1 = (short*) malloc(sizeof(short)*IMAGE_WIDTH*IMAGE_HEIGHT);

        for(ii=0; ii<OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING; ii++)
            buffraw1[ii] = ii;
        maxcnt = 10;
        ocam2_descramble(UserHookData.id, &number, buffim1, buffraw1);


        for(sliceNB=0; sliceNB<NBslices; sliceNB++)
            {
                nbpixslice[sliceNB] = 0;
                UserHookData.slice_firstelem[sliceNB] = 100000000;
                UserHookData.slice_lastelem[sliceNB] = 0;
            }
        for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
        {
            sliceNB = buffim1[ii]/(NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
            if(sliceNB<NBslices)
                nbpixslice[sliceNB]++;
        }
        cntpix = 0;
        maxcnt = 0;
        if ((fp=fopen("pixsliceNB.txt", "w"))==NULL)
        {
            printf("ERROR: cannot create file pixsliceNB.txt\n");
            exit(0);
        }
        for(sliceNB=0; sliceNB<NBslices; sliceNB++)
        {
            if(nbpixslice[sliceNB]>maxcnt)
                maxcnt = nbpixslice[sliceNB];
            cntpix += nbpixslice[sliceNB];
          //  printf("Slice %2d : %5ld pix    [cumul = %5ld]\n", sliceNB, nbpixslice[sliceNB], cntpix);
            fprintf(fp, "%d %ld %ld\n", sliceNB, nbpixslice[sliceNB], cntpix);
        }
        fclose(fp);
        
        naxis = 3;

        imsize[0] = (uint32_t) (sqrt(maxcnt)+1.0);
        imsize[1] = (uint32_t) (sqrt(maxcnt)+1.0);
        imsize[2] = (uint32_t) NBslices;

        printf("FORMAT = %ld %ld %ld\n", (long) imsize[0], (long) imsize[1], (long) imsize[2]);

		atype = _DATATYPE_UINT16;		
		NBkw = 0;

		ImageStreamIO_createIm(&imarray[1], "ocam2krc", naxis, imsize, atype, 1, NBkw); // raw cube (data)
		
		
        //create_image_shm(1, "ocam2krc", naxis, size, USHORT, 0); // raw cube (data)
        //COREMOD_MEMORY_image_set_createsem(1, 2);
        
        ImageStreamIO_createIm(&imarray[2], "ocam2kpixr", naxis, imsize, atype, 1, NBkw); // pixel mapping: index in raw
        //create_image_shm(2, "ocam2kpixr", naxis, size, USHORT, 0); // pixel mapping: index in raw
        //COREMOD_MEMORY_image_set_createsem(2, 2);
                
        ImageStreamIO_createIm(&imarray[3], "ocam2kpixi", naxis, imsize, atype, 1, NBkw);  // pixel mapping: index in image
        //create_image_shm(3, "ocam2kpixi", naxis, size, USHORT, 0); // pixel mapping: index in image
        //COREMOD_MEMORY_image_set_createsem(3, 2);
 
        UserHookData.rc = imarray[1].array.UI16;
        UserHookData.pixr = imarray[2].array.UI16;
        UserHookData.pixi = imarray[3].array.UI16;

        for(sliceNB=0; sliceNB<NBslices; sliceNB++)
            nbpixslice[sliceNB] = 0;
            
        for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
        {
            sliceNB = buffim1[ii]/(NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
            jj = imsize[0]*imsize[1]*sliceNB+nbpixslice[sliceNB];
            imarray[2].array.UI16[jj] = buffim1[ii];
            imarray[3].array.UI16[jj] = ii;
            if(buffim1[ii]<UserHookData.slice_firstelem[sliceNB])
                UserHookData.slice_firstelem[sliceNB] = buffim1[ii];
             if(buffim1[ii]>UserHookData.slice_lastelem[sliceNB])
                UserHookData.slice_lastelem[sliceNB] = buffim1[ii];
                
            nbpixslice[sliceNB]++;
        }

        for(ii=0; ii<OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING; ii++)
            buffraw1[ii] = 0;
        ocam2_descramble(UserHookData.id, &number, buffim1, buffraw1);


        for(sliceNB=0; sliceNB<NBslices; sliceNB++)
            printf("Slice %2d : %5ld pix    [cumul = %5ld]   raw pix   : %6d -> %6d\n", sliceNB, nbpixslice[sliceNB], cntpix, UserHookData.slice_firstelem[sliceNB], UserHookData.slice_lastelem[sliceNB]);

        free(buffraw1);
        free(buffim1);
    }


    UserHookData.buff1 = (short*) malloc(sizeof(short)*(OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING*NBslices+12));
    UserHookData.framecnt = 0;
    UserHookData.hookinit = 0;

    printf("Setting up frame grabber step 00\n");
    fflush(stdout);

    MilGrabBufferList = (MIL_ID*) malloc(sizeof(MIL_ID)*BUFFERING_SIZE_MAX);
    if(MilGrabBufferList==NULL)
        initMilError("CANNOT ALLOCATE MilGrabBufferList");

    printf("Setting up frame grabber step 01\n");
    fflush(stdout);

    /* Allocate a MIL application. */
    if (M_NULL == MappAlloc(MIL_TEXT("M_DEFAULT"), M_DEFAULT, &MilApplication))
        initMilError("MappAlloc failed !");

    printf("Setting up frame grabber step 02\n");
    fflush(stdout);


    /* Allocate a MIL system. */
    if (M_NULL == MsysAlloc(M_DEFAULT, MT("M_DEFAULT"), M_DEFAULT, M_DEFAULT, &MilSystem))
        initMilError("MsysAlloc failed !");

    /* Allow MIL to call ProcessingFunction in several thread. */
    MsysControl(MilSystem, M_MODIFIED_BUFFER_HOOK_MODE, M_MULTI_THREAD + BUFFERING_SIZE_MAX);


    /* Allocate a MIL digitizer if supported and sets the target image size. */
    if (MsysInquire(MilSystem, M_DIGITIZER_NUM, M_NULL) > 0)
    {
        if (M_NULL == MdigAlloc(MilSystem, M_DEFAULT, "ocam2_mil_binning.dcf", M_DEFAULT, &MilDigitizer))
            initMilError("MdigAlloc failed !");

        MdigInquire(MilDigitizer, M_SIZE_X, &BufSizeX);
        MdigInquire(MilDigitizer, M_SIZE_Y, &BufSizeY);

        printf("Frame grabber image size : %ld %ld\n", BufSizeX, BufSizeY);
        fflush(stdout);
        if ( (IMAGE_WIDTH_RAW!=BufSizeX) || (IMAGE_HEIGHT_RAW!=BufSizeY) )
            initMilError("Dcf file informations(height or width) invalid !");
    }
    else
        initMilError("Can't find a grabber, exiting...");

    /* Allocate the grab buffers and clear them. */
    for(i = 0; i<BUFFERING_SIZE_MAX; i++)
    {
        MbufAlloc2d(MilSystem,
                    MdigInquire(MilDigitizer, M_SIZE_X, M_NULL),
                    MdigInquire(MilDigitizer, M_SIZE_Y, M_NULL),
                    MdigInquire(MilDigitizer, M_TYPE, M_NULL),
                    M_IMAGE+M_GRAB,
                    &MilGrabBufferList[i]);

        if (MilGrabBufferList[i])
        {
            MbufClear(MilGrabBufferList[i], 0xff);
        }
        else
            initMilError("MbufAlloc2d failed !");
    }

    /* MIL event allocation for grab end hook. */
    if (M_NULL ==  MthrAlloc(MilSystem, M_MUTEX, M_DEFAULT, M_NULL, M_NULL, &MilMutex))
        initMilError("MthrAlloc failed !");

    /* Print a message. */
    MosPrintf(MIL_TEXT("\nMULTIPLE BUFFERED PROCESSING.\n"));
    MosPrintf(MIL_TEXT("-----------------------------\n\n"));
    MosPrintf(MIL_TEXT("00 Press <Enter> to start processing.\r"));

    printf(" =========== %ld %ld\n\n\n\n", (long) imarray[1].md[0].size[0], (long) imarray[1].md[0].size[1]);
    fflush(stdout);
    /* Grab continuously on the display and wait for a key press. */
    /*  MdigGrabContinuous(MilDigitizer, MilImageDisp);
    MosGetch();
    */
    /* Halt continuous grab. */
    // MdigHalt(MilDigitizer);

    /* Initialize the user's processing function data structure. */
    UserHookData.MilDigitizer = MilDigitizer;
    UserHookData.ProcessedImageCount = 0;



    for(sliceNB=0; sliceNB<NBslices; sliceNB++)
        MdigHookFunction(MilDigitizer, M_GRAB_LINE_END+((NUM_LINE_SLICE)*sliceNB), Grab_nth_Line, &UserHookData);
    



    /* Start the processing. The processing function is called with every frame grabbed. */
//    MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, M_START, M_DEFAULT, ProcessingFunction, &UserHookData);
	
    /* Print a message and wait for a key press after a minimum number of frames. */
//    MosPrintf(MIL_TEXT("01 Press <Enter> to stop. \n\n"));
//    MosGetch();

    /* Stop the processing. */
//    MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, M_STOP, M_DEFAULT, ProcessingFunction, &UserHookData);


	
	
	
	int loopcnt = 0;
	int CONTmode = 1;	
	while ( CONTmode == 1 )
	{
		printf("\n\n");
		
		char fname[500];
		FILE *fpcont;
		sprintf(fname, "/home/scexao/ocam2kmode_cont.txt");
		if( (fpcont=fopen(fname,"r")) == NULL)
		{
			CONTmode = 0;
		}
		else
		{
			CONTmode = 1;
			fclose(fpcont);
			
			// WAIT FOR RESTART FILE 
			int restartacqu = 0;
			long cnt = 0;
			while ( restartacqu == 0 )
			{
				usleep(100000);
				char fnameacqr[500];
				FILE *fpacqr;
				sprintf(fnameacqr, "/home/scexao/ocam2kmode_restart.txt");
				if( (fpacqr = fopen(fnameacqr, "r")) == NULL )
				{
					printf("\r [%10ld] Waiting for restart file     ", cnt);
					cnt++;
				}
				else
				{
					printf("\n\n");
					restartacqu = 1;
					system("rm /home/scexao/ocam2kmode_restart.txt");
				}
			}
		}	
		
/*		printf("LOOP %d\n", loopcnt);
		MosPrintf(MIL_TEXT("02 Press <Enter> to restart. \n\n"));
		MosGetch();
  */  
    
		/* Start the processing. The processing function is called with every frame grabbed. */
		MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, M_START, M_DEFAULT, ProcessingFunction, &UserHookData);
		
		MosPrintf(MIL_TEXT("03 Press <Enter> to re-loop. \n\n"));
		MosGetch();		
		
		/* Stop the processing. */
		MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, M_STOP, M_DEFAULT, ProcessingFunction, &UserHookData);
		
		loopcnt++;
	}



	/* Print a message and wait for a key press after a minimum number of frames. */
	MosPrintf(MIL_TEXT("04 Press <Enter> to stop. \n\n"));
	MosGetch();

	/* Stop the processing. */
	MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, M_STOP, M_DEFAULT, ProcessingFunction, &UserHookData);








    /* Print statistics. */
    MdigInquire(MilDigitizer, M_PROCESS_FRAME_COUNT, &ProcessFrameCount);
    MdigInquire(MilDigitizer, M_PROCESS_FRAME_RATE, &ProcessFrameRate);
    MosPrintf(MIL_TEXT("\n\n%ld frames grabbed at %.1f frames/sec (%.1f ms/frame).\n"),
              ProcessFrameCount, ProcessFrameRate, 1000.0/ProcessFrameRate);
    MosPrintf(MIL_TEXT("05 Press <Enter> to end.\n\n"));
    MosGetch();



    exitMil();

    rc = ocam2_exit(ocamid);
    if (OCAM2_OK == rc)
        printf("ocam2_exit: success (ocamid:%d)\n", ocamid);

    free(imarray);
    free(imsize);
    return 0;
}



/* User's processing function called every time a grab buffer is ready. */
/* -------------------------------------------------------------------- */

/* Local defines. */
#define STRING_LENGTH_MAX 20

MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType, MIL_ID HookId, void* HookDataPtr)
{
    // HookDataStruct *UserHookDataPtr = (HookDataStruct *)HookDataPtr;
    //MIL_ID ModifiedBufferId;
   // MIL_TEXT_CHAR Text[STRING_LENGTH_MAX]= {MIL_TEXT('\0'),};
    //short *pMilGrabBuff;
    //long ii, sliceii;
    //double total;
   // long slice;
    
  //  MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID, &ModifiedBufferId);
   // MbufInquire(ModifiedBufferId, M_HOST_ADDRESS, &pMilGrabBuff);

  //  UserHookDataPtr->slice = 0;

 //   UserHookDataPtr->framecnt ++;


/*
    image[0].md[0].write = 1;
    image[0].md[0].cnt0 ++;
   for(slice=0;slice<NBslices;slice++)
   {
       sliceii = slice*image[1].md[0].size[0]*image[1].md[0].size[1];
    for(ii=0;ii<nbpixslice[slice];ii++)
        image[0].array.U[ image[3].array.U[sliceii + ii] ] = image[1].array.U[sliceii + ii];
   }
    image[0].md[0].cnt1 = slice;
    sem_post(image[0].semptr[0]);
    image[0].md[0].write = 0;

    UserHookDataPtr->imgcnt[0]++;
    total = 0.0;
    for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
        total += UserHookDataPtr->dataptr[ii];    
    UserHookDataPtr->ProcessedImageCount++;
*/

    /* Print and draw the frame count (remove to reduce CPU usage). */
    //MosPrintf(MIL_TEXT(" ---- Processed frame #%d \n"), UserHookDataPtr->framecnt);
 //   MosSprintf(Text, STRING_LENGTH_MAX, MIL_TEXT("%ld"), UserHookDataPtr->ProcessedImageCount);

    return 0;
}





MIL_INT MFTYPE Grab_nth_Line(MIL_INT HookType, MIL_ID HookId, void* HookDataPtr)
{
    MIL_ID ModifiedBufferId;
    //   short *pMilGrabBuff;
    int slice;
    long ii;
    int sliceii; //, slicejj;
    HookDataStruct *UserHookDataPtr = (HookDataStruct *)HookDataPtr;
    int offset, bsize;


   // if(UserHookDataPtr->hookinit==0)
   // {
        /* Retrieve the MIL_ID of the grabbed buffer. */
        MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID, &ModifiedBufferId);
        MbufInquire(ModifiedBufferId, M_HOST_ADDRESS, &UserHookDataPtr->pMilGrabBuff);
    //    UserHookDataPtr->hookinit = 1;
   // }

    slice = UserHookDataPtr->slice;
    offset = UserHookDataPtr->slice_firstelem[slice];
    bsize = UserHookDataPtr->slice_lastelem[slice]-UserHookDataPtr->slice_firstelem[slice];
    bsize += 2;
   
    //if(slice<NBslices)
    memcpy(UserHookDataPtr->buff1+offset, UserHookDataPtr->pMilGrabBuff+offset, sizeof(short)*bsize);
    //memcpy(UserHookDataPtr->buff1+offset, pMilGrabBuff+offset, OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING);

    //UserHookData.slice_firstelem[sliceNB], UserHookData.slice_lastelem[sliceNB]);

    sliceii = slice*imarray[1].md[0].size[0]*imarray[1].md[0].size[1];
    //slicejj = slice*OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING;
     //MosPrintf(MIL_TEXT("[%d  (%5ld-%5ld) ->] "), slice, sliceii, sliceii+nbpixslice[slice]);
   
	imarray[1].md[0].write = 1;
    imarray[1].md[0].cnt0 ++;
    for(ii=0; ii<nbpixslice[slice]; ii++)
        imarray[1].array.UI16[sliceii + ii] = UserHookDataPtr->buff1[imarray[2].array.UI16[sliceii + ii]];
    imarray[1].md[0].cnt1 = slice;
    sem_post(imarray[1].semptr[0]);
   // sem_post(image[1].semptr[1]);
    /*if(slice==NBslices)
        {
            sem_post(image[1].semptr[0]);
            image[1].md[0].cnt0 ++;
        }*/
    imarray[1].md[0].write = 0;
    //MosPrintf(MIL_TEXT("[->%d] "), slice);
    /* Increment the frame counter. */
    UserHookDataPtr->ProcessedImageCount++;

    if(slice==NBslices-1)
    {
        UserHookDataPtr->slice = 0;
        sem_post(imarray[1].semptr[1]);
        UserHookDataPtr->framecnt ++;
        MosPrintf(MIL_TEXT(" ---- Processed frame #%d   %d\r"), UserHookDataPtr->framecnt, M_BUFFER_ID);
    }
    else
        UserHookDataPtr->slice++;

    return(0);
}



