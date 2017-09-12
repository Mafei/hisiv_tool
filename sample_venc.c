#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"

#define	MAX_FILE_NAME_LENGTH	256

typedef struct HiMppTestCmd_t {
	HI_U8 		file_input[MAX_FILE_NAME_LENGTH];
	HI_U8		file_output[MAX_FILE_NAME_LENGTH];
	HI_U32		enc_mode;
	HI_U32		fix_qp;
	HI_U32		width;
	HI_U32		height;
	HI_U32    vframes; // cnt of frames
}HiMppTestCmd;

char file_name[250];
int iFix_qp;

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;

static void yuv420to420sp(HI_U8 *src, HI_U8 *dst, HI_U32 width, HI_U32 height)
{
	HI_U32 i, j;
	HI_U32 size = width * height;
	
	HI_U8 *y = src;
	HI_U8 *u = src + size;
	HI_U8 *v = src + size * 5 / 4;
	
	HI_U8 *y_tmp  = dst;
	HI_U8 *uv_tmp = dst + size; 

	memcpy(y_tmp, y, size);
	
	for(j = 0, i = 0; j < size /2; j += 2, i++) {
		uv_tmp[j]   = v[i];
		uv_tmp[j+1] = u[i]; 
	}
}

/******************************************************************************
* funciton : get stream from each channels and save them
******************************************************************************/
HI_VOID* SAMPLE_COMM_VENC_GetVencStreamProcEx(HiMppTestCmd *cmd)
{
    HI_S32 i;
    VENC_CHN_ATTR_S stVencChnAttr;
    SAMPLE_VENC_GETSTREAM_PARA_S* pstPara;
    HI_S32 maxfd = 0;
    struct timeval TimeoutVal;
    fd_set read_fds;
    HI_S32 VencFd;
    FILE* pFile1, *pFile2;
    VENC_CHN_STAT_S stStat;
    VENC_STREAM_S stStream;
    HI_S32 s32Ret ,frame = 0, eos = 0;
    VENC_CHN VencChn = 0;
    PAYLOAD_TYPE_E enPayLoadType;
	
	if(cmd->enc_mode == 0)
		enPayLoadType = PT_H264;
	else if(cmd->enc_mode == 1)
		enPayLoadType = PT_H265;
	else
	{
		SAMPLE_PRT("unknown payload type\n");
		return -1;
	}
	
    /******************************************
     step 1:  check & prepare save-file & venc-fd
    ******************************************/
    /* decide the stream file name, and open file to save stream */
    /* Set Venc Fd. */
	pFile1 = fopen(cmd->file_output,"wb+");
	if(pFile1 == NULL)
	{
		printf("failed to open output file %s\n", cmd->file_output);
		return -1;
	}
	
	pFile2 = fopen(cmd->file_input,"r+");
	if(pFile2 == NULL)
	{
		printf("failed to open input file %s\n", cmd->file_input);
		return -1;
	}
	
    VencFd = HI_MPI_VENC_GetFd(VencChn);
    if (VencFd < 0)
    {
        SAMPLE_PRT("HI_MPI_VENC_GetFd failed with %#x!\n",VencFd);
        return NULL;
    }
	
	VB_BLK handleY = VB_INVALID_HANDLE;
	HI_U32 phyYaddr,*pVirYaddr;
	HI_U8  *tmp;
	VIDEO_FRAME_INFO_S *pstFrame = malloc(sizeof(VIDEO_FRAME_INFO_S));
	
	tmp = malloc(cmd->width * cmd->height * 3 / 2);
	if(!tmp) {
		SAMPLE_PRT("failed to malloc tmp buffer\n");
		return -1;
	}
    /******************************************
     step 2:  Start to get streams of each channel.
    ******************************************/
    while (!eos)
    {
		
		/* 分配物理buffer并且映射到用户空间 */
		do
		{
			handleY = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, cmd->width * cmd->height * 3 / 2, NULL);
		}
		while (VB_INVALID_HANDLE == handleY);	
		if( handleY == VB_INVALID_HANDLE)
		{
			printf("getblock for y failed\n");
			return -1;
		}
		
		VB_POOL poolID =  HI_MPI_VB_Handle2PoolId (handleY);

		phyYaddr = HI_MPI_VB_Handle2PhysAddr(handleY);
		if( phyYaddr == 0)
		{
			printf("HI_MPI_VB_Handle2PhysAddr for handleY failed\n");
			return -1;
		}
		
		pVirYaddr = (HI_U8*) HI_MPI_SYS_Mmap(phyYaddr, cmd->width * cmd->height * 3 / 2);
		
		/* 图像帧结构初始化 */
		memset(&(pstFrame->stVFrame),0x00,sizeof(VIDEO_FRAME_S));
		
		pstFrame->stVFrame.u32Width = cmd->width;
		pstFrame->stVFrame.u32Height = cmd->height;
		pstFrame->stVFrame.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		pstFrame->u32PoolId = poolID;
		pstFrame->stVFrame.u32PhyAddr[0] = phyYaddr;
		pstFrame->stVFrame.u32PhyAddr[1] = phyYaddr + cmd->width * cmd->height;
		
		pstFrame->stVFrame.pVirAddr[0] = pVirYaddr;
		pstFrame->stVFrame.pVirAddr[1] = pVirYaddr + cmd->width * cmd->height;
		
		pstFrame->stVFrame.u32Stride[0] = cmd->width ;
		pstFrame->stVFrame.u32Stride[1] = cmd->width ;
		pstFrame->stVFrame.u32Field     = VIDEO_FIELD_FRAME;
		
		pstFrame->stVFrame.enCompressMode = COMPRESS_MODE_NONE;
		pstFrame->stVFrame.enVideoFormat  = VIDEO_FORMAT_LINEAR;
		pstFrame->stVFrame.u64pts     = frame * 40;
        pstFrame->stVFrame.u32TimeRef = frame * 2;


		/*  从原始文件读取yuv420sp帧，然后调用给编码接口 */
		//s32Ret = fread(pVirYaddr,1920 * 1080 * 3 / 2, 1, pFile2);
		s32Ret = fread(tmp,cmd->width * cmd->height * 3 / 2, 1, pFile2);
		if(s32Ret < 0)
		{
			printf("fread yuv420sp failed\n");
			return -1;
		} else if(s32Ret == 0) {
			eos = 1;
			continue;
		}
		
		yuv420to420sp(tmp, pVirYaddr, cmd->width, cmd->height);
	
		/* 开始发送读到的数据到编码接口 */
		s32Ret = HI_MPI_VENC_SendFrame(VencChn, pstFrame, 50);
		if(s32Ret != 0)
		{
			printf("HI_MPI_VENC_SendFrame failed %d\n",s32Ret);
			return -1;
		}
			
        FD_ZERO(&read_fds);
        FD_SET(VencFd, &read_fds);
  
        TimeoutVal.tv_sec  = 2;
        TimeoutVal.tv_usec = 0;
        s32Ret = select(VencFd + 1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0)
        {
            SAMPLE_PRT("select failed!\n");
            break;
        }
        else if (s32Ret == 0)
        {
            SAMPLE_PRT("get venc stream time out, exit thread\n");
            continue;
        }
        else
        {
        
            if (FD_ISSET(VencFd, &read_fds))
            {
				// printf("select has data can read\n");
                /*******************************************************
                 step 2.1 : query how many packs in one-frame stream.
                *******************************************************/
                memset(&stStream, 0, sizeof(stStream));
                s32Ret = HI_MPI_VENC_Query(VencChn, &stStat);
                if (HI_SUCCESS != s32Ret)
                {
                    SAMPLE_PRT("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", VencChn, s32Ret);
                    break;
                }
				
				/*******************************************************
				step 2.2 :suggest to check both u32CurPacks and u32LeftStreamFrames at the same time,for example:
				 if(0 == stStat.u32CurPacks || 0 == stStat.u32LeftStreamFrames)
				 {
					SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
					continue;
				 }
				*******************************************************/
				if(0 == stStat.u32CurPacks)
				{
					  SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
					  continue;
				}
                /*******************************************************
                 step 2.3 : malloc corresponding number of pack nodes.
                *******************************************************/
                stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
                if (NULL == stStream.pstPack)
                {
                    SAMPLE_PRT("malloc stream pack failed!\n");
                    break;
                }

                /*******************************************************
                 step 2.4 : call mpi to get one-frame stream
                *******************************************************/
                stStream.u32PackCount = stStat.u32CurPacks;
                s32Ret = HI_MPI_VENC_GetStream(VencChn, &stStream, HI_TRUE);
                if (HI_SUCCESS != s32Ret)
                {
                    free(stStream.pstPack);
                    stStream.pstPack = NULL;
                    SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", \
                               s32Ret);
                    break;
                }
                //printf("HI_MPI_VENC_GetStream ok \n");
                /*******************************************************
                 step 2.5 : save frame to file
                *******************************************************/
                s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType, pFile1, &stStream);
                if (HI_SUCCESS != s32Ret)
                {
                    free(stStream.pstPack);
                    stStream.pstPack = NULL;
                    SAMPLE_PRT("save stream failed!\n");
                    break;
                }
                /*******************************************************
                 step 2.6 : release stream
                *******************************************************/
                s32Ret = HI_MPI_VENC_ReleaseStream(VencChn, &stStream);
                if (HI_SUCCESS != s32Ret)
                {
                    free(stStream.pstPack);
                    stStream.pstPack = NULL;
                    break;
                }
                /*******************************************************
                 step 2.7 : free pack nodes
                *******************************************************/
                free(stStream.pstPack);
                stStream.pstPack = NULL;
				
				/* 释放掉获取的vb物理地址和虚拟地址 */
			    HI_MPI_SYS_Munmap(pVirYaddr, cmd->width * cmd->height * 3 / 2);
                HI_MPI_VB_ReleaseBlock(handleY);
				handleY = VB_INVALID_HANDLE;
            }
            
        }
		
      SAMPLE_PRT("encode frame :%d\n", frame++);
      if(frame >= cmd->vframes) {
        eos = 1;
        SAMPLE_PRT("encode get eos.\n");
      }
    }

    /*******************************************************
    * step 3 : close save-file
    *******************************************************/
    //fclose(VencChn);
	fclose(pFile1);
	fclose(pFile2);
	
	SAMPLE_PRT("success to encode %d frames\n", frame);
    return HI_SUCCESS;
}



