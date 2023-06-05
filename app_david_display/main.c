#include <utils/perf_stats/include/app_perf_stats.h>
#include <utils/grpx/include/app_grpx.h>

#include "app_common.h"
#include "david_display_module.h"

typedef struct {
    vx_uint32 stop_task;
    vx_uint32 stop_task_done;
    tivx_task task;
} TaskObj;

typedef struct {
    app_perf_point_t total_perf;
    app_perf_point_t fileio_perf;
} PerfObj;

typedef struct {
    DisplayObj displayObj;
    TaskObj taskObj;
    PerfObj perfObj;

    vx_image input[APP_BUFFER_Q_DEPTH];
    vx_image output[APP_BUFFER_Q_DEPTH];
    vx_image gray;
    vx_image grad_x;
    vx_image grad_y;
    vx_image magnitude;

    vx_int32 width;
    vx_int32 height;

    /* config options */
    vx_char input_file_path[APP_MAX_FILE_PATH];
    vx_char output_file_path[APP_MAX_FILE_PATH];
    vx_int32 start_frame;
    vx_int32 num_frames;

    /* OpenVX references */
    vx_context context;
    vx_graph graph;

    vx_node channel_extract_node;
    vx_node sobel_node;
    vx_node magnitude_node;
    vx_node convert_depth_node;

    vx_scalar shift;

    vx_int32 input_graph_parameter_index;
    vx_int32 output_graph_parameter_index;

    vx_int32 enqueueCnt;
    vx_int32 dequeueCnt;
    vx_int32 pipeline;

    vx_uint32 delay_in_msecs;
} AppObj;

AppObj gAppObj;

static void app_default_param_set(AppObj *obj);
static vx_status app_init(AppObj *obj);
static void app_deinit(AppObj *obj);
static vx_status app_create_graph(AppObj *obj);
static vx_status app_verify_graph(AppObj *obj);
static void app_delete_graph(AppObj *obj);
static vx_status app_run_graph(AppObj *obj);
static vx_status app_run_graph_interactive(AppObj *obj);
static vx_status read_yuv_input(vx_char *file_name, vx_image image);
static vx_status add_graph_parameter_by_node_index(vx_graph graph, vx_node node, vx_uint32 node_parameter_index);

static char menu[] = {
    "\n"
    "\n ========================="
    "\n Demo : David Display Demo"
    "\n ========================="
    "\n"
    "\n p: Print performance statistics"
    "\n"
    "\n x: Exit"
    "\n"
    "\n Enter Choice: "
};

int app_david_dispaly_main(int argc, char *argv[])
{
    AppObj *obj = &gAppObj;
    vx_status status = VX_SUCCESS;

    app_default_param_set(obj);

    status = app_init(obj);

    if(status == VX_SUCCESS)
    {
        status = app_create_graph(obj);
    }

    if (status == VX_SUCCESS)
    {
        status = app_verify_graph(obj);
    }

    #if APP_RUN_GRAPH_INTERACTIVE
    if (status == VX_SUCCESS)
    {
        status = app_run_graph_interactive(obj);
    }
    #else
    if (status == VX_SUCCESS)
    {
        status = app_run_graph(obj);
    }
    #endif

    app_delete_graph(obj);

    app_deinit(obj);

    return status;
}

static void app_run_task(void *app_var)
{
    AppObj *obj = (AppObj *)app_var;
    vx_status status = VX_SUCCESS;

    while(!obj->taskObj.stop_task && (status == VX_SUCCESS))
    {
        status = app_run_graph(obj);
    }
    obj->taskObj.stop_task_done = 1;
}

static vx_status app_run_task_create(AppObj *obj)
{
    tivx_task_create_params_t params;
    vx_status status;

    tivxTaskSetDefaultCreateParams(&params);
    params.task_main = app_run_task;
    params.app_var = obj;

    obj->taskObj.stop_task_done = 0;
    obj->taskObj.stop_task = 0;

    status = tivxTaskCreate(&obj->taskObj.task, &params);

    return status;
}

static void app_run_task_delete(AppObj *obj)
{
    while(obj->taskObj.stop_task_done==0)
    {
        tivxTaskWaitMsecs(100);
    }

    tivxTaskDelete(&obj->taskObj.task);
}

