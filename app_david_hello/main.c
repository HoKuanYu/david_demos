#include <TI/tivx.h>
#include <tivx_utils_file_rd_wr.h>

#define IN_FILE "./test_data/colors.bmp"
#define OUT_FILE "./test/colors_output.bmp"

int app_david_hello_main(int argc, char *argv[])
{

    vx_context context = vxCreateContext();
    vx_graph graph = vxCreateGraph(context);

    vx_image input = vxCreateImage(context, 640, 480, VX_DF_IMAGE_U8);
    vx_image output = vxCreateImage(context, 640, 480, VX_DF_IMAGE_U8);
    vx_image intermediate = vxCreateVirtualImage(graph, 640, 480, VX_DF_IMAGE_U8);

    vx_node node1 = vxNotNode(graph, input, intermediate);
    vx_node node2 = vxGaussian3x3Node(graph, intermediate, output);

    vxVerifyGraph(graph);

    tivx_utils_load_vximage_from_bmpfile(input, IN_FILE, (vx_bool)vx_true_e);

    vxProcessGraph(graph);

    tivx_utils_save_vximage_to_bmpfile(OUT_FILE, output);

    vxSetReferenceName((vx_reference)input, "InputImageU8");
    vxSetReferenceName((vx_reference)output, "OutputImageU8");
    vxSetReferenceName((vx_reference)intermediate, "IntermediateVirtialImageU8");
    vxSetReferenceName((vx_reference)node1, "NotNode");
    vxSetReferenceName((vx_reference)node2, "Gaussian3x3Node");
    tivxExportGraphToDot(graph,".", "vx_david_hello");

    return VX_SUCCESS; 
}