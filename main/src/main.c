
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include "time.h"

#include "global_config.h"
#include "libmaix_debug.h"
#include "libmaix_err.h"
#include "libmaix_cam.h"
#include "libmaix_image.h"
#include "libmaix_disp.h"

// #include "rotate.h"

#define CALC_FPS(tips)                                                                                     \
  {                                                                                                        \
    static int fcnt = 0;                                                                                   \
    fcnt++;                                                                                                \
    static struct timespec ts1, ts2;                                                                       \
    clock_gettime(CLOCK_MONOTONIC, &ts2);                                                                  \
    if ((ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000) - (ts1.tv_sec * 1000 + ts1.tv_nsec / 1000000) >= 1000) \
    {                                                                                                      \
      printf("%s => H26X FPS:%d\n", tips, fcnt);                                                  \
      ts1 = ts2;                                                                                           \
      fcnt = 0;                                                                                            \
    }                                                                                                      \
  }

#include "sys/time.h"

static struct timeval old, now;

static void cap_set()
{
  gettimeofday(&old, NULL);
}

static void cap_get(const char *tips)
{
  gettimeofday(&now, NULL);
  if (now.tv_usec > old.tv_usec)
    printf("%20s - %5ld ms\r\n", tips, (now.tv_usec - old.tv_usec) / 1000);
}

/******************************************************
 *YUV422：Y：U：V=2:1:1
 *RGB24 ：B G R
******************************************************/
int YUV422PToRGB24(void *RGB24, void *YUV422P, int width, int height)
{
  unsigned char *src_y = (unsigned char *)YUV422P;
  unsigned char *src_u = (unsigned char *)YUV422P + width * height;
  unsigned char *src_v = (unsigned char *)YUV422P + width * height * 3 / 2;

  unsigned char *dst_RGB = (unsigned char *)RGB24;

  int temp[3];

  if (RGB24 == NULL || YUV422P == NULL || width <= 0 || height <= 0)
  {
    printf(" YUV422PToRGB24 incorrect input parameter!\n");
    return -1;
  }

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      int Y = y * width + x;
      int U = Y >> 1;
      int V = U;

      temp[0] = src_y[Y] + ((7289 * src_u[U]) >> 12) - 228;                             //b
      temp[1] = src_y[Y] - ((1415 * src_u[U]) >> 12) - ((2936 * src_v[V]) >> 12) + 136; //g
      temp[2] = src_y[Y] + ((5765 * src_v[V]) >> 12) - 180;                             //r

      dst_RGB[3 * Y] = (temp[0] < 0 ? 0 : temp[0] > 255 ? 255
                                                        : temp[0]);
      dst_RGB[3 * Y + 1] = (temp[1] < 0 ? 0 : temp[1] > 255 ? 255
                                                            : temp[1]);
      dst_RGB[3 * Y + 2] = (temp[2] < 0 ? 0 : temp[2] > 255 ? 255
                                                            : temp[2]);
    }
  }

  return 0;
}

struct {
  int w0, h0;
  struct libmaix_cam *cam0;
  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  struct libmaix_cam *cam1;
  #endif
  uint8_t *rgb888;

  struct libmaix_disp *disp;

  int is_run;
} test = { 0 };

static void test_handlesig(int signo)
{
  if (SIGINT == signo || SIGTSTP == signo || SIGTERM == signo || SIGQUIT == signo || SIGPIPE == signo || SIGKILL == signo)
  {
    test.is_run = 0;
  }
  // exit(0);
}

inline static unsigned char make8color(unsigned char r, unsigned char g, unsigned char b)
{
	return (
	(((r >> 5) & 7) << 5) |
	(((g >> 5) & 7) << 2) |
	 ((b >> 6) & 3)	   );
}

inline static unsigned short make16color(unsigned char r, unsigned char g, unsigned char b)
{
	return (
	(((r >> 3) & 31) << 11) |
	(((g >> 2) & 63) << 5)  |
	 ((b >> 3) & 31)		);
}

void test_init() {

  libmaix_camera_module_init();

  test.w0 = 480, test.h0 = 480;

  test.cam0 = libmaix_cam_create(0, test.w0, test.h0, 1, 0);
  if (NULL == test.cam0) return ;  test.rgb888 = (uint8_t *)malloc(test.w0 * test.h0 * 3);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  test.cam1 = libmaix_cam_create(1, test.w0, test.h0, 0, 0);
  if (NULL == test.cam0) return ;  test.rgb888 = (uint8_t *)malloc(test.w0 * test.h0 * 3);
  #endif

  test.disp = libmaix_disp_create(0);
  if(NULL == test.disp) return ;

  test.is_run = 1;

  // ALOGE(__FUNCTION__);
}

void test_exit() {

  if (NULL != test.cam0) libmaix_cam_destroy(&test.cam0);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  if (NULL != test.cam1) libmaix_cam_destroy(&test.cam1);
  #endif

  if (NULL != test.rgb888) free(test.rgb888), test.rgb888 = NULL;
  if (NULL != test.disp) libmaix_disp_destroy(&test.disp), test.disp = NULL;

  libmaix_camera_module_deinit();

  // ALOGE(__FUNCTION__);
}

