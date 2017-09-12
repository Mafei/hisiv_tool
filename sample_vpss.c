#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sample_comm.h"

FILE *f_in = NULL;
FILE *f_out = NULL;

typedef struct mmu_buf_t {
    HI_U32 phy_addr;
    HI_U8  *vir_addr;
} mmu_buf;

typedef struct yuv_buf_t {
    mmu_buf y_buf;
    mmu_buf uv_buf;
} yuv_buf;

typedef struct cmd_param_t {
	HI_U8   input_file[256];
	HI_U8	 output_file[256];
	HI_U32  width;
	HI_U32  height;
	HI_U32  Chroma_SF;
	HI_U32  Chroma_TF;
	HI_U32	IE_Post;
	HI_U32	IE_Strength;
	HI_U32	Luma_Motion;
	HI_U32	Luma_Move;
	HI_U32	Luma_Still;
	HI_U32	Luma_TF;
	HI_U32	Desand;
} cmd_param;

yuv_buf vpss_buf;
VIDEO_FRAME_INFO_S video_info;
VIDEO_FRAME_INFO_S out_info;

static HI_S32 yuv420to420sp(HI_U8 *src, HI_U8 *dst, HI_U32 width, HI_U32 height)
{
	HI_U32 i, j;
	HI_U32 size = width * height;

	HI_U8 *y = src;
	HI_U8 *u = src + size;
	HI_U8 *v = src + size * 5 / 4;

	HI_U8 *y_tmp = dst;
	HI_U8 *uv_tmp = dst + size;

	memcpy(y_tmp, y, size);
	for(j = 0, i = 0; j < size / 2; j += 2, i++) {
		uv_tmp[j] = u[i];
		uv_tmp[j+1] = v[i];
	}

	return 0;
}

static HI_S32 yuv420spto420(HI_U8 *src, HI_U8 *dst, HI_U32 width, HI_U32 height)
{
	HI_U32 size = width * height;
	HI_U32 i = 0, j = 0;

	HI_U8 *y = src;
	HI_U8 *u = src + size;
	HI_U8 *v = src + size;

	HI_U8 *y_tmp = dst;
	HI_U8 *u_tmp = dst + size;
	HI_U8 *v_tmp = dst + size * 5 / 4;

	memcpy(y_tmp, y, size);
	for(i = 0, j = 0; i < size / 2;i += 2, j++) {
		u_tmp[j] = u[i];
		v_tmp[j] = v[i+1];
	}

	return 0;
}

static HI_S32 parse_cmdline(HI_U32 argc, HI_S8 **argv, cmd_param *cmd)
{
	const char *opt;
	const char *next;
	HI_S32 optindex = 1;
	HI_S32 handleoptions = 1;

	if(argc < 2 || cmd == NULL) {
		fprintf(stderr, "Invalid INput Param.\n");
		return -1;
	}

	while(optindex < argc) {
		opt  = (const char *)argv[optindex++];
		next = (const char *)argv[optindex];

		if(handleoptions && opt[0] == '-' && opt[1] != '\0') {
			if(opt[1] == '-') {
				if(opt[2] != '\0') {
					opt++;
				} else {
					handleoptions = 0;
					continue;
				}
			}
			
			opt++;

			switch(*opt) {
			case '1':
				if(next) {
					cmd->Chroma_SF = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Chroma_SF.\n");
					goto end;
				}
				break;
			case '2':
				if(next) {
					cmd->Chroma_TF = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Chroma_TF.\n");
					goto end;
				}
				break;
			case '3':
				if(next) {
					cmd->IE_Post = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param IE_Post.\n");
					goto end;
				}
				break;
			case '4':
				if(next) {
					cmd->IE_Strength = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param IE_Strength.\n");
					goto end;
				}
				break;
			case '5':
				if(next) {
					cmd->Luma_Motion = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Luma_Motion.\n");
					goto end;
				}
				break;
			case '6':
				if(next) {
					cmd->Luma_Move = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Luma_Move.\n");
					goto end;
				}
				break;
			case '7':
				if(next) {
					cmd->Luma_Still = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Luma_Still.\n");
					goto end;
				}
				break;
			case '8':
				if(next) {
					cmd->Luma_TF = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Luma_TF.\n");
					goto end;
				}
				break;
			case '9':
					if(next) {
					cmd->Desand = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param Desand.\n");
					goto end;
				}
				break;
			case 'i':
				if(next) {
					strncpy(cmd->input_file, next, 256);
					cmd->input_file[strlen(next)] = '\0';
				} else {
					fprintf(stderr, "Invalid input param input_file.\n");
					goto end;
				}
				break;
			case 'w':
				if(next) {
					cmd->width = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param width.\n");
					goto end;
				}
			    break;
			case 'h':
				if(next) {
					cmd->height = atoi(next);
				} else {
					fprintf(stderr, "Invalid input param height.\n");
					goto end;
				}
				break;
			case 'o':
				if(next) {
					strncpy(cmd->output_file, next, 256);
				} else {
					fprintf(stderr, "Invalid input param output_file.\n");
					goto end;
				}
			default:
				break;
			}
		}
	}

end:
	return 0;
}

