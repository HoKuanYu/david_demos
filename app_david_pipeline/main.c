#include <TI/tivx.h>
#include <tivx_utils_file_rd_wr.h>

#define NUM_CH                      (1)
#define APP_MAX_FILE_PATH           (256u)
#define APP_BUFFER_Q_DEPTH          (2)
#define APP_PIPELINE_DEPTH          (6)
#define APP_MAX_BUFQ_DEPTH          (8)
#define INPUT_FILE_PATH             "/home/david/Desktop/PSDK_08_05/RTOS_x86/tiovx/conformance_tests/test_data/psdkra/tidl_demo_images/"
#define OUTPUT_FILE_PATH            "/home/david/Desktop/PSDK_08_05/RTOS_x86/tiovx/conformance_tests/test_data/psdkra/david/"

typedef struct {
    vx_image input[APP_BUFFER_Q_DEPTH];
    vx_image output[APP_BUFFER_Q_DEPTH];
    vx_image tmp;

    vx_bool app_enable_pipe_flow;

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

    vx_node convert_node;
    vx_node not_node;

} AppObj;

AppObj gAppObj;

static void app_default_param_set(AppObj *obj);
static vx_status app_init(AppObj *obj);
static void app_deinit(AppObj *obj);
static vx_status app_create_graph(AppObj *obj);
static vx_status app_verify_graph(AppObj *obj);
static void app_delete_graph(AppObj *obj);
static vx_status app_run_graph(AppObj *obj);
static vx_status read_yuv_input(vx_char *file_name, vx_image image);

int app_david_input_main(int argc, char *argv[])
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

    if (status == VX_SUCCESS)
    {
        status = app_run_graph(obj);
    }

    app_delete_graph(obj);

    app_deinit(obj);

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
            printf("Unable to open file %s \n", file_name);
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
            printf("Luma bytes read = %d, expected = %d\n", num_bytes, img_width * img_height);
        
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
                printf("CbCr bytes read = %d, expected = %d\n", num_bytes, img_width*img_height/2);

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

    vxReleaseContext(&obj->context);
}

static void app_delete_graph(AppObj *obj)
{
    vxReleaseGraph(&obj->graph);
}

static vx_status app_run_graph(AppObj *obj)
{
    vx_status status = VX_SUCCESS;

    vx_int32 frame_id = obj->start_frame;

    for (frame_id = obj->start_frame; frame_id < obj->start_frame + obj->num_frames; frame_id++)
    {
        vx_char input_file_name[APP_MAX_FILE_PATH];
        vx_char output_file_name[APP_MAX_FILE_PATH];

        snprintf(input_file_name, APP_MAX_FILE_PATH, "%s/%010d.yuv", obj->input_file_path, frame_id);

        status = read_yuv_input(input_file_name, obj->input[0]);

        if (status == VX_SUCCESS)
        {
            status = vxProcessGraph(obj->graph);
        }

        if (status == VX_SUCCESS)
        {
            snprintf(output_file_name, APP_MAX_FILE_PATH, "%s/%010d.bmp", obj->output_file_path, frame_id);

            status = tivx_utils_save_vximage_to_bmpfile(output_file_name, obj->output[0]);
        }
    }

    return status;
}

static vx_status app_verify_graph(AppObj *obj)
{
    vx_status status = VX_SUCCESS;

    status = vxVerifyGraph(obj->graph);

    if (status == VX_SUCCESS)
    {
        status = tivxExportGraphToDot(obj->graph, obj->output_file_path, "vx_app_david_input");
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
        obj->tmp = vxCreateVirtualImage(obj->graph, obj->width, obj->height, (vx_df_image)VX_DF_IMAGE_RGB);
        status = vxSetReferenceName((vx_reference)obj->tmp, "IntermediateVirtualRGB");
    }

    if (status == VX_SUCCESS)
    {
        obj->convert_node = vxColorConvertNode(obj->graph, obj->input[0], obj->tmp);
        status = vxSetReferenceName((vx_reference)obj->convert_node, "ConvertNode");
    }

    if (status == VX_SUCCESS)
    {
        obj->not_node = vxChannelExtractNode(obj->graph, obj->tmp, (vx_enum)VX_CHANNEL_R, obj->output[0]);
        status = vxSetReferenceName((vx_reference)obj->not_node, "ChannelExtractNode");
    }

    return status;
}

static vx_status app_init(AppObj *obj)
{
    vx_status status = VX_SUCCESS;

    obj->context = vxCreateContext();
    status = vxGetStatus((vx_reference)obj->context);

    if (status == VX_SUCCESS)
    {
        vx_int32 q;
        for (q = 0; q < APP_BUFFER_Q_DEPTH; q++)
        {
            obj->input[q] = vxCreateImage(obj->context, obj->width, obj->height, VX_DF_IMAGE_NV12);
            obj->output[q] = vxCreateImage(obj->context, obj->width, obj->height, VX_DF_IMAGE_U8);
        }
    }

    return status;
}

static void app_default_param_set(AppObj *obj)
{
    snprintf(obj->input_file_path, APP_MAX_FILE_PATH, INPUT_FILE_PATH);
    snprintf(obj->output_file_path, APP_MAX_FILE_PATH, OUTPUT_FILE_PATH);
    obj->app_enable_pipe_flow = vx_false_e;
    obj->start_frame = 500;
    obj->num_frames = 10;
    obj->width = 1024;
    obj->height = 512;
}