static libmaix_image_t *gray = NULL;


#include "zbar.h"

static zbar_image_scanner_t *scanner = NULL;

static void qrcode_init()
{
    if (gray == NULL) {
        gray = libmaix_image_create(test.w0, test.h0, LIBMAIX_IMAGE_MODE_GRAY, LIBMAIX_IMAGE_LAYOUT_HWC, NULL, true);
        if (gray) {
            /* create a reader */
            scanner = zbar_image_scanner_create();
            /* configure the reader */
            // zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 1);
            // zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_POSITION, 1);
            // zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_UNCERTAINTY, 2);
            // zbar_image_scanner_set_config(scanner, ZBAR_QRCODE, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_CODE128, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_CODE93, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_CODE39, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_CODABAR, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_COMPOSITE, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_PDF417, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_CODABAR, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_COMPOSITE, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_PDF417, ZBAR_CFG_UNCERTAINTY, 0);
            // zbar_image_scanner_set_config(scanner, ZBAR_DATABAR, ZBAR_CFG_UNCERTAINTY, 0);
        }
    }

}

static void qrcode_exit()
{
    if (gray) {
        libmaix_image_destroy(&gray);
        if (scanner != NULL) {
          zbar_image_scanner_destroy(scanner), scanner = NULL;
        }
    }
}

#define zbar_fourcc(a, b, c, d)                 \
        ((unsigned long)(a) |                   \
         ((unsigned long)(b) << 8) |            \
         ((unsigned long)(c) << 16) |           \
         ((unsigned long)(d) << 24))

static void qrcode_loop(libmaix_image_t* img)
{
    if (gray) {
        libmaix_err_t err = LIBMAIX_ERR_NONE;

        libmaix_cv_image_convert(img, LIBMAIX_IMAGE_MODE_GRAY, &gray);

        /* obtain image data */
        int width = gray->width, height = gray->height;
        uint8_t *raw = gray->data;

        /* wrap image data */
        zbar_image_t *image = zbar_image_create();
        zbar_image_set_format(image, zbar_fourcc('Y','8','0','0'));
        zbar_image_set_size(image, width, height);
        zbar_image_set_data(image, raw, width * height, NULL);

        // cap_set();

        /* scan the image for barcodes */
        int n = zbar_scan_image(scanner, image);

        // cap_get("zbar_scan_image");

        /* extract results */
        const zbar_symbol_t *symbol = zbar_image_first_symbol(image);
        for(; symbol; symbol = zbar_symbol_next(symbol)) {
            /* do something useful with results */
            zbar_symbol_type_t typ = zbar_symbol_get_type(symbol);
            const char *data = zbar_symbol_get_data(symbol);
            if (typ == ZBAR_QRCODE) {
                int datalen = strlen(data);
                // libmaix_cv_image_draw_string(img, 0, 0, zbar_get_symbol_name(typ), 1.5, MaixColor(0, 0, 255), 1);
                // libmaix_cv_image_draw_string(img, 0, 20, data, 1.5, MaixColor(255, 0, 0), 1);
                printf("decoded %s symbol \"%s\"\n", zbar_get_symbol_name(typ), data);
                // break;
            }
        }

        /* clean up */
        zbar_image_destroy(image); // use zbar_image_free_data
    }

}

void test_work() {

  test.cam0->start_capture(test.cam0);

  #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
  test.cam1->start_capture(test.cam1);
  #endif

  while (test.is_run)
  {
    // goal code
    libmaix_image_t *tmp = NULL;
    if (LIBMAIX_ERR_NONE == test.cam0->capture_image(test.cam0, &tmp))
    {
        // printf("w %d h %d p %d \r\n", tmp->width, tmp->height, tmp->mode);
        qrcode_loop(tmp);
        // apriltag_loop(tmp);

        if (tmp->width == test.disp->width && test.disp->height == tmp->height) {
            test.disp->draw_image(test.disp, tmp);
        } else {
            libmaix_image_t *rs = libmaix_image_create(test.disp->width, test.disp->height, LIBMAIX_IMAGE_MODE_RGB888, LIBMAIX_IMAGE_LAYOUT_HWC, NULL, true);
            if (rs) {
                libmaix_cv_image_resize(tmp, test.disp->width, test.disp->height, &rs);
                test.disp->draw_image(test.disp, rs);
                libmaix_image_destroy(&rs);
            }
        }
        CALC_FPS("maix_cam 0");

        #ifdef CONFIG_ARCH_V831 // CONFIG_ARCH_V831 & CONFIG_ARCH_V833
        libmaix_image_t *t = NULL;
        if (LIBMAIX_ERR_NONE == test.cam1->capture_image(test.cam1, &t))
        {
            // printf("w %d h %d p %d \r\n", t->width, t->height, t->mode);
            CALC_FPS("maix_cam 1");
        }
        #endif
    }
  }

}

int main(int argc, char **argv)
{
  signal(SIGINT, test_handlesig);
  signal(SIGTERM, test_handlesig);

  libmaix_image_module_init();

  test_init();
  qrcode_init();
  test_work();
  qrcode_exit();
  test_exit();


  libmaix_image_module_deinit();

  return 0;
}