static vx_status app_run_graph_interactive(AppObj *obj)
{
    vx_status status;
    char ch = '\0';

    status = app_run_task_create(obj);

    if (status == VX_SUCCESS)
    {
        appPerfStatsResetAll();
        while(!obj->taskObj.stop_task && status == VX_SUCCESS)
        {
            if (ch != '\r' && ch != '\n')
            {
                printf(menu);
            }
            ch = getchar();
            printf("\n");

            switch(ch)
            {
                case 'p':
                    appPerfStatsPrintAll();
                    status = tivx_utils_graph_perf_print(obj->graph);
                    appPerfPointPrint(&obj->perfObj.fileio_perf);
                    appPerfPointPrint(&obj->perfObj.total_perf);
                    printf("\n");
                    appPerfPointPrintFPS(&obj->perfObj.total_perf);
                    appPerfPointReset(&obj->perfObj.total_perf);
                    printf("\n");
                    break;
                case 'x':
                    obj->taskObj.stop_task = 1;
                    break;
            }
        }
        app_run_task_delete(obj);
    }

    return status;
}

static vx_status read_yuv_input(vx_char *file_name, vx_image image)
{
    vx_status status;

    status = vxGetStatus((vx_reference)image);

    if(status == VX_SUCCESS)
    {
        FILE * fp = fopen(file_name,"rb");
        vx_int32 i;

        if(fp == NULL)
        {
            APP_PRINTF("Unable to open file %s \n", file_name);
            return (VX_FAILURE);
        }

        vx_rectangle_t rect;
        vx_imagepatch_addressing_t image_addr;
        vx_map_id map_id;
        void *data_ptr;
        vx_uint32 num_bytes = 0;
        vx_uint32  img_width;
        vx_uint32  img_height;
        vx_uint32 img_format;

        vxQueryImage(image, VX_IMAGE_WIDTH, &img_width, sizeof(vx_uint32));
        vxQueryImage(image, VX_IMAGE_HEIGHT, &img_height, sizeof(vx_uint32));
        vxQueryImage(image, VX_IMAGE_FORMAT, &img_format, sizeof(vx_uint32));

        rect.start_x = 0;
        rect.start_y = 0;
        rect.end_x = img_width;
        rect.end_y = img_height;

        status = vxMapImagePatch(image,
                        &rect,
                        0,
                        &map_id,
                        &image_addr,
                        &data_ptr,
                        VX_WRITE_ONLY,
                        VX_MEMORY_TYPE_HOST,
                        VX_NOGAP_X);

        /* Copy Luma */
        for (i = 0; i < img_height; i++)
        {
            num_bytes += fread(data_ptr, 1, img_width, fp);
            data_ptr += image_addr.stride_y;
        }

        if(num_bytes != (img_width * img_height))
            APP_PRINTF("Luma bytes read = %d, expected = %d\n", num_bytes, img_width * img_height);
        
        vxUnmapImagePatch(image, map_id);

        if(img_format == VX_DF_IMAGE_NV12)
        {
            rect.start_x = 0;
            rect.start_y = 0;
            rect.end_x = img_width;
            rect.end_y = img_height / 2;
            status = vxMapImagePatch(image,
                                    &rect,
                                    1,
                                    &map_id,
                                    &image_addr,
                                    &data_ptr,
                                    VX_WRITE_ONLY,
                                    VX_MEMORY_TYPE_HOST,
                                    VX_NOGAP_X);


            /* Copy CbCr */
            num_bytes = 0;
            for (i = 0; i < img_height/2; i++)
            {
                num_bytes += fread(data_ptr, 1, img_width, fp);
                data_ptr += image_addr.stride_y;
            }

            if(num_bytes != (img_width*img_height/2))
                APP_PRINTF("CbCr bytes read = %d, expected = %d\n", num_bytes, img_width*img_height/2);

            vxUnmapImagePatch(image, map_id);
        }

        fclose(fp);
    }

    return status;
}