/******************************************************************************
* function : to process abnormal case
******************************************************************************/
void SAMPLE_VENC_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_ISP_Stop();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}

/******************************************************************************
* function : to process abnormal case - the case of stream venc
******************************************************************************/
void SAMPLE_VENC_StreamHandleSig(HI_S32 signo)
{

    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    exit(0);
}

HI_S32 SAMPLE_VENC_1080P_CLASSIC(HiMppTestCmd *cmd)
{
    PAYLOAD_TYPE_E enPayLoad;
	if(cmd->enc_mode == 0) {
		enPayLoad = PT_H264;
	} else {
		enPayLoad = PT_H265;
	}	
    PIC_SIZE_E enSize;
	HI_U32 u32Profile = 0;
	
    VB_CONF_S stVbConf;
    SAMPLE_VI_CONFIG_S stViConfig = {0};
    
    VPSS_GRP VpssGrp;
    VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_CHN_MODE_S stVpssChnMode;
    
    VENC_CHN VencChn;
    SAMPLE_RC_E enRcMode= SAMPLE_RC_FIXQP;
	
    HI_S32 s32ChnNum=1;
    
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;
    char c;

	switch(cmd->width) {
		case 3840 : 
			if(cmd->height == 2160)
				enSize = PIC_UHD4K;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 2592 : 
			if(cmd->height == 1944)
				enSize = PIC_5M;
			else if(cmd->height == 1520)
				enSize = PIC_2592x1520;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 2304 : 
			if(cmd->height == 1296)
				enSize = PIC_2304x1296;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 1920 : 
			if(cmd->height == 1080)
				enSize = PIC_HD1080;
			else if(cmd->height == 1200)
				enSize = PIC_WUXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 1280 : 
			if(cmd->height == 720)
				enSize = PIC_HD720;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 2560 : 
			if(cmd->height == 1600)
				enSize = PIC_WQXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;	
		case 1680 : 
			if(cmd->height == 1050)
				enSize = PIC_WSXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;	
		case 854 : 
			if(cmd->height == 480)
				enSize = PIC_WVGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 2048 : 
			if(cmd->height == 1536)
				enSize = PIC_QXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 1600 : 
			if(cmd->height == 1200)
				enSize = PIC_UXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 1400 : 
			if(cmd->height == 1050)
				enSize = PIC_SXGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 1024 : 
			if(cmd->height == 768)
				enSize = PIC_XGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 640 : 
			if(cmd->height == 480)
				enSize = PIC_VGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 320 : 
			if(cmd->height == 240)
				enSize = PIC_QVGA;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 352 : 
			if(cmd->height == 288)
				enSize = PIC_CIF;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 720 : 
			if(cmd->height == 480)
				enSize = PIC_720x480;
			else {
				SAMPLE_PRT("unknow fmt width:%d height:%d\n", cmd->width, cmd->height);
				return -1;
			}
			break;
		case 832:
			if(cmd->height == 480)
				enSize = PIC_832x480;
			break;
		case 416:
			if(cmd->height == 240)
				enSize = PIC_416x240;
			break;
		case 768:
		  if(cmd->height == 432)
		    enSize = PIC_768x432;
		  break;
	}

	SAMPLE_PRT("fmt width:%d height:%d\n", cmd->width, cmd->height);
	
    /******************************************
     step  1: init sys variable 
    ******************************************/
    memset(&stVbConf,0,sizeof(VB_CONF_S));
	
    stVbConf.u32MaxPoolCnt = 128;
	u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
	enSize, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
	
	stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	stVbConf.astCommPool[0].u32BlkCnt = 20;
    printf("--------blksize = %d----------------\n",u32BlkSize);

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_VENC_1080P_CLASSIC_0;
    }

    /******************************************
     step 5: start stream venc
    ******************************************/
	VencChn = 0;
	s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad,\
	                               gs_enNorm, enSize, enRcMode,u32Profile, cmd->fix_qp);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Start Venc failed!\n");
	    goto END_VENC_1080P_CLASSIC_5;
	}
    /******************************************
     step 6: stream venc process -- get stream, then save it to file. 
    ******************************************/
	s32Ret = SAMPLE_COMM_VENC_GetVencStreamProcEx(cmd);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed---!\n");
        goto END_VENC_1080P_CLASSIC_5;
    }
    /******************************************
     step 7: exit process
    ******************************************/
    SAMPLE_COMM_VENC_StopGetStream();
    