int main(int argc, char **argv)
{
	HI_S32 ret;
	cmd_param cmd;

	memset(&cmd, 0, sizeof(cmd_param));

	ret = parse_cmdline(argc, argv, &cmd);
	if(ret < 0) {
		printf("Invalid Parse.\n");
		return -1;
	}

    VB_CONF_S vb_s;
    MPP_SYS_CONF_S sys_s;
    sys_s.u32AlignWidth = 16;

    f_in = fopen(cmd.input_file, "r");
    if(!f_in) {
		printf("failed to open input file : %s.\n", cmd.input_file);
        return -1;
    }

	f_out = fopen(cmd.output_file, "wa+");
	if(!f_out) {
		printf("failed to open output file : %s.\n", cmd.output_file);
		return -1;
	}

    memset(&vb_s, 0, sizeof(VB_CONF_S));
    vb_s.u32MaxPoolCnt = 1;
    vb_s.astCommPool[0].u32BlkSize = 1920 * 1080 * 2;
    vb_s.astCommPool[0].u32BlkCnt = 1;

    memset(vb_s.astCommPool[0].acMmzName,0,sizeof(vb_s.astCommPool[0].acMmzName));

    ret = HI_MPI_VB_SetConf(&vb_s);
    if(ret != HI_SUCCESS) {
			printf("failed to set vb ret:%x.\n", ret);
			return -1;
    }

    ret = HI_MPI_VB_Init();
    if(ret != HI_SUCCESS) {
		printf("failed to init vb ret:%x.\n", ret);
        return -1;
    }

    ret = HI_MPI_SYS_SetConf(&sys_s);
    if(ret != HI_SUCCESS) {
		printf("failed to set sys ret:%x.\n", ret);
		return -1;
    }

    ret = HI_MPI_SYS_Init();
    if(ret != HI_SUCCESS) {
		printf("failed to init sys ret:%x.\n", ret);
		return -1;
    }
    
    VPSS_GRP_ATTR_S grp_attr;
    grp_attr.u32MaxW = cmd.width;
    grp_attr.u32MaxH = cmd.height;
    grp_attr.bIeEn = HI_FALSE;
    grp_attr.bDciEn = HI_FALSE;
    grp_attr.bNrEn = HI_FALSE;
    grp_attr.bHistEn = HI_FALSE;
    grp_attr.enDieMode = VPSS_DIE_MODE_NODIE;
    grp_attr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    ret = HI_MPI_VPSS_CreateGrp(0, &grp_attr);
    if(HI_SUCCESS != ret) {
		printf("failed to create vpss grp ret:%x.\n", ret);
		return -1;
    }

	ret = HI_MPI_VPSS_GetGrpAttr(0, &grp_attr);
	if (HI_SUCCESS != ret) {
		printf("failed to get grp attr ret:%x.\n", ret);
		goto end;
	}

	VPSS_GRP_PARAM_V2_S	param_s;
	ret = HI_MPI_VPSS_GetGrpParamV2(0, &param_s);
	if (HI_SUCCESS != ret) {
		printf("failed to get grp param ret:%x.\n", ret);
		goto end;
	}
	memset(&param_s, 0, sizeof(VPSS_GRP_PARAM_V2_S));

	param_s.Chroma_SF_Strength = cmd.Chroma_SF;
	param_s.Chroma_TF_Strength = cmd.Chroma_TF;
	param_s.IE_PostFlag = cmd.IE_Post;
	param_s.IE_Strength = cmd.IE_Strength;
	param_s.Luma_MotionThresh = cmd.Luma_Motion;
	param_s.Luma_SF_MoveArea = cmd.Luma_Move;
	param_s.Luma_SF_StillArea = cmd.Luma_Still;
	param_s.Luma_TF_Strength = cmd.Luma_TF;
	param_s.DeSand_Strength = cmd.Desand;

	ret = HI_MPI_VPSS_SetGrpParamV2(0, &param_s);
	if(HI_SUCCESS != ret) {
		printf("failed to set grp param ret:%x.\n", ret);
		goto end;
	}

	grp_attr.bNrEn = HI_TRUE;
	ret = HI_MPI_VPSS_SetGrpAttr(0, &grp_attr);
	if(HI_SUCCESS != ret) {
		printf("failed to set grp attr ret:%x.\n", ret);
		goto end;
	}

    VPSS_CHN_ATTR_S chn_attr;
    ret = HI_MPI_VPSS_GetChnAttr(0, 0, &chn_attr);
    if(HI_SUCCESS != ret) {
		printf("failed to get chn attr ret:%x.\n", ret);
        goto end;
    }

    chn_attr.bBorderEn = HI_FALSE;
	chn_attr.bSpEn = HI_FALSE;
	chn_attr.bMirror = HI_FALSE;
	chn_attr.bFlip = HI_FALSE;
	chn_attr.s32SrcFrameRate = -1;
	chn_attr.s32DstFrameRate = -1;
    ret = HI_MPI_VPSS_SetChnAttr(0, 0, &chn_attr);
	if(HI_SUCCESS != ret) {
		printf( "failed to set chn attr ret:%x.\n", ret);
		goto end;
	}

    VPSS_CHN_MODE_S chn_s;
    ret = HI_MPI_VPSS_GetChnMode(0, 0, &chn_s);
    if(HI_SUCCESS != ret) {
		printf( "failed to get chn mode ret:%x.\n", ret);
        goto end;
    }

    chn_s.enChnMode = VPSS_CHN_MODE_USER;
    chn_s.u32Width  = cmd.width;
    chn_s.u32Height = cmd.height;
    chn_s.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;

    ret = HI_MPI_VPSS_SetChnMode(0, 0, &chn_s);
    if(HI_SUCCESS != ret) {
		printf("failed to set chn mode ret:%x.\n", ret);
        goto end;
    }

    HI_U32 depth;
	ret = HI_MPI_VPSS_GetDepth(0, 0, &depth);
	if (HI_SUCCESS != ret) {
		printf("failed to get depth ret %x.\n", ret);
		goto end;
	}

	ret = HI_MPI_VPSS_SetDepth(0, 0, 8);
	if(HI_SUCCESS != ret) {
		printf( "failed to set depth ret %x.\n", ret);
		goto end;
	}
	

	printf("depth:%d.\n", depth);

    ret = HI_MPI_VPSS_EnableChn(0, 0);
	if(HI_SUCCESS != ret) {
		printf("failed to enable chn ret:%x.\n", ret);
		goto end;
	}

    ret = HI_MPI_VPSS_StartGrp(0);
    if(HI_SUCCESS != ret) {
		printf("failed to start vpss grp ret:%x.\n", ret);
        goto end;
    }

    HI_U32 width  = cmd.width;
    HI_U32 height = cmd.height; 
    HI_U32 size = width * height * 3 / 2;
	HI_U32 cnt = 0;
	VB_POOL VbPool = VB_INVALID_POOLID;
	VB_BLK handleY = VB_INVALID_HANDLE;
	HI_U32 phyAddr, *pVirYaddr;
	HI_U8 *tmp;

	tmp = malloc(size);
	if(!tmp) {
		printf("failed to malloc tmp.\n");
		goto end;
	}
	
	printf("Start to chn img.\n");
    do {
		VbPool = HI_MPI_VB_CreatePool(size, 1, NULL);
		if (VB_INVALID_POOLID == VbPool)
		{
			printf("create vb err\n");
			goto end;
		}

		//do {
		//	handleY = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, size, NULL);
		//} while(VB_INVALID_HANDLE == handleY);
		handleY = HI_MPI_VB_GetBlock(VbPool, size, NULL);
		if (VB_INVALID_HANDLE == handleY)
		{
			printf("get vb block err\n");
			(void)HI_MPI_VB_DestroyPool(VbPool);
			goto end;
		}

		VB_POOL poolID = HI_MPI_VB_Handle2PoolId(handleY);
		phyAddr = HI_MPI_VB_Handle2PhysAddr(handleY);
		if(phyAddr == 0) {
			printf("failed to chn handle to phyaddr.\n");
			break;
		}

		pVirYaddr = (HI_U8 *)HI_MPI_SYS_Mmap(phyAddr, size);

		memset(&video_info.stVFrame, 0x00, sizeof(VIDEO_FRAME_S));

		video_info.stVFrame.u32Width  = width;
		video_info.stVFrame.u32Height = height;
		video_info.stVFrame.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		video_info.u32PoolId = poolID;
		video_info.stVFrame.u32PhyAddr[0] = phyAddr;
		video_info.stVFrame.u32PhyAddr[1] = phyAddr + width * height;
		video_info.stVFrame.pVirAddr[0] = pVirYaddr;
		video_info.stVFrame.pVirAddr[1] = pVirYaddr + width * height;
		video_info.stVFrame.u32Stride[0] = width;
		video_info.stVFrame.u32Stride[1] = width;
		video_info.stVFrame.u32Field = VIDEO_FIELD_FRAME;
		video_info.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
		video_info.stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
		video_info.stVFrame.u64pts = cnt * 40;
		video_info.stVFrame.u32TimeRef = cnt * 2;

		ret = fread(tmp, 1, size, f_in);
		if(ret < 0) {
			printf("failed to read from %s.\n", cmd.input_file);
			break;
		} else if(ret == 0) {
			break;
		}

		yuv420to420sp(tmp, pVirYaddr, width, height);

		ret = HI_MPI_VPSS_EnableBackupFrame(0);
		if (HI_SUCCESS != ret) {
			printf("failed to enable back up frame ret:%x.\n", ret);
			break;
		}

        ret = HI_MPI_VPSS_SendFrame(0, &video_info, -1);
        if(HI_SUCCESS != ret) {
			printf("failed to send frame ret:%x.\n", ret);
            break;
        }

        ret = HI_MPI_VPSS_GetChnFrame(0, 0, &out_info, -1);
        if(HI_SUCCESS != ret) {
			printf("failed to get frame ret:%x.\n", ret);
            break;
        } else 
			printf("get chn frame %d width:%d height:%d.\n", cnt++, width, height);

		HI_U32 y_addr = out_info.stVFrame.u32PhyAddr[0];
		HI_U32 uv_addr = out_info.stVFrame.u32PhyAddr[1];
		/*HI_U32 v_addr = out_info.stVFrame.u32PhyAddr[2];*/

		HI_U8 *y_vir  = HI_MPI_SYS_Mmap(y_addr, width * height);
		HI_U8 *uv_vir = HI_MPI_SYS_Mmap(uv_addr, width * height / 2);

		HI_U8 *out_buf = malloc(size);
		if(!out_buf) {
			printf("failed to malloc out _buf.\n");
			break;
		}

		memcpy(tmp, y_vir, width * height);
		memcpy(tmp + width * height, uv_vir, width * height / 2);
		yuv420spto420(tmp, out_buf, width, height);

    if(cnt != 1)
    {
		    fwrite(out_buf, 1, size, f_out);
    }
		if (out_buf)
		{
			free(out_buf);
			out_buf = NULL;
		}

		HI_MPI_VPSS_ReleaseChnFrame(0, 0, &out_info);
		HI_MPI_SYS_Munmap(pVirYaddr, size);
		HI_MPI_VB_ReleaseBlock(handleY);
		HI_MPI_VB_DestroyPool(VbPool);
    } while(1);
	if (tmp)
	{
		free(tmp);
		tmp = NULL;
	}
	
	ret = HI_MPI_VPSS_StopGrp(0);
	if (HI_SUCCESS != ret) {
		printf("failed to stop vpss grp ret:%x.\n", ret);
		return ret;
	}

	ret = HI_MPI_VPSS_DisableChn(0, 0);
	if (HI_SUCCESS != ret) {
		printf("failed to disable chn ret:%x.\n", ret);
		return ret;
	}

	ret = HI_MPI_VPSS_DestroyGrp(0);
	if (HI_SUCCESS != ret) {
		printf("failed to create vpss grp ret:%x.\n", ret);
		return -1;
	}


end:
	ret = HI_MPI_SYS_Exit();
	if (HI_SUCCESS != ret)
	{
		printf("Mpi exit failed!\n");
		return ret;
	}
	ret = HI_MPI_VB_Exit();
	if (HI_SUCCESS != ret)
	{
		return ret;
	}


    if(f_in)
    	fclose(f_in);
   
	if(f_out)
		fclose(f_out);

	return 0;
}