static void app_deinit(AppObj *obj)
{

    vx_int32 q;

    for(q = 0; q < APP_BUFFER_Q_DEPTH; q++)
    {
        vxReleaseImage(&obj->input[q]);
        vxReleaseImage(&obj->output[q]);
    }

    vxReleaseScalar(&obj->shift);

    app_deinit_display(&obj->displayObj);

    tivxHwaUnLoadKernels(obj->context);

    vxReleaseContext(&obj->context);
}

static void app_delete_graph(AppObj *obj)
{
    vxReleaseNode(&obj->channel_extract_node);
    vxReleaseNode(&obj->sobel_node);
    vxReleaseNode(&obj->magnitude_node);
    vxReleaseNode(&obj->convert_depth_node);

    vxReleaseImage(&obj->gray);
    vxReleaseImage(&obj->grad_x);
    vxReleaseImage(&obj->grad_y);
    vxReleaseImage(&obj->magnitude);

    app_delete_display(&obj->displayObj);

    vxReleaseGraph(&obj->graph);
}

static vx_status app_run_graph(AppObj *obj)
{
    vx_status status = VX_SUCCESS;
    vx_int32 frame_id = obj->start_frame;
    vx_int32 i;
    uint64_t cur_time;

    for (i = 0; i < ITERATIONS; i++)
    {
        for (frame_id = obj->start_frame; frame_id < obj->start_frame + obj->num_frames; frame_id++)
        {
            cur_time = tivxPlatformGetTimeInUsecs();
            vx_char input_file_name[APP_MAX_FILE_PATH];
            vx_char output_file_name[APP_MAX_FILE_PATH];

            snprintf(input_file_name, APP_MAX_FILE_PATH, "%s/%010d.yuv", obj->input_file_path, frame_id);

            #ifndef APP_ENABLE_PIPELINE_FLOW
            snprintf(output_file_name, APP_MAX_FILE_PATH, "%s/%010d.bmp", obj->output_file_path, frame_id);

            if (status == VX_SUCCESS)
            {
                status = read_yuv_input(input_file_name, obj->input[0]);
            }

            if (status == VX_SUCCESS)
            {
                status = vxProcessGraph(obj->graph);
            }

            if (status == VX_SUCCESS)
            {
                status = tivx_utils_save_vximage_to_bmpfile(output_file_name, obj->output[0]);
            }
            #else

            appPerfPointBegin(&obj->perfObj.total_perf);
            if (obj->pipeline < 0)
            {
                if (status == VX_SUCCESS)
                {
                    status = vxGraphParameterEnqueueReadyRef(obj->graph, obj->output_graph_parameter_index, (vx_reference *)&obj->output[obj->enqueueCnt], 1);
                    APP_PRINTF("App Enqueue Output Image Done!\n");
                }

                appPerfPointBegin(&obj->perfObj.fileio_perf);
                if (status == VX_SUCCESS)
                {
                    status = read_yuv_input(input_file_name, obj->input[obj->enqueueCnt]);
                    APP_PRINTF("App Read Input Image %d Done!\n", frame_id);
                }
                appPerfPointEnd(&obj->perfObj.fileio_perf);

                if (status == VX_SUCCESS)
                {
                    status = vxGraphParameterEnqueueReadyRef(obj->graph, obj->input_graph_parameter_index, (vx_reference *)&obj->input[obj->enqueueCnt], 1);
                    APP_PRINTF("App Enqueue Input Image %d Done!\n", frame_id);
                }

                obj->enqueueCnt++;
                obj->enqueueCnt = (obj->enqueueCnt >= APP_BUFFER_Q_DEPTH) ? 0 : obj->enqueueCnt;
                obj->pipeline++;
            }
            else
            {
                vx_image input_image;
                vx_image output_image;
                vx_uint32 num_refs;
                vx_int32 input_index = -1;
                
                snprintf(output_file_name, APP_MAX_FILE_PATH, "%s/%010d.bmp", obj->output_file_path, frame_id - APP_BUFFER_Q_DEPTH);
                if(status == VX_SUCCESS)
                {
                    status = vxGraphParameterDequeueDoneRef(obj->graph, obj->input_graph_parameter_index, (vx_reference *)&input_image, 1, &num_refs);
                    APP_PRINTF("App Dequeue Input Image Done!\n");
                }

                if(status == VX_SUCCESS)
                {
                    status = vxGraphParameterDequeueDoneRef(obj->graph, obj->output_graph_parameter_index, (vx_reference *)&output_image, 1, &num_refs);
                    APP_PRINTF("App Dequeue Output Image Done!\n");
                }

                if(status == VX_SUCCESS)
                {
                    status = vxGraphParameterEnqueueReadyRef(obj->graph, obj->output_graph_parameter_index, (vx_reference *)&output_image, 1);
                    APP_PRINTF("App Enqueue Output Image Done!\n");
                }

                appPerfPointBegin(&obj->perfObj.fileio_perf);
                if (status == VX_SUCCESS)
                {
                    vx_int32 i;

                    for(i = 0; i < APP_BUFFER_Q_DEPTH; i++)
                    {
                        if ((vx_reference)input_image == (vx_reference)obj->input[i])
                        {
                            input_index = i;
                            break;
                        }
                    }
                }

                if (input_index != -1 && status == VX_SUCCESS)
                {
                    status = read_yuv_input(input_file_name, obj->input[input_index]);
                    APP_PRINTF("App Read Input Image %d Done!\n", frame_id);
                }
                appPerfPointEnd(&obj->perfObj.fileio_perf);

                if (status == VX_SUCCESS)
                {
                    status = vxGraphParameterEnqueueReadyRef(obj->graph, obj->input_graph_parameter_index, (vx_reference*)&obj->input[input_index], 1);
                    APP_PRINTF("App Enqueue Input Image %d Done!\n", frame_id);
                }

                obj->enqueueCnt++;
                obj->dequeueCnt++;
                obj->enqueueCnt = (obj->enqueueCnt >= APP_BUFFER_Q_DEPTH) ? 0 : obj->enqueueCnt;
                obj->dequeueCnt = (obj->dequeueCnt >= APP_BUFFER_Q_DEPTH) ? 0 : obj->dequeueCnt;
            }

            cur_time = tivxPlatformGetTimeInUsecs() - cur_time;
            cur_time = cur_time / 1000;
            if (cur_time < obj->delay_in_msecs)
            {
                tivxTaskWaitMsecs(obj->delay_in_msecs - cur_time);
            }

            if (obj->taskObj.stop_task || status == VX_FAILURE)
            {
                break;
            }

            appPerfPointEnd(&obj->perfObj.total_perf);

            APP_PRINTF("App Reading Input Done %d!\n", frame_id);
            #endif
        }

        if (obj->taskObj.stop_task || status == VX_FAILURE)
        {
            break;
        }
    }

    #ifdef APP_ENABLE_PIPELINE_FLOW
    vxWaitGraph(obj->graph);
    #endif

    return status;
}