END_VENC_1080P_CLASSIC_5:
	SAMPLE_COMM_VENC_Stop(VencChn);

END_VENC_1080P_CLASSIC_4:	//vpss stop

END_VENC_1080P_CLASSIC_3:    //vpss stop       
END_VENC_1080P_CLASSIC_2:    //vpss stop   
END_VENC_1080P_CLASSIC_1:	//vi stop
END_VENC_1080P_CLASSIC_0:	//system exit
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;    
}

int parse_cmd(int argc, char **argv, HiMppTestCmd *cmd)
{
	HI_U32 optindex = 1;
	const char *opt;
	const char *next;
	
	while(optindex < argc){
		opt  = (const char *)argv[optindex++];
		next = (const char *)argv[optindex];
		
		if(opt[0] == '-' && opt[1] != '\0') {
			opt++;
			switch(*opt) {
				case 'i' :
					if(next) {
						strncpy(cmd->file_input, next, MAX_FILE_NAME_LENGTH);
						cmd->file_input[strlen(next)] = '\0';

            strcpy(file_name,cmd->file_input);
            file_name[strlen(next)-3]='t';
            file_name[strlen(next)-2]='x';
            file_name[strlen(next)-1]='t';
            file_name[strlen(next)]='\0';

					} else {
						SAMPLE_PRT("failed to get input param\n");
						return -1;
					}
					break;
				case 'o' :
					if(next) {
						strncpy(cmd->file_output, next, MAX_FILE_NAME_LENGTH);
						cmd->file_output[strlen(next)] = '\0';
					} else {
						SAMPLE_PRT("failed to get output param\n");
						return -1;
					}
					break;
				case 'q' :
					if(next) {
						cmd->fix_qp = atoi(next);
					} else {
						SAMPLE_PRT("failed to get fix_qp param\n");
						return -1;
					}
					break;
				case 'm' :
					if(next) {
						cmd->enc_mode = atoi(next);
					} else {
						SAMPLE_PRT("failed to get enc mode param\n");
						return -1;
					}
					break;
				case 'w' :
					if(next) {
						cmd->width = atoi(next);
					} else {
						SAMPLE_PRT("failed to get width param\n");
						return -1;
					}
					break;
				case 'h' :
					if(next) {
						cmd->height = atoi(next);
					} else {
						SAMPLE_PRT("failed to get height param\n");
						return -1;
					}
					break;
				case 'n' :
				  if(next) {
				    cmd->vframes = atoi(next);
				  } else {
				    SAMPLE_PRT("failed to get vframes param\n");
				    return -1;
				  }
				default :
					break;
			}
		}
		optindex++;
	}
	SAMPLE_PRT("param : 	-i %s \n"
				            "				-o %s \n"
							"				-q %d \n"
							"				-m %d \n"
							"				-w %d \n"
							"				-h %d \n", cmd->file_input, cmd->file_output, cmd->fix_qp, cmd->enc_mode, cmd->width, cmd->height
		);
}

int main(int argc, char* argv[])
{
    HI_S32 s32Ret;
    HiMppTestCmd cmd;

    signal(SIGINT, SAMPLE_VENC_HandleSig);
    signal(SIGTERM, SAMPLE_VENC_HandleSig);

	if(argc < 2) {
		SAMPLE_PRT("param : 		-i input_file \n"
				            "				-o output_file \n"
							"				-q fix_qp	  \n"
							"				-m 0 : h264 \n"
							"   			    1 : h265 \n"
							"				-w picture width\n"
							"				-h picture height\n"
							"        -n number of frames to encode\n"
		);
		return -1;
	}
	parse_cmd(argc, argv, &cmd);

	iFix_qp = cmd.fix_qp;
	
	/* H.264@1080p@30fps+H.265@1080p@30fps+H.264@D1@30fps */
	s32Ret = SAMPLE_VENC_1080P_CLASSIC(&cmd);
 
    if (HI_SUCCESS == s32Ret)
    { printf("program exit normally!\n"); }
    else
    { printf("program exit abnormally!\n"); }
    exit(s32Ret);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