static vx_status app_verify_graph(AppObj *obj)
{
    vx_status status = VX_SUCCESS;

    status = vxVerifyGraph(obj->graph);

    APP_PRINTF("APP Verify Done!\n");

    if (status == VX_SUCCESS)
    {
        status = tivxExportGraphToDot(obj->graph, obj->output_file_path, "vx_app_david_pipeline");
    }

    return status;
}

static vx_status app_create_graph(AppObj *obj)
{
    vx_status status = VX_SUCCESS;

    obj->graph = vxCreateGraph(obj->context);
    status = vxSetReferenceName((vx_reference)obj->graph, "OpenVxGraph");

    if (status == VX_SUCCESS)
    {
        obj->gray = vxCreateVirtualImage(obj->graph, obj->width, obj->height, (vx_df_image)VX_DF_IMAGE_U8);
        status = vxSetReferenceName((vx_reference)obj->gray, "VirtualGrayU8");
        APP_PRINTF("App Create Virtual Image Gray Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->grad_x = vxCreateVirtualImage(obj->graph, obj->width, obj->height, (vx_df_image)VX_DF_IMAGE_S16);
        status = vxSetReferenceName((vx_reference)obj->grad_x, "VirtualGradXS16");
        APP_PRINTF("App Create Virtual Image Grad X Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->grad_y = vxCreateVirtualImage(obj->graph, obj->width, obj->height, (vx_df_image)VX_DF_IMAGE_S16);
        status = vxSetReferenceName((vx_reference)obj->grad_y, "VirtualGradYS16");
        APP_PRINTF("App Create Virtual Image Grad Y Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->magnitude = vxCreateVirtualImage(obj->graph, obj->width, obj->height, (vx_df_image)VX_DF_IMAGE_S16);
        status = vxSetReferenceName((vx_reference)obj->magnitude, "VirtualMagnitudeS16");
        APP_PRINTF("App Create Virtual Image Magnitude Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        vx_int32 shift_value = 0;
        obj->shift = vxCreateScalar(obj->context, (vx_enum)VX_TYPE_INT32, &shift_value);
        status = vxSetReferenceName((vx_reference)obj->shift, "ScalarShift");
        APP_PRINTF("App Create Scalar Shift Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->channel_extract_node = vxChannelExtractNode(obj->graph, obj->input[0], (vx_enum)VX_CHANNEL_Y, obj->gray);
        status = vxSetReferenceName((vx_reference)obj->channel_extract_node, "ChannelExtractNode");
        if (status == VX_SUCCESS)
        {
            status = vxSetNodeTarget(obj->channel_extract_node, VX_TARGET_STRING, TIVX_TARGET_DSP1);
        }
        APP_PRINTF("App Create Channel Extract Node Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->sobel_node = vxSobel3x3Node(obj->graph, obj->gray, obj->grad_x, obj->grad_y);
        status = vxSetReferenceName((vx_reference)obj->sobel_node, "SobelNode");
        if (status == VX_SUCCESS)
        {
            status = vxSetNodeTarget(obj->sobel_node, VX_TARGET_STRING, TIVX_TARGET_DSP2);
        }
        APP_PRINTF("App Create Sobel Node Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->magnitude_node = vxMagnitudeNode(obj->graph, obj->grad_x, obj->grad_y, obj->magnitude);
        status = vxSetReferenceName((vx_reference)obj->magnitude_node, "MagnitudeNode");
        if (status == VX_SUCCESS)
        {
            status = vxSetNodeTarget(obj->magnitude_node, VX_TARGET_STRING, TIVX_TARGET_DSP1);
        }
        APP_PRINTF("App Create Magnitude Node Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        obj->convert_depth_node = vxConvertDepthNode(obj->graph, obj->magnitude, obj->output[0], (vx_enum)VX_CONVERT_POLICY_WRAP, obj->shift);
        status = vxSetReferenceName((vx_reference)obj->convert_depth_node, "ConvertDepthNode");
        if (status == VX_SUCCESS)
        {
            status = vxSetNodeTarget(obj->convert_depth_node, VX_TARGET_STRING, TIVX_TARGET_DSP2);
        }
        APP_PRINTF("App Create Convert Depth Node Done!\n");
    }

    if (status == VX_SUCCESS)
    {
        status = app_create_graph_display(obj->graph, &obj->displayObj, obj->output[0]);
        APP_PRINTF("App Create Display Node Done!\n");
    }

    #ifdef APP_ENABLE_PIPELINE_FLOW
    vx_graph_parameter_queue_params_t graph_parameters_queue_params_list[2];
    vx_int32 graph_parameter_index = 0;

    if(status == VX_SUCCESS)
    {
        obj->input_graph_parameter_index = graph_parameter_index;
        status = add_graph_parameter_by_node_index(obj->graph, obj->channel_extract_node, 0);
        graph_parameters_queue_params_list[graph_parameter_index].graph_parameter_index = graph_parameter_index;
        graph_parameters_queue_params_list[graph_parameter_index].refs_list_size = APP_BUFFER_Q_DEPTH;
        graph_parameters_queue_params_list[graph_parameter_index].refs_list = (vx_reference *)&obj->input[0];
        graph_parameter_index++;
    }

    if(status == VX_SUCCESS)
    {   
        obj->output_graph_parameter_index = graph_parameter_index;
        status = add_graph_parameter_by_node_index(obj->graph, obj->convert_depth_node, 1);
        graph_parameters_queue_params_list[graph_parameter_index].graph_parameter_index = graph_parameter_index;
        graph_parameters_queue_params_list[graph_parameter_index].refs_list_size = APP_BUFFER_Q_DEPTH;
        graph_parameters_queue_params_list[graph_parameter_index].refs_list = (vx_reference *)&obj->output[0];
        graph_parameter_index++;
    }

    if (status == VX_SUCCESS)
    {
        status = vxSetGraphScheduleConfig(obj->graph,
                     VX_GRAPH_SCHEDULE_MODE_QUEUE_AUTO,
                     graph_parameter_index,
                     graph_parameters_queue_params_list);
    }

    if (status == VX_SUCCESS)
    {
        status = tivxSetGraphPipelineDepth(obj->graph, APP_PIPELINE_DEPTH);
    }

    if (status == VX_SUCCESS)
    {
        status = tivxSetNodeParameterNumBufByIndex(obj->channel_extract_node, 2, APP_BUFFER_Q_DEPTH);
    }

    if (status == VX_SUCCESS)
    {
        status = tivxSetNodeParameterNumBufByIndex(obj->sobel_node, 1, APP_BUFFER_Q_DEPTH);
    }

    if (status == VX_SUCCESS)
    {
        status = tivxSetNodeParameterNumBufByIndex(obj->sobel_node, 2, APP_BUFFER_Q_DEPTH);
    }

    if (status == VX_SUCCESS)
    {
        status = tivxSetNodeParameterNumBufByIndex(obj->magnitude_node, 2, APP_BUFFER_Q_DEPTH);
    }
    #endif

    return status;
}

static vx_status app_init(AppObj *obj)
{
    vx_status status = VX_SUCCESS;
    app_grpx_init_prms_t grpx_prms;

    obj->context = vxCreateContext();
    status = vxGetStatus((vx_reference)obj->context);

    if (status == VX_SUCCESS)
    {
        tivxHwaLoadKernels(obj->context);
    }

    if (status == VX_SUCCESS)
    {
        vx_int32 q;
        for (q = 0; q < APP_BUFFER_Q_DEPTH; q++)
        {
            obj->input[q] = vxCreateImage(obj->context, obj->width, obj->height, VX_DF_IMAGE_NV12);
            obj->output[q] = vxCreateImage(obj->context, obj->width, obj->height, VX_DF_IMAGE_U8);
        }
    }

    if(status == VX_SUCCESS)
    {
        status = app_init_display(obj->context, &obj->displayObj, "display_obj");
    }

    appPerfPointSetName(&obj->perfObj.total_perf, "TOTAL");
    appPerfPointSetName(&obj->perfObj.fileio_perf, "FILEIO");

    #ifndef x86_64
    if(obj->displayObj.display_option == 1)
    {
        appGrpxInitParamsInit(&grpx_prms, obj->context);
        grpx_prms.draw_callback = appGrpxDrawDefault;
        appGrpxInit(&grpx_prms);
    }
    #endif

    return status;
}

static void app_default_param_set(AppObj *obj)
{
    snprintf(obj->input_file_path, APP_MAX_FILE_PATH, INPUT_FILE_PATH);
    snprintf(obj->output_file_path, APP_MAX_FILE_PATH, OUTPUT_FILE_PATH);
    obj->pipeline = -APP_BUFFER_Q_DEPTH;
    obj->enqueueCnt = 0;
    obj->dequeueCnt = 0;
    obj->start_frame = 500;
    obj->num_frames = NUM_FRAMES;
    obj->width = 1024;
    obj->height = 512;
    obj->delay_in_msecs = 33;

    #ifdef x86_64
    obj->displayObj.display_option = 0;
    #else
    obj->displayObj.display_option = 1;
    #endif
}

static vx_status add_graph_parameter_by_node_index(vx_graph graph, vx_node node, vx_uint32 node_parameter_index)
{
    vx_parameter parameter = vxGetParameterByIndex(node, node_parameter_index);
    vx_status status;
    status = vxAddParameterToGraph(graph, parameter);
    if(status == VX_SUCCESS)
    {
        status = vxReleaseParameter(&parameter);
    }
    return status;
}